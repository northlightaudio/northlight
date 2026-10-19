#include "engine/Delay.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

namespace aural::engine
{

void DelayLine::setDelay(int samples)
{
    AURAL_ASSERT_NOT_REALTIME("DelayLine::setDelay");
    samples = std::max(0, samples);
    if (samples == delay_)
        return;
    delay_ = samples;
    position_ = 0;
    left_.assign(static_cast<size_t>(samples), 0.0f);
    right_.assign(static_cast<size_t>(samples), 0.0f);
}

void DelayLine::process(
    const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept
{
    if (delay_ == 0)
    {
        std::copy(inLeft, inLeft + numSamples, outLeft);
        std::copy(inRight, inRight + numSamples, outRight);
        return;
    }
    float* l = left_.data();
    float* r = right_.data();
    int p = position_;
    for (int i = 0; i < numSamples; ++i)
    {
        const float dl = l[p];
        const float dr = r[p];
        l[p] = inLeft[i];
        r[p] = inRight[i];
        outLeft[i] = dl;
        outRight[i] = dr;
        if (++p == delay_)
            p = 0;
    }
    position_ = p;
}

void DelayLine::reset() noexcept
{
    std::fill(left_.begin(), left_.end(), 0.0f);
    std::fill(right_.begin(), right_.end(), 0.0f);
    position_ = 0;
}

void DelayLine::continueFrom(const DelayLine& other) noexcept
{
    // The other line's memory holds its last `delay` inputs; replay the most
    // recent min(ours, theirs) of them into our ring so the oldest of ours is the
    // oldest we can still honour and the newest is the newest input.
    const int n = std::min(delay_, other.delay_);
    if (n <= 0)
        return;
    for (int k = n; k >= 1; --k)
    {
        // The sample written k steps ago sits at (other.position_ - k) mod their length.
        int src = other.position_ - k;
        while (src < 0)
            src += other.delay_;
        int dst = position_ - k;
        while (dst < 0)
            dst += delay_;
        left_[static_cast<size_t>(dst)] = other.left_[static_cast<size_t>(src)];
        right_[static_cast<size_t>(dst)] = other.right_[static_cast<size_t>(src)];
    }
}

std::shared_ptr<DelayLine> DelayCache::get(const DelayKey& key, int delaySamples)
{
    AURAL_ASSERT_NOT_REALTIME("DelayCache::get");
    delaySamples = std::max(0, delaySamples);
    auto& line = lines_[key];
    if (line != nullptr && line->delay() == delaySamples)
        return line;
    // Never resize a line a snapshot may be reading: make a new one and let the
    // old retire with its snapshot.
    auto fresh = std::make_shared<DelayLine>();
    fresh->setDelay(delaySamples);
    if (line != nullptr)
        fresh->continueFrom(*line);
    line = fresh;
    return line;
}

void DelayCache::prune()
{
    std::erase_if(lines_, [](const auto& entry) { return entry.second.use_count() == 1; });
}

} // namespace aural::engine
