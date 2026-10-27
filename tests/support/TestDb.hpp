#pragma once

// Test-only database helpers (see docs/testing.md "PostgreSQL tests").
// - File backends: :memory: databases (hermetic, no disk, no server).
// - POSTGRESQL user databases: each createTestDatabase() call provisions a
//   FRESH uniquely-named database (oatpp_test_<run>_<counter>) through a
//   maintenance connection, because PostgreSQL has no :memory: equivalent
//   and tests must not share schema. Only successful creations are recorded
//   for cleanup after all test fixtures have released their connections.
//   Databases from other or crashed runs are never swept. A missing server
//   surfaces errors::database::ConnectionFailed.
// - Connection parameters come from the environment (PGHOST/PGPORT/
//   PGDATABASE/PGUSER/PGPASSWORD) with local-throwaway defaults matching the
//   documented initdb setup.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

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

/// Tracks only databases created by this run, including the original target
/// and credentials. Cleanup is explicit so it runs before static destruction
/// and after the test fixtures release their connections.
class PgTestDbRegistry
{
public:
    PgTestDbRegistry() = default;
    PgTestDbRegistry(const PgTestDbRegistry&) = delete;
    PgTestDbRegistry& operator=(const PgTestDbRegistry&) = delete;

    std::string create(const PgTestParams& params)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        useCurrentProcess();
        auto maintenance = openPgMaintenance(params);
        // 24 hex digits plus a uint64 counter fit PostgreSQL's 63-byte name
        // limit. The run token prevents collisions across PID reuse/containers.
        const std::string name = "oatpp_test_" + runId_ + "_" + std::to_string(++counter_);
        Entry entry{params, name};
        // Allocate bookkeeping before CREATE, so allocation failure cannot
        // leave an unrecorded database after a successful statement.
        entries_.reserve(entries_.size() + 1);
        maintenance->execute("CREATE DATABASE " + quotePgIdentifier(name));
        entries_.push_back(std::move(entry));

        return name;
    }

    /// Drops exact recorded names only. Failed entries remain available for
    /// retry; diagnostics omit connection errors because they may carry secrets.
    bool cleanup(std::ostream& diagnostics)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        useCurrentProcess();
        auto entry = entries_.begin();
        while (entry != entries_.end())
        {
            try
            {
                auto maintenance = openPgMaintenance(entry->params);
                maintenance->execute("DROP DATABASE IF EXISTS " + quotePgIdentifier(entry->name));
                entry = entries_.erase(entry);
            }
            catch (...)
            {
                diagnostics << "[FAIL] PostgreSQL test database cleanup: " << entry->name << " (retained for retry)\n";
                ++entry;
            }
        }

        return entries_.empty();
    }

private:
    struct Entry
    {
        PgTestParams params;
        std::string name;
    };

    void useCurrentProcess()
    {
        const auto pid = ::getpid();
        if (pid != processId_)
        {
            // A forked child must never inherit authority to delete its
            // parent's databases. Generate its own run token on first use.
            auto runId = auth::sha256Hex(auth::generateRandomSecret(16)).substr(0, 24);
            entries_.clear();
            counter_ = 0;
            runId_ = std::move(runId);
            processId_ = pid;
        }
    }

    std::mutex mutex_;
    std::vector<Entry> entries_;
    std::string runId_;
    std::uint64_t counter_ = 0;
    pid_t processId_ = 0;
};

inline PgTestDbRegistry& pgTestDbRegistry()
{
    static PgTestDbRegistry registry;

    return registry;
}

inline std::string makeUniquePgTestDb(const PgTestParams& params)
{
    return pgTestDbRegistry().create(params);
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
