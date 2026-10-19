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

std::shared_ptr<DelayLine> DelayCache::get(const DelayKey& key, int delaySamples)
{
    AURAL_ASSERT_NOT_REALTIME("DelayCache::get");
    auto& line = lines_[key];
    if (line == nullptr)
        line = std::make_shared<DelayLine>();
    line->setDelay(delaySamples);
    return line;
}

void DelayCache::prune()
{
    std::erase_if(lines_, [](const auto& entry) { return entry.second.use_count() == 1; });
}

} // namespace aural::engine
