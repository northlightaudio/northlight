#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// A hardware face built from rendered parts (design-system.md "Rendered parts"):
// drawn at one design size and scaled uniformly to fit whatever holds it,
// never above 100%. Its controls are its children, placed in design pixels, so
// they scale with it.
//
// It paints in three layers:
//  - the static art (renders, plates, printed ticks and rules), drawn once per
//    scale and cacheKey() into an image;
//  - the static words (names, captions, printed numbers), drawn straight to the
//    screen at every paint, so the window's renderer sets them at their real
//    size instead of the cache's software renderer drawing them into an image
//    that is then resampled;
//  - the live layer (values, meters, lamps), drawn at every paint.
// A subclass draws the first two in one paintStatic(): images, fills and ticks
// only when artPass() is true, words through print(), which draws only in the
// words pass.
class RenderedFace : public juce::Component
{
public:
    RenderedFace(float designWidth, float designHeight);

    float designWidth() const noexcept { return width_; }
    float designHeight() const noexcept { return height_; }

    // The static layer changed (a page, a mode): draw it again.
    void invalidate();

    // Places the face in `body` (in its parent's pixels): scaled to fit and
    // centred across, at the body's top; hidden when there is no room.
    void placeIn(juce::Rectangle<int> body);
    // The face's width when `bodyHeight` tall (a card narrows with it).
    int widthForHeight(int bodyHeight) const noexcept;

    // Compact: shown below compactBelow of its size (the Editor dock), where
    // small print would be a few pixels tall. A face then drops its values,
    // printed scale numbers and small position legends, and prints its names
    // compactNameScale larger (a stepped selector shows its setting instead).
    static constexpr float compactBelow = 0.65f;
    static constexpr float compactNameScale = 1.45f;
    bool isCompact() const noexcept { return compact_; }
    void setCompact(bool compact);

    // The control being hovered or turned (its parameter index, or -1): in
    // compact a face shows its value where its name is (and repaints for it;
    // at full size nothing changes). Set by the card.
    // `area` is where its print lies (in the face's pixels), which is all that
    // repaints; empty, the whole face.
    void setActiveControl(int control, juce::Rectangle<int> area = {});
    int activeControl() const noexcept { return activeControl_; }

    // The value a control shows, by its parameter's index, set by the card: a
    // face prints it under its control, and in compact in place of the name of
    // the control in hand.
    std::function<juce::String(int param)> value;

    void paint(juce::Graphics& g) final;

protected:
    // The static layer: called once with artPass() true, into the cache, and
    // once with it false, to the screen.
    virtual void paintStatic(juce::Graphics& g) const = 0;
    // What changes with the values, over everything static.
    virtual void paintLive(juce::Graphics& g) const { juce::ignoreUnused(g); }
    // What the cached art depends on beyond the scale (a page, a cabinet): a
    // change redraws it.
    virtual int cacheKey() const { return 0; }

    bool artPass() const noexcept { return pass_ == Pass::art; }

    // A name's print: compactNameScale larger in compact, and its tracking
    // compactTracking as wide, so it stays readable at the dock's size.
    static constexpr float compactTracking = 0.4f;
    float nameSize(float size) const noexcept { return compact_ ? size * compactNameScale : size; }
    float nameTracking(float tracking) const noexcept
    {
        return compact_ ? tracking * compactTracking : tracking;
    }
    // A control's printed name, or in compact its value while it is in hand.
    juce::String nameFor(int param, const juce::String& name) const
    {
        return compact_ && activeControl_ == param && value ? value(param) : name;
    }
    // Words printed on the face: drawn in the words pass and the live layer,
    // never into the cached art, set crisply (hardware::drawCrispText).
    void print(juce::Graphics& g,
               const juce::String& text,
               juce::Rectangle<float> area,
               juce::Justification justification = juce::Justification::centred,
               bool centreInk = false) const;

private:
    float width_, height_;
    juce::Image cache_;
    float cacheScale_ = 0.0f;
    int cacheKey_ = 0;
    bool cacheValid_ = false;
    bool compact_ = false;
    int activeControl_ = -1;
    juce::Rectangle<int> activeArea_;
    enum class Pass
    {
        art,
        words,
        live
    };
    Pass pass_ = Pass::live;
};

} // namespace northlight::ui
