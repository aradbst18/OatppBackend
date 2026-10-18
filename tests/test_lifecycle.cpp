#include "support/TestRunner.hpp"

#include "support/TestDb.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "app/application/Application.hpp"
#include "app/application/ManagedThread.hpp"
#include "app/application/TerminateHandler.hpp"
#include "app/config/AppSettings.hpp"

using namespace app;

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
    config::AppSettings s = config::AppSettings::defaults();
    tests::applyTestDatabaseSettings(s);
    s.jwtSecret = "test-secret-123456789012345678901234";
    s.logLevel = "ERROR";
    s.logPath = "";  // keep tests hermetic: no log files on disk

    application::Application app(std::move(s));
    app.initialize();
    app.initialize();

    EXPECT_TRUE(app.authService() != nullptr);
    EXPECT_TRUE(app.jwtService() != nullptr);
    EXPECT_TRUE(app.database() != nullptr);

    app.stop();
    app.stop();  // idempotent
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
