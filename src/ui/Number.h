#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace northlight::ui
{

// A number with exactly `decimals` places. juce::String(value, 0) is not that:
// 0 places there means "as many as it takes", so -23.4182 shows in full and a
// readout sized for "-23" truncates.
inline juce::String fixedText(double value, int decimals)
{
    if (decimals <= 0)
        return juce::String(static_cast<juce::int64>(std::llround(value)));
    return juce::String(value, decimals);
}

} // namespace northlight::ui
