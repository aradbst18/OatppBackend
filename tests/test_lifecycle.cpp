#include "support/TestRunner.hpp"

#include "support/ScopedEnvironment.hpp"
#include "support/TestDb.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

#include "app/application/Application.hpp"
#include "app/application/ManagedThread.hpp"
#include "app/application/TerminateHandler.hpp"
#include "app/config/AppSettings.hpp"
#include "app/errors/AppException.hpp"

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
    const tests::ScopedEnvironment username("APP_SEED_ADMIN_USERNAME", "customAdmin");
    const tests::ScopedEnvironment password("APP_SEED_ADMIN_PASSWORD", "TestAdmin8391");
    application::Application app(applicationSettings());
    app.initialize();
    EXPECT_EQ(app.authService()->login("customAdmin", "TestAdmin8391").role, std::string("admin"));

    auto users = app.userService();
    EXPECT_THROW_AS(users->deleteUser("customAdmin"), errors::authorization::ResourceAccessDenied);
    const services::UpdateUserInput rename("renamedAdmin", "TestAdmin8391", "admin");
    EXPECT_THROW_AS(users->updateUser("customAdmin", rename), errors::authorization::ResourceAccessDenied);
    services::PatchUserInput patch;
    patch.username = "renamedAdmin";
    EXPECT_THROW_AS(users->patchUser("customAdmin", patch), errors::authorization::ResourceAccessDenied);

    users->createUser({"admin", "OtherAdmin8391", "viewer"});
    EXPECT_EQ(users->deleteAllUsers().deletedCount, 1);
    EXPECT_EQ(users->listUsers().size(), 1U);
    EXPECT_EQ(users->getUser("customAdmin").role, std::string("admin"));
    app.stop();
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
