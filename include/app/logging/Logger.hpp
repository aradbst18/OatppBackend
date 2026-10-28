#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "oatpp/core/base/Environment.hpp"

namespace app::logging
{

/// Log severity levels. Ordered; a sink configured at level L emits L and above.
enum class Level : std::int32_t
{
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5
};

/// Convert a level to its canonical name (e.g. "INFO").
const char* toString(Level level) noexcept;

/// Parse a level name (case-insensitive). Returns true on success.
bool tryParseLevel(const std::string& text, Level& out) noexcept;

/**
 * Central thread-safe logger.
 *
 * Design notes:
 * - One process-wide instance behind `instance()`; configuration (level, sink,
 *   log file directory) is explicit and testable via `configure()` /
 *   `setLogFileDirectory()`.
 * - Sink calls and file writes are serialized between threads in this process,
 *   separately from configuration access. Same-thread nested logging is allowed.
 *   The sink is a std::function so tests can capture output without filesystem access.
 * - Every emitted line starts with a local timestamp (`HH:MM:SS.mmm`),
 *   followed by the level, tag, and message:
 *   `HH:MM:SS.mmm [LEVEL] [tag] message`. This holds for both the console
 *   sink and the daily log file.
 * - File logging is opt-in via `setLogFileDirectory(dir)`: lines are appended
 *   to `<dir>/yyyy-mm-dd.log` (local date, resolved per write so midnight
 *   rollover is automatic). An empty dir disables file output. The logger
 *   never throws; file failures surface as a stderr notice.
 * - Never log secrets: callers must redact passwords, JWTs and DB keys before
 *   calling into the logger. The logger itself has no redaction heuristics.
 */
class Logger
{
public:
    using Sink = std::function<void(Level level, const std::string& tag, const std::string& message)>;

    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Set minimum emitted level and the output sink.
    /// In-flight records may still use the previous sink. Callbacks may access configuration
    /// and log on the same thread, but must not wait for another thread to finish logging.
    void configure(Level minLevel, Sink sink);

    /// Restore the default (INFO to stderr, no file logging).
    void resetToDefault();

    void setMinLevel(Level level);
    Level minLevel() const;

    /// Set the directory for daily log files; empty disables file output.
    /// The directory is created best-effort (errors surface on first write).
    void setLogFileDirectory(const std::string& dir);
    std::string logFileDirectory() const;

    /// Current local date as `yyyy-mm-dd` (daily log file stem).
    static std::string currentDateString();

    /// Current local time as `HH:MM:SS.mmm` (log line prefix).
    static std::string currentTimeString();

    /// Full path of today's log file inside `dir`
    /// (`<dir>/yyyy-mm-dd.log`). Empty `dir` yields an empty path.
    static std::string resolveLogFilePath(const std::string& dir);

    /// One log line: `HH:MM:SS.mmm [LEVEL] [tag] message`.
    static std::string formatLine(const std::string& time, Level level, const std::string& tag,
                                  const std::string& message);

    void log(Level level, const std::string& tag, const std::string& message);

    bool isEnabled(Level level) const noexcept;

private:
    Logger();
    static void defaultSink(Level level, const std::string& tag, const std::string& message);
    static void appendToFile(const std::string& dir, const std::string& line);

    mutable std::mutex mutex_;
    std::recursive_mutex outputMutex_;
    Level minLevel_{Level::Info};
    Sink sink_{&Logger::defaultSink};
    std::string logFileDir_;
};

void log(Level level, const std::string& tag, const std::string& message);

inline void logTrace(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Trace))
    {
        log(Level::Trace, tag, message);
    }
}

inline void logDebug(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Debug))
    {
        log(Level::Debug, tag, message);
    }
}

inline void logInfo(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Info))
    {
        log(Level::Info, tag, message);
    }
}

inline void logWarn(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Warn))
    {
        log(Level::Warn, tag, message);
    }
}

inline void logError(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Error))
    {
        log(Level::Error, tag, message);
    }
}

inline void logCritical(const std::string& tag, const std::string& message)
{
    if (Logger::instance().isEnabled(Level::Critical))
    {
        log(Level::Critical, tag, message);
    }
}

}  // namespace app::logging

/// Lazy logging macros that avoid evaluating the message expression if the level is disabled.
#define APP_LOG_IS_ENABLED(lvl) (::app::logging::Logger::instance().isEnabled(::app::logging::Level::lvl))

#define APP_LOG(lvl, tag, expr)                                                       \
    do                                                                                \
    {                                                                                 \
        if (::app::logging::Logger::instance().isEnabled(::app::logging::Level::lvl)) \
        {                                                                             \
            ::app::logging::log(::app::logging::Level::lvl, (tag), (expr));           \
        }                                                                             \
    } while (false)

#define APP_LOG_TRACE(tag, expr) APP_LOG(Trace, tag, expr)
#define APP_LOG_DEBUG(tag, expr) APP_LOG(Debug, tag, expr)
#define APP_LOG_INFO(tag, expr) APP_LOG(Info, tag, expr)
#define APP_LOG_WARN(tag, expr) APP_LOG(Warn, tag, expr)
#define APP_LOG_ERROR(tag, expr) APP_LOG(Error, tag, expr)
#define APP_LOG_CRITICAL(tag, expr) APP_LOG(Critical, tag, expr)
