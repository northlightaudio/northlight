#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>

// The parts the MIDI faces share (design-system.md "MIDI face", "Keyboard
// strip"): a run of piano keys showing notes, and a display that its card paints
// and handles the pointer for.

namespace northlight::ui::strip
{

// What a keyboard strip shows. `lit` is each note's fill in the hue (0: none,
// 1: full); `held` the keys held going in; `scaleMask` (bit n = pitch class n)
// the key's notes, dotted.
struct KeyboardView
{
    int lowNote = 48; // a C
    int octaves = 2;
    std::array<float, 128> lit{};
    std::array<bool, 128> held{};
    std::uint16_t scaleMask = 0;
};

// The strip's height (it grows with a taller display, to the largest) and its
// smallest white key.
inline constexpr float keyboardHeight = 36.0f;
inline constexpr float keyboardLargest = 56.0f;
inline constexpr float minimumWhiteKey = 8.0f;

// Draws the keys across `area` (its full width, as tall as the area allows from
// keyboardHeight to keyboardLargest, from its bottom): white keys surface3, black
// surface0, notes lit in `hue`, held keys barred at their foot, the key's notes
// dotted, C keys named. A lit note past either end shows as a chevron on the end
// key.
void drawKeyboard(juce::Graphics& g,
                  const Theme& theme,
                  juce::Rectangle<float> area,
                  juce::Colour hue,
                  const KeyboardView& view);
// The bounds of one key in a strip drawn in `area` (empty for a note outside it).
juce::Rectangle<float> keyBounds(juce::Rectangle<float> area, const KeyboardView& view, int note);

// A display in a MIDI face: its card paints it and takes its pointer events.
class FaceDisplay final : public juce::Component
{
public:
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> onPaint;
    std::function<void(const juce::MouseEvent&)> onDown, onDrag, onUp, onMove;
    std::function<void()> onExit;

    void paint(juce::Graphics& g) override
    {
        if (onPaint)
            onPaint(g, getLocalBounds().toFloat());
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onDown)
            onDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (onDrag)
            onDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onUp)
            onUp(e);
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        if (onMove)
            onMove(e);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (onExit)
            onExit();
    }
};

} // namespace northlight::ui::strip
