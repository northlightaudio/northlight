// The guitar pedals' cards (screens/plugins.md 7.2a): each a stompbox seen from
// above on a pedalboard, a hardware face (design-system.md "Hardware face") built
// from rendered parts (assets/pedals; "Rendered parts"), drawn at 360 x 600 and
// scaled to fit the card's body, the card narrowing with it.
//
// A pedal has what a pedal has: its knobs, at most one toggle, the lamp, the
// footswitch and its name. The footswitch switches the pedal on and off (the
// insert's bypass), as the header's power button does; the tuner's mutes its
// output instead, and the tuner has its display where the others have knobs.

#include "core/Command.h"
#include "core/NoteNames.h"
#include "plugins/Pedals.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/FaceParts.h"
#include "ui/strip/RenderedCard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace northlight::ui::strip
{

namespace pedals = plugins;

namespace
{
using hardware::legendFont;
using hardware::polar;

// ----- where things are on the face (pixels at 100%) ----------------------------------------

constexpr float faceWidth = 360.0f, faceHeight = 600.0f;
constexpr float centreX = faceWidth * 0.5f;
// A knob's image is this many times its flange (the flange is 1.6 of the render's 3.0).
constexpr float knobImageRatio = 3.0f / 1.6f;
// The pointer's image (and the knob's bounds) is this many times the flange.
constexpr float pointerImageRatio = 1.2f;
// The footswitch: Corona's chrome button, drawn this size round its centre.
const juce::Point<float> footswitchCentre{centreX, 506.0f};
constexpr float footswitchSize = 96.0f;
// The lamp: Corona's jewel at this width (its image is 130 x 150, the lens centred
// in its top 130 x 130).
const juce::Point<float> lampCentre{centreX, 50.0f};
constexpr float lampWidth = 34.0f;
// The name and what the pedal is, under the controls; the maker at the foot.
constexpr float nameRow = 404.0f, kindRow = 434.0f, makerRow = 553.0f;
// The toggles: Corona's bat lever at this share of its render.
constexpr float leverScale = 0.4f;
// The gap from a knob's flange to its name's capitals.
constexpr float nameGap = 13.0f;

juce::String tenths(double v)
{
    return fixedText(v, 1);
}

juce::String signedDb(double db)
{
    const int t = juce::roundToInt(db * 10.0);
    return (t < 0   ? juce::String::fromUTF8("\xe2\x88\x92")
            : t > 0 ? juce::String("+")
                    : juce::String()) +
           juce::String(std::abs(t) / 10) + "." + juce::String(std::abs(t) % 10) + " dB";
}

const juce::Image& part(const juce::String& file)
{
    return hardware::image("pedals", file);
}

// ----- what each pedal is ---------------------------------------------------------------------

struct PedalKnob
{
    int param = 0;
    juce::Point<float> centre;
    float skirt = 64.0f;
    juce::String name;
};

struct PedalToggle
{
    int param = 0;
    juce::Point<float> nut;
    juce::String name;
    // From the top position down, with the value each sets.
    std::vector<juce::String> legends;
    std::vector<float> values;
};

struct Inks
{
    juce::Colour paint, ink, inkSoft;
};

struct PedalSpec
{
    juce::String finish; // the enclosure's render: enclosure-<finish>.jpg
    juce::String name, kind;
    std::vector<PedalKnob> knobs;
    std::optional<PedalToggle> toggle;
    bool tuner = false; // the display, and a footswitch that mutes
};

Inks inksFor(const Theme& theme, const juce::String& finish)
{
    const auto& hw = theme.hardware;
    if (finish == "gravity")
        return {hw.pedalGravity, hw.pedalGravityInk, hw.pedalGravityInkSoft};
    if (finish == "flare")
        return {hw.pedalFlare, hw.pedalFlareInk, hw.pedalFlareInkSoft};
    if (finish == "comet")
        return {hw.pedalComet, hw.pedalCometInk, hw.pedalCometInkSoft};
    if (finish == "tide")
        return {hw.pedalTide, hw.pedalTideInk, hw.pedalTideInkSoft};
    return {hw.pedalPolaris, hw.pedalPolarisInk, hw.pedalPolarisInkSoft};
}

// Three knobs in a triangle (two up top, one below between them), a toggle between
// the top two.
constexpr float topRow = 116.0f, lowRow = 246.0f;
constexpr float leftX = 96.0f, rightX = 264.0f;
constexpr float knobSkirt = 66.0f;

PedalSpec specFor(const std::string& type)
{
    namespace g = plugins::gravity;
    namespace f = plugins::flare;
    namespace c = plugins::comet;
    namespace t = plugins::tide;
    namespace p = plugins::polaris;
    if (type == g::type)
        return {"gravity",
                "GRAVITY",
                juce::String::fromUTF8("COMPRESSOR \xc2\xb7 SUSTAINER"),
                {{g::level, {104.0f, topRow}, 62.0f, "LEVEL"},
                 {g::sustain, {256.0f, topRow}, 62.0f, "SUSTAIN"},
                 {g::attack, {104.0f, lowRow}, 62.0f, "ATTACK"},
                 {g::blend, {256.0f, lowRow}, 62.0f, "BLEND"}},
                std::nullopt,
                false};
    if (type == f::type)
        return {
            "flare",
            "FLARE",
            "DISTORTION",
            {{f::level, {leftX, topRow}, knobSkirt, "LEVEL"},
             {f::drive, {rightX, topRow}, knobSkirt, "DRIVE"},
             {f::tone, {centreX, lowRow}, knobSkirt, "TONE"}},
            PedalToggle{
                f::clip,
                {centreX - 12.0f, topRow - 4.0f},
                "CLIP",
                {"SI", "LED", "OPEN"},
                {static_cast<float>(f::silicon), static_cast<float>(f::led), static_cast<float>(f::open)}},
            false};
    if (type == c::type)
        return {"comet",
                "COMET",
                juce::String::fromUTF8("ANALOG DELAY"),
                {{c::mix, {leftX, topRow}, knobSkirt, "MIX"},
                 {c::time, {rightX, topRow}, knobSkirt, "TIME"},
                 {c::repeats, {centreX, lowRow}, knobSkirt, "REPEATS"}},
                PedalToggle{c::mod, {centreX - 12.0f, topRow - 4.0f}, "MOD", {"ON", "OFF"}, {1.0f, 0.0f}},
                false};
    if (type == t::type)
        return {"tide",
                "TIDE",
                "CHORUS",
                {{t::rate, {leftX, topRow}, knobSkirt, "RATE"},
                 {t::depth, {rightX, topRow}, knobSkirt, "DEPTH"},
                 {t::mix, {centreX, lowRow}, knobSkirt, "MIX"}},
                PedalToggle{t::mode, {centreX - 12.0f, topRow - 4.0f}, "MODE", {"CHO", "VIB"}, {0.0f, 1.0f}},
                false};
    return {"polaris",
            "POLARIS",
            "CHROMATIC TUNER",
            {{p::reference, {centreX, 306.0f}, 54.0f, "REF"}},
            std::nullopt,
            true};
}

// The tuner's window on the face.
const juce::Rectangle<float> tunerWindow{50.0f, 84.0f, 260.0f, 172.0f};
constexpr float tunerRadius = 6.0f;

// ----- the face's parts -----------------------------------------------------------------------

// What the tuner shows: the note, how far off it is, and whether it is in tune.
struct TunerState
{
    bool on = false;      // the pedal is on
    bool reading = false; // a note is being read now
    int midi = 69;
    float cents = 0.0f; // smoothed
    float glow = 0.0f;  // fades the reading out after the note stops
    float reference = 440.0f;
};

// The tuner's display: a black glass window, a row of LEDs across its top from
// flat to sharp (the middle one green and lit when in tune), the note in large
// type under it with its octave, and how far off it is in cents.
class TunerDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TunerDisplay(const Theme& theme, const TunerState& state) : theme_(theme), state_(state)
    {
        setTooltip("Tuner");
    }

    static constexpr int segments = 21;
    static constexpr float inTuneCents = 3.0f;

    // The segment a reading lights (0..segments - 1; the middle one is in tune).
    static int segmentFor(float cents) noexcept
    {
        const float step = 50.0f / static_cast<float>(segments / 2);
        if (std::abs(cents) <= inTuneCents)
            return segments / 2;
        return std::clamp(segments / 2 + juce::roundToInt(cents / step), 0, segments - 1);
    }

    // The line along the foot: the cents and the reference.
    juce::Rectangle<int> footArea() const { return getLocalBounds().reduced(16, 0).removeFromBottom(30); }

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        const auto r = getLocalBounds().toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, tunerRadius);
        g.reduceClipRegion(clip);
        g.setColour(hw.tunerGlass);
        g.fillRect(r);

        const bool live = state_.on && state_.glow > 0.0f;
        const float a = live ? std::clamp(state_.glow, 0.0f, 1.0f) : 0.0f;
        const bool inTune = live && std::abs(state_.cents) <= inTuneCents;
        // The LED row.
        const auto row = r.reduced(18.0f, 0.0f).withY(r.getY() + 18.0f).withHeight(18.0f);
        const float gap = 3.0f;
        const float w = (row.getWidth() - gap * (segments - 1)) / segments;
        const int lit = live ? segmentFor(state_.cents) : -1;
        for (int i = 0; i < segments; ++i)
        {
            const bool centre = i == segments / 2;
            const float h = centre ? row.getHeight() : row.getHeight() * (i % 5 == 0 ? 0.85f : 0.62f);
            const auto seg = juce::Rectangle<float>(
                row.getX() + static_cast<float>(i) * (w + gap), row.getBottom() - h, w, h);
            const auto colour = centre ? hw.tunerInTune : hw.tunerOff;
            if (i == lit || (inTune && centre))
            {
                g.setColour(colour.withAlpha(0.3f * a));
                g.fillRoundedRectangle(seg.expanded(2.0f), 2.5f);
                g.setColour(colour.withAlpha(std::max(0.25f, a)));
                g.fillRoundedRectangle(seg, 1.0f);
            }
            else
            {
                g.setColour(hw.tunerSegmentOff);
                g.fillRoundedRectangle(seg, 1.0f);
            }
        }
        // The flat and sharp marks at the row's ends.
        const auto marks = legendFont(theme_, 14.0f, 700, 0.0f);
        g.setFont(marks);
        g.setColour(hw.tunerText.withAlpha(0.55f));
        hardware::drawCrispText(g,
                                juce::String::fromUTF8("\xe2\x99\xad"),
                                {row.getX() - 2.0f, row.getBottom() + 4.0f, 20.0f, 16.0f},
                                juce::Justification::centredLeft);
        hardware::drawCrispText(g,
                                juce::String::fromUTF8("\xe2\x99\xaf"),
                                {row.getRight() - 18.0f, row.getBottom() + 4.0f, 20.0f, 16.0f},
                                juce::Justification::centredRight);

        // The note, its octave, and the cents.
        const auto noteArea = juce::Rectangle<float>(r.getX(), row.getBottom() + 14.0f, r.getWidth(), 78.0f);
        const auto ink = inTune ? hw.tunerInTune : hw.tunerText;
        if (live)
        {
            const juce::String name = core::pitchClassName(state_.midi);
            const auto big = legendFont(theme_, 76.0f, 700, 0.0f);
            const auto small = legendFont(theme_, 24.0f, 700, 0.0f);
            const float letterW = juce::GlyphArrangement::getStringWidth(big, name.substring(0, 1));
            const bool sharp = name.length() > 1;
            g.setColour(ink.withAlpha(a));
            g.setFont(big);
            hardware::drawCrispText(g,
                                    name.substring(0, 1),
                                    noteArea.withSizeKeepingCentre(letterW + 8.0f, noteArea.getHeight()),
                                    juce::Justification::centred,
                                    true);
            g.setFont(small);
            const float right = noteArea.getCentreX() + letterW * 0.5f + 4.0f;
            if (sharp)
                hardware::drawCrispText(g,
                                        juce::String::fromUTF8("\xe2\x99\xaf"),
                                        {right, noteArea.getY() + 6.0f, 30.0f, 26.0f},
                                        juce::Justification::centredLeft);
            hardware::drawCrispText(g,
                                    juce::String(core::octaveOf(state_.midi)),
                                    {right, noteArea.getBottom() - 32.0f, 30.0f, 26.0f},
                                    juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(hw.tunerSegmentOff.brighter(0.4f));
            g.setFont(legendFont(theme_, 76.0f, 700, 0.0f));
            hardware::drawCrispText(
                g, juce::String::fromUTF8("\xe2\x80\x93"), noteArea, juce::Justification::centred, true);
        }
        // The foot line: the cents left, the reference right.
        const auto foot = footArea().toFloat();
        g.setFont(theme_.value());
        g.setColour(hw.tunerText.withAlpha(live ? 0.9f * a : 0.0f));
        if (live)
        {
            const int c = juce::roundToInt(state_.cents);
            const auto text = (c > 0   ? juce::String("+")
                               : c < 0 ? juce::String::fromUTF8("\xe2\x88\x92")
                                       : juce::String()) +
                              juce::String(std::abs(c)) + juce::String::fromUTF8(" \xc2\xa2");
            hardware::drawCrispText(g, text, foot, juce::Justification::centredLeft);
        }
        g.setColour(hw.tunerText.withAlpha(state_.on ? 0.6f : 0.3f));
        hardware::drawCrispText(g,
                                "A " + juce::String(juce::roundToInt(state_.reference)) + " Hz",
                                foot,
                                juce::Justification::centredRight);

        // The glass: a shade under the bezel's lip, a reflection across the top left.
        g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.5f),
                                               0.0f,
                                               r.getY(),
                                               hw.shadow.withAlpha(0.0f),
                                               0.0f,
                                               r.getY() + 8.0f,
                                               false));
        g.fillRect(r.withHeight(8.0f));
        g.setGradientFill(juce::ColourGradient(hw.highlight.withAlpha(0.09f),
                                               r.getX(),
                                               r.getY(),
                                               hw.highlight.withAlpha(0.0f),
                                               r.getX() + r.getWidth() * 0.3f,
                                               r.getY() + r.getHeight() * 0.55f,
                                               false));
        g.fillRect(r);
    }

private:
    const Theme& theme_;
    const TunerState& state_;
};

// ----- the face -----------------------------------------------------------------------------------

class Face final : public RenderedFace
{
public:
    Face(const Theme& theme, PedalSpec spec)
        : RenderedFace(faceWidth, faceHeight), theme_(theme), spec_(std::move(spec)),
          inks_(inksFor(theme, spec_.finish))
    {
    }

    const PedalSpec& spec() const noexcept { return spec_; }
    std::function<float(int param)> paramValue;

private:
    juce::Rectangle<float> nameArea(const PedalKnob& k, float width) const
    {
        return {k.centre.x - width * 0.5f, k.centre.y + k.skirt * 0.5f + nameGap, width, 20.0f};
    }

    static juce::Rectangle<int> reach(const PedalKnob& k)
    {
        const float r = k.skirt * 0.5f;
        return juce::Rectangle<float>(k.centre.x - 72.0f, k.centre.y - r - 12.0f, 144.0f, 2.0f * r + 76.0f)
            .getSmallestIntegerContainer();
    }

    void paintStatic(juce::Graphics& g) const override
    {
        if (artPass())
        {
            g.setColour(inks_.paint);
            g.fillAll();
            hardware::drawScaled(g, part("enclosure-" + spec_.finish + ".jpg"), getLocalBounds().toFloat());
            // A knob's scale: eleven small ticks over its 270 degrees.
            g.setColour(inks_.inkSoft);
            for (const auto& k : spec_.knobs)
                hardware::drawTicks(
                    g, k.centre, k.skirt * 0.5f + 4.0f, k.skirt * 0.5f + 9.0f, 11, 1.4f, 5, 2.0f);
            for (const auto& k : spec_.knobs)
                knobBody(g, k);
            if (spec_.tuner)
            {
                // The display's black bezel, standing a little proud of the paint.
                const auto bezel = tunerWindow.expanded(7.0f);
                g.setColour(theme_.hardware.shadow.withAlpha(0.45f));
                g.fillRoundedRectangle(bezel.translated(0.0f, 3.0f).expanded(1.0f), tunerRadius + 8.0f);
                g.setColour(theme_.hardware.tunerBezel);
                g.fillRoundedRectangle(bezel, tunerRadius + 7.0f);
                g.setColour(theme_.hardware.highlight.withAlpha(0.12f));
                g.drawRoundedRectangle(bezel.reduced(0.5f), tunerRadius + 7.0f, 1.0f);
            }
        }

        // The knobs' names.
        for (const auto& k : spec_.knobs)
        {
            if (!artPass() && !g.clipRegionIntersects(reach(k)))
                continue;
            g.setColour(inks_.ink);
            g.setFont(legendFont(theme_, nameSize(15.0f), 700, isCompact() ? 0.06f : 0.14f));
            const bool active = isCompact() && activeControl() == k.param && value;
            print(g, active ? value(k.param) : k.name, nameArea(k, 120.0f));
        }

        // The toggle's positions beside it, and its name under it.
        if (const auto& t = spec_.toggle)
        {
            g.setColour(inks_.ink);
            const int n = static_cast<int>(t->legends.size());
            if (!isCompact())
            {
                g.setFont(legendFont(theme_, 11.5f, 700, 0.08f));
                for (int i = 0; i < n; ++i)
                {
                    const float y = t->nut.y + (n == 3 ? (static_cast<float>(i) - 1.0f) * 22.0f
                                                       : (i == 0 ? -20.0f : 20.0f));
                    print(g,
                          t->legends[static_cast<size_t>(i)],
                          {t->nut.x + 18.0f, y - 8.0f, 44.0f, 16.0f},
                          juce::Justification::centredLeft);
                }
            }
            g.setFont(legendFont(theme_, nameSize(12.5f), 700, 0.14f));
            print(g, t->name, {t->nut.x - 40.0f, t->nut.y + 44.0f, 80.0f, 18.0f});
        }

        // The tuner's lamp legend.
        if (spec_.tuner)
        {
            g.setColour(inks_.ink);
            g.setFont(legendFont(theme_, nameSize(12.5f), 700, 0.14f));
            print(g,
                  "MUTE",
                  {lampCentre.x + lampWidth * 0.5f + 4.0f, lampCentre.y - 9.0f, 80.0f, 18.0f},
                  juce::Justification::centredLeft);
        }

        // The name, what it is, and the maker at the foot.
        g.setColour(inks_.ink);
        g.setFont(legendFont(theme_, 46.0f, 700, 0.22f));
        print(g,
              spec_.name,
              {24.0f, nameRow - 26.0f, faceWidth - 48.0f, 52.0f},
              juce::Justification::centred,
              true);
        if (!isCompact())
        {
            g.setColour(inks_.inkSoft);
            g.setFont(legendFont(theme_, 12.5f, 700, 0.3f));
            print(g, spec_.kind, {24.0f, kindRow - 9.0f, faceWidth - 48.0f, 18.0f});
        }
        g.setColour(inks_.inkSoft);
        g.setFont(legendFont(theme_, nameSize(11.0f), 700, 0.4f));
        print(g, "NORTHLIGHT", {24.0f, makerRow - 8.0f, faceWidth - 48.0f, 16.0f});
    }

    void knobBody(juce::Graphics& g, const PedalKnob& k) const
    {
        const float image = k.skirt * knobImageRatio;
        if (const auto& body = part("knob-body.png"); body.isValid())
        {
            hardware::drawScaled(
                g,
                body,
                juce::Rectangle<float>(
                    k.centre.x - image * 0.5f, k.centre.y - image * 0.5f, image, image * 1.5f));
            return;
        }
        const auto& hw = theme_.hardware;
        g.setColour(hw.knobBody);
        g.fillEllipse(juce::Rectangle<float>(k.skirt, k.skirt).withCentre(k.centre));
    }

    void paintLive(juce::Graphics& g) const override
    {
        if (isCompact() || !value)
            return;
        // The tuner's reference reads on its display instead.
        if (spec_.tuner)
            return;
        for (const auto& k : spec_.knobs)
        {
            if (!g.clipRegionIntersects(reach(k)))
                continue;
            g.setColour(inks_.inkSoft);
            g.setFont(legendFont(theme_, 13.5f, 600, 0.04f));
            hardware::drawCrispText(g, value(k.param), nameArea(k, 128.0f).translated(0.0f, 19.0f));
        }
    }

    const Theme& theme_;
    PedalSpec spec_;
    Inks inks_;
};

// ----- the card -----------------------------------------------------------------------------------

class PedalCard final : public RenderedCard
{
public:
    PedalCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme, specFor(insert.type)),
          footswitch_(theme, "Bypass", false), lamp_(theme, [this] { return lampLit(); })
    {
        addAndMakeVisible(face_);
        const auto& spec = face_.spec();
        face_.value = [this](int index) { return valueText(index); };
        face_.paramValue = [this](int index) { return param(index); };

        for (const auto& k : spec.knobs)
        {
            auto& knob = bindRenderedKnob(
                k.param, k.centre, k.skirt, k.skirt * pointerImageRatio, part("knob-pointer.png"));
            knob.format = formatFor(k.param);
        }
        if (const auto& t = spec.toggle)
        {
            std::vector<juce::String> positions;
            for (const auto& l : t->legends)
                positions.push_back(l);
            auto& lever = addLever(t->name.substring(0, 1) + t->name.substring(1).toLowerCase(),
                                   positions,
                                   "pedals",
                                   {"toggle-up.png", "toggle-centre.png", "toggle-down.png"},
                                   t->nut,
                                   leverScale,
                                   52.0f * leverScale);
            lever_ = &lever;
            lever.onChoose = [this](int position)
            {
                const auto& toggle = *face_.spec().toggle;
                const auto value = toggle.values[static_cast<size_t>(
                    std::clamp(position, 0, static_cast<int>(toggle.values.size()) - 1))];
                beginGesture(toggle.name.substring(0, 1) + toggle.name.substring(1).toLowerCase());
                setParam(toggle.param, value);
                endGesture();
            };
        }
        if (spec.tuner)
        {
            display_ = std::make_unique<TunerDisplay>(theme_, tuner_);
            display_->setBounds(tunerWindow.toNearestInt());
            face_.addAndMakeVisible(*display_);
        }

        footswitch_.onPress = [this] { stomp(); };
        footswitch_.setBounds(juce::Rectangle<float>(footswitchSize, footswitchSize)
                                  .withCentre(footswitchCentre)
                                  .toNearestInt());
        lamp_.setBounds(juce::Rectangle<float>(lampWidth, lampWidth * 150.0f / 130.0f)
                            .withPosition(lampCentre.x - lampWidth * 0.5f, lampCentre.y - lampWidth * 0.5f)
                            .toNearestInt());
        face_.addAndMakeVisible(footswitch_);
        face_.addAndMakeVisible(lamp_);
        paramsChanged();
    }

private:
    bool isTuner() const noexcept { return face_.spec().tuner; }
    bool muted() const noexcept { return param(plugins::polaris::mute) >= 0.5f; }
    bool lampLit() const noexcept { return isTuner() ? isOn() && muted() : isOn(); }

    // The footswitch: the pedal on or off; the tuner's mutes.
    void stomp()
    {
        if (isTuner())
        {
            beginGesture(muted() ? "Unmute" : "Mute");
            setParam(plugins::polaris::mute, muted() ? 0.0f : 1.0f);
            endGesture();
            return;
        }
        if (host_.onCommand)
            host_.onCommand(std::make_unique<core::SetInsertBypass>(channelId_, moduleId_, isOn()));
    }

    std::function<juce::String(double)> formatFor(int index) const
    {
        const auto& i = info(index);
        if (i.unit == "dB")
            return signedDb;
        if (i.unit == "ms")
            return [](double ms) { return juce::String(juce::roundToInt(ms)) + " ms"; };
        if (i.unit == "Hz")
            return [](double hz) { return juce::String(juce::roundToInt(hz)) + " Hz"; };
        return tenths;
    }

    juce::String valueText(int index) const
    {
        const auto* k = knobFor(index);
        return k != nullptr && k->format ? k->format(param(index)) : juce::String();
    }

    void paramsChanged() override
    {
        if (const auto& t = face_.spec().toggle; t && lever_ != nullptr)
        {
            const float v = param(t->param);
            int position = 0;
            for (size_t i = 0; i < t->values.size(); ++i)
                if (std::abs(t->values[i] - v) < 0.5f)
                    position = static_cast<int>(i);
            lever_->setPosition(position);
            lever_->setTooltip(
                t->name.substring(0, 1) + t->name.substring(1).toLowerCase() + ": " +
                juce::String(info(t->param).choices[static_cast<size_t>(std::clamp(
                    juce::roundToInt(v), 0, static_cast<int>(info(t->param).choices.size()) - 1))]));
        }
        if (isTuner())
        {
            footswitch_.setTooltip(muted() ? "Unmute" : "Mute");
            lamp_.setTooltip(muted() ? "Muted" : "Not muted");
            if (const float reference = param(plugins::polaris::reference); reference != tuner_.reference)
            {
                tuner_.reference = reference;
                if (display_ != nullptr)
                    display_->repaint(display_->footArea());
            }
        }
        else
        {
            footswitch_.setTooltip(isOn() ? "Switch off" : "Switch on");
            lamp_.setTooltip(isOn() ? "On" : "Off");
        }
        lamp_.repaint();
        repaintChanged();
    }

    // A knob's print: its ticks, and its name and value under it.
    juce::Rectangle<int> printArea(const Knob& knob) const override
    {
        const auto b = knob.getBounds();
        const int skirt = knob.ringSize();
        const auto centre = b.getCentre();
        const int top = centre.y - skirt / 2 - 12;
        const int bottom = centre.y + skirt / 2 + juce::roundToInt(nameGap) + 60;
        return juce::Rectangle<int>(centre.x - 66, top, 132, bottom - top).getUnion(b);
    }

    // The toggle shows on its own lever; the mute on the lamp and footswitch.
    bool printsWidely(int index) const override
    {
        if (const auto& t = face_.spec().toggle; t && index == t->param)
            return false;
        if (isTuner() && index == plugins::polaris::mute)
            return false;
        return knobFor(index) == nullptr;
    }

    // Below this a reading is a chord or noise, not a string: it shows nothing.
    static constexpr float minClarity = 0.8f;

    void framesArrived(double elapsedMs) override
    {
        if (!isTuner() || display_ == nullptr)
            return;
        plugins::polaris::Reading reading;
        const bool have = plugins::polaris::readingOf(liveProcessor(), reading);
        const bool was = tuner_.glow > 0.0f;
        tuner_.on = isOn();
        if (have && isOn() && reading.hz > 0.0f && reading.clarity > minClarity)
        {
            const auto note = plugins::polaris::noteFor(reading.hz, tuner_.reference);
            // A new note jumps; the same note's cents settle smoothly.
            if (note.midi != tuner_.midi || tuner_.glow <= 0.0f)
                tuner_.cents = note.cents;
            else
                tuner_.cents += (note.cents - tuner_.cents) * 0.35f;
            tuner_.midi = note.midi;
            tuner_.glow = 1.0f;
        }
        else
        {
            // The last reading holds a moment, then fades.
            held_ += elapsedMs;
            if (held_ > 600.0)
                tuner_.glow = std::max(0.0f, tuner_.glow - static_cast<float>(elapsedMs / 400.0));
        }
        if (tuner_.glow >= 1.0f)
            held_ = 0.0;
        if (tuner_.glow > 0.0f || was)
            display_->repaint();
    }

    Face face_;
    Footswitch footswitch_; // Corona's, without its LED
    JewelLamp lamp_;        // Corona's red jewel, lit while the pedal is on (the tuner's while it mutes)
    HardwareLever* lever_ = nullptr;
    std::unique_ptr<TunerDisplay> display_;
    TunerState tuner_;
    double held_ = 0.0;
};

} // namespace

std::unique_ptr<ModuleCard>
createPedalCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<PedalCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
