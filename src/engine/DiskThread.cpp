#include "engine/DiskThread.h"

#include "engine/rt/RealtimeGuard.h"

#include <chrono>

namespace northlight::engine
{

DiskThread::DiskThread(int idleMilliseconds) : idleMilliseconds_(idleMilliseconds), thread_([this] { run(); })
{
}

DiskThread::~DiskThread()
{
    stop_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void DiskThread::add(const std::shared_ptr<StreamingAudioSource>& source)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("DiskThread::add");
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        std::erase_if(sources_, [](const auto& weak) { return weak.expired(); });
        sources_.push_back(source);
    }
    wake_.notify_all();
}

void DiskThread::poke()
{
    wake_.notify_all();
}

std::uint64_t DiskThread::totalUnderruns() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& weak : sources_)
        if (const auto source = weak.lock())
            total += source->underruns();
    return total;
}

void DiskThread::run()
{
    std::vector<std::shared_ptr<StreamingAudioSource>> live;
    while (!stop_.load(std::memory_order_acquire))
    {
        live.clear();
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& weak : sources_)
                if (auto source = weak.lock())
                    live.push_back(std::move(source));
        }

        bool worked = false;
        for (auto& source : live)
            worked = source->service() || worked;
        live.clear(); // release references before sleeping so sources can be freed

        if (!worked)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(idleMilliseconds_));
        }
    }
}

} // namespace northlight::engine
