#include "app/logging/OatppLoggerBridge.hpp"

#include "app/logging/Logger.hpp"

namespace app::logging
{

OatppLoggerBridge::OatppLoggerBridge(Level minLevel)
{
    ::app::logging::Logger::instance().setMinLevel(minLevel);
}

void OatppLoggerBridge::log(v_uint32 priority, const std::string& tag, const std::string& message)
{
    ::app::logging::Logger::instance().log(toLevel(priority), tag.empty() ? "oatpp" : tag, message);
}

Level OatppLoggerBridge::toLevel(v_uint32 priority) noexcept
{
    if (priority <= oatpp::base::Logger::PRIORITY_V)
    {
        return Level::Trace;
    }

    if (priority <= oatpp::base::Logger::PRIORITY_D)
    {
        return Level::Debug;
    }

    if (priority <= oatpp::base::Logger::PRIORITY_I)
    {
        return Level::Info;
    }

    if (priority <= oatpp::base::Logger::PRIORITY_W)
    {
        return Level::Warn;
    }

    if (priority <= oatpp::base::Logger::PRIORITY_E)
    {
        return Level::Error;
    }

    return Level::Critical;
}

}  // namespace app::logging
