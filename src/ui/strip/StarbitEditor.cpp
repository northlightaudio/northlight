// Starbit's editor (07-plugins.md 7.4e). A chiptune synth's face is pixel art:
// a blocky wordmark in a run of palette colours, square stars that blink in
// whole steps, windows with notched corners, and displays drawn on a grid of
// cells with scanlines over them. The wave display is also the wave editor:
// with the Wave shape chosen, click and drag to set its 32 steps. The knobs
// stay the app's own, so the synth still plays like the rest of Northlight.

#include "ui/strip/StarbitEditor.h"

#include "plugins/BeatSync.h"
#include "plugins/Starbit.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <array>
#include <cmath>

namespace northlight::ui::strip
{

namespace
{
namespace sb = plugins::starbit;

struct Hues
{
    juce::Colour wave, envelope, arp, pitch, lofi;
    explicit Hues(const Theme& t)
        : wave(paletteColour(t, "Lime")), envelope(paletteColour(t, "Amber")),
          arp(paletteColour(t, "Magenta")), pitch(paletteColour(t, "Sky")), lofi(paletteColour(t, "Coral"))
    {
    }
};

// A pixel window: a panel with its corners notched a pixel step in, and a
// two-pixel border.
juce::Path notched(juce::Rectangle<float> r, float step)
{
    juce::Path p;
    p.startNewSubPath(r.getX() + step, r.getY());
    p.lineTo(r.getRight() - step, r.getY());
    p.lineTo(r.getRight() - step, r.getY() + step);
    p.lineTo(r.getRight(), r.getY() + step);
    p.lineTo(r.getRight(), r.getBottom() - step);
    p.lineTo(r.getRight() - step, r.getBottom() - step);
    p.lineTo(r.getRight() - step, r.getBottom());
    p.lineTo(r.getX() + step, r.getBottom());
    p.lineTo(r.getX() + step, r.getBottom() - step);
    p.lineTo(r.getX(), r.getBottom() - step);
    p.lineTo(r.getX(), r.getY() + step);
    p.lineTo(r.getX() + step, r.getY() + step);
    p.closeSubPath();
    return p;
}

// A display screen in pixel style: the well's colour, a notched outline and
// scanlines over whatever is drawn in it (call after the content).
void pixelScreen(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> r)
{
    g.setColour(theme.material.enabled ? theme.material.meterWell : theme.colours.canvas);
    g.fillPath(notched(r, 3.0f));
}
void pixelScreenOver(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> r)
{
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(notched(r, 3.0f));
    g.setColour(theme.colours.canvas.withAlpha(0.28f));
    for (float y = r.getY() + 1.0f; y < r.getBottom(); y += 3.0f)
        g.fillRect(r.getX(), y, r.getWidth(), 1.0f);
    g.setColour(theme.colours.lineStrong);
    g.strokePath(notched(r.reduced(1.0f), 3.0f), juce::PathStrokeType(2.0f));
}

// One lit cell, with a highlight on its top-left like a sprite.
void cell(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour(colour);
    g.fillRect(r);
    g.setColour(colour.brighter(0.5f));
    g.fillRect(r.withHeight(1.0f));
}

// ----- the wordmark's pixel font ------------------------------------------------------------------

const char* glyph(char c)
{
    switch (c) // 5 by 7, rows top to bottom
    {
        case 'S':
            return ".####"
                   "#...."
                   "#...."
                   ".###."
                   "....#"
                   "....#"
                   "####.";
        case 'T':
            return "#####"
                   "..#.."
                   "..#.."
                   "..#.."
                   "..#.."
                   "..#.."
                   "..#..";
        case 'A':
            return ".###."
                   "#...#"
                   "#...#"
                   "#####"
                   "#...#"
                   "#...#"
                   "#...#";
        case 'R':
            return "####."
                   "#...#"
                   "#...#"
                   "####."
                   "#.#.."
                   "#..#."
                   "#...#";
        case 'B':
            return "####."
                   "#...#"
                   "#...#"
                   "####."
                   "#...#"
                   "#...#"
                   "####.";
        case 'I':
            return "#####"
                   "..#.."
                   "..#.."
                   "..#.."
                   "..#.."
                   "..#.."
                   "#####";
        default:
            return "...................................";
    }
}

// ----- displays -----------------------------------------------------------------------------------

// The wave: one cycle as 32 columns of 16 levels. With the Wave shape it is
// an editor: click and drag across to set each step.
class WaveDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveDisplay(ModuleCard& card, juce::Colour hue)
        : card_(card), hue_(hue), shape_(card.theme(), hue), duty_(card.theme(), hue),
          noise_(card.theme(), hue)
    {
        shape_.choices = {"Pulse", "Triangle", "Saw", "Noise", "Wave"};
        shape_.setTooltip("Wave");
        shape_.onChoose = [this](int i) { choose(sb::wave, i, "Wave"); };
        duty_.choices = {"12.5%", "25%", "50%", "75%"};
        duty_.setTooltip("Duty cycle");
        duty_.onChoose = [this](int i) { choose(sb::duty, i, "Duty"); };
        noise_.choices = {"Long", "Short"};
        noise_.setTooltip("Noise: long (hiss) or short (metallic)");
        noise_.onChoose = [this](int i) { choose(sb::noiseMode, i, "Noise mode"); };
        for (auto* c : {&shape_, &duty_, &noise_})
            addChildComponent(c);
        shape_.setVisible(true);
    }
    void resized() override
    {
        auto top = getLocalBounds().reduced(6).removeFromTop(16);
        shape_.setBounds(top.removeFromLeft(shape_.preferredWidth()));
        top.removeFromLeft(4);
        duty_.setBounds(top.removeFromLeft(duty_.preferredWidth()));
        noise_.setBounds(duty_.getBounds().withWidth(noise_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        pixelScreen(g, theme, b);
        const int kind = juce::jlimit(0, 4, static_cast<int>(std::lround(card_.param(sb::wave))));
        shape_.selected = kind;
        duty_.selected = juce::jlimit(0, 3, static_cast<int>(std::lround(card_.param(sb::duty))));
        noise_.selected = card_.param(sb::noiseMode) >= 0.5f ? 1 : 0;
        duty_.setVisible(kind == sb::pulse);
        noise_.setVisible(kind == sb::noise);
        setTooltip(kind == sb::custom ? "Click and drag to draw the wave's 32 steps" : "");
        setMouseCursor(kind == sb::custom ? juce::MouseCursor::CrosshairCursor
                                          : juce::MouseCursor::NormalCursor);
        const auto grid = area();
        const float cw = grid.getWidth() / sb::waveSteps, ch = grid.getHeight() / 16.0f;
        const auto shape = sb::waveShape(card_.params());
        // The grid's faint cells, then each step's column from the middle and its top cell.
        g.setColour(theme.colours.gridBeat.withAlpha(0.5f));
        for (int x = 0; x <= sb::waveSteps; x += 4)
            g.fillRect(grid.getX() + x * cw, grid.getY(), 1.0f, grid.getHeight());
        g.fillRect(grid.getX(), grid.getY() + 8.0f * ch, grid.getWidth(), 1.0f);
        for (int s = 0; s < sb::waveSteps; ++s)
        {
            const int level = shape[static_cast<size_t>(s)];
            const float x = grid.getX() + s * cw;
            const int from = std::min(level, 8), to = std::max(level, 8);
            for (int l = from; l <= to; ++l)
            {
                const auto r = juce::Rectangle<float>(
                    x + 0.5f, grid.getBottom() - (l + 1) * ch + 0.5f, cw - 1.0f, ch - 1.0f);
                if (l == level)
                    cell(g, r, hue_);
                else
                    cell(g, r, hue_.withAlpha(0.28f));
            }
        }
        pixelScreenOver(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (std::lround(card_.param(sb::wave)) != sb::custom)
            return;
        drawing_ = true;
        card_.beginGesture("Draw wave");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!drawing_)
            return;
        const auto grid = area();
        const int s =
            juce::jlimit(0,
                         sb::waveSteps - 1,
                         static_cast<int>((e.position.x - grid.getX()) / grid.getWidth() * sb::waveSteps));
        const int level = juce::jlimit(
            0, 15, static_cast<int>((grid.getBottom() - e.position.y) / grid.getHeight() * 16.0f));
        // Fill the steps between this drag event and the last, so a fast stroke leaves no gaps.
        const int from = last_ >= 0 ? std::min(last_, s) : s, to = last_ >= 0 ? std::max(last_, s) : s;
        for (int i = from; i <= to; ++i)
            card_.setParam(sb::firstWaveStep + i, static_cast<float>(level));
        last_ = s;
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drawing_)
            card_.endGesture();
        drawing_ = false;
        last_ = -1;
    }

private:
    juce::Rectangle<float> area() const
    {
        return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(18.0f);
    }
    void choose(int param, int value, const char* name)
    {
        card_.beginGesture(name);
        card_.setParam(param, static_cast<float>(value));
        card_.endGesture();
    }

    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice shape_, duty_, noise_;
    bool drawing_ = false;
    int last_ = -1;
};

// The volume envelope in 16 steps over time: bars rising, falling to the
// sustain, holding, then the release.
class EnvelopeDisplay final : public juce::Component
{
public:
    EnvelopeDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        pixelScreen(g, theme, b);
        const auto grid = b.reduced(8.0f, 8.0f);
        const int columns = 24;
        const float cw = grid.getWidth() / columns, ch = grid.getHeight() / 16.0f;
        // A second and a half across: attack, decay, hold, release in proportion.
        const float a = card_.param(sb::attack), d = card_.param(sb::decay), r = card_.param(sb::release);
        const float s = card_.param(sb::sustain);
        const float hold = 300.0f, total = a + d + hold + r;
        for (int c = 0; c < columns; ++c)
        {
            const float t = (c + 0.5f) / columns * total;
            float level;
            if (t < a)
                level = t / std::max(1.0f, a);
            else if (t < a + d)
                level = 1.0f - (1.0f - s) * (t - a) / std::max(1.0f, d);
            else if (t < a + d + hold)
                level = s;
            else
                level = s * (1.0f - (t - a - d - hold) / std::max(1.0f, r));
            const int steps = static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, level) * 15.0f));
            for (int l = 0; l < steps; ++l)
                cell(g,
                     juce::Rectangle<float>(grid.getX() + c * cw + 0.5f,
                                            grid.getBottom() - (l + 1) * ch + 0.5f,
                                            cw - 1.0f,
                                            ch - 1.0f),
                     l == steps - 1 ? hue_ : hue_.withAlpha(0.35f));
        }
        pixelScreenOver(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// The arpeggio: its steps as blocks as high as their offsets, the step
// playing now lit. Mode and voices in the corner chips.
class ArpDisplay final : public juce::Component
{
public:
    ArpDisplay(ModuleCard& card, juce::Colour hue)
        : card_(card), hue_(hue), mode_(card.theme(), hue), voices_(card.theme(), hue)
    {
        mode_.choices = {"Off", "Chord", "Octave", "Major", "Minor", "Power", "Maj7", "Min7"};
        mode_.setTooltip("Arpeggio: Chord cycles the notes you hold");
        mode_.onChoose = [this](int i) { choose(sb::arp, i, "Arpeggio"); };
        voices_.choices = {"Mono", "Poly"};
        voices_.setTooltip("Voices");
        voices_.onChoose = [this](int i) { choose(sb::voices, i, "Voices"); };
        addAndMakeVisible(mode_);
        addAndMakeVisible(voices_);
    }
    int liveStep = -1;
    void resized() override
    {
        auto top = getLocalBounds().reduced(6).removeFromTop(16);
        mode_.setBounds(top.removeFromLeft(mode_.preferredWidth()));
        voices_.setBounds(top.removeFromRight(voices_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        pixelScreen(g, theme, b);
        const int shape = juce::jlimit(0, 7, static_cast<int>(std::lround(card_.param(sb::arp))));
        mode_.selected = shape;
        voices_.selected = card_.param(sb::voices) >= 0.5f ? 1 : 0;
        auto grid = b.reduced(8.0f, 6.0f).withTrimmedTop(20.0f);
        std::vector<int> steps = sb::arpSteps(shape);
        const bool chord = shape == sb::arpChord;
        if (chord)
            steps = {0, 4, 7}; // stands for the notes held
        if (steps.empty())
            steps = {0};
        const float cw = grid.getWidth() / 8.0f, ch = grid.getHeight() / 13.0f;
        for (int i = 0; i < 8; ++i)
        {
            const int offset = steps[static_cast<size_t>(i) % steps.size()];
            const bool now = liveStep >= 0 && shape != sb::arpOff && liveStep % 8 == i;
            const auto colour = shape == sb::arpOff ? hue_.withAlpha(0.35f)
                                : now               ? theme.colours.textPrimary
                                                    : hue_;
            const int top = juce::jlimit(0, 12, offset);
            cell(g,
                 juce::Rectangle<float>(grid.getX() + i * cw + 1.0f,
                                        grid.getBottom() - (top + 1) * ch + 0.5f,
                                        cw - 2.0f,
                                        ch - 1.0f),
                 colour);
            for (int l = 0; l < top; ++l)
                cell(g,
                     juce::Rectangle<float>(grid.getX() + i * cw + 1.0f,
                                            grid.getBottom() - (l + 1) * ch + 0.5f,
                                            cw - 2.0f,
                                            ch - 1.0f),
                     colour.withAlpha(0.25f));
        }
        if (chord)
        {
            g.setFont(
                theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.semibold));
            g.setColour(theme.colours.textTertiary);
            g.drawText("HELD NOTES", grid.removeFromTop(12.0f), juce::Justification::centredRight);
        }
        pixelScreenOver(g, theme, b);
    }

private:
    void choose(int param, int value, const char* name)
    {
        card_.beginGesture(name);
        card_.setParam(param, static_cast<float>(value));
        card_.endGesture();
    }

    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice mode_, voices_;
};

// The pitch over a note's first second: the sweep into it, then the vibrato
// after its delay, as a run of cells.
class PitchDisplay final : public juce::Component
{
public:
    PitchDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        pixelScreen(g, theme, b);
        const auto grid = b.reduced(8.0f, 8.0f);
        const int columns = 36, rows = 15;
        const float cw = grid.getWidth() / columns, ch = grid.getHeight() / rows;
        const float sweep = card_.param(sb::sweep), sweepMs = card_.param(sb::sweepTime);
        const float depth = card_.param(sb::vibratoDepth), rateHz = card_.param(sb::vibratoRate);
        const float delayMs = card_.param(sb::vibratoDelay);
        const float range = std::max(2.0f, std::max(std::abs(sweep), depth * 2.0f));
        g.setColour(theme.colours.gridBeat);
        g.fillRect(grid.getX(), grid.getCentreY(), grid.getWidth(), 1.0f);
        for (int c = 0; c < columns; ++c)
        {
            const float ms = (c + 0.5f) / columns * 1000.0f;
            float semis = ms < sweepMs ? sweep * (1.0f - ms / std::max(1.0f, sweepMs)) : 0.0f;
            if (ms > delayMs)
                semis +=
                    depth * std::sin(juce::MathConstants<float>::twoPi * rateHz * (ms - delayMs) * 0.001f);
            const int row = juce::jlimit(
                0, rows - 1, static_cast<int>(std::round((semis / range * 0.5f + 0.5f) * (rows - 1))));
            cell(g,
                 juce::Rectangle<float>(grid.getX() + c * cw + 0.5f,
                                        grid.getBottom() - (row + 1) * ch + 0.5f,
                                        cw - 1.0f,
                                        ch - 1.0f),
                 hue_);
        }
        pixelScreenOver(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// The lo-fi end: a sine as the bits and the rate leave it, staircase and all.
class LofiDisplay final : public juce::Component
{
public:
    LofiDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        pixelScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 10.0f);
        const float levels = std::exp2(std::round(card_.param(sb::bits))) * 0.5f;
        // The hold: as many steps across a cycle as the rate allows (at 440 Hz,
        // shown over two cycles), at least a few.
        const int holds = juce::jlimit(4, 96, static_cast<int>(card_.param(sb::rate) / 440.0f * 2.0f));
        juce::Path stairs;
        for (int i = 0; i <= holds; ++i)
        {
            const float t = static_cast<float>(i) / holds;
            const float v =
                std::round(std::sin(t * juce::MathConstants<float>::twoPi * 2.0f) * levels) / levels;
            const float x0 = p.getX() + t * p.getWidth(),
                        x1 = p.getX() + std::min(1.0f, t + 1.0f / holds) * p.getWidth();
            const float y = p.getCentreY() - v * p.getHeight() * 0.45f;
            if (i == 0)
                stairs.startNewSubPath(x0, y);
            else
                stairs.lineTo(x0, y);
            stairs.lineTo(x1, y);
        }
        g.setColour(hue_.withAlpha(0.3f));
        g.strokePath(stairs,
                     juce::PathStrokeType(4.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        g.setColour(hue_);
        g.strokePath(stairs,
                     juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        g.setFont(
            theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.semibold));
        g.setColour(theme.colours.textTertiary);
        g.drawText(juce::String(static_cast<int>(std::round(card_.param(sb::bits)))) + "-BIT",
                   b.reduced(8.0f, 4.0f),
                   juce::Justification::topRight);
        pixelScreenOver(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// ----- the editor ---------------------------------------------------------------------------------

class StarbitCard final : public ModuleCard
{
public:
    StarbitCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), wave_(*this, hues_.wave),
          envelope_(*this, hues_.envelope), arp_(*this, hues_.arp), pitch_(*this, hues_.pitch),
          lofi_(*this, hues_.lofi), arpSync_(theme, hues_.arp)
    {
        for (auto* c : std::initializer_list<juce::Component*>{&wave_, &envelope_, &arp_, &pitch_, &lofi_})
            addAndMakeVisible(c);
        const auto percent = [](double v)
        { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; };
        const auto integer = [](const char* suffix)
        {
            return [suffix](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                return (n > 0 ? "+" : "") + juce::String(n) + suffix;
            };
        };
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
        waveKnobs_ = {knob(sb::dutySweep, "Duty sweep", hues_.wave, percent),
                      knob(sb::octave, "Octave", hues_.wave, integer("")),
                      knob(sb::coarse, "Coarse", hues_.wave, integer(" st")),
                      knob(sb::velocity, "Velocity", hues_.wave, percent)};
        envelopeKnobs_ = {knob(sb::attack, "Attack", hues_.envelope),
                          knob(sb::decay, "Decay", hues_.envelope),
                          knob(sb::sustain,
                               "Sustain",
                               hues_.envelope,
                               [](double v)
                               { return juce::String(static_cast<int>(std::lround(v * 15.0))) + "/15"; }),
                          knob(sb::release, "Release", hues_.envelope)};
        arpKnobs_ = {knob(sb::arpRate,
                          "Rate",
                          hues_.arp,
                          [this](double v)
                          {
                              // Synced, the rate is the note value a step.
                              if (const int c = plugins::sync::choiceOf(param(sb::arpSync)); c > 0)
                                  return juce::String(plugins::sync::noteValues[static_cast<size_t>(c)].name);
                              return juce::String(static_cast<int>(std::lround(v))) + " Hz";
                          }),
                     knob(sb::glide, "Glide", hues_.arp)};
        for (const auto& c : plugins::sync::choices())
            arpSync_.choices.emplace_back(c);
        arpSync_.choices.front() = "Free";
        arpSync_.setTooltip("Sync the arpeggio's steps to the project tempo");
        arpSync_.onChoose = [this](int i)
        {
            beginGesture("Arp sync");
            setParam(sb::arpSync, static_cast<float>(i));
            endGesture();
        };
        arpSync_.selected = plugins::sync::choiceOf(param(sb::arpSync));
        arpKnobs_.front()->setAlpha(arpSync_.selected > 0 ? 0.5f : 1.0f);
        addAndMakeVisible(arpSync_);
        pitchKnobs_ = {knob(sb::vibratoDepth,
                            "Vibrato",
                            hues_.pitch,
                            [](double v) { return juce::String(v, 2) + " st"; }),
                       knob(sb::vibratoRate,
                            "Vib rate",
                            hues_.pitch,
                            [](double v) { return juce::String(v, 1) + " Hz"; }),
                       knob(sb::vibratoDelay, "Vib delay", hues_.pitch),
                       knob(sb::sweep, "Sweep", hues_.pitch, integer(" st")),
                       knob(sb::sweepTime, "Sweep time", hues_.pitch)};
        lofiKnobs_ = {knob(sb::bits,
                           "Bits",
                           hues_.lofi,
                           [](double v) { return juce::String(static_cast<int>(std::lround(v))); }),
                      knob(sb::rate,
                           "Rate",
                           hues_.lofi,
                           [](double v)
                           {
                               return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz"
                                                  : juce::String(static_cast<int>(v)) + " Hz";
                           }),
                      knob(sb::echo, "Echo", hues_.lofi, percent),
                      knob(sb::level, "Level", hues_.lofi)};
        for (auto* b : {&previous_, &next_, &presetName_})
        {
            b->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
            addAndMakeVisible(b);
        }
        previous_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        presetName_.setTooltip("Presets");
        previous_.onClick = [this] { stepPreset(-1); };
        next_.onClick = [this] { stepPreset(1); };
        presetName_.onClick = [this] { showPresets(); };
        presetName_.setButtonText(currentPresetName());
    }

    // Thirteen knob columns in five windows.
    int preferredWidth() const override { return 13 * cellW + 5 * 2 * pad + 4 * gap + 2 * theme_.spacing.sm; }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, captionH = 18, cellW = 56, gap = 8, pad = 8, pixel = 4;

    struct Section
    {
        const char* title;
        juce::Colour hue;
        juce::Rectangle<int> bounds;
    };

    void layoutBody(juce::Rectangle<int> body) override
    {
        sky_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        wordmark_ = header.removeFromLeft(300);
        voices_ =
            header.removeFromRight(sb::polyVoices * 12 + 4).withSizeKeepingCentre(sb::polyVoices * 12 + 4, 8);
        header.removeFromRight(theme_.spacing.lg);
        auto preset = header.removeFromRight(240).withSizeKeepingCentre(240, theme_.sizes.controlCompact);
        presetBar_ = preset;
        previous_.setBounds(preset.removeFromLeft(theme_.sizes.controlCompact));
        next_.setBounds(preset.removeFromRight(theme_.sizes.controlCompact));
        presetName_.setBounds(preset.reduced(2, 0));
        body.removeFromTop(4);
        sections_.clear();
        int x = body.getX();
        const auto panel = [&](const char* title,
                               juce::Colour hue,
                               int columns,
                               int rows,
                               juce::Component& display,
                               const std::vector<Knob*>& knobs) -> juce::Rectangle<int>
        {
            const int w = columns * cellW + 2 * pad;
            const juce::Rectangle<int> r(x, body.getY(), w, body.getHeight());
            sections_.push_back({title, hue, r});
            auto inner = r.reduced(pad);
            inner.removeFromTop(captionH);
            const int knobsH = rows * knobCellHeight();
            display.setBounds(inner.removeFromTop(std::max(36, inner.getHeight() - knobsH - pad)));
            inner.removeFromTop(pad);
            const auto area = inner.removeFromTop(knobsH);
            layoutKnobs(area, columns, knobs);
            x += w + gap;
            return area;
        };
        panel("WAVE", hues_.wave, 4, 1, wave_, waveKnobs_);
        panel("VOLUME", hues_.envelope, 2, 2, envelope_, envelopeKnobs_);
        {
            // The arp's second row: the sync chip, under its caption.
            auto row = panel("ARP", hues_.arp, 2, 2, arp_, arpKnobs_).withTrimmedTop(knobCellHeight());
            auto chip = row.withSizeKeepingCentre(row.getWidth(), 16 + captionH);
            arpSyncCaption_ = chip.removeFromTop(captionH);
            arpSync_.setBounds(
                chip.withSizeKeepingCentre(std::min(chip.getWidth(), arpSync_.preferredWidth()), 16));
        }
        panel("PITCH", hues_.pitch, 3, 2, pitch_, pitchKnobs_);
        panel("LO-FI", hues_.lofi, 2, 2, lofi_, lofiKnobs_);
    }

    // ----- drawing ------------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = sky_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(notched(bounds, 6.0f));
        g.setColour(c.canvas.darker(0.6f));
        g.fillRect(bounds);
        paintStars(g, bounds);
        paintWordmark(g);
        paintVoices(g);
        for (const auto& s : sections_)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1.withAlpha(0.92f));
            g.fillPath(notched(r, 4.0f));
            g.setColour(c.lineStrong);
            g.strokePath(notched(r.reduced(1.0f), 4.0f), juce::PathStrokeType(2.0f));
            // A square in the hue, then the caption in pixel-ish caps.
            auto caption = s.bounds.reduced(pad).removeFromTop(captionH - 4);
            cell(g, caption.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f), s.hue);
            caption.removeFromLeft(theme_.spacing.sm);
            g.setFont(
                theme_.font(Theme::Face::mono, theme_.fonts.size.caption, theme_.fonts.weight.semibold));
            g.setColour(c.textSecondary);
            g.drawText(s.title, caption, juce::Justification::centredLeft);
        }
        g.setFont(
            theme_.font(Theme::Face::mono, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.semibold));
        g.setColour(c.textTertiary);
        g.drawText("SYNC", arpSyncCaption_, juce::Justification::centred);
        g.setColour(c.surface1);
        g.fillPath(notched(presetBar_.toFloat(), 3.0f));
        g.setColour(c.lineStrong);
        g.strokePath(notched(presetBar_.toFloat().reduced(1.0f), 3.0f), juce::PathStrokeType(2.0f));
    }

    // Square stars, blinking in whole steps.
    void paintStars(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        for (int i = 0; i < 40; ++i)
        {
            const float fx = std::fmod(static_cast<float>(i) * 0.6180339f + 0.31f, 1.0f);
            const float fy = std::fmod(static_cast<float>(i) * 0.7548777f + 0.11f, 1.0f);
            const bool on = (static_cast<int>(time_ * 4.0f) + i * 3) % 7 != 0;
            if (!on)
                continue;
            const float x = std::floor((b.getX() + fx * b.getWidth()) / pixel) * pixel;
            const float y = std::floor((b.getY() + fy * b.getHeight()) / pixel) * pixel;
            const float size = i % 5 == 0 ? static_cast<float>(pixel) : pixel * 0.5f;
            g.setColour(theme_.colours.textPrimary.withAlpha(i % 3 == 0 ? 0.55f : 0.25f));
            g.fillRect(x, y, size, size);
        }
    }

    // STARBIT in a 5 by 7 pixel font, each letter its own palette colour, with
    // a drop shadow a pixel down and right.
    void paintWordmark(juce::Graphics& g) const
    {
        static const char* hues[] = {"Coral", "Amber", "Lime", "Mint", "Sky", "Violet", "Magenta"};
        const float px = 4.0f;
        float x = static_cast<float>(wordmark_.getX()) + 4.0f;
        const float y = static_cast<float>(wordmark_.getCentreY()) - 3.5f * px;
        const juce::String word = "STARBIT";
        for (int i = 0; i < word.length(); ++i)
        {
            const char* rows = glyph(static_cast<char>(word[i]));
            const auto colour = paletteColour(theme_, hues[i % 7]);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (rows[row * 5 + col] == '#')
                    {
                        const auto r = juce::Rectangle<float>(x + col * px, y + row * px, px, px);
                        g.setColour(theme_.colours.canvas);
                        g.fillRect(r.translated(px * 0.5f, px * 0.5f));
                        g.setColour(colour);
                        g.fillRect(r);
                    }
            x += 6.0f * px;
        }
        g.setFont(
            theme_.font(Theme::Face::mono, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.semibold));
        g.setColour(theme_.colours.textTertiary);
        g.drawText(juce::String::fromUTF8("CHIP SYNTH  \xc2\xb7  NORTHLIGHT"),
                   juce::Rectangle<float>(x + 6.0f, y + 2.0f * px, 160.0f, 14.0f),
                   juce::Justification::centredLeft);
    }

    // Four voice lights as square pixels.
    void paintVoices(juce::Graphics& g) const
    {
        for (int i = 0; i < sb::polyVoices; ++i)
        {
            const auto r =
                juce::Rectangle<float>(8.0f, 8.0f)
                    .withPosition(static_cast<float>(voices_.getX()) + 2.0f + static_cast<float>(i) * 12.0f,
                                  static_cast<float>(voices_.getY()));
            const bool lit = i < activeVoices_;
            if (lit)
                cell(g, r, hues_.wave);
            else
            {
                g.setColour(theme_.colours.surface3);
                g.fillRect(r);
            }
        }
    }

    // ----- presets --------------------------------------------------------------------------------

    juce::String currentPresetName() const
    {
        for (const auto& p : sb::presets())
        {
            bool same = p.values.size() <= params_.size();
            for (size_t i = 0; same && i < p.values.size(); ++i)
                same = std::abs(p.values[i] - params_[i]) < 1.0e-4f;
            if (same)
                return juce::String(p.name);
        }
        return "Custom";
    }
    void applyPreset(const sb::Preset& preset)
    {
        beginGesture("Preset " + juce::String(preset.name));
        for (size_t i = 0; i < preset.values.size(); ++i)
            setParam(static_cast<int>(i), preset.values[i]);
        endGesture();
        presetName_.setButtonText(currentPresetName());
    }
    void stepPreset(int direction)
    {
        const auto& list = sb::presets();
        int at = -1;
        const auto current = currentPresetName();
        for (size_t i = 0; i < list.size(); ++i)
            if (juce::String(list[i].name) == current)
                at = static_cast<int>(i);
        const int n = static_cast<int>(list.size());
        applyPreset(
            list[static_cast<size_t>(at < 0 ? (direction > 0 ? 0 : n - 1) : (at + direction + n) % n)]);
    }
    void showPresets()
    {
        juce::PopupMenu menu;
        const auto current = currentPresetName();
        for (size_t i = 0; i < sb::presets().size(); ++i)
            menu.addItem(static_cast<int>(i) + 1,
                         juce::String(sb::presets()[i].name),
                         true,
                         current == juce::String(sb::presets()[i].name));
        juce::Component::SafePointer<StarbitCard> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetName_),
                           [safe](int r)
                           {
                               if (safe != nullptr && r > 0 && static_cast<size_t>(r) <= sb::presets().size())
                                   safe->applyPreset(sb::presets()[static_cast<size_t>(r - 1)]);
                           });
    }

    // ----- live -----------------------------------------------------------------------------------

    void paramsChanged() override
    {
        presetName_.setButtonText(currentPresetName());
        arpSync_.selected = plugins::sync::choiceOf(param(sb::arpSync));
        arpSync_.repaint();
        arpKnobs_.front()->setAlpha(arpSync_.selected > 0 ? 0.5f : 1.0f);
        arpKnobs_.front()->repaint();
        for (auto* c : std::initializer_list<juce::Component*>{&wave_, &envelope_, &arp_, &pitch_, &lofi_})
            c->repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            const int voices = static_cast<int>(std::lround(last.reduction));
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(voices_.expanded(4));
            }
            const int step = voices > 0 ? static_cast<int>(last.extra * 8.0f) : -1;
            if (step != arp_.liveStep)
            {
                arp_.liveStep = step;
                arp_.repaint();
            }
        }
        // The stars blink a few times a second while it plays (chunky on purpose).
        if (theme_.motion.reduceMotion || activeVoices_ == 0)
            return;
        sinceSky_ += elapsedMs;
        if (sinceSky_ >= 125.0)
        {
            time_ += static_cast<float>(sinceSky_ * 0.001);
            sinceSky_ = 0.0;
            repaint(sky_.withHeight(headerH + 8));
        }
    }

    Hues hues_;
    WaveDisplay wave_;
    EnvelopeDisplay envelope_;
    ArpDisplay arp_;
    PitchDisplay pitch_;
    LofiDisplay lofi_;
    std::vector<Knob*> waveKnobs_, envelopeKnobs_, arpKnobs_, pitchKnobs_, lofiKnobs_;
    juce::TextButton previous_{juce::String::fromUTF8("\xe2\x80\xb9")},
        next_{juce::String::fromUTF8("\xe2\x80\xba")};
    juce::TextButton presetName_;
    std::vector<Section> sections_;
    juce::Rectangle<int> sky_, wordmark_, voices_, presetBar_, arpSyncCaption_;
    CornerChoice arpSync_;
    int activeVoices_ = 0;
    float time_ = 0.0f;
    double sinceSky_ = 0.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createStarbitEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<StarbitCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
