#pragma once

#include "plugins/Biquad.h"
#include "plugins/ChannelStrip.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/TapSpectrum.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace northlight::ui::strip
{

// The bands an EqGraph edits, so one graph serves the channel strip's EQ and
// the built-in EQ: how many there are, which parameter holds each band's
// fields, and how to compute a band's filter sections for the curve.
struct EqBands
{
    using Field = plugins::strip::eq::Field;
    int count = 0;
    int selected = 0;          // the band selected at first
    int output = -1;           // the output gain's parameter, or -1
    double lowestNodeHz = 0.0; // a node below this (a high-pass at 0 Hz, off) sits here
    // A field's parameter for a band, or -1 when the band has none.
    std::function<int(int band, Field field)> index;
    std::function<bool(int band)> isCut;
    std::function<juce::Colour(const Theme& theme, int band)> colour;
    std::function<juce::String(int band)> name;  // for undo and menus
    std::function<juce::String(int band)> label; // on the node
    // Whether a band is on, and switching it (by its on parameter by default).
    std::function<bool(const ModuleCard& card, int band)> isOn;
    std::function<void(ModuleCard& card, int band, bool on)> setOn;
    // A band's filter sections (identity where off), at most maxSections.
    std::function<void(const float* params, double sampleRate, plugins::Biquad* out, int band)> design;
    static constexpr int maxSections = plugins::strip::eq::maxSections;
};

// The channel strip's EQ: a low cut, four parametric bands, a high cut.
EqBands stripEqBands();

// The parametric EQ display (screens/channel-strip.md 12.4): the response on a log
// frequency axis (20 Hz-20 kHz, +-24 dB) with a node per band to drag
// (frequency across, gain up and down; the wheel sets Q, or a cut's slope;
// double-click switches a band; right-click picks its shape), and behind it a
// spectrum analyser of the signal after the EQ (filled) and before it (a line),
// from the processor's taps.
class EqGraph final : public juce::Component
{
public:
    explicit EqGraph(ModuleCard& card, EqBands bands = stripEqBands());
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

    int index(int band, EqBands::Field field) const { return bands_.index(band, field); }

    ModuleCard& card_;
    EqBands bands_;
    int selected_ = 0;
    int hovered_ = -1, dragging_ = -1;
    juce::Point<float> hoverPoint_{-1.0f, -1.0f};
    bool analyser_ = true;

    // The response, per column, recomputed when the parameters or size change.
    bool curveValid_ = false;
    std::vector<float> curveDb_;
    std::vector<std::vector<float>> bandDb_;
    std::vector<std::array<float, 5>> bandKeys_; // each band's settings as drawn
    float lastOutput_ = 0.0f;
    double lastRate_ = 0.0;

    // The analyser: tap 0 before the EQ, 1 after it.
    TapSpectrum spectrum_{2};
    double sinceAnalysisMs_ = 0.0;
    PillToggle analyserToggle_;
};

} // namespace northlight::ui::strip
