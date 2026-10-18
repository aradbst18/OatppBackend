#include "app/infrastructure/database/PostgresqlLocalServer.hpp"

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlLocalServer.cpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/PostgresqlDatabase.hpp"
#include "app/logging/Logger.hpp"

namespace app::infrastructure::database
{

namespace
{

std::string shellQuote(const std::string& value)
{
    std::string out = "'";
    for (char c : value)
    {
        if (c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += c;
        }
    }

    out += "'";
    return out;
}

int runCommand(const std::string& cmd)
{
    // NOTE: callers never interpolate key material into cmd (pwfile for
    // initdb, libpq for SQL), so logging the command is safe.
    logging::logInfo("database", "Running: " + cmd);

    const int rc = std::system(cmd.c_str());
    if (rc != 0)
    {
        logging::logWarn("database", "Command exited with status " + std::to_string(rc) + ": " + cmd);
    }

    return rc;
}

std::string quoteIdentifier(const std::string& name)
{
    std::string out = "\"";
    for (char c : name)
    {
        out += (c == '"') ? "\"\"" : std::string(1, c);
    }

    out += "\"";
    return out;
}

std::string quoteLiteral(const std::string& value)
{
    std::string out = "'";
    for (char c : value)
    {
        out += (c == '\'') ? "''" : std::string(1, c);
    }

    out += "'";
    return out;
}

bool fileContainsTrustAuth(const std::string& pgHbaPath)
{
    std::ifstream in(pgHbaPath);
    if (!in)
    {
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        std::string stripped = line;
        stripped.erase(0, stripped.find_first_not_of(" \t"));
        if (stripped.empty() || stripped[0] == '#')
        {
            continue;
        }

        std::istringstream words(stripped);
        std::string token;
        while (words >> token)
        {
            if (token == "trust")
            {
                return true;
            }
        }
    }

    return false;
}

void rewriteTrustToScram(const std::string& pgHbaPath)
{
    std::ifstream in(pgHbaPath);
    if (!in)
    {
        throw errors::database::ConnectionFailed("Cannot read pg_hba.conf at '" + pgHbaPath + "'");
    }

    std::ostringstream rewritten;
    std::string line;
    while (std::getline(in, line))
    {
        std::string stripped = line;
        stripped.erase(0, stripped.find_first_not_of(" \t"));
        if (!stripped.empty() && stripped[0] != '#')
        {
            std::istringstream words(line);
            std::ostringstream rebuilt;
            std::string token;
            bool first = true;
            while (words >> token)
            {
                if (token == "trust")
                {
                    token = "scram-sha-256";
                }

                if (!first)
                {
                    rebuilt << '\t';
                }

                rebuilt << token;
                first = false;
            }

            rewritten << rebuilt.str() << '\n';
        }
        else
        {
            rewritten << line << '\n';
        }
    }

    const std::string tmpPath = pgHbaPath + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out)
        {
            throw errors::database::ConnectionFailed("Cannot write '" + tmpPath + "'");
        }

        out << rewritten.str();
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, pgHbaPath, ec);
    if (ec)
    {
        throw errors::database::ConnectionFailed("Cannot replace pg_hba.conf: " + ec.message());
    }
}

bool tcpPortInUse(std::uint16_t port) noexcept
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool inUse = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return inUse;
}

}  // namespace

PostgresqlLocalServer::PostgresqlLocalServer(std::string socketDir, std::uint16_t port, std::string dbname,
                                             std::string user, std::string key)
    : socketDir_(std::move(socketDir)),
      port_(port),
      dbname_(std::move(dbname)),
      user_(std::move(user)),
      key_(std::move(key))
{
    if (socketDir_.empty())
    {
        throw errors::database::ConnectionFailed("PostgreSQL socket directory must not be empty (database.path)");
    }

    if (key_.empty())
    {
        throw errors::database::ConnectionFailed(
            "PostgreSQL key must not be empty (database.key, scram-sha-256 password required)");
    }

    // postgres -k requires an absolute socket directory;
    // relative config values like "data/postgresql" are resolved
    // against the process working directory. TCP hosts (no slash) pass through.
    if (socketDir_.find('/') != std::string::npos)
    {
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(socketDir_, ec);
        if (!ec)
        {
            socketDir_ = abs.string();
        }
    }
}

PostgresqlBinaries findPostgresqlBinaries()
{
    const std::array<const char*, 4> versioned = {"18", "17", "16", "15"};
    for (const char* ver : versioned)
    {
        const std::string dir = std::string("/usr/lib/postgresql/") + ver + "/bin";
        const std::string initdb = dir + "/initdb";
        const std::string pgCtl = dir + "/pg_ctl";

        if (std::filesystem::exists(initdb) && std::filesystem::exists(pgCtl))
        {
            return {initdb, pgCtl};
        }
    }

    // PATH fallback (e.g. /usr/bin/wrapper or custom installs).
    return {"initdb", "pg_ctl"};
}

bool PostgresqlLocalServer::isInitialized() const
{
    return std::filesystem::exists(pgdataDir() + "/PG_VERSION");
}

bool PostgresqlLocalServer::isRunning() const
{
    const PostgresqlBinaries bins = findPostgresqlBinaries();
    const std::string cmd = shellQuote(bins.pgCtl) + " -D " + shellQuote(pgdataDir()) + " status >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

void PostgresqlLocalServer::ensureSocketDir() const
{
    std::error_code ec;
    std::filesystem::create_directories(socketDir_, ec);
    if (ec)
    {
        throw errors::database::ConnectionFailed("Cannot create PostgreSQL socket dir '" + socketDir_ +
                                                 "': " + ec.message());
    }
}

void PostgresqlLocalServer::runInitdb() const
{
    const PostgresqlBinaries bins = findPostgresqlBinaries();
    ensureSocketDir();

    const std::string pgdata = pgdataDir();
    if (isInitialized())
    {
        return;
    }

    if (std::filesystem::exists(pgdata) && !std::filesystem::is_empty(pgdata))
    {
        throw errors::database::ConnectionFailed(
            "PostgreSQL data dir '" + pgdata +
            "' exists but is not initialized (missing PG_VERSION): move it aside and retry");
    }

    // initdb with scram from the start: password via 0600 pwfile so the key
    // never appears in argv or logs.
    const std::string pwfile = socketDir_ + "/.initpw.tmp";
    {
        std::ofstream out(pwfile, std::ios::trunc);
        if (!out)
        {
            throw errors::database::ConnectionFailed("Cannot write initdb pwfile in '" + socketDir_ + "'");
        }

        out << key_;
    }

    std::error_code ec;
    std::filesystem::permissions(pwfile, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, ec);
    (void)ec;

    const std::string cmd = shellQuote(bins.initdb) + " -D " + shellQuote(pgdata) +
                            " -U postgres --auth=scram-sha-256 --pwfile=" + shellQuote(pwfile);
    const int rc = runCommand(cmd);

    std::filesystem::remove(pwfile, ec);
    (void)ec;

    if (rc != 0)
    {
        throw errors::database::ConnectionFailed("initdb failed for '" + pgdata +
                                                 "' (is the postgres package installed?)");
    }

    logging::logInfo("database", "PostgreSQL cluster initialized at '" + pgdata + "' (scram-sha-256)");
}

void PostgresqlLocalServer::startServer() const
{
    const PostgresqlBinaries bins = findPostgresqlBinaries();
    ensureSocketDir();

    const std::string opts = "-k " + socketDir_ + " -p " + std::to_string(port_) + " -h 127.0.0.1";
    const std::string cmd = shellQuote(bins.pgCtl) + " -D " + shellQuote(pgdataDir()) + " -o " + shellQuote(opts) +
                            " -l " + shellQuote(logFile()) + " start";

    if (runCommand(cmd) != 0)
    {
        throw errors::database::ConnectionFailed("Cannot start PostgreSQL server at '" + pgdataDir() + "' (see " +
                                                 logFile() + ")");
    }

    // Wait for readiness (socket may lag behind pg_ctl's fork).
    for (int i = 0; i < 50; ++i)
    {
        if (isRunning())
        {
            try
            {
                PostgresqlDatabase probe(socketDir_, port_, "postgres", "postgres", key_);
                probe.open();
                logging::logInfo("database", "PostgreSQL server accepting connections");
                return;
            }
            catch (...)
            {
                // Still starting (or trust-auth transition); keep polling.
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    throw errors::database::ConnectionFailed("PostgreSQL server did not become ready at '" + socketDir_ + "'");
}

void PostgresqlLocalServer::restartServer() const
{
    const PostgresqlBinaries bins = findPostgresqlBinaries();
    std::string cmd = shellQuote(bins.pgCtl) + " -D " + shellQuote(pgdataDir()) + " stop -m fast >/dev/null 2>&1";
    (void)std::system(cmd.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    startServer();
}

void PostgresqlLocalServer::ensureRoleAndDb() const
{
    PostgresqlDatabase maintenance(socketDir_, port_, "postgres", "postgres", key_);
    try
    {
        maintenance.open();
    }
    catch (const errors::database::ConnectionFailed& ex)
    {
        throw errors::database::ConnectionFailed(std::string("Cannot reach PostgreSQL maintenance db as 'postgres': ") +
                                                 ex.what() +
                                                 " (is the server running with the configured database.key?)");
    }

    // Both probes select text via cast(... as text): the oatpp-postgresql
    // result mapper maps text-typed columns only (see PostgresqlDatabase.hpp).
    const auto roleRows = maintenance.execute("SELECT cast(1 as text) FROM pg_roles WHERE rolname = ?", {user_});
    if (roleRows.empty())
    {
        maintenance.execute("CREATE ROLE " + quoteIdentifier(user_) + " LOGIN PASSWORD " + quoteLiteral(key_) +
                            " CREATEDB");
        logging::logInfo("database", "PostgreSQL role '" + user_ + "' created");
    }
    else
    {
        // Enforce the configured key so config and server never drift apart.
        maintenance.execute("ALTER ROLE " + quoteIdentifier(user_) + " WITH LOGIN PASSWORD " + quoteLiteral(key_) +
                            " CREATEDB");
    }

    const auto dbRows = maintenance.execute("SELECT cast(1 as text) FROM pg_database WHERE datname = ?", {dbname_});
    if (dbRows.empty())
    {
        maintenance.execute("CREATE DATABASE " + quoteIdentifier(dbname_) + " OWNER " + quoteIdentifier(user_));
        logging::logInfo("database", "PostgreSQL database '" + dbname_ + "' created");
    }
}

void PostgresqlLocalServer::ensurePasswordProtection()
{
    const std::string pgHba = pgdataDir() + "/pg_hba.conf";
    if (!fileContainsTrustAuth(pgHba))
    {
        return;
    }

    logging::logWarn("database", "pg_hba.conf still uses trust auth; upgrading to scram-sha-256");

    // Set passwords while trust still lets us in, then flip pg_hba and restart.
    {
        PostgresqlDatabase maintenance(socketDir_, port_, "postgres", "postgres", key_);
        maintenance.open();
        maintenance.execute("ALTER USER " + quoteIdentifier("postgres") + " WITH PASSWORD " + quoteLiteral(key_));
        // App role password enforced in ensureRoleAndDb; repeat here in case
        // this path runs standalone (text cast as everywhere, see ensureRoleAndDb).
        const auto roleRows = maintenance.execute("SELECT cast(1 as text) FROM pg_roles WHERE rolname = ?", {user_});
        if (!roleRows.empty())
        {
            maintenance.execute("ALTER ROLE " + quoteIdentifier(user_) + " WITH PASSWORD " + quoteLiteral(key_));
        }
    }

    rewriteTrustToScram(pgHba);
    logging::logInfo("database", "pg_hba.conf upgraded to scram-sha-256; restarting");
    restartServer();

    // Verify scram actually works with the configured key now.
    PostgresqlDatabase verify(socketDir_, port_, "postgres", "postgres", key_);
    verify.open();
    logging::logInfo("database", "Password protection verified (scram-sha-256)");
}

void PostgresqlLocalServer::ensureProvisioned()
{
    if (key_.empty())
    {
        throw errors::database::ConnectionFailed("PostgreSQL key must not be empty (database.key)");
    }

    if (!isInitialized())
    {
        if (tcpPortInUse(port_))
        {
            throw errors::database::ConnectionFailed(
                "Port " + std::to_string(port_) +
                " is already in use by another PostgreSQL server; cannot initialize database at '" + pgdataDir() +
                "' there");
        }

        logging::logInfo("database", "PostgreSQL server missing at '" + pgdataDir() + "'; creating");

        runInitdb();
        startServer();
        ensureRoleAndDb();
        ensurePasswordProtection();

        logging::logInfo("database", "PostgreSQL server created and started");
        return;
    }

    if (!isRunning())
    {
        if (tcpPortInUse(port_))
        {
            throw errors::database::ConnectionFailed(
                "Port " + std::to_string(port_) +
                " is already in use by another PostgreSQL server; cannot start database at '" + pgdataDir() +
                "' there");
        }

        logging::logInfo("database", "PostgreSQL server exists but is not running; starting");
        startServer();
    }
    else
    {
        logging::logInfo("database", "PostgreSQL server already running");
    }

    // Role/db + scram enforcement on every boot (idempotent). If the server
    // was running with stale socket options, the maintenance connect below
    // fails and we restart once with the configured options.
    try
    {
        ensureRoleAndDb();
    }
    catch (const errors::database::ConnectionFailed&)
    {
        if (isRunning())
        {
            logging::logWarn("database",
                             "Maintenance connection failed while server reports running; "
                             "restarting with configured socket/port");

            restartServer();
            ensureRoleAndDb();
        }
        else
        {
            throw;
        }
    }

    ensurePasswordProtection();
}

}  // namespace app::infrastructure::database
