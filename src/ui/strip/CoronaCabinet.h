#pragma once

// Corona's cabinet page, its live parts (screens/plugins.md 7.2a): the baffle's
// speakers with each mic's marker over the first, and the handles that move the
// mics. The baffle itself is drawn by the face.

#include "plugins/Cabinet.h"
#include "ui/Theme.h"
#include "ui/components/HardwareParts.h"
#include "ui/strip/CoronaFace.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <functional>

namespace northlight::ui::strip::coronacabinet
{

namespace ef = coronaface;
namespace pc = plugins::cabinet;

// One of Corona's renders (assets/corona).
inline const juce::Image& part(const char* file)
{
    return hardware::image("corona", file);
}

// Live text on the face and its layers, set crisply (hardware::drawCrispText).
inline void text(juce::Graphics& g,
                 const juce::String& s,
                 juce::Rectangle<float> area,
                 juce::Justification j = juce::Justification::centred)
{
    hardware::drawCrispText(g, s, area, j);
}

// A mic's handle over its capsule: what takes the mouse and the keys. A drag
// moves the mic across the cone (along its ray from the dust cap), the wheel or
// Up and Down move it nearer and further, Left and Right along the cone,
// double-click puts it back on its default place.
class MicHandle final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MicHandle(const Theme& theme, int mic) : theme_(theme), mic_(mic)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        setTitle(mic == 0 ? "Mic A" : "Mic B");
    }

    // The card's hooks: a place on the cone (0..1) or a distance (cm) the user
    // chose, and the drag's gesture.
    std::function<void(float position)> onPosition;
    std::function<void(float distanceCm)> onDistance;
    std::function<void()> onReset;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<float(juce::Point<float> facePoint)> positionAt;

    float position = 0.3f, distanceCm = 3.0f;

    void describe(const juce::String& text)
    {
        setTooltip(getTitle() + ": " + text);
        setDescription(text);
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(2.0f);
        if (isEnabled() && isMouseOver(true))
        {
            g.setColour(theme_.hardware.highlight.withAlpha(isMouseButtonDown() ? 0.10f : 0.07f));
            g.fillEllipse(r);
        }
        hardware::drawFocusRing(g, *this, r, r.getWidth() * 0.5f);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (!isEnabled())
            return;
        grabKeyboardFocus();
        if (onGestureBegin)
            onGestureBegin();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isEnabled() || !positionAt)
            return;
        const auto onFace = e.getEventRelativeTo(getParentComponent()).position;
        if (onPosition)
            onPosition(positionAt(onFace));
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (isEnabled() && onGestureEnd)
            onGestureEnd();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (isEnabled() && onReset)
            onReset();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (isEnabled() && onDistance && wheel.deltaY != 0.0f)
            onDistance(nearer(wheel.deltaY > 0.0f));
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (!isEnabled())
            return false;
        const float step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
        if (key == juce::KeyPress::leftKey && onPosition)
            onPosition(std::clamp(position - step, 0.0f, 1.0f));
        else if (key == juce::KeyPress::rightKey && onPosition)
            onPosition(std::clamp(position + step, 0.0f, 1.0f));
        else if (key == juce::KeyPress::upKey && onDistance)
            onDistance(nearer(true));
        else if (key == juce::KeyPress::downKey && onDistance)
            onDistance(nearer(false));
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    // A step nearer the speaker or further from it, an eighth of an octave of distance.
    float nearer(bool closer) const
    {
        return std::clamp(distanceCm * (closer ? 1.0f / 1.09f : 1.09f), 1.0f, 100.0f);
    }

    const Theme& theme_;
    int mic_ = 0;
};

// The baffle: the cabinet's speakers, and each mic's stand and capsule over the
// first speaker, its label beside it. The baffle itself is the face's; this is
// the live layer over it.
class Baffle final : public juce::Component
{
public:
    explicit Baffle(const Theme& theme) : theme_(theme) { setInterceptsMouseClicks(false, true); }

    int model = pc::open2x12;
    bool cabinetOn = true;
    std::array<pc::Mic, 2> mics{};
    juce::String message; // over the speakers instead of the mics (a custom response, the line out)

    // Face coordinates to this layer's (it sits at the baffle's corner).
    static juce::Point<float> local(ef::Point p) { return {p.x - ef::baffle.x, p.y - ef::baffle.y}; }

    void paint(juce::Graphics& g) override;

    // Where a mic's capsule is, on this layer.
    juce::Rectangle<float> capsule(int m) const;

private:
    // A mic's type as a small glyph: a dynamic's slim capsule on its clip, a
    // ribbon's slotted box, a condenser's big round head.
    static void typeGlyph(juce::Graphics& g, int type, juce::Rectangle<float> box, float stroke);

    // A mic's marker over the cone: a ring in the mic's colour, as large as the
    // mic would look from the speaker (smaller the further it is) and squeezed by
    // its angle off axis, its type's glyph inside and its label beside it. A mic
    // that is off is a faint dashed ring.
    void paintMic(juce::Graphics& g, const ef::Speaker& speaker, int m) const;

    const Theme& theme_;
};

} // namespace northlight::ui::strip::coronacabinet
