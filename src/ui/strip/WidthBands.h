#pragma once

// The stereo widener's width bands (screens/plugins.md 7.2a), and the output's two
// channels they and the vectorscope read from the processor's taps.

#include "engine/Feedback.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/WidthScale.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <complex>
#include <cstdint>
#include <optional>
#include <vector>

namespace northlight::ui::strip::widthbands
{

constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder; // the width spectrum's window (85 ms at 48 kHz)
constexpr int spectrumBands = 56;      // log-spaced, 20 Hz to 20 kHz

// ----- the output's two channels, read from the processor's taps ------------------------------

// Tap 0 carries the output's left channel and tap 1 its right; both are written
// per block, so a read is good when neither moved while it was taken.
class StereoTaps
{
public:
    StereoTaps() : left_(fftSize, 0.0f), right_(fftSize, 0.0f) {}

    // True when new samples were read.
    bool update(engine::ProcessorFeedback* fb)
    {
        if (fb == nullptr || !fb->hasTaps())
            return false;
        fb->setTapsEnabled(true);
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto p0 = fb->tapPosition(0), p1 = fb->tapPosition(1);
            if (p0 != p1)
                continue;
            if (p0 == position_)
                return false;
            if (!fb->readTap(0, left_.data(), fftSize) || !fb->readTap(1, right_.data(), fftSize))
                return false;
            if (fb->tapPosition(0) == p0 && fb->tapPosition(1) == p1)
            {
                position_ = p0;
                return true;
            }
        }
        return false;
    }

    const std::vector<float>& left() const noexcept { return left_; }
    const std::vector<float>& right() const noexcept { return right_; }

private:
    std::vector<float> left_, right_;
    std::uint64_t position_ = 0;
};

// ----- the width bands ---------------------------------------------------------------------------

// The three bands on a frequency axis, width up (0 mono, 100% as it came, 200%
// twice the side): each band's set width as a block in the hue, the crossovers
// and Mono below as lines to drag, and the width the output measures at each
// frequency (its side over its mid) as a line over them. A guard that narrows
// shows the width it leaves as a dashed line in warning.
class WidthBands final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WidthBands(ModuleCard& card);

    void update(const StereoTaps& taps, bool fresh);

    void paint(juce::Graphics& g) override;

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // For tests: the measured width (%) at a frequency, when there is signal there.
    std::optional<float> measuredWidthAt(double hz) const;

private:
    enum class Drag
    {
        low = 0,
        mid = 1,
        high = 2,
        lowSplit,
        highSplit,
        monoBelow,
        none
    };

    static const std::array<juce::String, 3>& bandNames();

    static int paramOf(Drag d);

    juce::Rectangle<float> plot() const;
    static constexpr int captionRow = 20; // the bands' names above the plot
    static float monoHandleY(juce::Rectangle<float> p) { return p.getY() + 12.0f; }
    static float widthToY(float w, juce::Rectangle<float> p) { return widthscale::toY(w, p); }

    int bandAt(float x) const;

    Drag lineAt(juce::Point<float> at) const;

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> p) const;

    // A line's grip: a small upright pill, so it reads as "drag sideways" beside
    // the bands' round handles, which drag up and down.
    void drawGrip(juce::Graphics& g, juce::Point<float> at, bool active, bool dim = false) const;

    void drawFrequency(juce::Graphics& g, float x, float hz) const;

    void drawMeasured(juce::Graphics& g, juce::Rectangle<float> p) const;

    // Over the top: above each run of bands measuring past 210%, a small
    // triangle in the warning colour at the strip's top, with the run's widest
    // ("340%", or ">800%" off the strip) beside it where there is room.
    void drawOverMarkers(juce::Graphics& g, juce::Rectangle<float> p) const;

    static double bandOf(double hz) { return std::log(hz / minHz) / std::log(maxHz / minHz) * spectrumBands; }
    static double centreHz(int band);

    // A band's width with its neighbours' (a third of an octave or so each side),
    // none where it is too quiet to say: below about -80 dBFS, or 60 dB under
    // the loudest band.
    std::optional<float> widthOf(int band) const;

    // The output's mid and side power in each band, from one window of both taps.
    void analyse(const StereoTaps& taps);

    ModuleCard& card_;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_;
    std::vector<juce::dsp::Complex<float>> in_, out_; // reused every analysis
    std::array<float, spectrumBands> mid_{}, side_{};
    float loudest_ = 0.0f;
    // Below about -80 dB per bin (a full-scale sine through the Hann window is
    // about (fftSize / 4)^2): too quiet to say how wide.
    const float quietPower_ = 1.0e-8f * static_cast<float>(fftSize) * static_cast<float>(fftSize) / 16.0f;
    Drag drag_ = Drag::none, hoverLine_ = Drag::none;
    int hover_ = -1;
};

} // namespace northlight::ui::strip::widthbands
