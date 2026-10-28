#include "app/application/ManagedThread.hpp"

#include "app/logging/Logger.hpp"

namespace app::application
{

namespace
{
// Self-calls must be recognized without touching thread_ while an owner joins it.
thread_local const ManagedThread* currentWorker = nullptr;
}  // namespace

ManagedThread::ManagedThread(std::string name, Work work) : name_(std::move(name)), work_(std::move(work))
{
}

ManagedThread::~ManagedThread()
{
    stop();
}

void ManagedThread::start()
{
    if (currentWorker == this)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(lifecycleMutex_);
    lifecycleCv_.wait(lock, [this] { return !joining_; });
    if (running_.load())
    {
        return;
    }
    if (thread_.joinable())
    {
        thread_.join();
    }
    stopRequested_.store(false);
    running_.store(true);
    try
    {
        thread_ = std::thread(
            [work = work_, name = name_, this]()
            {
                currentWorker = this;
                logging::logInfo("threads", "Thread '" + name + "' started");
                try
                {
                    work(stopRequested_);
                }
                catch (const std::exception& ex)
                {
                    logging::logError("threads", "Thread '" + name + "' failed: " + ex.what());
                }
                catch (...)
                {
                    logging::logError("threads", "Thread '" + name + "' failed with unknown exception");
                }

                running_.store(false);
                logging::logInfo("threads", "Thread '" + name + "' stopped");
            });
    }
    catch (...)
    {
        running_.store(false);
        throw;
    }
}

void ManagedThread::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ManagedThread::stop()
{
    if (currentWorker == this)
    {
        requestStop();
        return;
    }

    std::unique_lock<std::mutex> lock(lifecycleMutex_);
    requestStop();
    lifecycleCv_.wait(lock, [this] { return !joining_; });
    // A waiting start() may have reset the flag while another caller was joining.
    requestStop();
    joinLocked(lock);
}

void ManagedThread::join()
{
    if (currentWorker == this)
    {
        return;
    }

    std::unique_lock<std::mutex> lock(lifecycleMutex_);
    lifecycleCv_.wait(lock, [this] { return !joining_; });
    joinLocked(lock);
}

void ManagedThread::joinLocked(std::unique_lock<std::mutex>& lock)
{
    if (!thread_.joinable())
    {
        return;
    }

    joining_ = true;
    lock.unlock();  // Let stop() signal the worker while join() waits for it.
    try
    {
        thread_.join();
    }
    catch (...)
    {
        lock.lock();
        joining_ = false;
        lifecycleCv_.notify_all();
        throw;
    }
    lock.lock();
    joining_ = false;
    lifecycleCv_.notify_all();
}

bool ManagedThread::isRunning() const noexcept
{
    return running_.load();
}

}  // namespace app::application
