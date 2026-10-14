#include "app/application/TerminateHandler.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <thread>

namespace app::application
{

namespace
{

std::atomic<bool> g_installed{false};

void describeNested(const std::exception& ex, int depth)
{
    std::cerr << "terminate: [" << depth << "] " << ex.what() << '\n';

    try
    {
        std::rethrow_if_nested(ex);
    }
    catch (const std::exception& nested)
    {
        describeNested(nested, depth + 1);
    }
    catch (...)
    {
        std::cerr << "terminate: [" << (depth + 1) << "] unknown nested exception\n";
    }
}

void onTerminate() noexcept
{
    try
    {
        if (const auto ex = std::current_exception())
        {
            try
            {
                std::rethrow_exception(ex);
            }
            catch (const std::exception& known)
            {
                std::cerr << "terminate: uncaught exception (see chain below)\n";
                describeNested(known, 0);
            }
            catch (...)
            {
                std::cerr << "terminate: uncaught unknown exception\n";
            }
        }
        else
        {
            std::cerr << "terminate: called without an active exception\n";
        }
    }
    catch (...)
    {
        // The handler itself must never throw.
    }

    std::cerr << "terminate: exiting process\n";
    std::cerr.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::abort();
}

}  // namespace

void installTerminateHandler()
{
    bool expected = false;
    if (g_installed.compare_exchange_strong(expected, true))
    {
        std::set_terminate(&onTerminate);
    }
}

}  // namespace app::application
