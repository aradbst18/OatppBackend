#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include "oatpp/core/base/Environment.hpp"

#include "app/logging/Logger.hpp"

namespace app::logging
{

/**
 * Bridge that forwards oatpp::base::Logger output into app::logging::Logger.
 *
 * Why this exists: oatpp writes its own diagnostics through a global
 * oatpp::base::Logger. Without a bridge those messages bypass our level
 * filtering and sink configuration. Installing this bridge once during
 * application startup keeps all backend logs centralized.
 */
class OatppLoggerBridge : public oatpp::base::Logger
{
public:
    explicit OatppLoggerBridge(Level minLevel = Level::Debug);

    void log(v_uint32 priority, const std::string& tag, const std::string& message) override;

private:
    static Level toLevel(v_uint32 priority) noexcept;
};

}  // namespace app::logging
