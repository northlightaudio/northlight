// Lumen's editor (screens/plugins.md 7.4h). An electric piano's face should feel
// warm and close: a dark room lit from below by a lamp, still, with glass panels
// over it. The tine is the centrepiece: a steel rod from its clamp to its tip in
// front of the pickup's coil, the pickup's field drawn beside the pole; drag
// across to bring the tip closer (Bark), up and down to move it off the pole's
// centre (Symmetry). While Lumen sounds the tine vibrates, slowed down. Then the
// hammer's velocity curve, the preamp's tone, the tremolo's two speakers, the
// phaser's notches and the chorus's sweep.

#include "ui/strip/LumenEditor.h"

#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/Lumen.h"
#include "plugins/dsp/Modulation.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/strip/PresetBar.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <array>
#include <cmath>
#include <complex>

namespace northlight::ui::strip
{

namespace
{
namespace lu = plugins::lumen;

struct Hues
{
    juce::Colour tine, hammer, tone, tremolo, phaser, chorus, brass;
    explicit Hues(const Theme& t)
        : tine(paletteColour(t, "Gorse")), hammer(paletteColour(t, "Rowan")), tone(paletteColour(t, "Straw")),
          tremolo(paletteColour(t, "Loch")), phaser(paletteColour(t, "Heather")),
          chorus(paletteColour(t, "Sea grass")), brass(paletteColour(t, "Kelp"))
    {
    }
};

constexpr int lightCount = 16;

// The log frequency axis the tone and phaser displays share: 20 Hz to 20 kHz.
float axisX(double hz, juce::Rectangle<float> r)
{
    const double t = std::log(juce::jlimit(20.0, 20000.0, hz) / 20.0) / std::log(1000.0);
    return r.getX() + static_cast<float>(t) * r.getWidth();
}
double axisHz(float t)
{
    return 20.0 * std::pow(1000.0, static_cast<double>(t));
}

// A biquad's gain in dB at `hz` (the tone display draws the preamp's own shelves).
double biquadDb(const plugins::Biquad& f, double hz, double rate)
{
    const std::complex<double> z1 = std::polar(1.0, -2.0 * juce::MathConstants<double>::pi * hz / rate);
    const auto z2 = z1 * z1;
    const auto c = [](float v) { return static_cast<double>(v); };
    const auto h = (c(f.b0) + c(f.b1) * z1 + c(f.b2) * z2) / (1.0 + c(f.a1) * z1 + c(f.a2) * z2);
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(h)));
}

juce::String tuneText(double v)
{
    const bool whole = std::abs(v - std::round(v)) < 0.05;
    return juce::String(v, whole ? 0 : 1) + " Hz";
}

juce::String hzText(double v)
{
    return juce::String(v, v < 1.0 ? 2 : 1) + " Hz";
}

// ----- the tine ---------------------------------------------------------------------------------

// The tine from the side: its clamp and tone bar on the left, the rod, its tip in
// front of the pickup's coil on the right and the coil's field as a bell beside
// the pole. The tip's distance from the coil is Bark, its offset from the pole's
// centre Symmetry.
class TineDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TineDisplay(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setTooltip("Drag across for Bark (towards the coil is closer), up and down for Symmetry");
        setTitle("Tine and pickup");
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    float swing = 0.0f; // 0..1, how wide the tine moves now
    float phase = 0.0f; // radians, the slowed-down vibration

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(12.0f, 12.0f);
        const float bark = card_.param(lu::bark) * 0.01f, symmetry = card_.param(lu::symmetry) * 0.01f;
        // The model's units on screen: a full strike's swing (0.5) fills a third of the height.
        const float unit = p.getHeight() * 0.62f;
        const float restY = p.getCentreY();
        const float distance = 2.0f * std::pow(0.1f, bark); // the pickup's d
        const float coilW = 20.0f;
        const float coilX = p.getRight() - coilW;
        const float gap = juce::jmap(juce::jlimit(0.2f, 2.0f, distance), 0.2f, 2.0f, 5.0f, 44.0f);
        const float tipX = coilX - gap;
        const float poleOffset =
            juce::jlimit(-0.45f * p.getHeight(), 0.45f * p.getHeight(), distance * symmetry * unit);
        const float poleY = restY - poleOffset;

        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            // The field: the flux across the tip's travel, a bell bulging towards the tip.
            juce::Path field;
            const float fieldW = juce::jmax(10.0f, gap * 0.9f + 14.0f);
            for (int i = 0; i <= 48; ++i)
            {
                const float y = p.getY() + static_cast<float>(i) / 48.0f * p.getHeight();
                const double flux = lu::pickupFlux((restY - y) / unit, bark, symmetry) / (1.54 * distance);
                const float x = coilX - static_cast<float>(juce::jlimit(0.0, 1.0, flux)) * fieldW;
                if (i == 0)
                    field.startNewSubPath(coilX, y);
                field.lineTo(x, y);
            }
            field.lineTo(coilX, p.getBottom());
            field.closeSubPath();
            g.setGradientFill(juce::ColourGradient(hues_.tine.withAlpha(0.30f),
                                                   coilX,
                                                   0.0f,
                                                   hues_.tine.withAlpha(0.04f),
                                                   coilX - fieldW,
                                                   0.0f,
                                                   false));
            g.fillPath(field);
            g.setColour(hues_.tine.withAlpha(0.55f));
            g.strokePath(field, juce::PathStrokeType(1.0f));

            // The tip's reach at a full strike, marked across the field.
            g.setColour(hues_.tine.withAlpha(0.35f));
            g.drawVerticalLine(static_cast<int>(tipX), restY - 0.5f * unit, restY + 0.5f * unit);
            for (const float y : {restY - 0.5f * unit, restY + 0.5f * unit})
                g.drawHorizontalLine(static_cast<int>(y), tipX - 3.0f, tipX + 4.0f);

            // The coil: windings on a bobbin, the pole's centre marked.
            const juce::Rectangle<float> coil(
                coilX, poleY - p.getHeight() * 0.3f, coilW, p.getHeight() * 0.6f);
            g.setColour(theme.colours.surface3);
            g.fillRoundedRectangle(coil, 3.0f);
            g.setColour(hues_.brass.withAlpha(0.55f));
            for (float y = coil.getY() + 4.0f; y < coil.getBottom() - 3.0f; y += 3.0f)
                g.drawHorizontalLine(static_cast<int>(y), coil.getX() + 3.0f, coil.getRight() - 3.0f);
            g.setColour(theme.colours.textPrimary.withAlpha(0.75f));
            g.fillRoundedRectangle(juce::Rectangle<float>(4.0f, 8.0f).withCentre({coilX - 1.0f, poleY}),
                                   1.5f);

            // The clamp and the tone bar above the tine.
            const float clampX = p.getX();
            g.setColour(theme.colours.surface3);
            g.fillRoundedRectangle(juce::Rectangle<float>(clampX, restY - 16.0f, 10.0f, 32.0f), 2.0f);
            const float barEnd = clampX + (tipX - clampX) * 0.62f;
            g.setGradientFill(juce::ColourGradient(hues_.brass.brighter(0.2f),
                                                   0.0f,
                                                   restY - 20.0f,
                                                   hues_.brass.darker(0.4f),
                                                   0.0f,
                                                   restY - 11.0f,
                                                   false));
            g.fillRoundedRectangle(
                juce::Rectangle<float>(clampX + 6.0f, restY - 20.0f, barEnd - clampX - 6.0f, 8.0f), 3.0f);

            // The tine: a cantilever's shape, its tip swinging.
            const float tipY = restY + swing * 0.5f * unit * std::sin(phase);
            juce::Path rod;
            const float x0 = clampX + 10.0f;
            rod.startNewSubPath(x0, restY);
            for (int i = 1; i <= 24; ++i)
            {
                const float t = static_cast<float>(i) / 24.0f;
                const float shape = t * t * (3.0f - t) * 0.5f; // a clamped rod's bend
                rod.lineTo(x0 + t * (tipX - x0), restY + (tipY - restY) * shape);
            }
            g.setColour(hues_.tine.withAlpha(0.25f));
            g.strokePath(
                rod, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(theme.colours.textPrimary.withAlpha(0.85f));
            g.strokePath(
                rod, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(hues_.tine);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({tipX, tipY}));
        }
        drawWellFrame(g, theme, b);
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText(
            barkWord(bark), b.reduced(8.0f, 5.0f).removeFromTop(14.0f), juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        start_ = {card_.param(lu::bark), card_.param(lu::symmetry)};
        card_.beginGesture("Voicing");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float speed = e.mods.isShiftDown() ? 0.25f : 1.0f;
        card_.setParam(
            lu::bark,
            juce::jlimit(
                0.0f, 100.0f, start_.x + speed * static_cast<float>(e.getDistanceFromDragStartX()) * 0.6f));
        card_.setParam(
            lu::symmetry,
            juce::jlimit(
                0.0f, 100.0f, start_.y - speed * static_cast<float>(e.getDistanceFromDragStartY()) * 0.8f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    static juce::String barkWord(float bark)
    {
        return bark < 0.25f ? "Round" : bark < 0.55f ? "Warm" : bark < 0.8f ? "Barking" : "Growling";
    }

    ModuleCard& card_;
    const Hues& hues_;
    juce::Point<float> start_;
};

// ----- the hammer -------------------------------------------------------------------------------

// The velocity curve: key velocity across, strike speed up, with the felt's
// contact time for a full strike at middle C in the corner.
class HammerDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    HammerDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("Key velocity across, the hammer's speed up; drag up and down for Velocity");
        setTitle("Velocity curve");
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(10.0f, 10.0f).withTrimmedTop(12.0f);
        g.setColour(theme.colours.gridBeat);
        g.drawLine(p.getX(), p.getBottom(), p.getRight(), p.getY(), 1.0f);
        const float amount = card_.param(lu::velocity) * 0.01f;
        juce::Path curve;
        curve.startNewSubPath(p.getX(), p.getBottom());
        for (int v = 1; v <= 127; v += 2)
        {
            const float x = p.getX() + static_cast<float>(v) / 127.0f * p.getWidth();
            const float y = p.getBottom() - static_cast<float>(lu::strikeSpeed(v, amount)) * p.getHeight();
            curve.lineTo(x, y);
        }
        juce::Path area = curve;
        area.lineTo(p.getRight(), p.getBottom());
        area.closeSubPath();
        g.setGradientFill(juce::ColourGradient(
            hue_.withAlpha(0.35f), 0.0f, p.getY(), hue_.withAlpha(0.03f), 0.0f, p.getBottom(), false));
        g.fillPath(area);
        g.setColour(hue_);
        g.strokePath(curve,
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawWellFrame(g, theme, b);
        // The contact time at middle C, full strike.
        const double ms = lu::noteModel(60, card_.params()).contactSeconds * 1000.0;
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText(juce::String(ms, 1) + " ms",
                   b.reduced(8.0f, 5.0f).removeFromTop(14.0f),
                   juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        start_ = card_.param(lu::velocity);
        card_.beginGesture("Velocity");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float speed = e.mods.isShiftDown() ? 0.25f : 1.0f;
        card_.setParam(
            lu::velocity,
            juce::jlimit(
                0.0f, 100.0f, start_ - speed * static_cast<float>(e.getDistanceFromDragStartY()) * 0.8f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    float start_ = 0.0f;
};

// ----- the tone ---------------------------------------------------------------------------------

// The preamp's two shelves, 20 Hz to 20 kHz; drag on the left half for Bass, the
// right half for Treble.
class ToneDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToneDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("Drag up and down on the left for Bass, on the right for Treble");
        setTitle("Tone");
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 10.0f);
        constexpr double rate = 48000.0, range = 15.0;
        plugins::Biquad low, high;
        low.setLowShelf(rate, 220.0, card_.param(lu::bass));
        high.setHighShelf(rate, 2800.0, card_.param(lu::treble));
        const auto yOf = [&](double db)
        { return p.getCentreY() - static_cast<float>(db / range) * p.getHeight() * 0.5f; };
        g.setColour(theme.colours.gridBeat);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());
        for (const double hz : {100.0, 1000.0, 10000.0})
            g.drawVerticalLine(static_cast<int>(axisX(hz, p)), p.getY(), p.getBottom());
        juce::Path curve;
        for (int i = 0; i <= 60; ++i)
        {
            const float t = static_cast<float>(i) / 60.0f;
            const double hz = axisHz(t);
            const float y = yOf(biquadDb(low, hz, rate) + biquadDb(high, hz, rate));
            if (i == 0)
                curve.startNewSubPath(p.getX(), y);
            else
                curve.lineTo(p.getX() + t * p.getWidth(), y);
        }
        juce::Path area = curve;
        area.lineTo(p.getRight(), p.getCentreY());
        area.lineTo(p.getX(), p.getCentreY());
        area.closeSubPath();
        g.setColour(hue_.withAlpha(0.18f));
        g.fillPath(area);
        g.setColour(hue_);
        g.strokePath(curve,
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawWellFrame(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        which_ = e.position.x < static_cast<float>(getWidth()) * 0.5f ? lu::bass : lu::treble;
        start_ = card_.param(which_);
        card_.beginGesture(which_ == lu::bass ? "Bass" : "Treble");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float speed = e.mods.isShiftDown() ? 0.25f : 1.0f;
        card_.setParam(
            which_,
            juce::jlimit(
                -12.0f, 12.0f, start_ - speed * static_cast<float>(e.getDistanceFromDragStartY()) * 0.2f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    int which_ = lu::bass;
    float start_ = 0.0f;
};

// ----- the tremolo ------------------------------------------------------------------------------

// Two speakers from the front, each lit as its side's level: in Pan the light
// moves between them, in Amplitude they pulse together. The mode and the sync
// value in the corners; drag up and down for Depth.
class TremoloDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TremoloDisplay(ModuleCard& card, juce::Colour hue)
        : card_(card), hue_(hue), mode_(card.theme(), hue), sync_(card.theme(), hue)
    {
        setTooltip("The two speakers, lit as the tremolo moves; drag up and down for Depth");
        setTitle("Tremolo");
        mode_.setChoices(card.info(lu::tremoloMode));
        mode_.setTooltip("Tremolo mode");
        mode_.onChoose = [this](int i) { choose(lu::tremoloMode, "Tremolo", i); };
        for (const auto& c : plugins::sync::choices())
            sync_.choices.emplace_back(c);
        sync_.choices.front() = "Free";
        sync_.setTooltip("Sync the tremolo to the project tempo");
        sync_.onChoose = [this](int i) { choose(lu::tremoloSync, "Tremolo sync", i); };
        addAndMakeVisible(mode_);
        addAndMakeVisible(sync_);
    }
    float live = 0.5f; // the wave, 0..1

    void resized() override
    {
        auto area = getLocalBounds().reduced(5);
        const auto top = area.removeFromTop(16);
        const auto bottom = area.removeFromBottom(16);
        mode_.setBounds(top.withSizeKeepingCentre(std::min(top.getWidth(), mode_.preferredWidth()), 16));
        sync_.setBounds(
            bottom.withSizeKeepingCentre(std::min(bottom.getWidth(), sync_.preferredWidth()), 16));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        mode_.selected = juce::jlimit(0, 2, static_cast<int>(std::lround(card_.param(lu::tremoloMode))));
        sync_.selected = plugins::sync::choiceOf(card_.param(lu::tremoloSync));
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 8.0f).withTrimmedTop(16.0f).withTrimmedBottom(16.0f);
        // Each side's level now, as the processor sets it.
        float l = 1.0f, r = 1.0f;
        plugins::dsp::Tremolo t;
        t.value = live * 2.0f - 1.0f;
        const auto mode = static_cast<plugins::dsp::Tremolo::Mode>(mode_.selected);
        t.apply(l, r, mode, card_.param(lu::tremoloDepth) * 0.01f);
        const float size = juce::jmin(p.getHeight() * 0.8f, p.getWidth() * 0.36f);
        const float y = p.getCentreY();
        const std::array<std::pair<float, float>, 2> sides{
            {{p.getX() + p.getWidth() * 0.27f, l}, {p.getX() + p.getWidth() * 0.73f, r}}};
        for (const auto& [x, gain] : sides)
        {
            const auto cone = juce::Rectangle<float>(size, size).withCentre({x, y});
            const float lit =
                mode == plugins::dsp::Tremolo::Mode::off ? 0.35f : juce::jlimit(0.0f, 1.0f, gain * 0.7f);
            g.setColour(hue_.withAlpha(0.08f + 0.35f * lit));
            g.fillEllipse(cone.expanded(4.0f));
            g.setColour(theme.colours.surface3);
            g.fillEllipse(cone);
            g.setColour(hue_.withAlpha(0.25f + 0.6f * lit));
            g.drawEllipse(cone.reduced(size * 0.18f), 1.2f);
            g.fillEllipse(cone.withSizeKeepingCentre(size * 0.28f, size * 0.28f));
        }
        drawWellFrame(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        start_ = card_.param(lu::tremoloDepth);
        card_.beginGesture("Tremolo depth");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float speed = e.mods.isShiftDown() ? 0.25f : 1.0f;
        card_.setParam(
            lu::tremoloDepth,
            juce::jlimit(
                0.0f, 100.0f, start_ - speed * static_cast<float>(e.getDistanceFromDragStartY()) * 0.8f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    void choose(int index, const juce::String& name, int value)
    {
        card_.beginGesture(name);
        card_.setParam(index, static_cast<float>(value));
        card_.endGesture();
    }

    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice mode_, sync_;
    float start_ = 0.0f;
};

// ----- the phaser -------------------------------------------------------------------------------

// The phaser's response at the middle of its sweep, with the sweep's reach behind
// it; dim when its mix is off.
class PhaserDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PhaserDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("The phaser's notches at the middle of the sweep, and how far it reaches");
        setTitle("Phaser");
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 10.0f);
        const float mix = card_.param(lu::phaserMix) * 0.01f;
        const float depth = card_.param(lu::phaserDepth) * 0.01f;
        const float feedback = card_.param(lu::phaserFeedback) * 0.01f;
        const float alpha = 0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, mix * 2.0f);
        const auto curveAt = [&](double lfo)
        {
            const double centre =
                plugins::dsp::Phaser::centreHz * std::exp2(depth * plugins::dsp::Phaser::octaves * lfo);
            juce::Path curve;
            for (int i = 0; i <= 80; ++i)
            {
                const float t = static_cast<float>(i) / 80.0f;
                const double hz = axisHz(t);
                // Four first-order all-passes at the centre, fed back, summed with the dry.
                const double w = juce::MathConstants<double>::pi * hz / 48000.0;
                const double tc = std::tan(juce::MathConstants<double>::pi * centre / 48000.0);
                const double a = (tc - 1.0) / (tc + 1.0);
                const auto z = std::polar(1.0, -2.0 * w);
                const auto stage = (a + z) / (1.0 + a * z);
                const auto chain = stage * stage * stage * stage;
                const auto wet = chain / (1.0 - static_cast<double>(feedback) * chain);
                const double m = static_cast<double>(std::max(mix, 1.0f));
                const double gain = std::abs((1.0 - 0.5 * m) + 0.5 * m * wet);
                const float y =
                    p.getBottom() - static_cast<float>(juce::jlimit(0.0, 1.0, gain / 1.6)) * p.getHeight();
                if (i == 0)
                    curve.startNewSubPath(p.getX(), y);
                else
                    curve.lineTo(p.getX() + t * p.getWidth(), y);
            }
            return curve;
        };
        g.setColour(hue_.withAlpha(0.18f * alpha));
        for (const double lfo : {-1.0, 1.0})
            g.strokePath(curveAt(lfo), juce::PathStrokeType(1.0f));
        g.setColour(hue_.withAlpha(alpha));
        g.strokePath(curveAt(0.0),
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// ----- the chorus -------------------------------------------------------------------------------

// The two sides' delays as lines wavering in opposite phase, as deep as Depth;
// dim when its mix is off.
class ChorusDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChorusDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("The two sides' delays, sweeping in opposite phase");
        setTitle("Chorus");
    }
    float time = 0.0f; // seconds, while it sounds

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 12.0f);
        const float mix = card_.param(lu::chorusMix) * 0.01f;
        const float depth = 0.15f + 0.85f * card_.param(lu::chorusDepth) * 0.01f;
        const float alpha = 0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, mix * 2.0f);
        const float offset = time * card_.param(lu::chorusRate);
        for (const float side : {0.0f, 0.5f})
        {
            juce::Path wave;
            for (int i = 0; i <= 48; ++i)
            {
                const float t = static_cast<float>(i) / 48.0f;
                float ph = t * 1.5f + side + offset;
                ph -= std::floor(ph);
                const float tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
                const float y = p.getCentreY() - tri * depth * p.getHeight() * 0.42f;
                if (i == 0)
                    wave.startNewSubPath(p.getX(), y);
                else
                    wave.lineTo(p.getX() + t * p.getWidth(), y);
            }
            g.setColour(hue_.withAlpha(alpha * (side == 0.0f ? 1.0f : 0.55f)));
            g.strokePath(
                wave,
                juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        drawWellFrame(g, theme, b);
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.caption - 2.0f, theme.fonts.weight.semibold));
        g.setColour(theme.colours.textTertiary);
        g.drawText("L", b.reduced(6.0f, 4.0f), juce::Justification::topLeft);
        g.drawText("R", b.reduced(6.0f, 4.0f), juce::Justification::bottomLeft);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// ----- the editor -------------------------------------------------------------------------------

class LumenCard final : public ModuleCard
{
public:
    LumenCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), tine_(*this, hues_),
          hammer_(*this, hues_.hammer), tone_(*this, hues_.tone), tremolo_(*this, hues_.tremolo),
          phaser_(*this, hues_.phaser), chorus_(*this, hues_.chorus)
    {
        for (auto* c : displays())
            addAndMakeVisible(c);
        const auto knob = [this](int p,
                                 const juce::String& label,
                                 juce::Colour hue,
                                 std::function<juce::String(double)> format = {}) -> Knob*
        {
            auto& k = addKnob(p, label);
            k.setArcColour(hue);
            if (format)
                k.format = std::move(format);
            return &k;
        };
        tineKnobs_ = {knob(lu::bark, "Bark", hues_.tine),
                      knob(lu::symmetry, "Symmetry", hues_.tine),
                      knob(lu::bell, "Bell", hues_.tine),
                      knob(lu::age, "Age", hues_.tine),
                      knob(lu::decay, "Decay", hues_.tine),
                      knob(lu::damper, "Damper", hues_.tine),
                      knob(lu::tune, "Tune", hues_.tine, tuneText)};
        hammerKnobs_ = {knob(lu::hardness, "Hardness", hues_.hammer),
                        knob(lu::velocity, "Velocity", hues_.hammer),
                        knob(lu::noise, "Noise", hues_.hammer),
                        knob(lu::voices,
                             "Voices",
                             hues_.hammer,
                             [](double v) { return juce::String(static_cast<int>(std::lround(v))); })};
        toneKnobs_ = {knob(lu::bass, "Bass", hues_.tone),
                      knob(lu::treble, "Treble", hues_.tone),
                      knob(lu::drive, "Drive", hues_.tone),
                      knob(lu::level, "Level", hues_.tone)};
        tremoloKnobs_ = {knob(lu::tremoloRate,
                              "Rate",
                              hues_.tremolo,
                              [this](double v)
                              {
                                  // Synced, the rate is a note value.
                                  if (const int c = plugins::sync::choiceOf(param(lu::tremoloSync)); c > 0)
                                      return juce::String(
                                          plugins::sync::noteValues[static_cast<size_t>(c)].name);
                                  return hzText(v);
                              }),
                         knob(lu::tremoloDepth, "Depth", hues_.tremolo),
                         knob(lu::tremoloShape, "Shape", hues_.tremolo)};
        phaserKnobs_ = {knob(lu::phaserRate, "Rate", hues_.phaser, hzText),
                        knob(lu::phaserDepth, "Depth", hues_.phaser),
                        knob(lu::phaserFeedback, "Feedback", hues_.phaser),
                        knob(lu::phaserMix, "Mix", hues_.phaser)};
        chorusKnobs_ = {knob(lu::chorusRate, "Rate", hues_.chorus, hzText),
                        knob(lu::chorusDepth, "Depth", hues_.chorus),
                        knob(lu::chorusMix, "Mix", hues_.chorus)};
        dimSyncedRate();
        // The preset bar: the library's list (the factory's and the user's), each
        // applied as one undo step.
        presets_.onApply = [this](const std::vector<float>& values, const juce::String& name)
        { applyValues(values, "Preset " + name); };
        presets_.setValues(values());
        addAndMakeVisible(presets_);
    }

    // Fourteen knob columns in six panels.
    int preferredWidth() const override { return 14 * cellW + 6 * 2 * pad + 5 * gap + 2 * theme_.spacing.sm; }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, captionH = 18, cellW = 52, gap = 8, pad = 8;

    struct Section
    {
        const char* title;
        juce::Colour hue;
        juce::Rectangle<int> bounds;
    };

    std::array<juce::Component*, 6> displays() noexcept
    {
        return {&tine_, &hammer_, &tone_, &tremolo_, &phaser_, &chorus_};
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        room_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        wordmark_ = header.removeFromLeft(300);
        lights_ = header.removeFromRight(lightCount * 9 + 4).withSizeKeepingCentre(lightCount * 9 + 4, 12);
        header.removeFromRight(theme_.spacing.lg);
        presetBar_ = header.removeFromRight(260).withSizeKeepingCentre(260, theme_.sizes.controlCompact);
        presets_.setBounds(presetBar_);
        body.removeFromTop(4);

        sections_.clear();
        int x = body.getX();
        const auto panel = [&](const char* title,
                               juce::Colour hue,
                               int columns,
                               juce::Component& display,
                               const std::vector<Knob*>& knobs)
        {
            const int w = columns * cellW + 2 * pad;
            const juce::Rectangle<int> r(x, body.getY(), w, body.getHeight());
            sections_.push_back({title, hue, r});
            auto inner = r.reduced(pad);
            inner.removeFromTop(captionH);
            const int knobsH = 2 * knobCellHeight();
            display.setBounds(inner.removeFromTop(std::max(36, inner.getHeight() - knobsH - pad)));
            inner.removeFromTop(pad);
            layoutKnobs(inner.removeFromTop(knobsH), columns, knobs);
            x += w + gap;
        };
        panel("TINE", hues_.tine, 4, tine_, tineKnobs_);
        panel("HAMMER", hues_.hammer, 2, hammer_, hammerKnobs_);
        panel("TONE", hues_.tone, 2, tone_, toneKnobs_);
        panel("TREMOLO", hues_.tremolo, 2, tremolo_, tremoloKnobs_);
        panel("PHASER", hues_.phaser, 2, phaser_, phaserKnobs_);
        panel("CHORUS", hues_.chorus, 2, chorus_, chorusKnobs_);
        makeWordmark();
    }

    // ----- the room -------------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = room_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(
            c.canvas.darker(0.5f), 0.0f, bounds.getY(), c.surface0, 0.0f, bounds.getBottom(), false));
        g.fillRect(bounds);
        // The lamp: a low warm glow rising from the bottom edge, hotter at its heart.
        const juce::Point<float> lamp{bounds.getCentreX(), bounds.getBottom() + bounds.getHeight() * 0.15f};
        g.setGradientFill(juce::ColourGradient(hues_.tine.withAlpha(0.16f),
                                               lamp,
                                               hues_.tine.withAlpha(0.0f),
                                               {lamp.x + bounds.getWidth() * 0.55f, lamp.y},
                                               true));
        g.fillRect(bounds);
        g.setGradientFill(juce::ColourGradient(hues_.hammer.withAlpha(0.10f),
                                               lamp,
                                               hues_.hammer.withAlpha(0.0f),
                                               {lamp.x + bounds.getWidth() * 0.22f, lamp.y},
                                               true));
        g.fillRect(bounds);

        paintWordmark(g);
        paintLights(g);
        for (const auto& s : sections_)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1.withAlpha(0.72f));
            g.fillRoundedRectangle(r, theme_.radius.md);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.drawHorizontalLine(
                static_cast<int>(r.getY()) + 1, r.getX() + theme_.radius.md, r.getRight() - theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.md, 1.0f);
            auto caption = s.bounds.reduced(pad).removeFromTop(captionH - 4);
            g.setColour(s.hue);
            g.fillEllipse(caption.removeFromLeft(10).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
            caption.removeFromLeft(theme_.spacing.xs);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textSecondary);
            g.drawText(s.title, caption, juce::Justification::centredLeft);
        }
        g.setColour(c.surface1.withAlpha(0.8f));
        g.fillRoundedRectangle(presetBar_.toFloat(), presetBar_.getHeight() * 0.5f);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(presetBar_.toFloat().reduced(0.5f), presetBar_.getHeight() * 0.5f, 1.0f);
    }

    // The wordmark's outline, made once per layout.
    void makeWordmark()
    {
        const auto area = wordmark_.toFloat();
        const auto font = theme_.font(Theme::Face::ui, 26.0f, theme_.fonts.weight.regular);
        juce::GlyphArrangement glyphs;
        float x = area.getX() + 4.0f;
        baseline_ = area.getCentreY() + 4.0f;
        for (const auto ch : juce::String("LUMEN"))
        {
            juce::GlyphArrangement one;
            one.addLineOfText(font, juce::String::charToString(ch), x, baseline_);
            const float w = one.getBoundingBox(0, -1, true).getWidth();
            glyphs.addGlyphArrangement(one);
            x += w + 7.0f;
        }
        wordmarkPath_.clear();
        glyphs.createPath(wordmarkPath_);
    }

    void paintWordmark(juce::Graphics& g) const
    {
        const auto box = wordmarkPath_.getBounds();
        g.setGradientFill(
            juce::ColourGradient(hues_.tine, box.getX(), 0.0f, hues_.hammer, box.getRight(), 0.0f, false));
        g.fillPath(wordmarkPath_);
        const auto font = theme_.font(Theme::Face::ui, theme_.fonts.size.micro, theme_.fonts.weight.medium);
        const auto text = juce::String::fromUTF8("ELECTRIC PIANO  \xc2\xb7  NORTHLIGHT");
        g.setFont(font);
        g.setColour(theme_.colours.textTertiary);
        g.drawText(text,
                   juce::Rectangle<float>(box.getRight() + 12.0f,
                                          baseline_ - 10.0f,
                                          juce::GlyphArrangement::getStringWidth(font, text) + 4.0f,
                                          12.0f),
                   juce::Justification::centredLeft);
    }

    void paintLights(juce::Graphics& g) const
    {
        const int allowed = std::max(1, static_cast<int>(std::lround(param(lu::voices))));
        const int lit = activeVoices_ <= 0
                            ? 0
                            : std::min(lightCount, (activeVoices_ * lightCount + allowed - 1) / allowed);
        for (int i = 0; i < lightCount; ++i)
        {
            const auto r =
                juce::Rectangle<float>(6.0f, 6.0f)
                    .withCentre({static_cast<float>(lights_.getX()) + 4.0f + static_cast<float>(i) * 9.0f,
                                 static_cast<float>(lights_.getCentreY())});
            if (i < lit)
            {
                g.setColour(hues_.tine.withAlpha(0.35f));
                g.fillEllipse(r.expanded(3.0f));
            }
            g.setColour(i < lit ? hues_.tine : theme_.colours.surface3);
            g.fillEllipse(r);
        }
    }

    // ----- live -------------------------------------------------------------------------------------

    void dimSyncedRate()
    {
        tremoloKnobs_.front()->setAlpha(plugins::sync::choiceOf(param(lu::tremoloSync)) > 0 ? 0.5f : 1.0f);
    }

    void paramsChanged() override
    {
        presets_.setValues(values());
        dimSyncedRate();
        tremoloKnobs_.front()->repaint();
        for (auto* c : displays())
            c->repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        // The newest frame carries the voice count and the tremolo's wave (0..1).
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            const int voices = static_cast<int>(std::lround(last.reduction));
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(lights_.expanded(4));
            }
            tremoloWave_ = juce::jlimit(0.0f, 1.0f, last.extra);
        }
        const float level = juce::jlimit(0.0f, 1.0f, (outputDb() + 48.0f) / 48.0f);
        const bool still = theme_.motion.reduceMotion || (activeVoices_ == 0 && tine_.swing < 0.01f);
        if (still)
        {
            if (tine_.swing != 0.0f)
            {
                tine_.swing = 0.0f;
                tine_.repaint();
            }
            return;
        }
        since_ += elapsedMs;
        if (since_ < 33.0)
            return;
        const float seconds = static_cast<float>(since_ * 0.001);
        since_ = 0.0;
        // The tine, slowed to a few swings a second, as wide as the output is loud.
        tine_.swing += (level - tine_.swing) * 0.3f;
        tine_.phase = std::fmod(tine_.phase + seconds * juce::MathConstants<float>::twoPi * 3.0f,
                                juce::MathConstants<float>::twoPi);
        tine_.repaint();
        if (std::lround(param(lu::tremoloMode)) != 0 && std::abs(tremoloWave_ - tremolo_.live) > 0.004f)
        {
            tremolo_.live = tremoloWave_;
            tremolo_.repaint();
        }
        if (param(lu::chorusMix) > 0.0f)
        {
            chorus_.time += seconds;
            chorus_.repaint();
        }
    }

    Hues hues_;
    TineDisplay tine_;
    HammerDisplay hammer_;
    ToneDisplay tone_;
    TremoloDisplay tremolo_;
    PhaserDisplay phaser_;
    ChorusDisplay chorus_;
    std::vector<Knob*> tineKnobs_, hammerKnobs_, toneKnobs_, tremoloKnobs_, phaserKnobs_, chorusKnobs_;
    PresetBar presets_{theme_, lu::type, true};
    std::vector<Section> sections_;
    juce::Rectangle<int> room_, wordmark_, lights_, presetBar_;
    juce::Path wordmarkPath_;
    float baseline_ = 0.0f;
    int activeVoices_ = 0;
    float tremoloWave_ = 0.5f;
    double since_ = 0.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createLumenEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<LumenCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
