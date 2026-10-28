#include "app/logging/Logger.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace app::logging
{

const char* toString(Level level) noexcept
{
    switch (level)
    {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Critical:
            return "CRITICAL";
    }

    return "UNKNOWN";
}

bool tryParseLevel(const std::string& text, Level& out) noexcept
{
    std::string upper;
    upper.reserve(text.size());
    for (char c : text)
    {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (upper == "TRACE")
    {
        out = Level::Trace;
        return true;
    }

    if (upper == "DEBUG")
    {
        out = Level::Debug;
        return true;
    }

    if (upper == "INFO")
    {
        out = Level::Info;
        return true;
    }

    if (upper == "WARN" || upper == "WARNING")
    {
        out = Level::Warn;
        return true;
    }

    if (upper == "ERROR")
    {
        out = Level::Error;
        return true;
    }

    if (upper == "CRITICAL" || upper == "FATAL")
    {
        out = Level::Critical;
        return true;
    }

    return false;
}

Logger& Logger::instance()
{
    static Logger inst;
    return inst;
}

Logger::Logger() = default;

void Logger::configure(Level minLevel, Sink sink)
{
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = minLevel;
    if (sink)
    {
        sink_ = std::move(sink);
    }
}

void Logger::resetToDefault()
{
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = Level::Info;
    sink_ = &Logger::defaultSink;
    logFileDir_.clear();
}

void Logger::setMinLevel(Level level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

Level Logger::minLevel() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return minLevel_;
}

void Logger::setLogFileDirectory(const std::string& dir)
{
    std::lock_guard<std::mutex> lock(mutex_);
    logFileDir_ = dir;
    if (!logFileDir_.empty())
    {
        // Best-effort: a failure here surfaces as a stderr notice on first write,
        // never as an exception (the logger never throws).
        std::error_code ec;
        std::filesystem::create_directories(logFileDir_, ec);
        (void)ec;
    }
}

std::string Logger::logFileDirectory() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return logFileDir_;
}

std::string Logger::currentDateString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};

#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif  // _WIN32 || _WIN64

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d");
    return out.str();
}

std::string Logger::currentTimeString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm local{};

#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif  // _WIN32 || _WIN64

    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << millis.count();
    return out.str();
}

std::string Logger::resolveLogFilePath(const std::string& dir)
{
    if (dir.empty())
    {
        return "";
    }

    std::string base = dir;
    while (base.size() > 1 && (base.back() == '/' || base.back() == '\\'))
    {
        base.pop_back();
    }

    return base + "/" + currentDateString() + ".log";
}

std::string Logger::formatLine(const std::string& time, Level level, const std::string& tag, const std::string& message)
{
    return time + " [" + toString(level) + "] [" + tag + "] " + message;
}

void Logger::log(Level level, const std::string& tag, const std::string& message)
{
    Sink sinkCopy;
    std::string dirCopy;
    std::string timestamp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(level) < static_cast<int>(minLevel_))
        {
            return;
        }

        sinkCopy = sink_;
        dirCopy = logFileDir_;
        timestamp = currentTimeString();
    }

    std::lock_guard<std::recursive_mutex> outputLock(outputMutex_);
    sinkCopy(level, tag, message);
    if (!dirCopy.empty())
    {
        appendToFile(dirCopy, formatLine(timestamp, level, tag, message));
    }
}

bool Logger::isEnabled(Level level) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(level) >= static_cast<int>(minLevel_);
}

void Logger::defaultSink(Level level, const std::string& tag, const std::string& message)
{
    std::cerr << formatLine(currentTimeString(), level, tag, message) << '\n';
}

void Logger::appendToFile(const std::string& dir, const std::string& line)
{
    try
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        (void)ec;

        const std::string path = resolveLogFilePath(dir);
        if (path.empty())
        {
            return;
        }

        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out)
        {
            // No recursion into Logger::log here: report directly so a broken log
            // directory can never deadlock or recurse.
            std::cerr << formatLine(currentTimeString(), Level::Error, "logging", "Cannot write log file: " + path)
                      << '\n';
            return;
        }

        out << line << '\n';
        out.flush();
    }
    catch (...)
    {
        // The logger never throws.
    }
}

void log(Level level, const std::string& tag, const std::string& message)
{
    Logger::instance().log(level, tag, message);
}

}  // namespace app::logging
