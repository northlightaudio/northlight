#pragma once

#include "engine/Gain.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

// Gains as the user sees them: one floor and one formatter for every view, so a
// gain reads the same in the mixer, the inspector, the Editor panel and the
// arrange view, and a value typed in one is not clipped by another.
namespace northlight::ui::db
{

// At or below this a gain is silence (0) and reads "-inf".
constexpr double silence = -100.0;

inline float toGain(double db) noexcept
{
    return static_cast<float>(engine::dbToGain(db, silence));
}

inline double fromGain(float gain) noexcept
{
    return engine::gainToDb(static_cast<double>(gain), silence);
}

// A control whose travel stops above `silence` (a slider down to -60 dB): its
// bottom means silence, and a gain below it shows at the bottom.
inline float toGain(double db, double bottom) noexcept
{
    return db <= bottom + 0.05 ? 0.0f : toGain(db);
}
inline double fromGain(float gain, double bottom, double top) noexcept
{
    return std::clamp(fromGain(gain), bottom, top);
}

// "-6.0", "+2.5", "-inf"; no unit (callers add " dB" where it reads well).
inline juce::String text(double db, int decimals = 1)
{
    if (db <= silence + 0.05)
        return "-inf";
    return (db > 0.05 ? "+" : "") + juce::String(db, decimals);
}
inline juce::String gainText(float gain, int decimals = 1)
{
    return text(fromGain(gain), decimals);
}

// What a user typed: a number, or "inf" / "-inf" for silence.
inline double parse(const juce::String& typed)
{
    return typed.containsIgnoreCase("inf") ? silence : typed.getDoubleValue();
}

} // namespace northlight::ui::db
