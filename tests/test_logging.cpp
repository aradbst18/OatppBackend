#include "support/TestRunner.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/logging/Logger.hpp"

using namespace app;

namespace
{

/// Restore the default sink and saved settings before captured test state is destroyed.
struct LoggerGuard
{
    logging::Level level;
    std::string dir;

    LoggerGuard() : level(logging::Logger::instance().minLevel()), dir(logging::Logger::instance().logFileDirectory())
    {
    }

    ~LoggerGuard()
    {
        logging::Logger::instance().resetToDefault();
        logging::Logger::instance().setMinLevel(level);
        logging::Logger::instance().setLogFileDirectory(dir);
    }
};

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();

    return out.str();
}

}  // namespace

TEST_CASE("logging: date string follows yyyy-mm-dd")
{
    const std::string date = logging::Logger::currentDateString();

    EXPECT_TRUE(std::regex_match(date, std::regex(R"(^\d{4}-\d{2}-\d{2}$)")));
}

TEST_CASE("logging: time string follows HH:MM:SS.mmm")
{
    const std::string time = logging::Logger::currentTimeString();

    EXPECT_TRUE(std::regex_match(time, std::regex(R"(^\d{2}:\d{2}:\d{2}\.\d{3}$)")));
}

TEST_CASE("logging: resolveLogFilePath joins dated filename")
{
    const std::string path = logging::Logger::resolveLogFilePath("/tmp/opencode/logs");

    EXPECT_TRUE(std::regex_match(path, std::regex(R"(^/tmp/opencode/logs/\d{4}-\d{2}-\d{2}\.log$)")));

    // Trailing slashes do not double up.
    const std::string slashed = logging::Logger::resolveLogFilePath("/tmp/opencode/logs//");

    EXPECT_EQ(slashed, path);

    // Empty directory disables file logging: empty path.
    EXPECT_EQ(logging::Logger::resolveLogFilePath(""), std::string(""));
}

TEST_CASE("logging: formatLine starts with timestamp then level tag message")
{
    EXPECT_EQ(logging::Logger::formatLine("12:34:56.789", logging::Level::Info, "auth", "hello"),
              std::string("12:34:56.789 [INFO] [auth] hello"));

    EXPECT_EQ(logging::Logger::formatLine("00:00:00.001", logging::Level::Error, "db", "boom"),
              std::string("00:00:00.001 [ERROR] [db] boom"));
}

TEST_CASE("logging: file sink appends dated log lines")
{
    LoggerGuard guard;
    const std::string dir = "/tmp/opencode/oatpp-backend-logtest-file";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    logging::Logger::instance().setMinLevel(logging::Level::Trace);
    logging::Logger::instance().setLogFileDirectory(dir);

    logging::logInfo("logging-test", "first line");
    logging::logWarn("logging-test", "second line");

    const std::string path = logging::Logger::resolveLogFilePath(dir);
    const std::string content = readWholeFile(path);

    EXPECT_TRUE(
        std::regex_search(content, std::regex(R"(\d{2}:\d{2}:\d{2}\.\d{3} \[INFO\] \[logging-test\] first line)")));
    EXPECT_TRUE(
        std::regex_search(content, std::regex(R"(\d{2}:\d{2}:\d{2}\.\d{3} \[WARN\] \[logging-test\] second line)")));

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("logging: level filter applies to file output")
{
    LoggerGuard guard;
    const std::string dir = "/tmp/opencode/oatpp-backend-logtest-level";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    logging::Logger::instance().setMinLevel(logging::Level::Error);
    logging::Logger::instance().setLogFileDirectory(dir);

    logging::logInfo("logging-test", "dropped line");
    logging::logError("logging-test", "kept line");

    const std::string content = readWholeFile(logging::Logger::resolveLogFilePath(dir));

    EXPECT_FALSE(content.find("dropped line") != std::string::npos);
    EXPECT_TRUE(content.find("kept line") != std::string::npos);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("logging: empty directory disables file output")
{
    LoggerGuard guard;
    const std::string dir = "/tmp/opencode/oatpp-backend-logtest-disabled-xyz";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    logging::Logger::instance().setMinLevel(logging::Level::Trace);
    logging::Logger::instance().setLogFileDirectory("");

    EXPECT_EQ(logging::Logger::instance().logFileDirectory(), std::string(""));

    logging::logInfo("logging-test", "console only");

    EXPECT_FALSE(std::filesystem::exists(dir, ec));
}

TEST_CASE("logging: lazy macro avoids evaluating expression when level disabled")
{
    LoggerGuard guard;
    logging::Logger::instance().setMinLevel(logging::Level::Error);

    bool evaluated = false;
    auto expensiveMessage = [&evaluated]()
    {
        evaluated = true;

        return std::string("should not be evaluated");
    };

    APP_LOG_DEBUG("lazy-test", expensiveMessage());
    EXPECT_FALSE(evaluated);

    APP_LOG_ERROR("lazy-test", expensiveMessage());
    EXPECT_TRUE(evaluated);
}

TEST_CASE("logging: sink calls from different threads do not overlap")
{
    std::atomic<int> active{0};
    std::atomic<int> calls{0};
    std::atomic<bool> overlap{false};
    LoggerGuard guard;
    auto& logger = logging::Logger::instance();
    logger.setLogFileDirectory("");
    logger.configure(logging::Level::Info,
                     [&](logging::Level, const std::string&, const std::string&)
                     {
                         if (active.fetch_add(1) != 0)
                         {
                             overlap = true;
                         }
                         std::this_thread::sleep_for(std::chrono::microseconds(200));
                         ++calls;
                         --active;
                     });

    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<void>> callers;
    for (int i = 0; i < 8; ++i)
    {
        callers.push_back(std::async(std::launch::async,
                                     [&]
                                     {
                                         ready.wait();
                                         for (int j = 0; j < 16; ++j)
                                         {
                                             logging::logInfo("concurrent", "message");
                                         }
                                     }));
    }
    start.set_value();
    for (auto& caller : callers)
    {
        caller.get();
    }
    EXPECT_EQ(calls.load(), 128);
    EXPECT_EQ(active.load(), 0);
    EXPECT_FALSE(overlap.load());
}

TEST_CASE("logging: concurrent sink and file output preserve complete records in matching order")
{
    std::vector<std::string> messages;
    LoggerGuard guard;
    const std::string dir = "/tmp/opencode/oatpp-backend-logtest-concurrent";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    auto& logger = logging::Logger::instance();
    logger.setLogFileDirectory(dir);
    logger.configure(logging::Level::Info,
                     [&](logging::Level, const std::string&, const std::string& message)
                     {
                         messages.push_back(message);
                     });

    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<void>> callers;
    for (int i = 0; i < 8; ++i)
    {
        callers.push_back(std::async(std::launch::async,
                                     [&, i]
                                     {
                                         ready.wait();
                                         for (int j = 0; j < 16; ++j)
                                         {
                                             logging::logInfo("concurrent", std::to_string(i) + ":" +
                                                                                std::to_string(j) + ":" +
                                                                                std::string(16384, 'x'));
                                         }
                                     }));
    }
    start.set_value();
    for (auto& caller : callers)
    {
        caller.get();
    }
    EXPECT_EQ(messages.size(), std::size_t{128});
    std::ifstream file(logging::Logger::resolveLogFilePath(dir), std::ios::binary);
    std::string line;
    for (const auto& message : messages)
    {
        EXPECT_TRUE(std::getline(file, line));
        EXPECT_EQ(line.substr(13), "[INFO] [concurrent] " + message);
    }
    EXPECT_FALSE(std::getline(file, line));
    file.close();
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("logging: sinks can access configuration and emit a nested log")
{
    std::vector<std::string> messages;
    LoggerGuard guard;
    auto& logger = logging::Logger::instance();
    logger.setLogFileDirectory("");
    logger.configure(logging::Level::Info,
                     [&](logging::Level, const std::string&, const std::string& message)
                     {
                         messages.push_back(message);
                         EXPECT_TRUE(logger.isEnabled(logging::Level::Info));
                         EXPECT_TRUE(logger.minLevel() == logging::Level::Info);
                         EXPECT_EQ(logger.logFileDirectory(), std::string(""));
                         if (message == "outer")
                         {
                             logger.setMinLevel(logging::Level::Info);
                             logger.configure(logging::Level::Info,
                                              [&](logging::Level, const std::string&, const std::string& nested)
                                              {
                                                  messages.push_back(nested);
                                              });
                             logging::logInfo("reentrant", "inner");
                         }
                     });
    logging::logInfo("reentrant", "outer");
    EXPECT_EQ(messages.size(), std::size_t{2});
    EXPECT_EQ(messages[0], std::string("outer"));
    EXPECT_EQ(messages[1], std::string("inner"));
}
