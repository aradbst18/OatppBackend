#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
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
 * - The work function must poll its stopRequested argument and return promptly.
 * - start()/stop()/join() are serialized; join() alone does not request a stop.
 * - On the worker, start()/join() are no-ops and stop() only requests a stop.
 * - The owner must outlive the work and all lifecycle callers; destruction
 *   must be external to the worker and must not race with other callers.
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
    void joinLocked(std::unique_lock<std::mutex>& lock);

    std::string name_;
    Work work_;
    std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    bool joining_{false};
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
};

}  // namespace app::application
