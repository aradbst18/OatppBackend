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

#include "oatpp/web/protocol/http/outgoing/ResponseFactory.hpp"
#include "app/logging/Logger.hpp"
#include "app/middlewares/LoggingInterceptor.hpp"

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

TEST_CASE("logging: parse Level::Off and ALL/NONE synonyms")
{
    logging::Level level = logging::Level::Info;

    EXPECT_TRUE(logging::tryParseLevel("OFF", level));
    EXPECT_TRUE(level == logging::Level::Off);
    EXPECT_EQ(std::string(logging::toString(level)), std::string("OFF"));

    EXPECT_TRUE(logging::tryParseLevel("none", level));
    EXPECT_TRUE(level == logging::Level::Off);

    EXPECT_TRUE(logging::tryParseLevel("ALL", level));
    EXPECT_TRUE(level == logging::Level::Trace);

    EXPECT_TRUE(logging::tryParseLevel("trace", level));
    EXPECT_TRUE(level == logging::Level::Trace);
    EXPECT_EQ(std::string(logging::toString(level)), std::string("TRACE"));
}

TEST_CASE("logging: tryParseLevel is case-insensitive and trims whitespace")
{
    logging::Level level{};

    // Variations of TRACE
    EXPECT_TRUE(logging::tryParseLevel("TRACE", level));
    EXPECT_TRUE(level == logging::Level::Trace);
    EXPECT_TRUE(logging::tryParseLevel("Trace", level));
    EXPECT_TRUE(level == logging::Level::Trace);
    EXPECT_TRUE(logging::tryParseLevel("trace", level));
    EXPECT_TRUE(level == logging::Level::Trace);
    EXPECT_TRUE(logging::tryParseLevel("tRaCe", level));
    EXPECT_TRUE(level == logging::Level::Trace);
    EXPECT_TRUE(logging::tryParseLevel("  trace  ", level));
    EXPECT_TRUE(level == logging::Level::Trace);

    // Variations of DEBUG
    EXPECT_TRUE(logging::tryParseLevel("debug", level));
    EXPECT_TRUE(level == logging::Level::Debug);
    EXPECT_TRUE(logging::tryParseLevel("Debug", level));
    EXPECT_TRUE(level == logging::Level::Debug);
    EXPECT_TRUE(logging::tryParseLevel("DEBUG", level));
    EXPECT_TRUE(level == logging::Level::Debug);

    // Variations of INFO
    EXPECT_TRUE(logging::tryParseLevel("info", level));
    EXPECT_TRUE(level == logging::Level::Info);
    EXPECT_TRUE(logging::tryParseLevel("Info", level));
    EXPECT_TRUE(level == logging::Level::Info);

    // Variations of WARN / WARNING
    EXPECT_TRUE(logging::tryParseLevel("warn", level));
    EXPECT_TRUE(level == logging::Level::Warn);
    EXPECT_TRUE(logging::tryParseLevel("Warn", level));
    EXPECT_TRUE(level == logging::Level::Warn);
    EXPECT_TRUE(logging::tryParseLevel("warning", level));
    EXPECT_TRUE(level == logging::Level::Warn);
    EXPECT_TRUE(logging::tryParseLevel("Warning", level));
    EXPECT_TRUE(level == logging::Level::Warn);

    // Variations of ERROR
    EXPECT_TRUE(logging::tryParseLevel("error", level));
    EXPECT_TRUE(level == logging::Level::Error);
    EXPECT_TRUE(logging::tryParseLevel("Error", level));
    EXPECT_TRUE(level == logging::Level::Error);

    // Variations of CRITICAL / FATAL
    EXPECT_TRUE(logging::tryParseLevel("critical", level));
    EXPECT_TRUE(level == logging::Level::Critical);
    EXPECT_TRUE(logging::tryParseLevel("Critical", level));
    EXPECT_TRUE(level == logging::Level::Critical);
    EXPECT_TRUE(logging::tryParseLevel("fatal", level));
    EXPECT_TRUE(level == logging::Level::Critical);

    // Variations of OFF / NONE
    EXPECT_TRUE(logging::tryParseLevel("off", level));
    EXPECT_TRUE(level == logging::Level::Off);
    EXPECT_TRUE(logging::tryParseLevel("Off", level));
    EXPECT_TRUE(level == logging::Level::Off);
    EXPECT_TRUE(logging::tryParseLevel("OFF", level));
    EXPECT_TRUE(level == logging::Level::Off);
    EXPECT_TRUE(logging::tryParseLevel("none", level));
    EXPECT_TRUE(level == logging::Level::Off);
    EXPECT_TRUE(logging::tryParseLevel("None", level));
    EXPECT_TRUE(level == logging::Level::Off);

    // Invalid values
    EXPECT_FALSE(logging::tryParseLevel("", level));
    EXPECT_FALSE(logging::tryParseLevel("   ", level));
    EXPECT_FALSE(logging::tryParseLevel("unknown", level));
    EXPECT_FALSE(logging::tryParseLevel("123", level));
}

TEST_CASE("logging: Level::Off silences all levels including Critical")
{
    LoggerGuard guard;
    auto& logger = logging::Logger::instance();

    std::vector<std::string> messages;
    logger.configure(logging::Level::Off,
                     [&](logging::Level, const std::string&, const std::string& msg)
                     {
                         messages.push_back(msg);
                     });

    EXPECT_FALSE(logger.isEnabled(logging::Level::Trace));
    EXPECT_FALSE(logger.isEnabled(logging::Level::Debug));
    EXPECT_FALSE(logger.isEnabled(logging::Level::Info));
    EXPECT_FALSE(logger.isEnabled(logging::Level::Warn));
    EXPECT_FALSE(logger.isEnabled(logging::Level::Error));
    EXPECT_FALSE(logger.isEnabled(logging::Level::Critical));

    logging::logTrace("test", "trace line");
    logging::logDebug("test", "debug line");
    logging::logInfo("test", "info line");
    logging::logWarn("test", "warn line");
    logging::logError("test", "error line");
    logging::logCritical("test", "critical line");

    EXPECT_TRUE(messages.empty());
}

TEST_CASE("logging: RequestContext ScopeGuard attaches request id to log line")
{
    // Without active request context
    EXPECT_EQ(logging::Logger::formatLine("10:00:00.000", logging::Level::Info, "auth", "ready"),
              std::string("10:00:00.000 [INFO] [auth] ready"));

    {
        logging::RequestContext::ScopeGuard guard("req-abc12345");
        EXPECT_EQ(logging::RequestContext::requestId(), std::string("req-abc12345"));
        EXPECT_EQ(logging::Logger::formatLine("10:00:00.000", logging::Level::Info, "auth", "ready"),
                  std::string("10:00:00.000 [INFO] [req=req-abc12345] [auth] ready"));

        {
            logging::RequestContext::ScopeGuard nested("req-suboperation");
            EXPECT_EQ(logging::RequestContext::requestId(), std::string("req-suboperation"));
            EXPECT_EQ(logging::Logger::formatLine("10:00:00.001", logging::Level::Trace, "db", "query"),
                      std::string("10:00:00.001 [TRACE] [req=req-suboperation] [db] query"));
        }

        // Restores outer scope on exit
        EXPECT_EQ(logging::RequestContext::requestId(), std::string("req-abc12345"));
    }

    // Completely cleared on outer exit
    EXPECT_TRUE(logging::RequestContext::requestId().empty());
    EXPECT_EQ(logging::Logger::formatLine("10:00:00.002", logging::Level::Info, "auth", "done"),
              std::string("10:00:00.002 [INFO] [auth] done"));
}

TEST_CASE("logging: LoggingInterceptor generates request id and attaches to response")
{
    LoggerGuard guard;
    auto interceptor = std::make_shared<middlewares::LoggingInterceptor>();

    std::vector<std::pair<logging::Level, std::string>> logs;
    logging::Logger::instance().configure(logging::Level::Trace,
                                          [&](logging::Level lvl, const std::string&, const std::string& msg)
                                          {
                                              logs.emplace_back(lvl, msg);
                                          });

    oatpp::web::protocol::http::RequestStartingLine line;
    line.method = "GET";
    line.path = "/api/v1/health";
    line.protocol = "HTTP/1.1";
    auto request = middlewares::LoggingInterceptor::IncomingRequest::createShared(
        nullptr, line, oatpp::web::protocol::http::Headers(), nullptr, nullptr);

    // Intercept request
    auto earlyResponse = interceptor->intercept(request);
    EXPECT_FALSE(earlyResponse);
    EXPECT_FALSE(logging::RequestContext::requestId().empty());
    const std::string generatedId = logging::RequestContext::requestId();
    EXPECT_EQ(generatedId.size(), 32u);

    // Create response and intercept response
    auto response = oatpp::web::protocol::http::outgoing::ResponseFactory::createResponse(
        oatpp::web::protocol::http::Status::CODE_200, "OK");
    auto finalResponse = interceptor->intercept(request, response);
    EXPECT_TRUE(finalResponse);
    EXPECT_TRUE(finalResponse->getHeader("X-Request-Id"));
    EXPECT_EQ(*finalResponse->getHeader("X-Request-Id"), generatedId);

    // Context cleared after response interception
    EXPECT_TRUE(logging::RequestContext::requestId().empty());

    // Verify incoming TRACE and outgoing DEBUG logs were emitted
    bool foundTrace = false;
    bool foundDebug = false;
    for (const auto& entry : logs)
    {
        if (entry.first == logging::Level::Trace && entry.second.find("--> GET /api/v1/health") != std::string::npos)
        {
            foundTrace = true;
        }
        if (entry.first == logging::Level::Debug &&
            entry.second.find("<-- GET /api/v1/health status=200") != std::string::npos)
        {
            foundDebug = true;
        }
    }
    EXPECT_TRUE(foundTrace);
    EXPECT_TRUE(foundDebug);
}

TEST_CASE("logging: milestone bypasses console minLevel when OFF but skips file writing")
{
    LoggerGuard guard;
    const std::string dir = "/tmp/oatpp_test_logging_milestone_off_" +
                            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::remove_all(dir);

    std::vector<std::pair<logging::Level, std::string>> sinkCalls;
    logging::Logger::instance().configure(logging::Level::Off,
                                          [&](logging::Level lvl, const std::string& tag, const std::string& msg)
                                          {
                                              sinkCalls.emplace_back(lvl, "[" + tag + "] " + msg);
                                          });
    logging::Logger::instance().setLogFileDirectory(dir);

    // Regular logs must be completely suppressed
    logging::logInfo("app", "Standard info should be silent");
    logging::logWarn("app", "Standard warn should be silent");
    EXPECT_EQ(sinkCalls.size(), 0u);

    // Milestone must reach the console sink
    logging::logMilestone("app", "Server running");
    EXPECT_EQ(sinkCalls.size(), 1u);
    EXPECT_TRUE(sinkCalls[0].first == logging::Level::Info);
    EXPECT_EQ(sinkCalls[0].second, std::string("[app] Server running"));

    // File output must NOT be written when minLevel is Off
    const std::string logFile = logging::Logger::resolveLogFilePath(dir);
    EXPECT_FALSE(std::filesystem::exists(logFile));

    std::filesystem::remove_all(dir);
}

TEST_CASE("logging: milestone writes to file when minLevel is INFO or lower")
{
    LoggerGuard guard;
    const std::string dir = "/tmp/oatpp_test_logging_milestone_info_" +
                            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::remove_all(dir);

    std::vector<std::string> sinkCalls;
    logging::Logger::instance().configure(logging::Level::Info,
                                          [&](logging::Level, const std::string&, const std::string& msg)
                                          {
                                              sinkCalls.push_back(msg);
                                          });
    logging::Logger::instance().setLogFileDirectory(dir);

    logging::logMilestone("app", "Services initialized");
    EXPECT_EQ(sinkCalls.size(), 1u);
    EXPECT_EQ(sinkCalls[0], std::string("Services initialized"));

    const std::string logFile = logging::Logger::resolveLogFilePath(dir);
    EXPECT_TRUE(std::filesystem::exists(logFile));
    const std::string content = readWholeFile(logFile);
    EXPECT_TRUE(content.find("[INFO] [app] Services initialized") != std::string::npos);

    std::filesystem::remove_all(dir);
}
