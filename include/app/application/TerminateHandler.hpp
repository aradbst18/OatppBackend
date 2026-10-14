#pragma once

namespace app::application
{

/// Install the process-wide std::set_terminate handler. The handler logs the
/// nested exception chain (when available) and exits without throwing.
/// Safe to call once during startup; subsequent calls are ignored.
void installTerminateHandler();

}  // namespace app::application
