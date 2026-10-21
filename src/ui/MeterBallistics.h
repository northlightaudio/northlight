#pragma once

#include <algorithm>
#include <cmath>

// How meters move (02-design-system.md, "Meter"). The engine reports a peak
// every few milliseconds; shown raw, a bar and its number jitter with every
// transient. A shown level instead rises towards a louder reading over a short
// time constant (motion.meterRiseMs: a single spike no longer jumps the bar)
// and falls at a steady rate; the number beside a meter changes a few times a
// second, holding the loudest reading of each period. The peak-hold line and
// the clip light still see the raw peaks.

namespace northlight::ui
{

// One step of a level in dB towards `targetDb` after `elapsedMs`.
inline float
followLevel(float shownDb, float targetDb, double elapsedMs, double riseMs, float fallDbPerSec) noexcept
{
    if (targetDb > shownDb)
    {
        if (riseMs <= 0.0)
            return targetDb;
        const auto k = static_cast<float>(1.0 - std::exp(-elapsedMs / riseMs));
        return shownDb + (targetDb - shownDb) * k;
    }
    return std::max(targetDb, shownDb - fallDbPerSec * static_cast<float>(elapsedMs * 0.001));
}

// Gain reduction (dB, positive): it rises over `riseMs` and lets go over a
// longer `releaseMs`, both exponential, like a compressor's needle.
inline float
followReduction(float shownDb, float targetDb, double elapsedMs, double riseMs, double releaseMs) noexcept
{
    const double tau = targetDb > shownDb ? riseMs : releaseMs;
    if (tau <= 0.0)
        return targetDb;
    const auto k = static_cast<float>(1.0 - std::exp(-elapsedMs / tau));
    const float next = shownDb + (targetDb - shownDb) * k;
    return std::abs(next - targetDb) < 0.01f ? targetDb : next;
}

// The number shown beside a meter: the loudest reading of each `periodMs`.
class MeterReadout
{
public:
    explicit MeterReadout(float floor = -120.0f) noexcept : floor_(floor), shown_(floor), loudest_(floor) {}
    // Feeds a reading; returns true when the shown value changed.
    bool update(float value, double elapsedMs, double periodMs = 250.0) noexcept
    {
        loudest_ = std::max(loudest_, value);
        age_ += elapsedMs;
        if (age_ < periodMs && !first_)
            return false;
        first_ = false;
        age_ = 0.0;
        const bool changed = loudest_ != shown_;
        shown_ = loudest_;
        loudest_ = floor_;
        return changed;
    }
    float value() const noexcept { return shown_; }
    void reset(float floor) noexcept
    {
        floor_ = shown_ = loudest_ = floor;
        age_ = 0.0;
        first_ = true;
    }

private:
    float floor_ = -120.0f, shown_ = -120.0f, loudest_ = -120.0f;
    double age_ = 0.0;
    bool first_ = true;
};

} // namespace northlight::ui
