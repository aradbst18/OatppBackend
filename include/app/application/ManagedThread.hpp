#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace app::application
{

/**
 * RAII wrapper around std::thread with explicit start/stop/join semantics.
 *
 * Rules (see docs/architecture.md):
 * - The owner creates the thread via start(), owns it, and joins it in
 *   stop()/destructor. Detach is never used.
 * - The work function must poll `stopRequested()` and return promptly.
 * - stop() is idempotent and safe to call from any thread except the worker.
 */
class ManagedThread
{
public:
    using Work = std::function<void(const std::atomic<bool>& stopRequested)>;

    explicit ManagedThread(std::string name, Work work);
    ~ManagedThread();

    ManagedThread(const ManagedThread&) = delete;
    ManagedThread& operator=(const ManagedThread&) = delete;
    ManagedThread(ManagedThread&&) = delete;
    ManagedThread& operator=(ManagedThread&&) = delete;

    void start();
    void requestStop() noexcept;
    void stop();
    void join();

    bool isRunning() const noexcept;
    const std::string& name() const noexcept
    {
        return name_;
    }

private:
    std::string name_;
    Work work_;
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
};

}  // namespace app::application
