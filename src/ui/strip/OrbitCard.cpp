// Orbit's card (screens/plugins.md, Orbit): a studio reel-to-reel in a dim
// mastering room, a hardware face (design-system.md "Hardware face") built from
// rendered layers (assets/orbit; "Rendered parts"). The face is drawn at
// 1160 x 600 and scaled to fit the card's body, the card narrowing with it.
//
// The brushed transport plate carries the reels (turning while the song plays,
// coasting to rest when it stops), the tape counter (the song's position) and
// the pilot lamp (lit while the insert is on). The control panel carries the two
// VU meters and their METER keys, RECORD (INPUT, BIAS), the MACHINE's keys (SPEED,
// EQ, LEVEL, TAPE), WEAR (HISS, FLUTTER) and REPRO (OUTPUT, MIX, LINK). Every word,
// the meters' scales and the counter are printed by the face, never rendered.

#include "plugins/Orbit.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/HardwareKey.h"
#include "ui/strip/OrbitFace.h"
#include "ui/strip/RenderedCard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

namespace ob = plugins::orbit;
namespace of = orbitface;

namespace
{
constexpr float renderScale = 2.0f; // the renders are twice the face's size
constexpr float litTop = 340.0f;    // lit.jpg is the panel only, from this far down

const juce::Image& part(const juce::String& file)
{
    return hardware::image("orbit", file);
}

// A patch of a render covering `area` (face pixels), the render starting `top`
// px down the face. Shares the render's pixels; empty without the render.
juce::Image patchOf(const juce::Image& source, juce::Rectangle<float> area, float top = 0.0f)
{
    if (!source.isValid())
        return {};
    const auto r =
        (area.translated(0.0f, -top) * renderScale).toNearestInt().getIntersection(source.getBounds());
    return r.isEmpty() ? juce::Image() : source.getClippedImage(r);
}

using hardware::legendFont;
using hardware::polar;

// ----- the meters ------------------------------------------------------------------------------

// A VU's needle and its shadow on the dial, then the dial's hood and glass over
// them; the dial and the printed scale are the face's.
class Meter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Meter(const Theme& theme, int index) : theme_(theme), index_(index)
    {
        setInterceptsMouseClicks(false, false);
        setTitle(index == 0 ? "Left meter" : "Right meter");
        setBounds(of::meterCover(index).toNearestInt());
    }

    void setReading(float vu)
    {
        const float angle = of::vuAngle(vu);
        if (std::abs(angle - angle_) < 0.05f)
            return;
        angle_ = angle;
        setDescription(juce::String(std::max(vu, -20.0f), 1) + " VU");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        const auto origin = getPosition().toFloat();
        const auto dial = of::dialArea(index_) - origin;
        const auto pivot = of::pivot(index_) - origin;
        {
            const juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(dial.toNearestInt());
            const auto base = polar(pivot, 34.0f, angle_), tip = polar(pivot, 100.0f, angle_);
            g.setColour(hw.shadow.withAlpha(0.28f));
            g.drawLine(juce::Line<float>(base.translated(2.0f, 4.0f), tip.translated(2.0f, 4.0f)), 1.8f);
            g.setColour(hw.orbitNeedle);
            g.drawLine(juce::Line<float>(base, tip), 1.4f);
        }
        const auto all = getLocalBounds().toFloat();
        const auto& hood = part(index_ == 0 ? "hood-left.png" : "hood-right.png");
        if (hood.isValid())
            hardware::drawScaled(g, hood, all);
        else
        {
            // Without its image: the pivot's hood, a dark trapezoid over the needle's foot.
            const float cx = dial.getCentreX(), foot = dial.getBottom();
            juce::Path hoodShape;
            hoodShape.startNewSubPath(cx - 42.5f, foot);
            hoodShape.lineTo(cx - 27.5f, foot - 22.5f);
            hoodShape.lineTo(cx + 27.5f, foot - 22.5f);
            hoodShape.lineTo(cx + 42.5f, foot);
            hoodShape.closeSubPath();
            g.setColour(hw.bezel);
            g.fillPath(hoodShape);
        }
        hardware::drawScaled(g, part(index_ == 0 ? "glass-left.png" : "glass-right.png"), all);
    }

private:
    const Theme& theme_;
    int index_;
    float angle_ = -50.0f;
};

// ----- the reels -------------------------------------------------------------------------------

// A reel turning: its two nearest rendered frames, the second faded over the
// first. At rest the face's still render shows it, so nothing is drawn.
class Reel final : public juce::Component
{
public:
    Reel(const Theme& theme, bool left) : theme_(theme), left_(left)
    {
        setInterceptsMouseClicks(false, false);
        setBounds(of::reelArea(left ? of::leftReel : of::rightReel).toNearestInt());
    }

    double angle() const noexcept { return angle_; }
    void setAngle(double degrees)
    {
        if (std::abs(degrees - angle_) < 0.05)
            return;
        angle_ = degrees;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const auto& first = frame(0);
        if (!first.isValid())
        {
            paintFlat(g, area);
            return;
        }
        if (of::atRest(static_cast<float>(angle_)))
            return;
        const auto f = of::framesFor(static_cast<float>(angle_));
        hardware::drawScaled(g, frame(f.first), area);
        if (f.blend > 0.01f)
            hardware::drawScaled(g, frame(f.second), area, f.blend);
    }

private:
    const juce::Image& frame(int i) const
    {
        return part(juce::String::formatted(left_ ? "reel-left-%02d.jpg" : "reel-right-%02d.jpg", i));
    }

    // Without its renders: a flat reel, its three windows turning.
    void paintFlat(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto& hw = theme_.hardware;
        const auto c = area.getCentre();
        constexpr float radius = 138.0f, hub = 32.0f;
        g.setColour(hw.orbitReel);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(c));
        g.setColour(hw.orbitPack);
        for (int k = 0; k < 3; ++k)
        {
            const float a =
                juce::degreesToRadians(-static_cast<float>(angle_) + 120.0f * static_cast<float>(k));
            juce::Path window;
            window.addPieSegment(
                juce::Rectangle<float>(236.0f, 236.0f).withCentre(c), a - 0.55f, a + 0.55f, 0.57f);
            g.fillPath(window);
        }
        g.setColour(hw.bezel);
        g.fillEllipse(juce::Rectangle<float>(hub * 2.0f, hub * 2.0f).withCentre(c));
    }

    const Theme& theme_;
    bool left_;
    double angle_ = 0.0;
};

// ----- the pilot lamp --------------------------------------------------------------------------

class Lamp final : public juce::Component
{
public:
    explicit Lamp(const Theme& theme) : theme_(theme)
    {
        setInterceptsMouseClicks(false, false);
        setBounds(
            juce::Rectangle<float>(of::lampPatch, of::lampPatch).withCentre(of::lampCentre).toNearestInt());
    }

    void setOn(bool on)
    {
        if (on == on_)
            return;
        on_ = on;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!on_)
            return;
        if (const auto& lit = part("lamp-lit.png"); lit.isValid())
            hardware::drawScaled(g, lit, getLocalBounds().toFloat());
        else
        {
            g.setColour(theme_.hardware.orbitLamp);
            g.fillEllipse(getLocalBounds().toFloat().reduced(10.0f));
        }
    }

private:
    const Theme& theme_;
    bool on_ = false;
};

// ----- the face --------------------------------------------------------------------------------

// The machine at its drawn size; the card scales it to fit. Everything that does
// not move is drawn once per scale into an image.
class Face final : public RenderedFace
{
public:
    explicit Face(const Theme& theme) : RenderedFace(of::width, of::height), theme_(theme) {}

    // The values printed under the knobs (by knob, in orbitface::knobs' order).
    std::function<juce::String(int knob)> knobValue;
    // The control each knob shows (its parameter), for compact's active control.
    std::array<int, 6> knobParam{};
    float levelDb = 6.0f; // the operating level, in the meters' caption
    juce::String counter; // the tape counter
    static juce::Rectangle<int> levelCaptionArea() { return {120, 527, 220, 20}; }

private:
    static constexpr std::array<const char*, 6> knobNames{
        "INPUT", "BIAS", "HISS", "FLUTTER", "OUTPUT", "MIX"};

    float sized(float size) const noexcept { return isCompact() ? size * compactNameScale : size; }

    // ----- the static layer --------------------------------------------------------------------
    void paintStatic(juce::Graphics& g) const override
    {
        if (artPass())
        {
            if (const auto& deck = part("deck.jpg"); deck.isValid())
                hardware::drawScaled(g, deck, {0.0f, 0.0f, of::width, of::height});
            else
                paintFlat(g);
            paintScales(g);
            paintRules(g);
        }
        paintPlateWords(g);
        paintPanelWords(g);
    }

    // Without the renders: the machine in flat token colours.
    void paintFlat(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The plate in shadow, brighter where the spot falls; the panel darkest at the
        // left, the rim light reaching it from the right.
        g.setGradientFill(
            juce::ColourGradient(hw.aluminium, 260.0f, 40.0f, hw.orbitPlateDark, 760.0f, 260.0f, false));
        g.fillRect(0.0f, 0.0f, of::width, of::plateBottom);
        g.setColour(hw.orbitPlate);
        g.fillRoundedRectangle(of::nameplate, 3.0f);
        g.setColour(hw.aluminium);
        g.fillRect(0.0f, of::plateBottom, of::width, of::panelTop - of::plateBottom);
        g.setGradientFill(juce::ColourGradient(
            hw.orbitPanel, 600.0f, of::panelTop, hw.orbitPanelRim, of::width, of::height, false));
        g.fillRect(0.0f, of::panelTop, of::width, of::height - of::panelTop);
        g.setColour(hw.orbitHead);
        g.fillRoundedRectangle({450.0f, 214.0f, 260.0f, 98.0f}, 6.0f);
        g.setColour(hw.orbitCounterWell);
        g.fillRect(of::counterWindow);
        for (int m = 0; m < 2; ++m)
        {
            g.setColour(hw.bezel);
            g.fillRoundedRectangle(of::dialArea(m).expanded(6.0f), 3.0f);
            g.setColour(hw.orbitDial);
            g.fillRect(of::dialArea(m));
        }
        for (const auto& k : of::knobs)
        {
            const float skirt = k.big ? of::bigSkirt : of::smallSkirt;
            g.setColour(hw.knobBody);
            g.fillEllipse(juce::Rectangle<float>(skirt, skirt).withCentre(k.centre));
        }
    }

    // The meters' scales, printed on their dials: the arc, the ticks (the
    // numbered ones longer), the red zone past 0 VU.
    void paintScales(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        for (int m = 0; m < 2; ++m)
        {
            const juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(of::dialArea(m).toNearestInt());
            const auto p = of::pivot(m);
            const auto arc = [&](float fromVu, float toVu, float radius, float thickness, juce::Colour colour)
            {
                juce::Path path;
                path.addCentredArc(p.x,
                                   p.y,
                                   radius,
                                   radius,
                                   0.0f,
                                   juce::degreesToRadians(of::vuAngle(fromVu)),
                                   juce::degreesToRadians(of::vuAngle(toVu)),
                                   true);
                g.setColour(colour);
                g.strokePath(path, juce::PathStrokeType(thickness));
            };
            arc(-20.0f, 0.0f, of::scaleRadius, 1.4f, hw.orbitDialInk);
            arc(0.0f, 3.0f, of::scaleRadius + 2.5f, 5.0f, hw.orbitDialRed);
            for (const float vu : {-20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f})
            {
                const bool numbered = vu <= -3.0f || vu == 0.0f || vu == 3.0f;
                const float a = of::vuAngle(vu);
                g.setColour(vu > 0.0f ? hw.orbitDialRed : hw.orbitDialInk);
                g.drawLine(juce::Line<float>(polar(p, numbered ? of::scaleRadius - 3.0f : of::scaleRadius, a),
                                             polar(p, of::scaleRadius + 8.0f, a)),
                           1.3f);
            }
        }
    }

    // A rule either side of each column's caption, out to the column's width.
    void paintRules(juce::Graphics& g) const
    {
        g.setColour(theme_.hardware.orbitRule);
        const auto font = legendFont(theme_, sized(14.0f), 700, 0.2f);
        for (const auto& c : of::captions)
        {
            const float text = juce::GlyphArrangement::getStringWidth(font, c.text) + 16.0f;
            const float left = c.centreX - c.columnWidth * 0.5f, right = c.centreX + c.columnWidth * 0.5f;
            if (c.centreX - text * 0.5f - left > 6.0f)
                g.fillRect(juce::Rectangle<float>(left, of::captionY, c.centreX - text * 0.5f - left, 1.0f));
            if (right - (c.centreX + text * 0.5f) > 6.0f)
                g.fillRect(juce::Rectangle<float>(
                    c.centreX + text * 0.5f, of::captionY, right - c.centreX - text * 0.5f, 1.0f));
        }
    }

    void paintPlateWords(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The nameplate (black anodised) carries the name, the kind and the model line.
        g.setColour(hw.orbitPlateInk);
        g.setFont(legendFont(theme_, 50.0f, 700, 0.26f));
        print(g,
              "ORBIT",
              juce::Rectangle<float>(360.0f, 54.0f).withCentre({580.0f, 50.0f}),
              juce::Justification::centred,
              true);
        if (!isCompact())
        {
            g.setColour(hw.orbitPlateInkSoft);
            g.setFont(legendFont(theme_, 16.0f, 600, 0.15f));
            print(g,
                  juce::String::fromUTF8("STUDIO TAPE MACHINE  \xc2\xb7  \xc2\xbd-INCH TWO-TRACK"),
                  juce::Rectangle<float>(370.0f, 20.0f).withCentre({580.0f, 84.0f}));
            g.setFont(legendFont(theme_, 12.5f, 700, 0.3f));
            print(g,
                  juce::String::fromUTF8("NORTHLIGHT  \xc2\xb7  MODEL OR-2"),
                  juce::Rectangle<float>(320.0f, 16.0f).withCentre({580.0f, 102.0f}));
            g.setColour(hw.orbitPlateInk); // on the plate in shadow, not the nameplate
            g.setFont(legendFont(theme_, 12.0f, 600, 0.12f));
            print(g, "TAPE", {652.0f, 127.0f, 60.0f, 13.0f}, juce::Justification::centredLeft);
            print(g, "TIME", {652.0f, 140.0f, 60.0f, 13.0f}, juce::Justification::centredLeft);
        }
        g.setColour(hw.orbitHeadInk);
        g.setFont(legendFont(theme_, isCompact() ? 15.0f : 13.0f, 600, 0.1f));
        const char* heads[] = {"ERASE", "RECORD", "PLAY"};
        const float headX[] = {503.0f, 580.0f, 657.0f};
        for (int i = 0; i < 3; ++i)
            print(g, heads[i], juce::Rectangle<float>(76.0f, 16.0f).withCentre({headX[i], 296.0f}));
    }

    void paintPanelWords(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The columns' captions.
        g.setColour(hw.orbitPanelInk);
        g.setFont(legendFont(theme_, sized(14.0f), 700, 0.2f));
        for (const auto& c : of::captions)
            print(g,
                  c.text,
                  juce::Rectangle<float>(c.columnWidth + 40.0f, 20.0f).withCentre({c.centreX, of::captionY}));
        // The knobs' names (compact: the knob in hand shows its value instead).
        g.setFont(legendFont(theme_, sized(15.0f), 600, isCompact() ? 0.03f : 0.08f));
        for (size_t i = 0; i < of::knobs.size(); ++i)
        {
            const auto& k = of::knobs[i];
            const bool active = isCompact() && knobValue && activeControl() == knobParam[i];
            print(g,
                  active ? knobValue(static_cast<int>(i)) : juce::String(knobNames[i]),
                  juce::Rectangle<float>(isCompact() ? 120.0f : 100.0f, isCompact() ? 26.0f : 18.0f)
                      .withCentre({k.centre.x, k.nameY + (isCompact() ? 4.0f : 0.0f)}));
        }
        // The keys' rows.
        g.setColour(hw.orbitPanelInkSoft);
        g.setFont(legendFont(theme_, sized(14.0f), 600, isCompact() ? 0.03f : 0.1f));
        const char* rows[] = {"SPEED", "EQ", "LEVEL", "TAPE"};
        for (size_t r = 0; r < 4; ++r)
            print(g,
                  rows[r],
                  {of::machineLabelRight - 110.0f, of::machineRowY[r] - 10.0f, 110.0f, 20.0f},
                  juce::Justification::centredRight);
        print(g, "METER", juce::Rectangle<float>(80.0f, 20.0f).withCentre({of::meterLabelX, of::meterKeyY}));
        if (!isCompact())
        {
            g.setFont(legendFont(theme_, 11.0f, 600, 0.08f));
            print(g, "OUT", juce::Rectangle<float>(80.0f, 12.0f).withCentre({of::linkKey.x, 551.0f}));
            print(g, "FOLLOWS IN", juce::Rectangle<float>(80.0f, 12.0f).withCentre({of::linkKey.x, 563.0f}));
        }
        // The meters' dials: the numbers, VU and the channel.
        for (int m = 0; m < 2; ++m)
        {
            const auto p = of::pivot(m);
            const auto c = of::meterCentres[static_cast<size_t>(m)];
            if (!isCompact())
            {
                g.setFont(legendFont(theme_, 12.0f, 600, 0.0f));
                for (const float vu : {-20.0f, -10.0f, -7.0f, -5.0f, -3.0f, 0.0f, 3.0f})
                {
                    g.setColour(vu > 0.0f ? hw.orbitDialRed : hw.orbitDialInk);
                    const auto label = vu > 0.0f ? juce::String("+3") : juce::String(static_cast<int>(-vu));
                    print(g,
                          label,
                          juce::Rectangle<float>(26.0f, 14.0f)
                              .withCentre(polar(p, of::scaleRadius + 17.0f, of::vuAngle(vu))));
                }
            }
            g.setColour(hw.orbitDialInk);
            g.setFont(legendFont(theme_, 18.0f, 700, 0.12f));
            print(g, "VU", juce::Rectangle<float>(60.0f, 22.0f).withCentre({c.x, c.y + 25.0f}));
            g.setFont(legendFont(theme_, 14.0f, 700, 0.0f));
            print(g,
                  m == 0 ? "L" : "R",
                  juce::Rectangle<float>(20.0f, 18.0f).withCentre({c.x - 70.0f, c.y + 25.0f}));
        }
    }

    // ----- what changes with the values -------------------------------------------------------
    void paintLive(juce::Graphics& g) const override
    {
        const auto& hw = theme_.hardware;
        // The tape counter, lit behind its glass: monospaced (font.mono), so the
        // digits hold still as they count.
        g.setColour(hw.orbitCounter);
        g.setFont(theme_.font(Theme::Face::mono, 19.0f, theme_.fonts.weight.medium));
        print(g, counter, of::counterWindow, juce::Justification::centred, true);
        if (isCompact())
            return; // the knob in hand shows its value where its name is
        g.setColour(hw.orbitPanelInkSoft);
        g.setFont(legendFont(theme_, 13.5f, 600, 0.04f));
        for (size_t i = 0; i < of::knobs.size() && knobValue; ++i)
        {
            const auto& k = of::knobs[i];
            print(
                g,
                knobValue(static_cast<int>(i)),
                juce::Rectangle<float>(100.0f, 16.0f).withCentre({k.centre.x, k.nameY + of::valueBelowName}));
        }
        g.setFont(legendFont(theme_, 12.0f, 600, 0.06f));
        print(g,
              "0 VU = +" + juce::String(static_cast<int>(levelDb)) + " dB OVER 185 nWb/m",
              levelCaptionArea().toFloat());
    }

    const Theme& theme_;
};

// ----- the card ----------------------------------------------------------------------------

class OrbitCard final : public RenderedCard
{
public:
    OrbitCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme), left_(theme, true),
          right_(theme, false), meters_{{Meter(theme, 0), Meter(theme, 1)}}, lamp_(theme)
    {
        addAndMakeVisible(face_);
        face_.addAndMakeVisible(left_);
        face_.addAndMakeVisible(right_);
        for (auto& m : meters_)
            face_.addAndMakeVisible(m);
        face_.addAndMakeVisible(lamp_);

        // The knobs, each a rendered pointer turning over the body the deck shows.
        constexpr std::array<int, 6> params{ob::input, ob::bias, ob::hiss, ob::flutter, ob::output, ob::mix};
        const char* files[] = {"input", "bias", "hiss", "flutter", "output", "mix"};
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto& place = of::knobs[i];
            knobs_[i] = &bindRenderedKnob(params[i],
                                          of::onCap(place),
                                          place.big ? of::bigSkirt : of::smallSkirt,
                                          place.big ? of::bigImage : of::smallImage,
                                          part(juce::String("pointer-") + files[i] + ".png"));
            face_.knobParam[i] = params[i];
        }
        const auto decibels = [](double v)
        {
            return juce::String(v >= 0.0 ? "+" : juce::String::fromUTF8("\xe2\x88\x92")) +
                   juce::String(std::abs(v), 1) + " dB";
        };
        knobs_[0]->format = decibels;
        knobs_[4]->format = decibels;
        knobs_[1]->format = [](double v)
        {
            if (std::abs(v) < 0.5)
                return juce::String("Cal");
            return juce::String(v < 0.0 ? "Under " : "Over ") +
                   juce::String(static_cast<int>(std::lround(std::abs(v)))) + "%";
        };
        knobs_[2]->format = [](double v)
        { return v < 0.5 ? juce::String("Off") : juce::String(static_cast<int>(std::lround(v))) + "%"; };
        knobs_[3]->format = [](double v) { return juce::String(v, 2) + "%"; };
        knobs_[5]->format = [](double v) { return juce::String(static_cast<int>(std::lround(v))) + "%"; };
        face_.knobValue = [this](int i) { return knobs_[static_cast<size_t>(i)]->accessibleValueText(); };

        // The keys: a lit key glows from its patch of the lit render.
        const char* speeds[] = {"7\xc2\xbd", "15", "30"};
        const char* speedNames[] = {"7.5 ips", "15 ips", "30 ips"};
        for (int i = 0; i < 3; ++i)
            speed_[static_cast<size_t>(i)] =
                &makeKey(juce::String::fromUTF8(speeds[i]),
                         juce::String("Speed: ") + speedNames[i],
                         {of::machineKeyX[static_cast<size_t>(i)], of::machineRowY[0]},
                         [this, i] { choose(ob::speed, "Speed", i); });
        const char* eqs[] = {"NAB", "CCIR"};
        for (int i = 0; i < 2; ++i)
            eq_[static_cast<size_t>(i)] =
                &makeKey(eqs[i],
                         juce::String("EQ: ") + eqs[i],
                         {of::machineKeyX[static_cast<size_t>(i)], of::machineRowY[1]},
                         [this, i] { choose(ob::eq, "EQ", i); });
        const char* levels[] = {"+3", "+6", "+9"};
        for (int i = 0; i < 3; ++i)
            level_[static_cast<size_t>(i)] =
                &makeKey(levels[i],
                         juce::String("Operating level: ") + levels[i] + " dB",
                         {of::machineKeyX[static_cast<size_t>(i)], of::machineRowY[2]},
                         [this, i] { choose(ob::level, "Level", i); });
        const char* tapes[] = {"CLASSIC", "HOT", "MODERN"};
        const char* tapeNames[] = {"Classic", "Hot", "Modern"};
        for (int i = 0; i < 3; ++i)
            tape_[static_cast<size_t>(i)] =
                &makeKey(tapes[i],
                         juce::String("Tape: ") + tapeNames[i],
                         {of::machineKeyX[static_cast<size_t>(i)], of::machineRowY[3]},
                         [this, i] { choose(ob::tape, "Tape", i); });
        const char* points[] = {"INPUT", "TAPE", "OUTPUT"};
        const char* pointNames[] = {
            "Meters read the input", "Meters read the tape", "Meters read the output"};
        for (int i = 0; i < 3; ++i)
            meter_[static_cast<size_t>(i)] = &makeKey(points[i],
                                                      pointNames[i],
                                                      {of::meterKeyX[static_cast<size_t>(i)], of::meterKeyY},
                                                      [this, i] { choose(ob::meter, "Meter", i); });
        link_ = &makeKey("LINK",
                         "Link: output follows input",
                         of::linkKey,
                         [this] { choose(ob::link, "Link", param(ob::link) >= 0.5f ? 0 : 1); });
        paramsChanged();
    }

private:
    HardwareKey& makeKey(const juce::String& label,
                         const juce::String& title,
                         juce::Point<float> centre,
                         std::function<void()> onPress)
    {
        const auto& hw = theme_.hardware;
        const auto key = of::keyArea(centre);
        const auto area = key.expanded(of::keyMargin);
        auto k = std::make_unique<HardwareKey>(theme_, label, title, false);
        HardwareKey::Rendered r;
        r.off = patchOf(part("deck.jpg"), area);
        r.on = patchOf(part("lit.jpg"), area, litTop);
        r.face = key - area.getPosition();
        r.ink = hw.orbitKeyInk;
        r.litInk = hw.orbitKeyInkLit;
        r.flat = hw.orbitKey;
        r.flatLit = hw.orbitKeyLit;
        k->setRendered(std::move(r));
        k->setBounds(area.toNearestInt());
        k->onPress = std::move(onPress);
        face_.addAndMakeVisible(*k);
        keys_.push_back(std::move(k));
        return *keys_.back();
    }

    void choose(int index, const juce::String& name, int value)
    {
        beginGesture(name);
        setParam(index, static_cast<float>(value));
        endGesture();
        paramsChanged();
    }

    static int chosen(float value, int count)
    {
        return juce::jlimit(0, count - 1, static_cast<int>(std::lround(value)));
    }

    void paramsChanged() override
    {
        for (int i = 0; i < 3; ++i)
        {
            speed_[static_cast<size_t>(i)]->setLit(i == chosen(param(ob::speed), 3));
            level_[static_cast<size_t>(i)]->setLit(i == chosen(param(ob::level), 3));
            tape_[static_cast<size_t>(i)]->setLit(i == chosen(param(ob::tape), 3));
            meter_[static_cast<size_t>(i)]->setLit(i == chosen(param(ob::meter), 3));
        }
        for (int i = 0; i < 2; ++i)
            eq_[static_cast<size_t>(i)]->setLit(i == chosen(param(ob::eq), 2));
        link_->setLit(param(ob::link) >= 0.5f);
        const float level = ob::operatingLevelDb(param(ob::level));
        if (level != face_.levelDb)
        {
            face_.levelDb = level;
            face_.repaint(Face::levelCaptionArea());
        }
        repaintChanged();
    }

    // The keys light themselves and the level's caption repaints alone: no
    // parameter reaches across the face.
    bool printsWidely(int index) const override
    {
        juce::ignoreUnused(index);
        return false;
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        RenderedCard::layoutBody(body);
        // Compact, the keys' labels are set larger to stay legible.
        const float size = face_.isCompact() ? 19.0f : 14.0f;
        for (auto& k : keys_)
            k->setLabelSize(size);
    }

    void framesArrived(double elapsedMs) override
    {
        const double seconds = elapsedMs * 0.001;
        const bool on = isOn();
        // The loudest of the frames since the last tick (a tick spans a few of them).
        float levels[] = {0.0f, 0.0f};
        const auto total = historyTotal();
        const int fresh = static_cast<int>(
            std::min<std::uint64_t>(total - seenFrames_, static_cast<std::uint64_t>(historySize())));
        seenFrames_ = total;
        for (int i = historySize() - std::max(1, fresh); on && i >= 0 && i < historySize(); ++i)
        {
            levels[0] = std::max(levels[0], historyAt(i).extra);
            levels[1] = std::max(levels[1], historyAt(i).extra2);
        }
        for (size_t m = 0; m < 2; ++m)
        {
            needles_[m].advance(levels[m], seconds);
            meters_[m].setReading(of::vuOf(needles_[m].level, ob::vuReferenceDbfs));
        }
        const auto* fb = liveFeedback();
        const bool playing = on && fb != nullptr && fb->transportPlaying();
        const auto text = of::counterText(fb != nullptr ? fb->transportSeconds() : 0.0);
        if (text != face_.counter)
        {
            face_.counter = text;
            face_.repaint(of::counterWindow.expanded(2.0f).toNearestInt());
        }
        // The reels turn with the tape while the song plays and coast to rest when it
        // stops (with reduced motion they stay still).
        const double ips = ob::speedIps(param(ob::speed));
        const auto turn = [&](Reel& reel, double packInches)
        {
            double a = reel.angle();
            if (theme_.motion.reduceMotion)
                a = 0.0;
            else if (playing)
                a = std::fmod(a + of::reelDegreesPerSecond(ips, packInches) * seconds,
                              of::reelSymmetry * 30.0);
            else
                a = of::coast(a, seconds);
            reel.setAngle(a);
        };
        turn(left_, of::supplyPackInches);
        turn(right_, of::takeUpPackInches);
        lamp_.setOn(on);
    }

    juce::String headerStatus() const override
    {
        const float squash = reductionReadoutDb();
        return isOn() && squash >= 0.3f ? "Tape " + juce::String(squash, 1) + " dB" : juce::String();
    }

    Face face_;
    Reel left_, right_;
    std::array<Meter, 2> meters_;
    Lamp lamp_;
    std::array<Knob*, 6> knobs_{};
    std::vector<std::unique_ptr<HardwareKey>> keys_;
    std::array<HardwareKey*, 3> speed_{}, level_{}, tape_{}, meter_{};
    std::array<HardwareKey*, 2> eq_{};
    HardwareKey* link_ = nullptr;
    std::array<of::VuNeedle, 2> needles_{};
    std::uint64_t seenFrames_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createOrbitCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<OrbitCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
