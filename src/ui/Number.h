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

// ----- readouts ---------------------------------------------------------------------
// What cards, faces and displays print, written one way everywhere: a true minus
// sign (U+2212) for a negative value, a plus where a sign matters, a space before
// the unit. A field the user types into shows db::text (ASCII), so what it shows
// can be typed back.

inline juce::String minusSign()
{
    return juce::String::fromUTF8("\xe2\x88\x92");
}

// "+1.5", "\u22121.5", "0.0": no sign for what rounds to zero.
inline juce::String signedText(double value, int decimals)
{
    const auto shown = fixedText(std::abs(value), decimals);
    const double rounded = shown.getDoubleValue();
    if (rounded == 0.0)
        return shown;
    return (value > 0.0 ? juce::String("+") : minusSign()) + shown;
}

// "+1.5 dB", "\u22126.0 dB", "\u2212\u221e dB" at or below the processors' floor.
inline juce::String dbText(double db, int decimals = 1)
{
    if (db <= -119.0)
        return minusSign() + juce::String::fromUTF8("\xe2\x88\x9e dB");
    return signedText(db, decimals) + " dB";
}

// Gain reduction (dB, >= 0) as a cut: "\u22123.2 dB"; `none` under a tenth of a dB.
inline juce::String reductionText(double reductionDb, const juce::String& none = {})
{
    return reductionDb >= 0.1 ? minusSign() + fixedText(reductionDb, 1) + " dB" : none;
}

// "440 Hz", "2.45 kHz", "12.5 kHz".
inline juce::String hzText(double hz)
{
    if (hz >= 1000.0)
        return fixedText(hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
    return juce::String(static_cast<int>(std::lround(hz))) + " Hz";
}

// For a knob's few characters: "440 Hz", "2.5 kHz", "12 kHz".
inline juce::String compactHzText(double hz)
{
    if (hz >= 10000.0)
        return juce::String(static_cast<int>(std::lround(hz / 1000.0))) + " kHz";
    if (hz >= 1000.0)
        return fixedText(hz / 1000.0, 1) + " kHz";
    return juce::String(static_cast<int>(std::lround(hz))) + " Hz";
}

// "250 ms", "1.8 s", "12 s".
inline juce::String secondsText(double seconds)
{
    if (seconds < 1.0)
        return juce::String(static_cast<int>(std::lround(seconds * 1000.0))) + " ms";
    return fixedText(seconds, seconds < 10.0 ? 1 : 0) + " s";
}

// Whole percent: "40%"; signed: "+40%", "\u221240%", "0%".
inline juce::String percentText(double percent)
{
    return juce::String(static_cast<int>(std::lround(percent))) + "%";
}
inline juce::String signedPercentText(double percent)
{
    return signedText(percent, 0) + "%";
}

// "48 kHz", "44.1 kHz".
inline juce::String sampleRateText(double rate)
{
    if (rate <= 0.0)
        return {};
    const double k = rate / 1000.0;
    return fixedText(k, std::abs(k - std::round(k)) < 0.05 ? 0 : 1) + " kHz";
}

} // namespace northlight::ui
