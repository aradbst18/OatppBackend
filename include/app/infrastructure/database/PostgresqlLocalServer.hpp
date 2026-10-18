#pragma once

#ifndef APP_WITH_POSTGRESQL
#error "PostgresqlLocalServer.hpp requires APP_WITH_POSTGRESQL (configure with -DAPP_ENABLE_POSTGRESQL=ON)"
#endif  // !APP_WITH_POSTGRESQL

#include <cstdint>
#include <string>

namespace app::infrastructure::database
{

/**
 * Local PostgreSQL server manager for the single-user setup
 * (socket dir = database.path, pgdata = <path>/pgdata, log = <path>/pg.log,
 * TCP on 127.0.0.1 plus the socket; see docs/postgresql.md).
 *
 * Startup contract (Application::initDatabase, POSTGRESQL builds):
 * - server missing (no pgdata/PG_VERSION) -> fail fast if TCP port is owned
 *   by a foreign server, else initdb --auth=scram-sha-256, start with
 *   pg_ctl, CREATE ROLE/DATABASE, enforce scram, continue;
 * - server exists but not running -> fail fast on foreign TCP port use,
 *   else start it, ensure role/db + scram;
 * - server running -> verify the admin user later (AuthService::ensureSeeded)
 *   after opening; missing role/db are created, trust auth is upgraded to
 *   scram-sha-256 automatically.
 *
 * Key material (database.key) is held only in memory and in a transient
 * 0600 pwfile during initdb; it is never logged and never appears in
 * process arguments (role/db work goes through libpq, not psql -c).
 */
class PostgresqlLocalServer
{
public:
    PostgresqlLocalServer(std::string socketDir, std::uint16_t port, std::string dbname, std::string user,
                          std::string key);

    const std::string& socketDir() const noexcept
    {
        return socketDir_;
    }

    std::string pgdataDir() const
    {
        return socketDir_ + "/pgdata";
    }

    std::string logFile() const
    {
        return socketDir_ + "/pg.log";
    }

    bool isInitialized() const;
    bool isRunning() const;

    /// Full startup flow described above. Idempotent; safe to call twice.
    void ensureProvisioned();

private:
    void ensureSocketDir() const;
    void runInitdb() const;
    void startServer() const;
    void restartServer() const;
    void ensureRoleAndDb() const;
    void ensurePasswordProtection();

    std::string socketDir_;
    std::uint16_t port_;
    std::string dbname_;
    std::string user_;
    std::string key_;
};

/// Binary lookup: versioned dirs first (/usr/lib/postgresql/18/bin, ...),
/// then PATH. Throws ConnectionFailed when neither initdb nor pg_ctl exists.
struct PostgresqlBinaries
{
    std::string initdb;
    std::string pgCtl;
};

PostgresqlBinaries findPostgresqlBinaries();

}  // namespace app::infrastructure::database
