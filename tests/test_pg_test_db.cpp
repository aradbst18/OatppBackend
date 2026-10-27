#include "support/TestRunner.hpp"

#ifdef APP_DATABASE_POSTGRESQL

#include <sstream>
#include <utility>

#include "support/TestDb.hpp"

using namespace app;

namespace
{

bool databaseExists(const std::shared_ptr<infrastructure::database::IDatabase>& maintenance, const std::string& name)
{
    return !maintenance->execute("SELECT cast(datname as text) FROM pg_database WHERE datname = ?", {name}).empty();
}

struct PgRun
{
    tests::PgTestDbRegistry databases;

    ~PgRun()
    {
        databases.cleanup(std::cerr);
    }
};

// These sentinels deliberately belong to neither the provisioning helper nor
// its cleanup registry. Only this fixture may remove them.
class RetainedPgDatabase
{
public:
    RetainedPgDatabase(const tests::PgTestParams& params, std::string name)
        : maintenance_(tests::openPgMaintenance(params)), name_(std::move(name))
    {
        maintenance_->execute("CREATE DATABASE " + tests::quotePgIdentifier(name_));
    }

    RetainedPgDatabase(const RetainedPgDatabase&) = delete;
    RetainedPgDatabase& operator=(const RetainedPgDatabase&) = delete;

    ~RetainedPgDatabase()
    {
        try
        {
            maintenance_->execute("DROP DATABASE IF EXISTS " + tests::quotePgIdentifier(name_));
        }
        catch (...)
        {
        }
    }

    bool exists() const
    {
        return databaseExists(maintenance_, name_);
    }

private:
    std::shared_ptr<infrastructure::database::IDatabase> maintenance_;
    std::string name_;
};

}  // namespace

TEST_CASE("pg_test_db: provisioning preserves idle databases outside the current run")
{
    const auto params = tests::pgTestParams();
    const std::string suffix = auth::sha256Hex(auth::generateRandomSecret(16)).substr(0, 16);
    const RetainedPgDatabase retained(params, "oatpp_test_archive_" + suffix);
    const RetainedPgDatabase lookalike(params, "oatppXtestYarchive_" + suffix);

    tests::makeUniquePgTestDb(params);

    EXPECT_TRUE(retained.exists());
    EXPECT_TRUE(lookalike.exists());

    std::ostringstream diagnostics;
    EXPECT_TRUE(tests::pgTestDbRegistry().cleanup(diagnostics));
    EXPECT_TRUE(retained.exists());
    EXPECT_TRUE(lookalike.exists());
}

TEST_CASE("pg_test_db: independent runs clean up only their own successful creations")
{
    const auto params = tests::pgTestParams();
    const auto maintenance = tests::openPgMaintenance(params);
    PgRun firstRun;
    PgRun secondRun;

    const auto first = firstRun.databases.create(params);
    const auto second = secondRun.databases.create(params);
    const auto third = firstRun.databases.create(params);
    EXPECT_TRUE(first != second);
    EXPECT_TRUE(first != third);
    // No target-database connections exist: activity must not provide the
    // protection against deleting another run's database.
    EXPECT_TRUE(databaseExists(maintenance, first));
    EXPECT_TRUE(databaseExists(maintenance, second));

    std::ostringstream diagnostics;
    EXPECT_TRUE(firstRun.databases.cleanup(diagnostics));
    EXPECT_FALSE(databaseExists(maintenance, first));
    EXPECT_FALSE(databaseExists(maintenance, third));
    EXPECT_TRUE(databaseExists(maintenance, second));
    EXPECT_TRUE(firstRun.databases.cleanup(diagnostics));
    EXPECT_TRUE(secondRun.databases.cleanup(diagnostics));
    EXPECT_FALSE(databaseExists(maintenance, second));
    EXPECT_TRUE(diagnostics.str().empty());
}

TEST_CASE("pg_test_db: failed creation never authorizes cleanup of an existing database")
{
    const auto params = tests::pgTestParams();
    const auto maintenance = tests::openPgMaintenance(params);
    PgRun run;
    const auto first = run.databases.create(params);
    // Occupy the next counter value to make CREATE fail without granting this
    // registry ownership of the pre-existing database.
    const auto next = first.substr(0, first.rfind('_') + 1) + "2";
    const RetainedPgDatabase collision(params, next);

    EXPECT_THROW_AS(run.databases.create(params), errors::database::QueryFailed);
    std::ostringstream diagnostics;
    EXPECT_TRUE(run.databases.cleanup(diagnostics));
    EXPECT_FALSE(databaseExists(maintenance, first));
    EXPECT_TRUE(collision.exists());
}

TEST_CASE("pg_test_db: failed cleanup retains ownership for a later retry")
{
    const auto params = tests::pgTestParams();
    const auto maintenance = tests::openPgMaintenance(params);
    PgRun run;
    const auto name = run.databases.create(params);
    auto connected = std::make_shared<infrastructure::database::PostgresqlDatabase>(params.path, params.port, name,
                                                                                    params.user, params.key);
    connected->open();

    std::ostringstream diagnostics;
    EXPECT_FALSE(run.databases.cleanup(diagnostics));
    EXPECT_TRUE(databaseExists(maintenance, name));
    EXPECT_TRUE(diagnostics.str().find(name) != std::string::npos);
    EXPECT_TRUE(diagnostics.str().find(params.key) == std::string::npos);

    connected.reset();
    EXPECT_TRUE(run.databases.cleanup(diagnostics));
    EXPECT_FALSE(databaseExists(maintenance, name));
}

TEST_CASE("pg_test_db: cleanup keeps the original connection settings")
{
    auto params = tests::pgTestParams();
    const auto maintenance = tests::openPgMaintenance(params);
    PgRun run;
    const auto name = run.databases.create(params);
    params.path = "/nonexistent/oatpp-cpp006";
    params.maintenanceDb = name;
    params.user = "no_such_test_role";
    params.key = "changed-after-creation";

    std::ostringstream diagnostics;
    EXPECT_TRUE(run.databases.cleanup(diagnostics));
    EXPECT_FALSE(databaseExists(maintenance, name));
    EXPECT_TRUE(diagnostics.str().empty());
}

#endif  // APP_DATABASE_POSTGRESQL
