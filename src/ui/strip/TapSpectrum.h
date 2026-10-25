#pragma once

#include "engine/Feedback.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <vector>

// The spectrum analyser behind the strip's displays (the EQ's analyser, the
// exciter's harmonic spectrum): a processor's taps through a Hann window and an
// FFT, mapped to the columns of a log-frequency plot (ui/strip/StripStyle.h's
// hzToX), tilted so a typical mix reads level, and smoothed (a quick rise, a
// fall of 40 dB a second, which is also how it fades once the taps stop).

namespace northlight::ui::strip
{

// A Hann window of `size` points (for any display that takes an FFT of a tap).
std::vector<float> hannWindow(int size);

class TapSpectrum
{
public:
    static constexpr int fftOrder = 13; // 8192 points: 6 Hz bins at 48 kHz, for the bass
    static constexpr int fftSize = 1 << fftOrder;
    // The plot's dB range (dBFS, tilted): a column is held within it, and up to
    // 12 dB over the top.
    static constexpr float topDb = 0.0f, bottomDb = -96.0f;
    // Tilt so a typical mix (falling about 3 dB an octave) reads flat-ish.
    static constexpr double tiltDbPerOctave = 3.0;

    // Analyses `taps` taps, each into a spectrum of its own.
    explicit TapSpectrum(int taps);

    // The newest samples of tap `tap` into its spectrum, one value per column of
    // `plot` (a log-frequency axis over its width); a tap with nothing new since
    // the last call (the device stopped) falls away instead.
    void analyse(const engine::ProcessorFeedback& fb, int tap, juce::Rectangle<float> plot, double elapsedMs);
    // A tap's spectrum: per column, dB (smoothed); empty until analysed, or after clear().
    const std::vector<float>& spectrum(int tap) const { return spectra_[static_cast<size_t>(tap)]; }
    // Forgets the spectra (the plot changed size, the analyser was switched).
    void clear();

private:
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_, fftData_, samples_;
    std::vector<std::vector<float>> spectra_;
    std::vector<std::uint64_t> lastTap_;
};

} // namespace northlight::ui::strip
