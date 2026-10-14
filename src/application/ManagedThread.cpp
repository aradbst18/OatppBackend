#include "app/application/ManagedThread.hpp"

#include "app/logging/Logger.hpp"

namespace app::application
{

ManagedThread::ManagedThread(std::string name, Work work) : name_(std::move(name)), work_(std::move(work))
{
}

ManagedThread::~ManagedThread()
{
    stop();
}

void ManagedThread::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
    {
        return;  // already running
    }

    stopRequested_.store(false);
    Work work = work_;
    std::string name = name_;
    thread_ = std::thread(
        [work, name, this]()
        {
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

void ManagedThread::requestStop() noexcept
{
    stopRequested_.store(true);
}

void ManagedThread::stop()
{
    requestStop();
    join();
}

void ManagedThread::join()
{
    if (thread_.joinable())
    {
        // Never join from the worker itself (would deadlock/terminate).
        if (std::this_thread::get_id() == thread_.get_id())
        {
            return;
        }

        thread_.join();
    }

    running_.store(false);
}

bool ManagedThread::isRunning() const noexcept
{
    return running_.load();
}

}  // namespace app::application
