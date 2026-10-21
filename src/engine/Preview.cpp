#include "engine/Preview.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

namespace aural::engine
{

void PreviewPlayer::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("PreviewPlayer::prepare");
    const auto size = static_cast<size_t>(std::max(1, maxBlockSize));
    left_.assign(size, 0.0f);
    right_.assign(size, 0.0f);
    fadeStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * fadeSeconds));
}

void PreviewPlayer::publish(std::shared_ptr<AudioSource> source)
{
    auto item = std::make_unique<Item>();
    item->source = std::move(source);
    item->id = ++nextId_;
    currentId_ = item->source != nullptr ? item->id : 0;
    publisher_.publish(std::move(item));
}

void PreviewPlayer::play(std::shared_ptr<AudioSource> source)
{
    AURAL_ASSERT_NOT_REALTIME("PreviewPlayer::play");
    pending_ = std::move(source);
    hasPending_ = true;
    if (currentId_ != 0)
        stopId_.store(currentId_, std::memory_order_release);
}

void PreviewPlayer::stop()
{
    AURAL_ASSERT_NOT_REALTIME("PreviewPlayer::stop");
    pending_.reset();
    hasPending_ = false;
    if (currentId_ != 0)
        stopId_.store(currentId_, std::memory_order_release);
}

void PreviewPlayer::pump(bool realtimeRunning)
{
    AURAL_ASSERT_NOT_REALTIME("PreviewPlayer::pump");
    const bool currentDone =
        currentId_ == 0 || !realtimeRunning || doneId_.load(std::memory_order_acquire) == currentId_;
    if (hasPending_ && currentDone)
    {
        hasPending_ = false;
        publish(std::move(pending_));
        pending_.reset();
    }
    else if (!hasPending_ && currentId_ != 0 && currentDone)
        publish(nullptr); // finished: let the source go
    publisher_.collect(realtimeRunning);
}

bool PreviewPlayer::isPlaying() const noexcept
{
    return (hasPending_ && pending_ != nullptr) ||
           (currentId_ != 0 && doneId_.load(std::memory_order_acquire) != currentId_ &&
            stopId_.load(std::memory_order_acquire) != currentId_);
}

void PreviewPlayer::process(float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const Item* item = publisher_.acquire();
    if (item == nullptr || item->source == nullptr || left_.empty())
        return;
    if (item->id != rtId_)
    {
        rtId_ = item->id;
        rtPosition_ = 0;
        rtFade_ = 0.0f;
        rtDone_ = false;
    }
    if (rtDone_)
        return;
    const auto& source = *item->source;
    const bool stopping = stopId_.load(std::memory_order_acquire) == rtId_;
    const float gain = gain_.load(std::memory_order_relaxed);
    const auto capacity = static_cast<int>(left_.size());
    float* outLeft = numOutputs > 0 ? outputs[0] : nullptr;
    float* outRight = numOutputs > 1 ? outputs[1] : nullptr;
    for (int done = 0; done < numSamples && !rtDone_; done += capacity)
    {
        const int count = std::min(capacity, numSamples - done);
        source.read(0, rtPosition_, left_.data(), count);
        source.read(1, rtPosition_, right_.data(), count); // a mono source reads channel 0 again
        for (int i = 0; i < count; ++i)
        {
            rtFade_ = stopping ? std::max(0.0f, rtFade_ - fadeStep_) : std::min(1.0f, rtFade_ + fadeStep_);
            const float g = gain * rtFade_;
            if (outLeft != nullptr)
                outLeft[done + i] += left_[static_cast<size_t>(i)] * g;
            if (outRight != nullptr)
                outRight[done + i] += right_[static_cast<size_t>(i)] * g;
        }
        rtPosition_ += count;
        if ((stopping && rtFade_ <= 0.0f) || rtPosition_ >= source.lengthSamples())
            rtDone_ = true;
    }
    position_.store(rtPosition_, std::memory_order_relaxed);
    if (rtDone_)
        doneId_.store(rtId_, std::memory_order_release);
}

} // namespace aural::engine
