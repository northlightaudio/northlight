#pragma once

#include "engine/Meters.h"

#include <juce_core/juce_core.h>

#include <array>

namespace northlight::ui
{

// The meters every view shows, read once a frame: the window's tick takes each
// slot of the engine's meter block (which a take empties) into a copy, then the
// mixer's strips and the arrange view's headers read the copies, so no view
// depends on another one's timer or steals another's readings.
class MeterReadings
{
public:
    explicit MeterReadings(rt::MeterBlock& block) noexcept : block_(block) {}

    // Takes every slot's reading since the last tick.
    void tick() noexcept
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        elapsedMs_ = lastTickMs_ > 0.0 ? now - lastTickMs_ : 1000.0 / 30.0;
        lastTickMs_ = now;
        for (int slot = 0; slot < rt::MeterBlock::maxSlots; ++slot)
            readings_[static_cast<size_t>(slot)] = block_.take(slot);
    }
    // The reading a slot had at the last tick (silence for a slot out of range).
    rt::MeterBlock::Reading reading(int slot) const noexcept
    {
        return slot >= 0 && slot < rt::MeterBlock::maxSlots ? readings_[static_cast<size_t>(slot)]
                                                            : rt::MeterBlock::Reading{};
    }
    // How long the last tick's readings cover, for the meters' ballistics.
    double elapsedMs() const noexcept { return elapsedMs_; }

private:
    rt::MeterBlock& block_;
    std::array<rt::MeterBlock::Reading, rt::MeterBlock::maxSlots> readings_{};
    double lastTickMs_ = 0.0, elapsedMs_ = 1000.0 / 30.0;
};

} // namespace northlight::ui
