#pragma once

#include "plugins/Biquad.h"
#include "plugins/ChannelStrip.h"
#include "ui/strip/ModuleCard.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace aural::ui::strip
{

// The parametric EQ display (12-channel-strip.md 12.4): the response on a log
// frequency axis (20 Hz-20 kHz, +-24 dB) with a node per band to drag
// (frequency across, gain up and down; the wheel sets Q, or a cut's slope;
// double-click switches a band; right-click picks its shape), and behind it a
// spectrum analyser of the signal after the EQ (filled) and before it (a line),
// from the processor's taps.
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph(ModuleCard& card);
    ~EqGraph() override;

    int selectedBand() const noexcept { return selected_; }
    void setSelectedBand(int band);
    std::function<void(int band)> onSelect;

    void setAnalyser(bool on);
    bool analyser() const noexcept { return analyser_; }

    // Each UI tick: runs the analyser (about 30 times a second) and repaints.
    void update(double elapsedMs);
    // The parameters changed: the curve is recomputed on the next paint.
    void invalidateCurve()
    {
        curveValid_ = false;
        repaint();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static constexpr float rangeDb = 24.0f;

private:
    juce::Rectangle<float> plot() const;
    juce::Point<float> nodePosition(int band) const;
    int bandAt(juce::Point<float> p) const;
    bool bandOn(int band) const;
    void showBandMenu(int band);
    void rebuildCurve();
    void analyse(int tap, std::vector<float>& smoothed, double elapsedMs);

    ModuleCard& card_;
    int selected_ = plugins::strip::eq::band2;
    int hovered_ = -1, dragging_ = -1;
    juce::Point<float> hoverPoint_{-1.0f, -1.0f};
    bool analyser_ = true;

    // The response, per column, recomputed when the parameters or size change.
    bool curveValid_ = false;
    std::vector<float> curveDb_;
    std::array<std::vector<float>, plugins::strip::eq::bandCount> bandDb_;
    std::array<std::array<float, 5>, plugins::strip::eq::bandCount>
        bandKeys_{}; // each band's settings as drawn
    float lastOutput_ = 0.0f;
    double lastRate_ = 0.0;

    // The analyser.
    static constexpr int fftOrder = 13; // 8192 points
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_, fftData_, samples_;
    std::array<std::vector<float>, 2> spectrum_; // per column, dBFS (smoothed); 0 = before, 1 = after
    std::array<std::uint64_t, 2> lastTap_{};
    double sinceAnalysisMs_ = 0.0;
    PillToggle analyserToggle_;
};

} // namespace aural::ui::strip
