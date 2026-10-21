#pragma once

#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// The real-time displays the dynamics and saturation cards share. Each reads
// its card's values and level history and edits through the card (so a drag
// is one gesture, one undo step).

namespace northlight::ui::strip
{

// A scrolling level history, newest on the right: the input (or key) level as
// a grey area, the output as a line in the module's hue, gain reduction hanging
// from the top, and an optional threshold line that drags vertically.
class HistoryGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Style
    {
        float topDb = 0.0f, bottomDb = -60.0f;
        bool levelFromExtra = false; // the grey area is the key/band level, not the input
        bool outputLine = true;
        bool reductionFromTop = true;
        int thresholdParam = -1; // a draggable line, -1 for none
        bool gateLane = false;   // a lane along the bottom showing open and closed
        float seconds = 3.0f;
        bool outputFill = true; // a faint fill under the output line
    };

    HistoryGraph(ModuleCard& card, Style style);

    void paint(juce::Graphics& g) override;
    void paintContent(juce::Graphics& g);
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    // The level drawn in a column at the last paint (linear), for tests.
    float levelColumnForTesting(int column) const
    {
        return column >= 0 && static_cast<size_t>(column) < level_.size()
                   ? level_[static_cast<size_t>(column)]
                   : 0.0f;
    }

private:
    juce::Rectangle<float> plot() const;
    bool nearThreshold(int y) const;

    ModuleCard& card_;
    Style style_;
    bool dragging_ = false;
    std::vector<float> level_, output_, reduction_; // per column, reused
};

// A compressor's static curve: input level across, output up, with the
// make-up applied; the live detector level rides the curve as a dot. Drag near
// the knee to move the threshold, above it to bend the ratio.
class TransferCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TransferCurve(ModuleCard& card);

    void paint(juce::Graphics& g) override;
    void paintContent(juce::Graphics& g);
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    enum class Mode
    {
        none,
        threshold,
        ratio
    };
    juce::Rectangle<float> plot() const;
    float outputDb(float inputDb) const;
    Mode modeAt(juce::Point<float> p) const;

    ModuleCard& card_;
    Mode mode_ = Mode::none;
    static constexpr float floorDb = -60.0f;
};

// A saturation curve: the input across (-1..1), the output up, at the current
// drive, type, trim and mix; the input's live peak shades the part of the curve
// the signal is using.
class SaturationCurve final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SaturationCurve(ModuleCard& card);
    void paint(juce::Graphics& g) override;
    void paintContent(juce::Graphics& g);

private:
    ModuleCard& card_;
};

// A threshold on its meter (screens/channel-strip.md 12.2): a vertical bar of the
// level the threshold is compared with (the input, a gate's key, a
// de-esser's band) with the threshold as a handle across it. Drag anywhere to
// set it (Shift: fine), the wheel steps half a decibel, double-click resets.
// The part of the level over the threshold shows in `overColour` (the hue for
// a gate opening, meterHot for reduction), so the bar says how far the signal
// goes past it.
class ThresholdMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Level
    {
        input,
        extra, // the key or band level (Frame::extra)
    };
    ThresholdMeter(ModuleCard& card,
                   int parameter,
                   Level level,
                   float bottomDb,
                   juce::String caption,
                   juce::Colour overColour);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    static constexpr int preferredWidth = 46;
    void setCaption(const juce::String& caption)
    {
        if (caption_ != caption)
        {
            caption_ = caption;
            repaint();
        }
    }

private:
    juce::Rectangle<float> bar() const;
    float dbAt(float y) const;

    ModuleCard& card_;
    int parameter_;
    Level level_;
    float bottomDb_;
    juce::String caption_;
    juce::Colour over_;
    bool dragging_ = false;
    float dragStartDb_ = 0.0f;
    int dragStartY_ = 0;
};

// Vertical meters: IN and OUT on the -60..0 dB meter scale, GR hanging from
// the top on a 0..24 dB scale, each with its value above.
class LevelBars final : public juce::Component
{
public:
    enum class Kind
    {
        input,
        reduction,
        output
    };
    LevelBars(ModuleCard& card, std::vector<Kind> kinds);
    void paint(juce::Graphics& g) override;
    int preferredWidth() const noexcept { return static_cast<int>(kinds_.size()) * 32; }

private:
    ModuleCard& card_;
    std::vector<Kind> kinds_;
};

} // namespace northlight::ui::strip
