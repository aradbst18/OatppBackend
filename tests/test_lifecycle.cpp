#include "support/TestRunner.hpp"

#include "support/ScopedEnvironment.hpp"
#include "support/TestDb.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "app/application/Application.hpp"
#include "app/application/ManagedThread.hpp"
#include "app/application/TerminateHandler.hpp"
#include "app/config/AppSettings.hpp"
#include "app/errors/AppException.hpp"
#include "app/logging/Logger.hpp"

using namespace app;

namespace
{

config::AppSettings applicationSettings()
{
    config::AppSettings settings = config::AppSettings::defaults();
    tests::applyTestDatabaseSettings(settings);
    settings.jwtSecret = "test-secret-123456789012345678901234";
    settings.logLevel = "ERROR";
    settings.logPath = "";
    return settings;
}

}  // namespace

TEST_CASE("lifecycle: managed thread starts and joins")
{
    std::atomic<int> ticks{0};
    application::ManagedThread worker("test-worker",
                                      [&](const std::atomic<bool>& stop)
                                      {
                                          while (!stop.load())
                                          {
                                              ++ticks;
                                              std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                          }
                                      });

    EXPECT_FALSE(worker.isRunning());

    worker.start();
    EXPECT_TRUE(worker.isRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    worker.requestStop();
    worker.join();

    EXPECT_FALSE(worker.isRunning());
    EXPECT_TRUE(ticks.load() > 0);
}

TEST_CASE("lifecycle: managed thread stop is idempotent")
{
    application::ManagedThread worker("idle",
                                      [](const std::atomic<bool>& stop)
                                      {
                                          while (!stop.load())
                                          {
                                              std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                          }
                                      });

    worker.start();
    worker.stop();
    worker.stop();

    EXPECT_FALSE(worker.isRunning());
}

TEST_CASE("lifecycle: managed thread restarts after natural completion")
{
    std::atomic<int> calls{0};
    application::ManagedThread worker("short-lived", [&](const std::atomic<bool>&) { ++calls; });

    for (int run = 1; run <= 3; ++run)
    {
        worker.start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (worker.isRunning() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        EXPECT_FALSE(worker.isRunning());
        EXPECT_EQ(calls.load(), run);
    }
    worker.join();
}

TEST_CASE("lifecycle: managed thread recovers from a failing work copy")
{
    struct Work
    {
        bool& fail;
        std::atomic<int>& calls;

        Work(bool& shouldFail, std::atomic<int>& count) : fail(shouldFail), calls(count) {}
        Work(const Work& other) : fail(other.fail), calls(other.calls)
        {
            if (fail)
            {
                throw std::runtime_error("forced work copy failure");
            }
        }
        void operator()(const std::atomic<bool>&) const { ++calls; }
    };

    bool fail = false;
    std::atomic<int> calls{0};
    application::ManagedThread worker("copy-failure", Work(fail, calls));
    fail = true;
    EXPECT_THROW_AS(worker.start(), std::runtime_error);
    EXPECT_FALSE(worker.isRunning());
    EXPECT_EQ(calls.load(), 0);

    fail = false;
    worker.start();
    worker.join();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_FALSE(worker.isRunning());
}

TEST_CASE("lifecycle: concurrent managed thread starts launch only one worker")
{
    std::atomic<int> calls{0};
    application::ManagedThread worker("concurrent-start", [&](const std::atomic<bool>& stop)
                                     {
                                         ++calls;
                                         while (!stop.load())
                                         {
                                             std::this_thread::yield();
                                         }
                                     });
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<void>> callers;
    for (int i = 0; i < 12; ++i)
    {
        callers.push_back(std::async(std::launch::async, [&] { ready.wait(); worker.start(); }));
    }
    start.set_value();
    for (auto& caller : callers)
    {
        caller.get();
    }
    worker.stop();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_FALSE(worker.isRunning());
}

TEST_CASE("lifecycle: managed thread serializes concurrent start stop and join")
{
    std::atomic<int> active{0};
    std::atomic<bool> overlap{false};
    application::ManagedThread worker("lifecycle-race", [&](const std::atomic<bool>& stop)
                                     {
                                         if (active.fetch_add(1) != 0)
                                         {
                                             overlap.store(true);
                                         }
                                         // A join may follow the last stop and wait on a fresh run.
                                         for (int tick = 0; tick < 500 && !stop.load(); ++tick)
                                         {
                                             std::this_thread::yield();
                                         }
                                         --active;
                                     });
    for (int round = 0; round < 20; ++round)
    {
        worker.start();
        std::promise<void> start;
        const auto ready = start.get_future().share();
        std::vector<std::future<void>> callers;
        for (int i = 0; i < 12; ++i)
        {
            callers.push_back(std::async(std::launch::async, [&, i]
                                         {
                                             ready.wait();
                                             if (i % 3 == 0)
                                                 worker.start();
                                             else if (i % 3 == 1)
                                                 worker.stop();
                                             else
                                                 worker.join();
                                         }));
        }
        start.set_value();
        for (auto& caller : callers)
        {
            caller.get();
        }
        worker.stop();
        EXPECT_FALSE(worker.isRunning());
    }
    EXPECT_EQ(active.load(), 0);
    EXPECT_FALSE(overlap.load());
}

TEST_CASE("lifecycle: managed thread stop can signal during an external join")
{
    std::promise<void> entered;
    bool expired = false;
    application::ManagedThread worker("join-then-stop", [&](const std::atomic<bool>& stop)
                                     {
                                         entered.set_value();
                                         const auto deadline = std::chrono::steady_clock::now() +
                                                               std::chrono::seconds(2);
                                         while (!stop.load())
                                         {
                                             if (std::chrono::steady_clock::now() >= deadline)
                                             {
                                                 expired = true;
                                                 break;
                                             }
                                             std::this_thread::yield();
                                         }
                                     });
    worker.start();
    entered.get_future().wait();
    auto joiner = std::async(std::launch::async, [&] { worker.join(); });
    const auto status = joiner.wait_for(std::chrono::milliseconds(20));
    worker.stop();
    joiner.get();
    EXPECT_TRUE(status == std::future_status::timeout);
    EXPECT_FALSE(expired);
    EXPECT_FALSE(worker.isRunning());
}

TEST_CASE("lifecycle: managed thread self calls do not block an external join")
{
    std::promise<void> entered;
    std::promise<void> release;
    const auto ready = release.get_future().share();
    bool stopObserved = false;
    application::ManagedThread worker("self-calls", [&](const std::atomic<bool>& stop)
                                     {
                                         entered.set_value();
                                         ready.wait();
                                         worker.start();
                                         worker.join();
                                         worker.stop();
                                         stopObserved = stop.load();
                                     });
    worker.start();
    entered.get_future().wait();
    auto joiner = std::async(std::launch::async, [&] { worker.join(); });
    const auto status = joiner.wait_for(std::chrono::milliseconds(20));
    release.set_value();
    joiner.get();
    EXPECT_TRUE(status == std::future_status::timeout);
    EXPECT_TRUE(stopObserved);
    EXPECT_FALSE(worker.isRunning());
}

TEST_CASE("lifecycle: application initialize is idempotent")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    application::Application app(applicationSettings());
    app.initialize();
    app.initialize();

    EXPECT_TRUE(app.authService() != nullptr);
    EXPECT_TRUE(app.jwtService() != nullptr);
    EXPECT_TRUE(app.database() != nullptr);

    app.stop();
    app.stop();  // idempotent
}

TEST_CASE("lifecycle: refresh cleanup retries after failure and stops before database shutdown")
{
    struct CleanupState
    {
        std::string path = "/tmp/oatpp-cpp018-" + auth::sha256Hex(auth::generateRandomSecret(16)) + ".db";
        std::promise<void> failed;
        std::atomic<bool> failureObserved{false};
        ~CleanupState()
        {
            logging::Logger::instance().resetToDefault();
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } state;
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    auto settings = applicationSettings();
#ifdef APP_DATABASE_SQLCIPHER
    settings.databasePath = state.path;
#endif
    {
        application::Application seed(settings);
        seed.initialize();
        const auto db = seed.database();
        if (db->backendName() == "POSTGRESQL")
        {
            db->executeVoid(
                "CREATE FUNCTION reject_refresh_delete() RETURNS trigger LANGUAGE plpgsql "
                "AS 'BEGIN RAISE EXCEPTION ''forced delete failure''; END';");
            db->executeVoid(
                "CREATE TRIGGER reject_refresh_delete BEFORE DELETE ON refresh_tokens "
                "FOR EACH ROW EXECUTE FUNCTION reject_refresh_delete();");
        }
        else
        {
            db->executeVoid(
                "CREATE TRIGGER reject_refresh_delete BEFORE DELETE ON refresh_tokens "
                "BEGIN SELECT RAISE(ABORT, 'forced delete failure'); END;");
        }
        db->executeVoid(
            "INSERT INTO refresh_tokens (token, user_id, session_version, expires_at, revoked) "
            "SELECT 'expired', id, session_version, 1, 1 FROM users WHERE username = 'admin';");
        db->executeVoid(
            "INSERT INTO refresh_tokens (token, user_id, session_version, expires_at, revoked) "
            "SELECT 'retained', id, session_version, 4102444800, 1 FROM users WHERE username = 'admin';");
        seed.stop();
    }
    auto failure = state.failed.get_future();
    logging::Logger::instance().configure(logging::Level::Error,
                                          [&](logging::Level, const std::string&, const std::string& message)
                                          {
                                              if (message.find("Refresh token cleanup failed:") == 0 &&
                                                  !state.failureObserved.exchange(true))
                                              {
                                                  state.failed.set_value();
                                              }
                                          });
    application::Application app(settings);
    app.initialize();
    EXPECT_TRUE(failure.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto db = app.database();
    EXPECT_EQ(db->execute("SELECT token FROM refresh_tokens WHERE token = 'expired';").size(), std::size_t{1});
    db->executeVoid(db->backendName() == "POSTGRESQL" ? "DROP TRIGGER reject_refresh_delete ON refresh_tokens;"
                                                      : "DROP TRIGGER reject_refresh_delete;");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(70);
    while (!db->execute("SELECT token FROM refresh_tokens WHERE token = 'expired';").empty() &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(db->execute("SELECT token FROM refresh_tokens WHERE token = 'expired';").empty());
    EXPECT_EQ(app.refreshTokenRepository()->getToken("retained")->revoked.getValue(0), 1);
    const auto stopping = std::chrono::steady_clock::now();
    app.stop();
    app.stop();
    EXPECT_TRUE(std::chrono::steady_clock::now() - stopping < std::chrono::seconds(2));
    EXPECT_FALSE(db->isOpen());
}

TEST_CASE("lifecycle: concurrent initialization publishes complete shared objects")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    application::Application app(applicationSettings());
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<std::array<const void*, 6>>> callers;
    for (int i = 0; i < 12; ++i)
    {
        callers.push_back(std::async(std::launch::async,
                                     [&, ready, i]() -> std::array<const void*, 6>
                                     {
                                         ready.wait();
                                         if (i % 2 == 0)
                                         {
                                             app.initialize();
                                         }
                                         return {app.router().get(),      app.authService().get(),
                                                 app.userService().get(), app.jwtService().get(),
                                                 app.database().get(),    app.refreshTokenRepository().get()};
                                     }));
    }
    start.set_value();
    std::vector<std::array<const void*, 6>> objects;
    for (auto& caller : callers)
    {
        objects.push_back(caller.get());
    }
    for (const auto& snapshot : objects)
    {
        for (const auto* object : snapshot)
        {
            EXPECT_TRUE(object != nullptr);
        }
        EXPECT_TRUE(snapshot == objects.front());
    }
    EXPECT_TRUE(app.database()->isOpen());
    EXPECT_EQ(app.authService()->login("admin", "TestAdmin8391").role, std::string("admin"));
    EXPECT_EQ(app.userService()->listUsers().size(), 1U);
}

TEST_CASE("lifecycle: startup failure persists until a new application is constructed")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", nullptr);
    const auto settings = applicationSettings();
    {
        application::Application app(settings);
        std::exception_ptr failure;
        try
        {
            app.initialize();
        }
        catch (const errors::config::Missing&)
        {
            failure = std::current_exception();
        }
        EXPECT_TRUE(failure != nullptr);

        const tests::ScopedEnvironment fixedUsername("APP_SEED_ADMIN_USERNAME", "admin");
        const tests::ScopedEnvironment fixedPassword("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            std::exception_ptr repeated;
            try
            {
                app.initialize();
            }
            catch (const errors::config::Missing&)
            {
                repeated = std::current_exception();
            }
            EXPECT_TRUE(repeated == failure);
            EXPECT_THROW_AS(app.router(), errors::config::Missing);
            EXPECT_THROW_AS(app.authService(), errors::config::Missing);
            EXPECT_THROW_AS(app.userService(), errors::config::Missing);
            EXPECT_THROW_AS(app.jwtService(), errors::config::Missing);
            EXPECT_THROW_AS(app.database(), errors::config::Missing);
            EXPECT_THROW_AS(app.refreshTokenRepository(), errors::config::Missing);
            EXPECT_THROW_AS(app.run(), errors::config::Missing);
            EXPECT_FALSE(app.isRunning());
            app.stop();
        }
    }

    const tests::ScopedEnvironment fixedUsername("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment fixedPassword("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    application::Application recovered(settings);
    recovered.initialize();
    EXPECT_TRUE(recovered.database()->isOpen());
    EXPECT_EQ(recovered.authService()->login("admin", "TestAdmin8391").role, std::string("admin"));
}

TEST_CASE("lifecycle: shutdown is synchronized with initialization")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    application::Application app(applicationSettings());
    std::promise<void> start;
    const auto ready = start.get_future().share();
    auto initialization = std::async(std::launch::async,
                                     [&]()
                                     {
                                         ready.wait();
                                         app.initialize();
                                     });
    std::vector<std::future<void>> shutdowns;
    for (int i = 0; i < 12; ++i)
    {
        shutdowns.push_back(std::async(std::launch::async,
                                       [&]()
                                       {
                                           ready.wait();
                                           app.stop();
                                       }));
    }
    start.set_value();
    initialization.get();
    for (auto& shutdown : shutdowns)
    {
        shutdown.get();
    }
    app.stop();
    EXPECT_FALSE(app.database()->isOpen());
    EXPECT_FALSE(app.isRunning());
}

TEST_CASE("environment: concurrent first use across application instances")
{
    // Fail after environment/database setup so instances never compete for a listening port.
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", nullptr);
    std::vector<std::unique_ptr<application::Application>> apps;
    for (int i = 0; i < 12; ++i)
    {
        apps.push_back(std::make_unique<application::Application>(applicationSettings()));
    }
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<void>> callers;
    for (const auto& app : apps)
    {
        callers.push_back(std::async(std::launch::async,
                                     [&, ready, instance = app.get()]()
                                     {
                                         ready.wait();
                                         EXPECT_THROW_AS(instance->initialize(), errors::config::Missing);
                                         EXPECT_FALSE(instance->isRunning());
                                     }));
    }
    start.set_value();
    for (auto& caller : callers)
    {
        caller.get();
    }
}

TEST_CASE("lifecycle: application startup requires explicit initial administrator credentials")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", nullptr);
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", nullptr);
    application::Application app(applicationSettings());
    EXPECT_THROW_AS(app.initialize(), errors::config::Missing);
    EXPECT_FALSE(app.isRunning());
}

TEST_CASE("lifecycle: application startup rejects the published bootstrap password")
{
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "admin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "Admin1234");
    application::Application app(applicationSettings());
    EXPECT_THROW_AS(app.initialize(), errors::config::Invalid);
    EXPECT_FALSE(app.isRunning());
}

TEST_CASE("lifecycle: custom bootstrap administrator is protected by user management")
{
    struct DatabaseFile
    {
        std::string path = "/tmp/oatpp-bootstrap-" + auth::sha256Hex(auth::generateRandomSecret(16)) + ".db";
        ~DatabaseFile()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } file;
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "customAdmin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    auto settings = applicationSettings();
#ifdef APP_DATABASE_SQLCIPHER
    settings.databasePath = file.path;
#endif
    application::Application app(settings);
    app.initialize();
    EXPECT_EQ(app.authService()->login("customAdmin", "TestAdmin8391").role, std::string("admin"));

    auto users = app.userService();
    EXPECT_THROW_AS(users->deleteUser("customAdmin"), errors::authorization::ResourceAccessDenied);
    const services::UpdateUserInput rename("renamedAdmin", "TestAdmin8391", "admin");
    EXPECT_THROW_AS(users->updateUser("customAdmin", rename), errors::authorization::ResourceAccessDenied);
    services::PatchUserInput patch;
    patch.username = "renamedAdmin";
    EXPECT_THROW_AS(users->patchUser("customAdmin", patch), errors::authorization::ResourceAccessDenied);

    const services::UpdateUserInput demote("customAdmin", "RejectedPassword1", "operator");
    EXPECT_THROW_AS(users->updateUser("customAdmin", demote), errors::authorization::ResourceAccessDenied);
    patch.username.reset();
    patch.role = "viewer";
    EXPECT_THROW_AS(users->patchUser("customAdmin", patch), errors::authorization::ResourceAccessDenied);

    users->createUser({"admin", "OtherAdmin8391", "viewer"});
    EXPECT_EQ(users->deleteAllUsers().deletedCount, 1);
    EXPECT_EQ(users->listUsers().size(), 1U);
    EXPECT_EQ(users->getUser("customAdmin").role, std::string("admin"));
    app.stop();

    const tests::ScopedEnvironment noPassword("APP_SEED_ADMIN_PASSWORD", nullptr);
    application::Application restarted(settings);
    restarted.initialize();
    EXPECT_EQ(restarted.authService()->login("customAdmin", "TestAdmin8391").role, std::string("admin"));
    restarted.stop();
}

TEST_CASE("lifecycle: terminate handler installs without crashing")
{
    application::installTerminateHandler();
    application::installTerminateHandler();  // second call ignored

    EXPECT_TRUE(true);
}

TEST_CASE("lifecycle: missing config file is created from defaults")
{
    const std::string path = "/tmp/opencode/oatpp-backend-missing-xyz.json";
    std::remove(path.c_str());

    const config::AppSettings s = application::loadSettingsOrDefaults(path);

    EXPECT_EQ(s.host, std::string("127.0.0.1"));
    EXPECT_TRUE(s.configSource.find("(created)") != std::string::npos);

    // The created file round-trips through the active store.
    const config::AppSettings reloaded = application::loadSettingsOrDefaults(path);

    EXPECT_EQ(reloaded.host, s.host);
    EXPECT_EQ(reloaded.port, s.port);

    std::remove(path.c_str());
}
