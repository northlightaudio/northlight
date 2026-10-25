#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <string_view>

// The drawing shared by a hardware face (design-system.md "Hardware face"):
// an effect drawn as a piece of studio equipment in the hardware tokens.
namespace northlight::ui::hardware
{

// A black chicken-head pointer knob, pointing at `angle` (radians clockwise from
// 12 o'clock): the pointed body with a cream line along it and a round cap.
void drawPointerKnob(juce::Graphics& g,
                     const Theme& theme,
                     juce::Point<float> centre,
                     float diameter,
                     float angle,
                     bool enabled,
                     bool hover);

// The printed scale round a knob: `count` ticks from `from` over `sweep`
// (radians), every `major`th one longer.
void drawScale(juce::Graphics& g,
               const Theme& theme,
               juce::Point<float> centre,
               float radius,
               float from,
               float sweep,
               int count,
               int major,
               bool enabled);

// Text printed on a textured panel: a soft shade under it keeps it legible
// over the light patches of a worn finish.
void drawLegend(juce::Graphics& g,
                const Theme& theme,
                const juce::String& text,
                juce::Rectangle<float> area,
                juce::Justification justification,
                juce::Colour colour);

// An image under assets/<folder>/ beside the executable, loaded once (invalid
// when the file is missing: a face falls back to its flat token colours). Looked
// up without building a string, so a face may call it from paint.
const juce::Image& image(std::string_view folder, std::string_view file);
inline const juce::Image& image(const char* folder, const char* file)
{
    return image(std::string_view(folder), std::string_view(file));
}
inline const juce::Image& image(const juce::String& folder, const juce::String& file)
{
    return image(std::string_view(folder.toRawUTF8()), std::string_view(file.toRawUTF8()));
}

// `source` shrunk to `width` x `height` pixels by halving it step by step and
// a final high-quality pass, kept for reuse. A rendered part drawn far below
// its size in one bilinear step aliases its highlights into sparkle; drawn from
// an image already the size it lands at, it stays clean.
// Returned by value (an Image shares its pixels): the cache may let go of old
// sizes while a caller still holds one.
juce::Image scaled(const juce::Image& source, int width, int height);

// Draws `source` into `area` from an image shrunk to the pixels the area
// covers on screen (the context's scale included), so it is drawn 1:1.
void drawScaled(juce::Graphics& g,
                const juce::Image& source,
                juce::Rectangle<float> area,
                float opacity = 1.0f);

// The same, turned `angle` radians clockwise about the area's centre: a layer
// that turns (a knob's pointer, a roller's drillings), shrunk first so turning
// it only resamples it 1:1.
void drawScaledRotated(juce::Graphics& g,
                       const juce::Image& source,
                       juce::Rectangle<float> area,
                       float angle,
                       float opacity = 1.0f);

// A rendered plate (a panel, a grille) whose image carries its drop shadow in
// a `margin` round `area`; without its image, `area` filled flat in `flat`.
void drawPlate(juce::Graphics& g,
               const juce::Image& source,
               juce::Rectangle<float> area,
               float margin,
               juce::Colour flat,
               float cornerRadius = 6.0f);

// The ticks of a knob's printed scale: `count` ticks evenly over `sweepDegrees`
// centred on 12 o'clock, from `inner` to `outer`; every `majorEvery`th one (0:
// none) reaches `majorExtra` further in. In the current colour.
void drawTicks(juce::Graphics& g,
               juce::Point<float> centre,
               float inner,
               float outer,
               int count,
               float thickness,
               int majorEvery = 0,
               float majorExtra = 0.0f,
               float sweepDegrees = 270.0f);

// Text set at its real size in device pixels, on a whole-pixel baseline, in the
// current font and colour, as drawText would place it in `area`. Under a scaled
// transform (a face scaled into the dock, or Windows display scaling) JUCE draws
// glyphs as scaled outlines with no grid fitting, and at a fractional position
// every stem straddles two pixels; this takes the scale out of the transform and
// sizes the font up instead, so DirectWrite renders the glyphs at the size they
// land, and rounds the baseline. Centred text centres on its letters' ink (so
// tracking after the last letter does not push it off centre). Text wider than
// the area is set smaller to fit
// it (down to 70% of its size), never cut.
// With `centreInk`, vertically centred text is centred on its letters' ink too
// (a name alone on a plate); without it on the font's height, so a row of labels
// keeps one baseline whatever their letters.
void drawCrispText(juce::Graphics& g,
                   const juce::String& text,
                   juce::Rectangle<float> area,
                   juce::Justification justification = juce::Justification::centred,
                   bool centreInk = false);

// A point `radius` from `centre` at `degrees` clockwise from 12 o'clock.
juce::Point<float> polar(juce::Point<float> centre, float radius, float degrees);

// The face legends' font (Barlow Condensed) at a size, weight and tracking (em).
juce::Font legendFont(const Theme& theme, float size, int weight, float trackingEm);

// The system's focus ring round `area`, when `component` has the keyboard focus.
void drawFocusRing(juce::Graphics& g, juce::Component& component, juce::Rectangle<float> area, float radius);

// A VU needle's angle from upright, in degrees, for a reading in VU. The scale
// is linear in voltage, as a VU's is: -20 near the left end, 0 about 71% across,
// +3 at the right; `halfSweep` degrees each side, and the needle stops 3 degrees
// past them.
inline float vuAngle(float vu, float halfSweep) noexcept
{
    const float share = std::pow(10.0f, vu / 20.0f) / std::pow(10.0f, 3.0f / 20.0f);
    return std::clamp(-halfSweep + 2.0f * halfSweep * share, -halfSweep - 3.0f, halfSweep + 3.0f);
}

// A VU needle's movement: a VU's 300 ms to 99% of a step, as two equal lags (a
// heavy needle, critically damped, so it never jumps).
struct VuBallistics
{
    float level = 0.0f; // what the needle shows, linear
    float inner = 0.0f;

    void advance(float target, double seconds) noexcept
    {
        constexpr double lag = 0.045; // two of these reach 99% of a step in 0.3 s
        const auto k = static_cast<float>(1.0 - std::exp(-std::max(0.0, seconds) / lag));
        inner += (target - inner) * k;
        level += (inner - level) * k;
    }
};

// Fitting a face drawn at one size (`faceWidth` x `faceHeight`) to a body: the
// largest scale that fits, never above 1; and the body's width at a height,
// so a card narrows with its face rather than leaving panel beside it.
float fitScale(float bodyWidth, float bodyHeight, float faceWidth, float faceHeight) noexcept;
int widthForHeight(int bodyHeight, float faceWidth, float faceHeight) noexcept;

} // namespace northlight::ui::hardware
