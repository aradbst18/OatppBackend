#pragma once

// Test-only database helpers (see docs/testing.md "PostgreSQL tests").
// - File backends: :memory: databases (hermetic, no disk, no server).
// - POSTGRESQL user databases: each createTestDatabase() call provisions a
//   FRESH uniquely-named database (oatpp_test_<pid>_<counter>) through a
//   maintenance connection, because PostgreSQL has no :memory: equivalent
//   and tests must not share schema. The previously provisioned database is
//   dropped on the next call (bounded to one leftover); stale names from
//   crashed runs are swept on first use. All best-effort: a missing server
//   surfaces errors::database::ConnectionFailed, never silence.
// - Connection parameters come from the environment (PGHOST/PGPORT/
//   PGDATABASE/PGUSER/PGPASSWORD) with local-throwaway defaults matching the
//   documented initdb setup.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "app/auth/PasswordHash.hpp"
#include "app/errors/AppException.hpp"
#include "app/infrastructure/database/DatabaseFactory.hpp"
#include "app/infrastructure/database/IDatabase.hpp"
#include "app/repositories/UserRepository.hpp"

#include <unistd.h>

#ifdef APP_WITH_POSTGRESQL
#include "app/infrastructure/database/PostgresqlDatabase.hpp"
#endif  // APP_WITH_POSTGRESQL

namespace app::tests
{

inline std::string testDatabaseKey()
{
#if defined(APP_USER_DATABASE_ENCRYPTED)
    return "test-key-123";
#else
    return "";
#endif  // APP_USER_DATABASE_ENCRYPTED
}

#ifdef APP_WITH_POSTGRESQL
struct PgTestParams
{
    std::string path = "127.0.0.1";
    std::uint16_t port = 5432;
    std::string maintenanceDb = "postgres";
    std::string user = "oatpp_tester";
    std::string key = "oatpp_test_pw";
};

inline std::string testEnv(const char* name, const std::string& fallback)
{
    const char* value = std::getenv(name);

    return (value != nullptr && value[0] != '\0') ? value : fallback;
}

inline PgTestParams pgTestParams()
{
    PgTestParams params;
    params.path = testEnv("PGHOST", params.path);

    try
    {
        params.port = static_cast<std::uint16_t>(std::stoi(testEnv("PGPORT", "5432")));
    }
    catch (const std::exception&)
    {
        throw errors::config::Invalid("PGPORT must be numeric");
    }

    params.maintenanceDb = testEnv("PGDATABASE", params.maintenanceDb);
    params.user = testEnv("PGUSER", params.user);
    params.key = testEnv("PGPASSWORD", params.key);

    return params;
}

inline std::shared_ptr<infrastructure::database::IDatabase> openPgMaintenance(const PgTestParams& params)
{
    auto db = std::make_shared<infrastructure::database::PostgresqlDatabase>(
        params.path, params.port, params.maintenanceDb, params.user, params.key);
    db->open();

    return db;
}

inline std::string quotePgIdentifier(const std::string& name)
{
    std::string out = "\"";
    for (char c : name)
    {
        out += (c == '"') ? "\"\"" : std::string(1, c);
    }

    out += "\"";

    return out;
}

// Provision a fresh test database; drops the previous one our process made
// and sweeps stale oatpp_test_* names once per process.
inline std::string makeUniquePgTestDb(const PgTestParams& params)
{
    static std::atomic<unsigned> counter{0};
    static std::string previous;
    static bool swept = false;

    auto maintenance = openPgMaintenance(params);
    if (!swept)
    {
        swept = true;
        try
        {
            // cast(datname as text): the oatpp-postgresql result mapper maps
            // text-typed columns only (see PostgresqlDatabase.hpp).
            const auto rows =
                maintenance->execute("SELECT cast(datname as text) FROM pg_database WHERE datname LIKE 'oatpp_test_%'");
            for (const auto& row : rows)
            {
                if (!row.empty() && row[0] != previous)
                {
                    try
                    {
                        maintenance->execute("DROP DATABASE IF EXISTS " + quotePgIdentifier(row[0]));
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    if (!previous.empty())
    {
        try
        {
            maintenance->execute("DROP DATABASE IF EXISTS " + quotePgIdentifier(previous));
        }
        catch (...)
        {
        }

        previous.clear();
    }

    const std::string name = "oatpp_test_" + std::to_string(static_cast<long long>(::getpid())) + "_" +
                             std::to_string(counter.fetch_add(1) + 1);
    maintenance->execute("CREATE DATABASE " + quotePgIdentifier(name));
    previous = name;

    return name;
}
#endif  // APP_WITH_POSTGRESQL

// Fill user-database settings for full-application tests (test_api,
// test_lifecycle, test_websocket): :memory:/key for file backends, a fresh
// provisioned database for POSTGRESQL builds. Unconditional so every backend
// calls one helper; the POSTGRESQL branch needs APP_WITH_POSTGRESQL (a
// hand-made build with the backend but without the support fails here and in
// DatabaseFactory, never silently).
template <typename Settings>
void applyTestDatabaseSettings(Settings& settings)
{
#ifdef APP_DATABASE_POSTGRESQL
    const PgTestParams params = pgTestParams();
    settings.databasePath = params.path;
    settings.databasePort = params.port;
    settings.databaseName = makeUniquePgTestDb(params);
    settings.databaseUser = params.user;
    settings.databaseKey = params.key;
#else
    settings.databasePath = ":memory:";

#ifdef APP_USER_DATABASE_ENCRYPTED
    settings.databaseKey = testDatabaseKey();
#endif  // APP_USER_DATABASE_ENCRYPTED
#endif  // APP_DATABASE_POSTGRESQL
}

inline std::shared_ptr<infrastructure::database::IDatabase> createTestDatabase(const std::string& path = ":memory:")
{
#ifdef APP_DATABASE_POSTGRESQL
    const PgTestParams params = pgTestParams();
    const std::string name = makeUniquePgTestDb(params);

    return infrastructure::database::DatabaseFactory::create(params.path, params.port, name, params.user, params.key);
#elif defined(APP_USER_DATABASE_ENCRYPTED)
    return infrastructure::database::DatabaseFactory::create(path, testDatabaseKey());
#else
    return infrastructure::database::DatabaseFactory::create(path);
#endif  // APP_DATABASE_POSTGRESQL
}

// Seeds one user through the repository model (the same API the application
// uses); tests call this instead of building a UserDto by hand.
inline void seedUser(repositories::UserRepository& repo, const std::string& username, const std::string& role,
                     const std::string& password = "Alice1234")
{
    auto user = oatpp::Object<repositories::UserDto>::createShared();
    user->username = username;
    user->passwordHash = auth::passwordHash(password);
    user->role = role;

    repo.createUser(user);
}

}  // namespace app::tests
