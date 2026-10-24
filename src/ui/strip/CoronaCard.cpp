// Corona's card (screens/plugins.md 7.2a): a tube amp's front and, on a second
// page, its cabinet with the grille off and two mics in front of a speaker. A
// hardware face (design-system.md "Hardware face", "Two-page face") built from
// rendered parts (assets/corona; "Rendered parts"), drawn at 1160 x 600 and
// scaled to fit the card's body, the card narrowing with it.
//
// The amp page: the champagne control plate (BRIGHT, VOLUME, TREBLE, MIDDLE,
// BASS, REVERB, SPEED, INTENSITY, MASTER, BOOST and the pilot jewel, which
// glows as hard as the power amp is driven), the grille with its badge. The
// cabinet page: the baffle with the cabinet's speakers and the two mics, dragged
// across the cone, and their controls on a plate beside it. The utility strip
// along the foot, on both pages: INPUT with its meter, GATE, SAG, OUTPUT, the
// QUALITY keys, the footswitch's REVERB and TREMOLO buttons, and the page keys.

#include "core/Command.h"
#include "plugins/Amp.h"
#include "plugins/Cabinet.h"
#include "ui/Number.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/CoronaFace.h"
#include "ui/strip/HardwareKey.h"
#include "ui/strip/HardwareLever.h"
#include "ui/strip/RenderedCard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

namespace pa = plugins::amp;
namespace pc = plugins::cabinet;
namespace ef = coronaface;

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

// ----- where things are on the face (pixels at 100%) ----------------------------------------

// Plates and their images (each image has a 12 px margin holding its shadow).
const juce::Rectangle<float> ampPlate{20.0f, 24.0f, 1120.0f, 250.0f};
const juce::Rectangle<float> grilleArea{20.0f, 290.0f, 1120.0f, 198.0f};
const juce::Rectangle<float> badgeArea{440.0f, 349.0f, 280.0f, 80.0f};
// The brass itself inside the badge's image (measured from badge.png: the plate
// sits a little above the image's middle, its shadow below): the name centres on it.
const juce::Rectangle<float> badgeBrass{446.5f, 353.0f, 267.0f, 67.5f};
const juce::Rectangle<float> stripArea{20.0f, 500.0f, 1120.0f, 92.0f};
const juce::Rectangle<float> baffleArea{ef::baffle.x, ef::baffle.y, ef::baffle.w, ef::baffle.h};
const juce::Rectangle<float> cabPlate{720.0f, 24.0f, 420.0f, 464.0f};
constexpr float shadowMargin = 12.0f;

// A knob's image is this many times its skirt (the chrome skirt is 55% of the
// render, the rest room for its shadow).
constexpr float knobImageRatio = 1.0f / 0.55f;

// The amp page's controls: the knob row's centres.
constexpr float knobRow = 134.0f;
constexpr float bigSkirt = 62.0f, bigImage = bigSkirt * knobImageRatio;
constexpr std::array<float, 8> ampKnobX{160.0f, 265.0f, 370.0f, 475.0f, 590.0f, 695.0f, 800.0f, 912.0f};
constexpr std::array<int, 8> ampKnobParam{
    pa::volume, pa::treble, pa::middle, pa::bass, pa::reverb, pa::speed, pa::intensity, pa::master};
constexpr std::array<const char*, 8> ampKnobName{
    "VOLUME", "TREBLE", "MIDDLE", "BASS", "REVERB", "SPEED", "INTENSITY", "MASTER"};
constexpr float brightX = 62.0f, boostX = 1000.0f;
const juce::Point<float> jewelCentre{1085.0f, 128.0f};
constexpr float nameRow = 196.0f, valueRow = 218.0f;
// Captions over the groups: (first x, last x, text).
struct Caption
{
    float from, to;
    const char* text;
};
constexpr std::array<Caption, 4> ampCaptions{{
    {brightX, 475.0f, "PREAMP"},
    {590.0f, 590.0f, "REVERB"},
    {695.0f, 800.0f, "TREMOLO"},
    {912.0f, boostX, "POWER"},
}};

// The levers, drawn at 70% of their render (the nut 36 px).
constexpr float leverScale = 0.7f;

// The utility strip.
constexpr float stripKnobRow = 531.0f, stripNameRow = 560.0f, stripValueRow = 577.0f;
constexpr float smallSkirt = 38.0f, smallImage = smallSkirt * knobImageRatio;
constexpr std::array<float, 4> stripKnobX{66.0f, 268.0f, 352.0f, 436.0f};
constexpr std::array<int, 4> stripKnobParam{pa::input, pa::gate, pa::sag, pa::output};
constexpr std::array<const char*, 4> stripKnobName{"INPUT", "GATE", "SAG", "OUTPUT"};
const juce::Rectangle<float> inputMeterArea{100.0f, 520.0f, 116.0f, 14.0f};
const juce::Point<float> gateLedCentre{291.0f, 512.0f};
constexpr float keyHeight = 26.0f;
const juce::Rectangle<float> qualityKeys{512.0f, 519.0f, 150.0f, keyHeight};
const juce::Rectangle<float> footswitchArea{706.0f, 506.0f, 200.0f, 80.0f};
constexpr std::array<float, 2> footswitchX{760.0f, 852.0f};
constexpr float footswitchY = 532.0f, footswitchSize = 40.0f;
// The page keys: two square keys, an icon over the page's name.
const juce::Rectangle<float> pageKeys{968.0f, 512.0f, 128.0f, 68.0f};

// The cabinet plate.
const juce::Point<float> cabinetKnob{800.0f, 92.0f};
constexpr float cabSkirt = 52.0f, cabImage = cabSkirt * knobImageRatio;
constexpr float speakerX = 915.0f, roomX = 1050.0f, cabNameRow = 150.0f, cabValueRow = 168.0f;
const juce::Rectangle<float> loadKey{736.0f, 182.0f, 96.0f, 24.0f};
const juce::Rectangle<float> fileText{842.0f, 182.0f, 282.0f, 24.0f};
constexpr float ruleY = 216.0f;
constexpr std::array<float, 2> micColumnX{736.0f, 940.0f};
constexpr float micColumnW = 188.0f;
constexpr float micTitleRow = 226.0f, micTypeRow = 258.0f;
constexpr std::array<float, 2> micKnobRow{330.0f, 424.0f};
constexpr float micSkirt = 40.0f, micImage = micSkirt * knobImageRatio;
// Each mic's four knobs: distance, angle (row 1); level, pan (row 2).
constexpr std::array<int, 4> micKnobField{pa::micDistance, pa::micAngle, pa::micLevel, pa::micPan};
constexpr std::array<const char*, 4> micKnobName{"DISTANCE", "ANGLE", "LEVEL", "PAN"};
constexpr std::array<const char*, 6> cabinetLegend{"2\xc3\x97"
                                                   "12",
                                                   "1\xc3\x97"
                                                   "12",
                                                   "4\xc3\x97"
                                                   "10",
                                                   "1\xc3\x97"
                                                   "15",
                                                   "4\xc3\x97"
                                                   "10B",
                                                   "IR"};

float micKnobX(int mic, int index)
{
    return micColumnX[static_cast<size_t>(mic)] + (index % 2 == 0 ? 46.0f : 142.0f);
}

const juce::Image& part(const char* file)
{
    return hardware::image("corona", file);
}

using hardware::legendFont;
using hardware::polar;

// Live text on the face and its layers, set crisply (hardware::drawCrispText).
void text(juce::Graphics& g,
          const juce::String& s,
          juce::Rectangle<float> area,
          juce::Justification j = juce::Justification::centred)
{
    hardware::drawCrispText(g, s, area, j);
}

// A plate whose render carries its shadow in a 12 px margin, or its flat fallback.
void plate(juce::Graphics& g, const char* file, juce::Rectangle<float> area, juce::Colour flat)
{
    hardware::drawPlate(g, part(file), area, shadowMargin, flat);
}

// ----- the page keys' icons -----------------------------------------------------------------

// The amp: a combo's outline, its control panel with three knobs over the grille.
void drawAmpIcon(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto box =
        r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight() * 1.35f), r.getHeight()).reduced(1.0f);
    const juce::PathStrokeType line(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    p.addRoundedRectangle(box, 2.5f);
    const float panel = box.getY() + box.getHeight() * 0.32f;
    p.startNewSubPath(box.getX(), panel);
    p.lineTo(box.getRight(), panel);
    const auto grille = box.withTop(panel).reduced(box.getWidth() * 0.12f, box.getHeight() * 0.12f);
    p.addRoundedRectangle(grille, 1.5f);
    g.strokePath(p, line);
    for (int i = 0; i < 3; ++i)
    {
        const float x = box.getX() + box.getWidth() * (0.25f + 0.25f * static_cast<float>(i));
        g.fillEllipse(
            juce::Rectangle<float>(2.0f, 2.0f).withCentre({x, box.getY() + box.getHeight() * 0.16f}));
    }
}

// The cabinet: a box with two speakers.
void drawCabinetIcon(juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto box =
        r.withSizeKeepingCentre(std::min(r.getWidth(), r.getHeight() * 1.35f), r.getHeight()).reduced(1.0f);
    const juce::PathStrokeType line(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    p.addRoundedRectangle(box, 2.5f);
    const float d = std::min(box.getWidth() * 0.4f, box.getHeight() * 0.72f);
    for (int i = 0; i < 2; ++i)
    {
        const juce::Point<float> c(box.getX() + box.getWidth() * (i == 0 ? 0.28f : 0.72f), box.getCentreY());
        p.addEllipse(juce::Rectangle<float>(d, d).withCentre(c));
        g.fillEllipse(juce::Rectangle<float>(d * 0.3f, d * 0.3f).withCentre(c));
    }
    g.strokePath(p, line);
}

// ----- a footswitch button -----------------------------------------------------------------

// The footswitch's push-button for REVERB or TREMOLO, a LED over it: lit while
// on (the tremolo's pulses with the light cell).
class Footswitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `name` for the tooltip ("Reverb: On"); the accessible title adds "footswitch".
    Footswitch(const Theme& theme, juce::String name) : theme_(theme), title_(std::move(name))
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(title_ + " footswitch");
        setTooltip(title_ + ": Off");
    }

    std::function<void()> onPress;

    void set(bool on, float glow)
    {
        if (on == on_ && std::abs(glow - glow_) < 0.02f)
            return;
        if (on != on_)
        {
            setTooltip(title_ + (on ? ": On" : ": Off"));
            setDescription(on ? "On" : "Off");
        }
        on_ = on;
        glow_ = glow;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        const bool enabled = isEnabled();
        const auto button = buttonBounds();
        const auto& image = part(isMouseButtonDown() ? "button-down.png" : "button-up.png");
        if (image.isValid())
            hardware::drawScaled(
                g, image, button.expanded(button.getWidth() * margin), enabled ? 1.0f : 0.5f);
        else
        {
            g.setColour(hw.aluminium.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
            g.fillEllipse(button);
            g.setColour(hw.shadow.withAlpha(0.35f));
            g.drawEllipse(button.reduced(button.getWidth() * 0.18f), 1.5f);
        }
        if (enabled && isMouseOver(true))
        {
            g.setColour(hw.highlight.withAlpha(isMouseButtonDown() ? 0.10f : 0.07f));
            g.fillEllipse(button);
        }
        // The LED to the button's right.
        const auto led =
            juce::Rectangle<float>(7.0f, 7.0f)
                .withCentre({button.getRight() + button.getWidth() * margin + 4.0f, button.getCentreY()});
        const float level = on_ ? glow_ : 0.0f;
        g.setColour(hw.ledOff);
        g.fillEllipse(led);
        if (level > 0.0f)
        {
            g.setColour(hw.led.withAlpha(level));
            g.fillEllipse(led);
            g.setColour(hw.led.withAlpha(0.3f * level));
            g.fillEllipse(led.expanded(4.0f));
        }
        hardware::drawFocusRing(g, *this, button, button.getWidth() * 0.5f);
    }

    bool hitTest(int x, int y) override
    {
        return buttonBounds().expanded(4.0f).contains(static_cast<float>(x), static_cast<float>(y));
    }
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (isEnabled() && buttonBounds().expanded(4.0f).contains(e.position) && onPress)
            onPress();
        repaint();
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key != juce::KeyPress::spaceKey && key != juce::KeyPress::returnKey)
            return false;
        if (onPress)
            onPress();
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler>(
            *this,
            juce::AccessibilityRole::toggleButton,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                   [this]
                                                   {
                                                       if (onPress)
                                                           onPress();
                                                   }));
    }

private:
public:
    // The share of the button's size its image reaches past it on each side (the
    // render's rim and shadow), and the room the LED takes to its right.
    static constexpr float margin = 0.3f;
    static constexpr float ledRoom = 12.0f;
    // The component's size for a button `size` across.
    static juce::Rectangle<float> boundsFor(juce::Point<float> centre, float size)
    {
        const float m = size * margin;
        return {centre.x - size * 0.5f - m,
                centre.y - size * 0.5f - m,
                size + 2.0f * m + ledRoom,
                size + 2.0f * m};
    }

private:
    juce::Rectangle<float> buttonBounds() const
    {
        const float h = static_cast<float>(getHeight());
        const float size = h / (1.0f + 2.0f * margin);
        return {(h - size) * 0.5f, (h - size) * 0.5f, size, size};
    }

    const Theme& theme_;
    juce::String title_;
    bool on_ = false;
    float glow_ = 1.0f;
};

// ----- the pilot jewel ---------------------------------------------------------------------

class Jewel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Jewel(const Theme& theme) : theme_(theme)
    {
        setTitle("Power amp drive");
        setTooltip("Power amp drive");
    }

    void set(float brightness, float drive)
    {
        if (std::abs(brightness - brightness_) < 0.01f)
            return;
        brightness_ = brightness;
        setDescription(drive >= 1.0f ? "Clipping" : juce::String(juce::roundToInt(drive * 100.0f)) + "%");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto& off = part("jewel-off.png");
        const auto& lit = part("jewel-lit.png");
        if (off.isValid() && lit.isValid())
        {
            hardware::drawScaled(g, off, area);
            hardware::drawScaled(g, lit, area, brightness_);
            return;
        }
        // Without its images: a chrome bezel round a red lens, its glow its opacity.
        const auto& hw = theme_.hardware;
        const auto lens = juce::Rectangle<float>(28.0f, 28.0f).withCentre(area.getCentre());
        g.setColour(hw.aluminium);
        g.fillEllipse(lens.expanded(5.0f));
        g.setColour(hw.knobBody);
        g.fillEllipse(lens);
        g.setColour(hw.coronaJewel.withAlpha(brightness_));
        g.fillEllipse(lens);
        g.setColour(hw.coronaJewel.withAlpha(0.3f * brightness_));
        g.fillEllipse(lens.expanded(8.0f));
    }

private:
    const Theme& theme_;
    float brightness_ = 0.0f;
};

// ----- the cabinet's mics --------------------------------------------------------------------

class Baffle;

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

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        const auto speakers = ef::speakers(model == pc::custom ? pc::open2x12 : model);
        const bool dim = !cabinetOn || model == pc::custom;
        const char* file = model == pc::closed4x10 || model == pc::bass4x10 ? "speaker-10.png"
                           : model == pc::bass1x15                          ? "speaker-15.png"
                                                                            : "speaker-12.png";
        for (const auto& s : speakers)
        {
            const auto box = juce::Rectangle<float>(s.diameter, s.diameter).withCentre(local({s.x, s.y}));
            const auto& image = part(file);
            if (image.isValid())
                hardware::drawScaled(g, image, box, dim ? 0.45f : 1.0f);
            else
            {
                // Without its image: the rim, the cone and the dust cap.
                g.setColour(hw.coronaSpeakerRim.withMultipliedAlpha(dim ? 0.45f : 1.0f));
                g.fillEllipse(box);
                g.setColour(hw.coronaCone.withMultipliedAlpha(dim ? 0.45f : 1.0f));
                g.fillEllipse(box.reduced(s.diameter * 0.06f));
                g.setColour(hw.shadow.withAlpha(0.35f));
                for (const float ring : {0.3f, 0.45f, 0.6f})
                    g.drawEllipse(box.reduced(s.diameter * ring * 0.5f), 1.0f);
                g.setColour(hw.knobBody.withMultipliedAlpha(dim ? 0.45f : 1.0f));
                g.fillEllipse(juce::Rectangle<float>(s.diameter * 0.22f, s.diameter * 0.22f)
                                  .withCentre(box.getCentre()));
            }
        }
        if (message.isNotEmpty())
        {
            const auto area = getLocalBounds().toFloat().reduced(60.0f, 0.0f);
            const auto band = juce::Rectangle<float>(area.getWidth(), 64.0f).withCentre(area.getCentre());
            g.setColour(hw.coronaBaffle.withAlpha(0.88f));
            g.fillRoundedRectangle(band, 6.0f);
            g.setColour(hw.coronaStripInk);
            g.setFont(legendFont(theme_, 20.0f, 700, 0.1f));
            text(g, message, band);
            return;
        }
        if (speakers.empty())
            return;
        // The mics: B first, so A (the main mic) is on top where they cross.
        for (int m = 1; m >= 0; --m)
            paintMic(g, speakers.front(), m);
    }

    // Where a mic's capsule is, on this layer.
    juce::Rectangle<float> capsule(int m) const
    {
        const auto speakers = ef::speakers(model == pc::custom ? pc::open2x12 : model);
        if (speakers.empty())
            return {};
        const auto& mic = mics[static_cast<size_t>(m)];
        const auto c = local(ef::micPoint(speakers.front(), m, mic.position));
        const float size = ef::capsuleSize[static_cast<size_t>(std::clamp(mic.type, 0, 2))] *
                           ef::capsuleScale(mic.distanceCm);
        return juce::Rectangle<float>(size, size).withCentre(c);
    }

private:
    // A mic's type as a small glyph: a dynamic's slim capsule on its clip, a
    // ribbon's slotted box, a condenser's big round head.
    static void typeGlyph(juce::Graphics& g, int type, juce::Rectangle<float> box, float stroke)
    {
        const juce::PathStrokeType line(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path p;
        if (type == pc::ribbon)
        {
            const auto body = box.withSizeKeepingCentre(box.getWidth() * 0.62f, box.getHeight() * 0.86f);
            p.addRoundedRectangle(body, stroke * 1.5f);
            for (int i = 1; i <= 4; ++i)
            {
                const float y = body.getY() + body.getHeight() * static_cast<float>(i) / 5.0f;
                p.startNewSubPath(body.getX() + stroke * 1.6f, y);
                p.lineTo(body.getRight() - stroke * 1.6f, y);
            }
        }
        else if (type == pc::condenser)
        {
            const auto head = box.withSizeKeepingCentre(box.getWidth() * 0.86f, box.getHeight() * 0.86f);
            p.addEllipse(head);
            p.startNewSubPath(head.getX() + head.getWidth() * 0.14f, head.getCentreY());
            p.lineTo(head.getRight() - head.getWidth() * 0.14f, head.getCentreY());
            p.startNewSubPath(head.getCentreX(), head.getY() + head.getHeight() * 0.14f);
            p.lineTo(head.getCentreX(), head.getBottom() - head.getHeight() * 0.14f);
        }
        else
        {
            const auto head = juce::Rectangle<float>(box.getWidth() * 0.44f, box.getHeight() * 0.56f)
                                  .withCentre({box.getCentreX(), box.getY() + box.getHeight() * 0.3f});
            p.addRoundedRectangle(head, head.getWidth() * 0.5f);
            p.startNewSubPath(head.getX() + stroke, head.getY() + head.getHeight() * 0.38f);
            p.lineTo(head.getRight() - stroke, head.getY() + head.getHeight() * 0.38f);
            // The clip round its foot, and the stem.
            p.addCentredArc(box.getCentreX(),
                            head.getBottom() - head.getHeight() * 0.25f,
                            head.getWidth() * 0.85f,
                            head.getWidth() * 0.85f,
                            0.0f,
                            pi * 0.5f,
                            pi * 1.5f,
                            true);
            p.startNewSubPath(box.getCentreX(), head.getBottom() + head.getWidth() * 0.6f);
            p.lineTo(box.getCentreX(), box.getBottom());
        }
        g.strokePath(p, line);
    }

    // A mic's marker over the cone: a ring in the mic's colour, as large as the
    // mic would look from the speaker (smaller the further it is) and squeezed by
    // its angle off axis, its type's glyph inside and its label beside it. A mic
    // that is off is a faint dashed ring.
    void paintMic(juce::Graphics& g, const ef::Speaker& speaker, int m) const
    {
        juce::ignoreUnused(speaker);
        const auto& hw = theme_.hardware;
        const auto& mic = mics[static_cast<size_t>(m)];
        const auto colour = m == 0 ? hw.coronaMarkerA : hw.coronaMarkerB;
        const float alpha = mic.on ? 1.0f : 0.35f;
        const auto box = capsule(m);
        const auto c = box.getCentre();
        const float squeeze = std::cos(mic.angleDeg * pi / 180.0f);
        const auto ring = box.withSizeKeepingCentre(box.getWidth() * squeeze, box.getHeight());

        // A dark halo keeps the ring clear over the cone's light and dark rings.
        g.setColour(hw.shadow.withAlpha(0.45f * alpha));
        g.drawEllipse(ring, 4.5f);
        g.setColour(colour.withAlpha(0.16f * alpha));
        g.fillEllipse(ring);
        g.setColour(colour.withAlpha(alpha));
        if (mic.on)
            g.drawEllipse(ring, 2.0f);
        else
        {
            juce::Path outline;
            outline.addEllipse(ring);
            juce::Path dashed;
            const float dashes[] = {5.0f, 4.0f};
            juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.fillPath(dashed);
        }
        const float glyph = std::clamp(box.getHeight() * 0.48f, 14.0f, 24.0f);
        typeGlyph(
            g, mic.type, juce::Rectangle<float>(glyph, glyph).withCentre(c), glyph > 18.0f ? 1.8f : 1.4f);

        // Its label: the mic's letter and distance, on the side away from the cap.
        const auto* face = dynamic_cast<const RenderedFace*>(getParentComponent());
        const bool compact = face != nullptr && face->isCompact();
        const float labelW = compact ? 122.0f : 86.0f, labelH = compact ? 30.0f : 22.0f;
        const auto label =
            juce::Rectangle<float>(labelW, labelH)
                .withCentre({c.x + box.getWidth() * 0.5f + labelW * 0.5f + 7.0f,
                             c.y + (m == 0 ? -box.getHeight() * 0.35f : box.getHeight() * 0.35f)});
        g.setColour(hw.coronaBaffle.withAlpha(0.85f * alpha));
        g.fillRoundedRectangle(label, 4.0f);
        g.setColour(colour.withAlpha(alpha));
        g.fillEllipse(
            juce::Rectangle<float>(7.0f, 7.0f).withCentre({label.getX() + 10.0f, label.getCentreY()}));
        g.setColour(hw.coronaStripInk.withAlpha(alpha));
        g.setFont(legendFont(theme_, compact ? 15.0f * RenderedFace::compactNameScale : 15.0f, 700, 0.04f));
        const auto cm = mic.distanceCm < 10.0f ? juce::String(mic.distanceCm, 1)
                                               : juce::String(juce::roundToInt(mic.distanceCm));
        text(g,
             juce::String(m == 0 ? "A" : "B") + juce::String::fromUTF8(" \xc2\xb7 ") + cm + " cm",
             label.withTrimmedLeft(14.0f));
    }

    const Theme& theme_;
};

// ----- the face ----------------------------------------------------------------------------

enum class Page
{
    amp,
    cabinet
};

// The two pages at their drawn size; the card scales the face to fit. What
// does not move is drawn once per page, scale and cabinet into an image.
class Face final : public RenderedFace
{
public:
    explicit Face(const Theme& theme) : RenderedFace(ef::width, ef::height), theme_(theme) {}

    Page page = Page::amp;
    int model = pc::open2x12;
    bool cabinetOn = true;
    int qualityIndex = 1;
    // Values drawn over the static layer.
    std::function<juce::String(int param)> value;
    juce::String fileName, fileError;
    int litInput = 0;
    bool gateShut = false;

private:
    // Compact (RenderedFace): names larger and tighter, and the control in hand
    // shows its value in place of its name.
    float nameSize(float size) const noexcept { return isCompact() ? size * compactNameScale : size; }
    float nameTracking(float tracking) const noexcept { return isCompact() ? tracking * 0.4f : tracking; }
    // A compact name's box: under its control with a clear gap (its skirt's
    // bottom plus `gap`), its text hung from the top.
    static juce::Rectangle<float> under(juce::Point<float> c, float skirt, float width, float gap = 22.0f)
    {
        return {c.x - width * 0.5f, c.y + skirt * 0.5f + gap, width, 32.0f};
    }
    juce::String nameFor(int param, const char* name) const
    {
        return isCompact() && activeControl() == param && value ? value(param) : juce::String(name);
    }

    // The art depends on the page and, on the cabinet page, the cabinet.
    int cacheKey() const override { return page == Page::amp ? -1 : model * 2 + (cabinetOn ? 1 : 0); }

    // ----- the static layer --------------------------------------------------------------------
    void paintStatic(juce::Graphics& g) const override
    {
        const auto& hw = theme_.hardware;
        if (artPass())
        {
            g.setColour(hw.coronaTolex);
            g.fillAll();
            hardware::drawScaled(g, part("tolex.jpg"), getLocalBounds().toFloat());
        }
        if (page == Page::amp)
            paintAmp(g);
        else
            paintCabinet(g);
        paintStrip(g);
    }

    // A knob's printed scale: a tick at each of 1..10 over 270 degrees, the
    // numbers round it (or none, for a small knob).
    void scale(juce::Graphics& g,
               juce::Point<float> c,
               float inner,
               float outer,
               int ticks,
               juce::Colour ink,
               const juce::StringArray& numbers,
               float numberRadius,
               float numberSize) const
    {
        g.setColour(ink);
        if (artPass())
            hardware::drawTicks(g, c, inner, outer, ticks, 1.6f);
        if (numbers.isEmpty() || isCompact())
            return;
        g.setFont(legendFont(theme_, numberSize, 600, 0.0f));
        for (int i = 0; i < numbers.size(); ++i)
        {
            const float a = -135.0f + 270.0f * static_cast<float>(i) / static_cast<float>(numbers.size() - 1);
            print(g, numbers[i], juce::Rectangle<float>(26.0f, 16.0f).withCentre(polar(c, numberRadius, a)));
        }
    }

    void knobBody(juce::Graphics& g, juce::Point<float> c, float image, float skirt) const
    {
        if (!artPass())
            return;
        const auto& body = part("knob-body.png");
        if (body.isValid())
        {
            hardware::drawScaled(
                g, body, juce::Rectangle<float>(c.x - image * 0.5f, c.y - image * 0.5f, image, image * 1.5f));
            return;
        }
        // Without its image: a black body with a chrome skirt.
        const auto& hw = theme_.hardware;
        g.setColour(hw.aluminium);
        g.fillEllipse(juce::Rectangle<float>(skirt, skirt).withCentre(c));
        g.setColour(hw.knobBody);
        g.fillEllipse(juce::Rectangle<float>(skirt * 0.8f, skirt * 0.8f).withCentre(c));
    }

    void paintAmp(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        if (artPass())
            plate(g, "panel.png", ampPlate, hw.coronaPanel);
        // The group captions, each over a hairline spanning its controls.
        for (const auto& cap : ampCaptions)
        {
            const float from = std::max(ampPlate.getX() + 16.0f, cap.from - 40.0f), to = cap.to + 40.0f;
            g.setColour(hw.coronaInkSoft);
            g.setFont(legendFont(theme_, nameSize(14.0f), 700, nameTracking(0.22f)));
            const auto area = juce::Rectangle<float>(from, 42.0f, to - from, 18.0f);
            print(g, cap.text, area);
            const float textWidth =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), cap.text)) +
                16.0f;
            if (artPass())
            {
                g.fillRect(juce::Rectangle<float>(from, 51.0f, (to - from - textWidth) * 0.5f, 1.0f));
                g.fillRect(juce::Rectangle<float>(
                    area.getCentreX() + textWidth * 0.5f, 51.0f, (to - from - textWidth) * 0.5f, 1.0f));
            }
        }
        const juce::StringArray numbers{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
        for (size_t i = 0; i < ampKnobX.size(); ++i)
        {
            const juce::Point<float> c(ampKnobX[i], knobRow);
            scale(g, c, 34.0f, 39.0f, 10, hw.coronaInkSoft, numbers, 47.0f, 12.0f);
            knobBody(g, c, bigImage, bigSkirt);
            g.setColour(hw.coronaInk);
            g.setFont(legendFont(theme_, nameSize(17.0f), 700, nameTracking(0.08f)));
            if (isCompact())
                print(g,
                      nameFor(ampKnobParam[i], ampKnobName[i]),
                      under(c, bigSkirt, 104.0f),
                      juce::Justification::centredTop);
            else
                print(g,
                      nameFor(ampKnobParam[i], ampKnobName[i]),
                      juce::Rectangle<float>(c.x - 52.0f, nameRow - 13.0f, 104.0f, 26.0f));
        }
        // The levers' names and positions.
        const auto lever = [&](float x, const char* name)
        {
            g.setColour(hw.coronaInk);
            g.setFont(legendFont(theme_, nameSize(17.0f), 700, nameTracking(0.08f)));
            if (isCompact())
            {
                // On the knobs' line, so the row reads as one.
                print(g, name, under({x, knobRow}, bigSkirt, 104.0f), juce::Justification::centredTop);
                return; // the lever's position shows its setting
            }
            print(g, name, juce::Rectangle<float>(x - 52.0f, nameRow - 13.0f, 104.0f, 26.0f));
            g.setColour(hw.coronaInkSoft);
            g.setFont(legendFont(theme_, 14.0f, 600, 0.06f));
            print(g,
                  "ON",
                  juce::Rectangle<float>(x + 20.0f, knobRow - 40.0f, 40.0f, 16.0f),
                  juce::Justification::centredLeft);
            print(g,
                  "OFF",
                  juce::Rectangle<float>(x + 20.0f, knobRow + 24.0f, 40.0f, 16.0f),
                  juce::Justification::centredLeft);
        };
        lever(brightX, "BRIGHT");
        lever(boostX, "BOOST");
        g.setColour(hw.coronaInk);
        g.setFont(legendFont(theme_, nameSize(17.0f), 700, nameTracking(0.08f)));
        if (isCompact())
            print(g,
                  "DRIVE",
                  under({jewelCentre.x, knobRow}, bigSkirt, 104.0f),
                  juce::Justification::centredTop);
        else
            print(g, "DRIVE", juce::Rectangle<float>(jewelCentre.x - 52.0f, nameRow - 13.0f, 104.0f, 26.0f));
        // The model line along the plate's foot, right.
        g.setColour(hw.coronaInkSoft);
        g.setFont(legendFont(theme_, 13.0f, 600, 0.22f));
        if (!isCompact())
            print(g,
                  juce::String::fromUTF8("MODEL EA-212 \xc2\xb7 TWO-TWELVE REVERB \xc2\xb7 NORTHLIGHT"),
                  juce::Rectangle<float>(ampPlate.getX() + 24.0f,
                                         ampPlate.getBottom() - 30.0f,
                                         ampPlate.getWidth() - 48.0f,
                                         18.0f),
                  juce::Justification::centredRight);

        // The grille and its badge.
        if (artPass())
            plate(g, "grille.png", grilleArea, hw.coronaGrille);
        if (const auto& badge = part("badge.png"); badge.isValid() && artPass())
            hardware::drawScaled(g, badge, badgeArea);
        else if (artPass())
        {
            g.setColour(hw.coronaBadge);
            g.fillRoundedRectangle(badgeArea.reduced(6.0f), badgeArea.getHeight() * 0.4f);
        }
        g.setColour(hw.coronaInk);
        g.setFont(legendFont(theme_, 46.0f, 700, 0.24f));
        print(g, "CORONA", badgeBrass, juce::Justification::centred, true);
    }

    void paintCabinet(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        if (artPass())
            plate(g, "baffle.png", baffleArea, hw.coronaBaffle);
        if (artPass())
            plate(g, "cab-panel.png", cabPlate, hw.coronaPanel);
        // CABINET's six positions are the live layer's (the chosen one bold).
        scale(g, cabinetKnob, 30.0f, 34.0f, 6, hw.coronaInkSoft, {}, 0.0f, 0.0f);
        knobBody(g, cabinetKnob, cabImage, cabSkirt);
        const juce::Point<float> room(roomX, cabinetKnob.y);
        scale(g,
              room,
              30.0f,
              34.0f,
              11,
              hw.coronaInkSoft,
              {"0", "", "", "", "", "", "", "", "", "", "100"},
              42.0f,
              12.0f);
        knobBody(g, room, cabImage, cabSkirt);
        g.setColour(hw.coronaInk);
        g.setFont(legendFont(theme_, nameSize(17.0f), 700, nameTracking(0.08f)));
        // Compact, the selector's positions are not printed: its name is its setting.
        print(g,
              isCompact() ? juce::String::fromUTF8(
                                cabinetLegend[static_cast<size_t>(std::clamp(model, 0, pc::modelCount - 1))])
                          : juce::String("CABINET"),
              isCompact() ? under(cabinetKnob, cabSkirt, 120.0f)
                          : juce::Rectangle<float>(cabinetKnob.x - 60.0f, cabNameRow - 13.0f, 120.0f, 26.0f),
              isCompact() ? juce::Justification::centredTop : juce::Justification::centred);
        print(g,
              "SPEAKER",
              isCompact() ? under({speakerX, cabinetKnob.y}, cabSkirt, 120.0f)
                          : juce::Rectangle<float>(speakerX - 60.0f, cabNameRow - 13.0f, 120.0f, 26.0f),
              isCompact() ? juce::Justification::centredTop : juce::Justification::centred);
        print(g,
              nameFor(pa::room, "ROOM"),
              isCompact() ? under({roomX, cabinetKnob.y}, cabSkirt, 120.0f)
                          : juce::Rectangle<float>(roomX - 60.0f, cabNameRow - 13.0f, 120.0f, 26.0f),
              isCompact() ? juce::Justification::centredTop : juce::Justification::centred);
        g.setColour(hw.coronaInkSoft);
        g.setFont(legendFont(theme_, 14.0f, 600, 0.06f));
        if (!isCompact()) // compact, the lever's position shows its setting
        {
            print(g,
                  "ON",
                  juce::Rectangle<float>(speakerX + 20.0f, cabinetKnob.y - 40.0f, 46.0f, 16.0f),
                  juce::Justification::centredLeft);
            print(g,
                  "LINE",
                  juce::Rectangle<float>(speakerX + 20.0f, cabinetKnob.y + 24.0f, 46.0f, 16.0f),
                  juce::Justification::centredLeft);
        }
        if (artPass())
        {
            g.fillRect(
                juce::Rectangle<float>(cabPlate.getX() + 16.0f, ruleY, cabPlate.getWidth() - 32.0f, 1.0f));
            g.fillRect(juce::Rectangle<float>(
                micColumnX[1] - 8.0f, ruleY + 10.0f, 1.0f, cabPlate.getBottom() - ruleY - 26.0f));
        }
        for (int m = 0; m < 2; ++m)
        {
            const float x0 = micColumnX[static_cast<size_t>(m)];
            g.setColour(hw.coronaInk);
            g.setFont(legendFont(theme_, nameSize(18.0f), 700, nameTracking(0.12f)));
            print(g,
                  m == 0 ? "MIC A" : "MIC B",
                  juce::Rectangle<float>(x0, micTitleRow - 2.0f, 88.0f, 28.0f),
                  juce::Justification::centredLeft);
            for (int k = 0; k < 4; ++k)
            {
                const juce::Point<float> c(micKnobX(m, k), micKnobRow[static_cast<size_t>(k / 2)]);
                scale(g, c, 23.0f, 27.0f, 11, hw.coronaInkSoft, {}, 0.0f, 0.0f);
                knobBody(g, c, micImage, micSkirt);
                g.setColour(hw.coronaInk);
                g.setFont(legendFont(theme_, nameSize(15.0f), 700, nameTracking(0.08f)));
                const int param = pa::micParam(m, micKnobField[static_cast<size_t>(k)]);
                if (isCompact())
                    print(g,
                          nameFor(param, micKnobName[static_cast<size_t>(k)]),
                          under(c, micSkirt, 96.0f, 9.0f),
                          juce::Justification::centredTop);
                else
                    print(g,
                          nameFor(param, micKnobName[static_cast<size_t>(k)]),
                          juce::Rectangle<float>(c.x - 48.0f, c.y + 26.0f, 96.0f, 18.0f));
            }
        }
    }

    void paintStrip(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        if (artPass())
            plate(g, "strip.png", stripArea, hw.coronaStrip);
        for (size_t i = 0; i < stripKnobX.size(); ++i)
        {
            const juce::Point<float> c(stripKnobX[i], stripKnobRow);
            scale(g, c, 21.0f, 25.0f, 11, hw.coronaStripInkSoft, {}, 0.0f, 0.0f);
            knobBody(g, c, smallImage, smallSkirt);
            g.setColour(hw.coronaStripInk);
            g.setFont(legendFont(theme_, nameSize(14.0f), 700, nameTracking(0.1f)));
            if (isCompact())
                print(g,
                      nameFor(stripKnobParam[i], stripKnobName[i]),
                      under(c, smallSkirt, 88.0f, 8.0f),
                      juce::Justification::centredTop);
            else
                print(g,
                      nameFor(stripKnobParam[i], stripKnobName[i]),
                      juce::Rectangle<float>(c.x - 44.0f, stripNameRow - 8.0f, 88.0f, 16.0f));
        }
        g.setColour(hw.coronaStripInk);
        g.setFont(legendFont(theme_, nameSize(14.0f), 700, nameTracking(0.1f)));
        // Compact, every name on the strip hangs from one line: the knobs' names'.
        const float compactTop = stripKnobRow + smallSkirt * 0.5f + 8.0f;
        const auto strip = [&](const char* name, float x, float width, float row)
        {
            if (isCompact())
                print(g,
                      name,
                      juce::Rectangle<float>(x, compactTop, width, 32.0f),
                      juce::Justification::centredTop);
            else
                print(g, name, juce::Rectangle<float>(x, row, width, 16.0f));
        };
        strip("LEVEL", inputMeterArea.getX(), inputMeterArea.getWidth(), stripNameRow - 8.0f);
        strip("QUALITY", qualityKeys.getX(), qualityKeys.getWidth(), stripNameRow - 8.0f);
        strip("REVERB", footswitchX[0] - 40.0f, 80.0f, stripNameRow + 1.0f);
        strip("TREMOLO", footswitchX[1] - 40.0f, 80.0f, stripNameRow + 1.0f);
        // A hairline between the amp's own controls and the footswitch and pages.
        if (artPass())
        {
            g.setColour(hw.coronaStripInkSoft.withAlpha(0.5f));
            g.fillRect(juce::Rectangle<float>(footswitchArea.getX() - 18.0f, 512.0f, 1.0f, 68.0f));
            g.fillRect(juce::Rectangle<float>(pageKeys.getX() - 22.0f, 512.0f, 1.0f, 68.0f));
        }
    }

    // ----- what changes with the values -------------------------------------------------------
    void paintLive(juce::Graphics& g) const override
    {
        const auto& hw = theme_.hardware;
        if (page == Page::cabinet && value)
            paintCabinetLive(g); // the response file's name, in compact too
        if (value && !isCompact())
        {
            if (page == Page::amp)
            {
                g.setColour(hw.coronaInkSoft);
                g.setFont(legendFont(theme_, 15.0f, 600, 0.03f));
                for (size_t i = 0; i < ampKnobX.size(); ++i)
                    text(g,
                         value(ampKnobParam[i]),
                         juce::Rectangle<float>(ampKnobX[i] - 52.0f, valueRow - 10.0f, 104.0f, 20.0f));
            }
            g.setColour(hw.coronaStripInkSoft);
            g.setFont(legendFont(theme_, 13.0f, 600, 0.03f));
            for (size_t i = 0; i < stripKnobX.size(); ++i)
                text(g,
                     value(stripKnobParam[i]),
                     juce::Rectangle<float>(stripKnobX[i] - 44.0f, stripValueRow - 8.0f, 88.0f, 16.0f));
        }
        // The input meter's ladder.
        const float w = (inputMeterArea.getWidth() - 3.0f * 9.0f) / 10.0f;
        for (int i = 0; i < 10; ++i)
        {
            const auto seg =
                juce::Rectangle<float>(inputMeterArea.getX() + static_cast<float>(i) * (w + 3.0f),
                                       inputMeterArea.getY(),
                                       w,
                                       inputMeterArea.getHeight());
            const bool lit = i < litInput;
            g.setColour(!lit ? hw.ledOff : i >= ef::inputWarnFrom ? hw.ledHot : hw.led);
            g.fillRoundedRectangle(seg, 1.5f);
        }
        // The gate's LED: lit while it holds the signal back.
        const auto led = juce::Rectangle<float>(6.0f, 6.0f).withCentre(gateLedCentre);
        g.setColour(gateShut ? hw.led : hw.ledOff);
        g.fillEllipse(led);
        if (gateShut)
        {
            g.setColour(hw.led.withAlpha(0.3f));
            g.fillEllipse(led.expanded(3.0f));
        }
    }

    void paintCabinetLive(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // CABINET's positions round its knob, the chosen one bold (compact: its
        // name shows the setting instead).
        for (int i = 0; i < pc::modelCount && !isCompact(); ++i)
        {
            const float a = -135.0f + 270.0f * static_cast<float>(i) / static_cast<float>(pc::modelCount - 1);
            g.setColour(i == model ? hw.coronaInk : hw.coronaInkSoft);
            g.setFont(legendFont(theme_, 13.0f, i == model ? 700 : 600, 0.0f));
            text(g,
                 juce::String::fromUTF8(cabinetLegend[static_cast<size_t>(i)]),
                 juce::Rectangle<float>(44.0f, 16.0f).withCentre(polar(cabinetKnob, 47.0f, a)));
        }
        g.setColour(hw.coronaInkSoft);
        g.setFont(legendFont(theme_, 15.0f, 600, 0.03f));
        if (!isCompact())
            text(g,
                 value(pa::room),
                 juce::Rectangle<float>(roomX - 52.0f, cabValueRow - 10.0f, 104.0f, 20.0f));
        // The response file.
        g.setFont(legendFont(theme_, nameSize(14.0f), 600, 0.02f));
        g.setColour(fileError.isNotEmpty() ? hw.coronaInk : hw.coronaInkSoft);
        const auto shown = fileError.isNotEmpty()  ? fileName + " (" + fileError + ")"
                           : fileName.isNotEmpty() ? fileName
                                                   : juce::String("No response file loaded");
        text(g, shown, fileText, juce::Justification::centredLeft);
        g.setFont(legendFont(theme_, 13.0f, 600, 0.03f));
        for (int m = 0; m < 2 && !isCompact(); ++m)
            for (int k = 0; k < 4; ++k)
            {
                const juce::Point<float> c(micKnobX(m, k), micKnobRow[static_cast<size_t>(k / 2)]);
                text(g,
                     value(pa::micParam(m, micKnobField[static_cast<size_t>(k)])),
                     juce::Rectangle<float>(c.x - 48.0f, c.y + 42.0f, 96.0f, 16.0f));
            }
    }

    const Theme& theme_;
};

// ----- the card ----------------------------------------------------------------------------

// The page each Corona shows, by insert, while the app runs (a card made again,
// in the Editor panel or a window, opens where its amp was left); and the page a
// new one starts on.
std::map<core::Id, Page>& lastPages()
{
    static std::map<core::Id, Page> pages;
    return pages;
}
Page startPage = Page::amp;

class CoronaCard final : public RenderedCard
{
public:
    CoronaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme), jewel_(theme), baffle_(theme)
    {
        addAndMakeVisible(face_);
        face_.value = [this](int index) { return valueText(index); };

        // The amp page.
        for (size_t i = 0; i < ampKnobX.size(); ++i)
            ampControls_.push_back(&bindKnob(ampKnobParam[i], {ampKnobX[i], knobRow}, bigSkirt, bigImage));
        bright_ = makeLever("Bright", {"On", "Off"}, brightX, knobRow, ampControls_);
        bright_->onChoose = [this](int p) { leverParam(pa::bright, "Bright", p == 0 ? 1.0f : 0.0f); };
        boost_ = makeLever("Boost", {"On", "Off"}, boostX, knobRow, ampControls_);
        boost_->onChoose = [this](int p) { leverParam(pa::boost, "Boost", p == 0 ? 1.0f : 0.0f); };
        const auto jewelArea = juce::Rectangle<float>(65.0f, 75.0f).withCentre(jewelCentre);
        jewel_.setBounds(jewelArea.toNearestInt());
        face_.addAndMakeVisible(jewel_);
        ampControls_.push_back(&jewel_);

        // The cabinet page.
        face_.addAndMakeVisible(baffle_);
        baffle_.setBounds(baffleArea.toNearestInt());
        cabControls_.push_back(&baffle_);
        cabinet_ = &bindKnob(pa::cabinet, cabinetKnob, cabSkirt, cabImage);
        cabinet_->setInterval(1.0);
        cabinet_->format = [](double v)
        {
            return juce::String(
                pc::modelName(juce::jlimit(0, pc::modelCount - 1, static_cast<int>(std::lround(v)))));
        };
        cabControls_.push_back(cabinet_);
        cabControls_.push_back(&bindKnob(pa::room, {roomX, cabinetKnob.y}, cabSkirt, cabImage));
        speaker_ = makeLever("Speaker", {"On", "Line out"}, speakerX, cabinetKnob.y, cabControls_);
        speaker_->onChoose = [this](int p) { leverParam(pa::cabinetOn, "Cabinet", p == 0 ? 1.0f : 0.0f); };
        load_ = makeKey("LOAD IR", "Load a response file", loadKey, cabControls_, false);
        load_->onPress = [this] { chooseFile(); };
        for (int m = 0; m < 2; ++m)
        {
            auto& mic = mics_[static_cast<size_t>(m)];
            const float x0 = micColumnX[static_cast<size_t>(m)];
            const juce::String name = m == 0 ? "Mic A" : "Mic B";
            mic.on = makeKey("ON", name + " on", {x0 + 92.0f, micTitleRow, 44.0f, 24.0f}, cabControls_);
            mic.on->onPress = [this, m]
            { toggleParam(pa::micParam(m, pa::micOn), m == 0 ? "Mic A on" : "Mic B on"); };
            mic.phase = makeKey(juce::String::fromUTF8("\xc3\x98"),
                                name + " polarity",
                                {x0 + 142.0f, micTitleRow, 44.0f, 24.0f},
                                cabControls_);
            mic.phase->onPress = [this, m]
            { toggleParam(pa::micParam(m, pa::micPolarity), m == 0 ? "Mic A polarity" : "Mic B polarity"); };
            for (int t = 0; t < pc::micTypeCount; ++t)
            {
                const char* labels[] = {"DYN", "RIB", "CON"};
                auto* key = makeKey(labels[t],
                                    name + ": " + pc::micTypeName(t),
                                    {x0 + static_cast<float>(t) * 62.0f, micTypeRow, 60.0f, keyHeight},
                                    cabControls_);
                key->onPress = [this, m, t]
                {
                    leverParam(pa::micParam(m, pa::micType),
                               m == 0 ? "Mic A type" : "Mic B type",
                               static_cast<float>(t));
                };
                mic.type[static_cast<size_t>(t)] = key;
            }
            for (int k = 0; k < 4; ++k)
            {
                auto& knob = bindKnob(pa::micParam(m, micKnobField[static_cast<size_t>(k)]),
                                      {micKnobX(m, k), micKnobRow[static_cast<size_t>(k / 2)]},
                                      micSkirt,
                                      micImage);
                mic.knobs[static_cast<size_t>(k)] = &knob;
                cabControls_.push_back(&knob);
            }
            auto handle = std::make_unique<MicHandle>(theme_, m);
            handle->positionAt = [this, m](juce::Point<float> p)
            {
                const auto speakers = ef::speakers(model());
                return speakers.empty() ? 0.0f : ef::positionAt(speakers.front(), m, {p.x, p.y});
            };
            handle->onPosition = [this, m](float position)
            {
                setParam(pa::micParam(m, pa::micPosition), position * 100.0f);
                paramsChanged();
            };
            handle->onDistance = [this, m](float cm)
            {
                beginGesture(m == 0 ? "Mic A distance" : "Mic B distance");
                setParam(pa::micParam(m, pa::micDistance), cm);
                endGesture();
                paramsChanged();
            };
            handle->onReset = [this, m]
            {
                const auto& i = info(pa::micParam(m, pa::micPosition));
                leverParam(pa::micParam(m, pa::micPosition),
                           m == 0 ? "Mic A position" : "Mic B position",
                           i.defaultValue);
            };
            handle->onGestureBegin = [this, m]
            { beginGesture(m == 0 ? "Mic A position" : "Mic B position"); };
            handle->onGestureEnd = [this] { endGesture(); };
            face_.addAndMakeVisible(*handle);
            cabControls_.push_back(handle.get());
            mic.handle = std::move(handle);
        }

        // The strip, on both pages.
        for (size_t i = 0; i < stripKnobX.size(); ++i)
            bindKnob(stripKnobParam[i], {stripKnobX[i], stripKnobRow}, smallSkirt, smallImage);
        const char* qualities[] = {"2\xc3\x97", "4\xc3\x97", "8\xc3\x97"};
        for (int q = 0; q < 3; ++q)
        {
            const float w = (qualityKeys.getWidth() - 8.0f) / 3.0f;
            std::vector<juce::Component*> none;
            auto* key = makeKey(
                juce::String::fromUTF8(qualities[q]),
                "Quality: " + juce::String(pa::qualityFactors[q]) + "x oversampling",
                {qualityKeys.getX() + static_cast<float>(q) * (w + 4.0f), qualityKeys.getY(), w, keyHeight},
                none);
            key->onPress = [this, q] { leverParam(pa::quality, "Quality", static_cast<float>(q)); };
            quality_[static_cast<size_t>(q)] = key;
        }
        for (int f = 0; f < 2; ++f)
        {
            auto sw = std::make_unique<Footswitch>(theme_, f == 0 ? "Reverb" : "Tremolo");
            sw->setBounds(
                Footswitch::boundsFor({footswitchX[static_cast<size_t>(f)], footswitchY}, footswitchSize)
                    .toNearestInt());
            sw->onPress = [this, f]
            { toggleParam(f == 0 ? pa::reverbOn : pa::tremoloOn, f == 0 ? "Reverb" : "Tremolo"); };
            face_.addAndMakeVisible(*sw);
            footswitch_[static_cast<size_t>(f)] = std::move(sw);
        }
        for (int p = 0; p < 2; ++p)
        {
            const float w = (pageKeys.getWidth() - 8.0f) / 2.0f;
            std::vector<juce::Component*> none;
            auto* key = makeKey(p == 0 ? "AMP" : "CABINET",
                                p == 0 ? "Show the amp" : "Show the cabinet",
                                {pageKeys.getX() + static_cast<float>(p) * (w + 8.0f),
                                 pageKeys.getY(),
                                 w,
                                 pageKeys.getHeight()},
                                none);
            key->icon = p == 0 ? drawAmpIcon : drawCabinetIcon;
            key->onPress = [this, p] { showPage(p == 0 ? Page::amp : Page::cabinet); };
            pages_[static_cast<size_t>(p)] = key;
        }
        const auto known = lastPages().find(moduleId_);
        showPage(known != lastPages().end() ? known->second : startPage);
        paramsChanged();
    }

    // The page showing (for the tests and the --insert-page launch flag).
    void showPage(Page page)
    {
        page_ = page;
        face_.page = page;
        lastPages()[moduleId_] = page;
        for (auto* c : ampControls_)
            c->setVisible(page == Page::amp);
        for (auto* c : cabControls_)
            c->setVisible(page == Page::cabinet);
        pages_[0]->setLit(page == Page::amp);
        pages_[1]->setLit(page == Page::cabinet);
        face_.invalidate();
    }

private:
    struct MicControls
    {
        HardwareKey* on = nullptr;
        HardwareKey* phase = nullptr;
        std::array<HardwareKey*, 3> type{};
        std::array<Knob*, 4> knobs{};
        std::unique_ptr<MicHandle> handle;
    };

    int model() const
    {
        const int m = juce::jlimit(0, pc::modelCount - 1, static_cast<int>(std::lround(param(pa::cabinet))));
        return m == pc::custom ? pc::open2x12 : m;
    }

    // A knob bound to its parameter, a rendered pointer over the body the face
    // draws, its own label hidden (the face prints names and values).
    Knob& bindKnob(int index, juce::Point<float> centre, float skirt, float image)
    {
        auto& k = bindRenderedKnob(index, centre, skirt, image, part("knob-pointer.png"));
        const auto& i = info(index);
        if (i.unit.empty() && i.choices.empty() && i.minimum == 1.0f && i.maximum == 10.0f)
            k.format = [](double v) { return juce::String(v, 1); };
        else if (i.unit == "cm")
            k.format = [](double v) { return fixedText(v, v < 10.0 ? 1 : 0) + " cm"; };
        else if (i.unit == "deg")
            k.format = [](double v)
            { return juce::String(juce::roundToInt(v)) + juce::String::fromUTF8("\xc2\xb0"); };
        else if (i.minimum == -100.0f && i.unit == "%") // a pan
            k.format = [](double v)
            {
                const int p = juce::roundToInt(v);
                return p == 0 ? juce::String("C") : (p < 0 ? "L" : "R") + juce::String(std::abs(p));
            };
        else if (index == pa::gate)
            k.format = [](double v)
            {
                return v <= pa::gateOffDb + 0.01 ? juce::String("Off")
                                                 : juce::String(juce::roundToInt(v)) + " dB";
            };
        knobsByParam_.emplace_back(index, &k);
        return k;
    }

    HardwareLever* makeLever(const juce::String& name,
                             std::vector<juce::String> positions,
                             float x,
                             float nutY,
                             std::vector<juce::Component*>& page)
    {
        auto& lever = addLever(name,
                               std::move(positions),
                               "corona",
                               {"toggle-up.png", "", "toggle-down.png"},
                               {x, nutY},
                               leverScale,
                               52.0f * leverScale);
        page.push_back(&lever);
        return &lever;
    }

    HardwareKey* makeKey(const juce::String& label,
                         const juce::String& title,
                         juce::Rectangle<float> area,
                         std::vector<juce::Component*>& page,
                         bool led = true)
    {
        auto key = std::make_unique<HardwareKey>(theme_, label, title, led);
        key->setBounds(area.toNearestInt());
        face_.addAndMakeVisible(*key);
        auto* raw = key.get();
        page.push_back(raw);
        keys_.push_back(std::move(key));
        return raw;
    }

    void leverParam(int index, const juce::String& name, float value)
    {
        beginGesture(name);
        setParam(index, value);
        endGesture();
        paramsChanged();
    }

    void toggleParam(int index, const juce::String& name)
    {
        leverParam(index, name, param(index) >= 0.5f ? 0.0f : 1.0f);
    }

    void chooseFile()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Load a cabinet response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (host_.onCommand)
                                      host_.onCommand(std::make_unique<core::SetInsertState>(
                                          channelId_,
                                          moduleId_,
                                          pa::encodeState(file.getFullPathName().toStdString()),
                                          "Load cabinet response"));
                                  leverParam(pa::cabinet, "Cabinet", static_cast<float>(pc::custom));
                              });
    }

    juce::String valueText(int index) const
    {
        for (const auto& b : knobsByParam_)
            if (b.first == index)
                return b.second->accessibleValueText();
        return {};
    }

    // A knob was turned: everything that shows its value follows.
    void controlMoved(Knob&) override { paramsChanged(); }

    void insertChanged(const core::Insert& insert) override
    {
        const auto path = juce::File(plugins::sampler::decodeState(insert.state));
        face_.fileName = insert.state.empty() ? juce::String() : path.getFileName();
        face_.repaint();
    }

    void paramsChanged() override
    {
        if (pages_[1] == nullptr)
            return; // still being built
        bright_->setPosition(param(pa::bright) >= 0.5f ? 0 : 1);
        boost_->setPosition(param(pa::boost) >= 0.5f ? 0 : 1);
        const bool cabinetOn = param(pa::cabinetOn) >= 0.5f;
        speaker_->setPosition(cabinetOn ? 0 : 1);
        const int chosen =
            juce::jlimit(0, pc::modelCount - 1, static_cast<int>(std::lround(param(pa::cabinet))));
        const bool custom = chosen == pc::custom;
        const int quality = juce::jlimit(0, 2, static_cast<int>(std::lround(param(pa::quality))));
        for (int q = 0; q < 3; ++q)
            quality_[static_cast<size_t>(q)]->setLit(q == quality);
        footswitch_[0]->set(param(pa::reverbOn) >= 0.5f, 1.0f);

        baffle_.model = chosen;
        baffle_.cabinetOn = cabinetOn;
        baffle_.message = !cabinetOn ? juce::String::fromUTF8("CABINET OFF \xc2\xb7 LINE OUT")
                          : custom   ? juce::String("USING THE LOADED RESPONSE")
                                     : juce::String();
        for (int m = 0; m < 2; ++m)
        {
            auto& mic = baffle_.mics[static_cast<size_t>(m)];
            mic.on = param(pa::micParam(m, pa::micOn)) >= 0.5f;
            mic.type = juce::jlimit(0, 2, static_cast<int>(std::lround(param(pa::micParam(m, pa::micType)))));
            mic.position = param(pa::micParam(m, pa::micPosition)) * 0.01f;
            mic.distanceCm = param(pa::micParam(m, pa::micDistance));
            mic.angleDeg = param(pa::micParam(m, pa::micAngle));
            auto& controls = mics_[static_cast<size_t>(m)];
            controls.on->setLit(mic.on);
            controls.phase->setLit(param(pa::micParam(m, pa::micPolarity)) >= 0.5f);
            for (int t = 0; t < 3; ++t)
                controls.type[static_cast<size_t>(t)]->setLit(t == mic.type);
            // A custom response or the line out leaves the mics with nothing to do.
            const bool usable = cabinetOn && !custom;
            controls.on->setEnabled(usable);
            controls.phase->setEnabled(usable && mic.on);
            for (auto* k : controls.type)
                k->setEnabled(usable && mic.on);
            for (auto* k : controls.knobs)
                k->setEnabled(usable && mic.on);
            auto& handle = *controls.handle;
            handle.position = mic.position;
            handle.distanceCm = mic.distanceCm;
            handle.setEnabled(usable && mic.on);
            handle.setVisible(page_ == Page::cabinet && usable);
            handle.setBounds(
                baffle_.capsule(m).translated(ef::baffle.x, ef::baffle.y).expanded(6.0f).toNearestInt());
            handle.describe(juce::String(juce::roundToInt(mic.position * 100.0f)) + "% out, " +
                            juce::String(mic.distanceCm, 1) + " cm");
        }
        cabinet_->setEnabled(cabinetOn);
        load_->setEnabled(cabinetOn);
        face_.model = chosen;
        face_.cabinetOn = cabinetOn;
        face_.qualityIndex = quality;
        if (const auto* live = liveProcessor())
            face_.fileError = juce::String(pa::userResponseError(*live));
        baffle_.repaint();
        repaintChanged();
    }

    // The levers, keys, footswitches, mic handles and the baffle draw their own
    // settings; the cabinet, its switch and the loaded file change the cabinet
    // page's art and print.
    bool printsWidely(int index) const override
    {
        if (index == pa::cabinet || index == pa::cabinetOn)
            return true;
        return false;
    }

    void framesArrived(double elapsedMs) override
    {
        juce::ignoreUnused(elapsedMs);
        if (pages_[1] == nullptr)
            return;
        const bool on = isOn();
        const Frame f = historySize() > 0 ? historyAt(historySize() - 1) : Frame{};
        jewel_.set(ef::jewelBrightness(f.extra, on), f.extra);
        const int lit = on ? ef::litSteps(inputDb()) : 0;
        const bool shut = on && f.reduction > 6.0f;
        footswitch_[1]->set(param(pa::tremoloOn) >= 0.5f, on ? 0.35f + 0.65f * f.extra2 : 0.35f);
        if (lit != face_.litInput || shut != face_.gateShut)
        {
            face_.litInput = lit;
            face_.gateShut = shut;
            face_.repaint(stripArea.toNearestInt());
        }
    }

    juce::String headerStatus() const override
    {
        if (param(pa::cabinetOn) < 0.5f)
            return "Line out";
        return {};
    }

    Face face_;
    Jewel jewel_;
    Baffle baffle_;
    Page page_ = Page::amp;
    std::vector<juce::Component*> ampControls_, cabControls_;
    std::vector<std::unique_ptr<HardwareKey>> keys_;
    std::vector<std::pair<int, Knob*>> knobsByParam_;
    HardwareLever* bright_ = nullptr;
    HardwareLever* boost_ = nullptr;
    HardwareLever* speaker_ = nullptr;
    Knob* cabinet_ = nullptr;
    HardwareKey* load_ = nullptr;
    std::array<HardwareKey*, 3> quality_{};
    std::array<HardwareKey*, 2> pages_{};
    std::array<std::unique_ptr<Footswitch>, 2> footswitch_;
    std::array<MicControls, 2> mics_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
} // namespace

std::unique_ptr<ModuleCard>
createEmberCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<CoronaCard>(theme, host, channelId, insert);
}

void setEmberStartPage(bool cabinet)
{
    startPage = cabinet ? Page::cabinet : Page::amp;
}

void showEmberPage(ModuleCard& card, bool cabinet)
{
    if (auto* corona = dynamic_cast<CoronaCard*>(&card))
        corona->showPage(cabinet ? Page::cabinet : Page::amp);
}

} // namespace northlight::ui::strip
