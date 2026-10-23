// The tape echo's card (screens/plugins.md 7.2a): the machine itself, live. The
// tape loop runs round its rollers at the motor's speed past the record head and
// the four playback heads (the active ones lit, each with its time); what the
// record head writes is drawn on the tape as a band as loud as it was and
// travels round, so each echo can be watched reaching the heads; the heads'
// feedback runs back to the record head in a line that turns to the warning
// colour as the loop nears self-oscillation. Inside the loop, the motor (head
// 1's time as it glides, or its note value, with the sync chip), the loop gain
// and the tape's saturation. Then the controls in five groups: Time, Heads and
// Feedback; the loop's tone; the tape; the stereo field; the output.

#include "plugins/BeatSync.h"
#include "plugins/TapeEcho.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::ui::strip
{

namespace te = plugins::tapeecho;

namespace
{
constexpr double frameSeconds = engine::ProcessorFeedback::frameSeconds;
// The loop gain from which the feedback reads as a warning (the edge of
// oscillation is 1).
constexpr float warnLoopGain = 0.85f;
// The saturation bar's range.
constexpr float saturationRangeDb = 12.0f;

// What the card works out once per tick for its display.
struct Machine
{
    double motorSeconds = 0.18;  // head 1's delay as the motor runs now
    double targetSeconds = 0.18; // where the motor is going
    float loopGain = 0.0f;
    std::uint8_t mask = 0;
    bool synced = false, clamped = false;
};

juce::Colour loopColour(const Theme& theme, juce::Colour hue, float loopGain)
{
    if (loopGain >= 1.0f)
        return theme.colours.meterHot;
    if (loopGain >= warnLoopGain)
        return theme.colours.warning;
    return hue;
}

juce::String msText(double seconds)
{
    return juce::String(static_cast<int>(std::lround(seconds * 1000.0))) + " ms";
}

const char* noteName(float syncChoice)
{
    return plugins::sync::noteValues[static_cast<size_t>(plugins::sync::choiceOf(syncChoice))].name;
}

// A level on the tape (linear) as 0..1 over the bottom 42 dB.
float levelShare(float linear)
{
    return juce::jlimit(0.0f, 1.0f, (toDb(linear) + 42.0f) / 42.0f);
}

float textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text));
}

// ----- the machine -----------------------------------------------------------------------------

class TapeLoopView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // A readout only: the knobs below set the machine.
    TapeLoopView(ModuleCard& card, const Machine& machine) : card_(card), machine_(machine)
    {
        setTooltip("The tape loop: what the record head (R) writes travels round past the playback heads, "
                   "and the lit heads play it. The line under them is the feedback to the record head, "
                   "warning-coloured near self-oscillation. Inside: the motor, the loop gain (at 1 the "
                   "echoes sustain themselves) and the tape's saturation.");
    }

    void advance(double elapsedMs)
    {
        const double seconds = elapsedMs * 0.001;
        travel_ += seconds / std::max(0.005, machine_.motorSeconds);
        wowPhase_ += seconds * 2.0 * juce::MathConstants<double>::pi * 0.55;
        flutterPhase_ += seconds * 2.0 * juce::MathConstants<double>::pi * 7.0;
        lastElapsed_ = elapsedMs;
        repaint();
    }

    // Where the sync chip sits: after the MOTOR caption.
    juce::Rectangle<int> syncChipBounds(int chipWidth) const
    {
        const auto inside = geometry().inside;
        const float x = inside.getX() + textWidth(card_.theme().captionCaps(), "MOTOR") + 8.0f;
        return juce::Rectangle<float>(x, inside.getY() - 2.0f, static_cast<float>(chipWidth), 16.0f)
            .toNearestInt();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto geo = geometry();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(area.reduced(1.0f).toNearestInt());
            drawRollers(g, geo);
            drawTape(g, geo);
            drawHeads(g, geo);
            drawFeedbackPath(g, geo);
            drawReadouts(g, geo);
        }
        drawWellFrame(g, theme, area);
    }

private:
    static constexpr float tapeWidth = 3.0f;
    static constexpr float maxBand = 12.0f; // the recorded band's width at full level
    static constexpr float roller = 13.0f;  // the corner rollers' radius (the tape wraps them)
    static constexpr float markPitch = 14.0f;

    struct Geometry
    {
        juce::Rectangle<float> loop;   // the tape's centre line: its top and bottom runs
        juce::Rectangle<float> inside; // the readouts, inside the loop
        float recordX = 0.0f;
        float unit = 1.0f; // pixels between heads
        float headX(int k) const { return recordX + unit * static_cast<float>(k + 1); }
    };

    Geometry geometry() const
    {
        const auto p = getLocalBounds().toFloat().reduced(12.0f, 8.0f);
        Geometry geo;
        // Under the bottom run: the heads, their labels and the feedback line.
        geo.loop = p.withTrimmedTop(maxBand * 0.5f).withTrimmedBottom(36.0f);
        const float start = geo.loop.getX() + roller + 26.0f;
        // Room past head 4 for its time ("1500 ms") clear of the capstan.
        const float end = geo.loop.getRight() - roller - 54.0f;
        geo.recordX = start;
        geo.unit = std::max(8.0f, (end - start) / static_cast<float>(te::headCount));
        geo.inside = geo.loop.reduced(roller + 14.0f, 0.0f).withTrimmedTop(18.0f).withTrimmedBottom(10.0f);
        return geo;
    }

    // The loop's centre line from the record head, in the direction of travel:
    // right along the bottom past the heads, round the capstan, back along the
    // top (slack, swinging with the wow) and down to the record head again.
    juce::Path tapePath(const Geometry& geo) const
    {
        const auto& l = geo.loop;
        const float r = roller;
        const float pi = juce::MathConstants<float>::pi;
        const float wow = card_.param(te::wow) * 0.01f, flutter = card_.param(te::flutter) * 0.01f;
        const float sag = (1.0f + 5.0f * wow) * (0.6f + 0.4f * static_cast<float>(std::sin(wowPhase_))) +
                          1.0f * flutter * static_cast<float>(std::sin(flutterPhase_));
        juce::Path path;
        path.startNewSubPath(geo.recordX, l.getBottom());
        path.lineTo(l.getRight() - r, l.getBottom());
        path.addCentredArc(l.getRight() - r, l.getBottom() - r, r, r, 0.0f, pi, pi * 0.5f);
        path.lineTo(l.getRight(), l.getY() + r);
        path.addCentredArc(l.getRight() - r, l.getY() + r, r, r, 0.0f, pi * 0.5f, 0.0f);
        path.quadraticTo(l.getCentreX(), l.getY() + 2.0f * sag, l.getX() + r, l.getY());
        path.addCentredArc(l.getX() + r, l.getY() + r, r, r, 0.0f, 2.0f * pi, 1.5f * pi);
        path.lineTo(l.getX(), l.getBottom() - r);
        path.addCentredArc(l.getX() + r, l.getBottom() - r, r, r, 0.0f, 1.5f * pi, pi);
        path.lineTo(geo.recordX, l.getBottom());
        return path;
    }

    // The tape, then what is recorded on it: a band as wide as the level that
    // was written, newest at R, fading as it goes round.
    void drawTape(juce::Graphics& g, const Geometry& geo) const
    {
        const auto& c = card_.theme().colours;
        const auto hue = card_.colour();
        const auto path = tapePath(geo);
        const float length = path.getLength();
        g.setColour(c.surface3);
        g.strokePath(path, juce::PathStrokeType(tapeWidth));
        const int size = card_.historySize();
        constexpr float step = 2.0f;
        // Opaque colours mixed with the well's, so the overlapping steps leave no seams.
        const auto& theme = card_.theme();
        const auto well = theme.material.enabled ? theme.material.meterWell : c.canvas;
        if (size > 0)
            for (float a = 0.0f; a + step <= length; a += step)
            {
                const double age = (a + 0.5 * step) / geo.unit * machine_.motorSeconds;
                const int frame = size - 1 - static_cast<int>(age / frameSeconds);
                if (frame < 0)
                    break;
                const float share = levelShare(card_.historyAt(frame).extra);
                if (share <= 0.0f)
                    continue;
                const float fade = 1.0f - 0.45f * a / length;
                g.setColour(well.interpolatedWith(hue, (0.3f + 0.7f * share) * fade));
                // Round ends, so the band stays smooth round the rollers.
                juce::Path piece;
                piece.startNewSubPath(path.getPointAlongPath(a));
                piece.lineTo(path.getPointAlongPath(a + step));
                g.strokePath(piece,
                             juce::PathStrokeType(tapeWidth + (maxBand - tapeWidth) * share,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
            }
        // Marks along the tape show it moving (until it moves too fast to follow).
        const double pixelsPerSecond = geo.unit / std::max(0.005, machine_.motorSeconds);
        if (pixelsPerSecond * std::max(1.0, lastElapsed_) * 0.001 < markPitch * 0.45)
        {
            const auto offset =
                static_cast<float>(std::fmod(travel_ * geo.unit, static_cast<double>(markPitch)));
            g.setColour(c.textTertiary.withAlpha(0.6f));
            for (float a = offset; a < length; a += markPitch)
                g.fillEllipse(juce::Rectangle<float>(1.5f, 1.5f).withCentre(path.getPointAlongPath(a)));
        }
    }

    void drawRollers(juce::Graphics& g, const Geometry& geo) const
    {
        const auto& c = card_.theme().colours;
        const auto& l = geo.loop;
        const float r = roller;
        // The rollers turn with the tape: its travel over their circumference.
        const auto turn =
            static_cast<float>(std::fmod(travel_ * geo.unit / r, 2.0 * juce::MathConstants<double>::pi));
        const std::array<juce::Point<float>, 4> centres{{{l.getX() + r, l.getY() + r},
                                                         {l.getRight() - r, l.getY() + r},
                                                         {l.getX() + r, l.getBottom() - r},
                                                         {l.getRight() - r, l.getBottom() - r}}};
        const float radius = r - tapeWidth * 0.5f;
        for (size_t i = 0; i < centres.size(); ++i)
        {
            const auto centre = centres[i];
            const bool capstan = i == 3;
            const auto disc = juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre);
            g.setColour(c.surface2);
            g.fillEllipse(disc);
            g.setColour(c.lineStrong);
            g.drawEllipse(disc.reduced(0.5f), 1.0f);
            g.setColour(capstan ? c.textSecondary : c.textTertiary);
            for (int spoke = 0; spoke < 3; ++spoke)
            {
                const float angle =
                    turn + static_cast<float>(spoke) * juce::MathConstants<float>::twoPi / 3.0f;
                g.drawLine(juce::Line<float>(centre.getPointOnCircumference(2.5f, angle),
                                             centre.getPointOnCircumference(radius - 2.5f, angle)),
                           1.2f);
            }
            g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(centre));
        }
        // The pinch roller presses the tape onto the capstan from outside.
        const auto pinch = centres[3].translated(r * 0.95f, r * 0.95f);
        const auto disc = juce::Rectangle<float>(10.0f, 10.0f).withCentre(pinch);
        g.setColour(c.surface2);
        g.fillEllipse(disc);
        g.setColour(c.lineStrong);
        g.drawEllipse(disc.reduced(0.5f), 1.0f);
    }

    // The level on the tape `units` past the record head (what a head there
    // plays now).
    float levelAt(double units) const
    {
        const int size = card_.historySize();
        const int frame = size - 1 - static_cast<int>(units * machine_.motorSeconds / frameSeconds);
        return frame >= 0 && frame < size ? card_.historyAt(frame).extra : 0.0f;
    }

    static juce::Rectangle<float> labelBox(float x, float tapeY)
    {
        return {x - 10.0f, tapeY + 14.0f, 20.0f, 13.0f};
    }

    void drawHeads(juce::Graphics& g, const Geometry& geo) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const float y = geo.loop.getBottom();
        const auto block = [&](float x) { return juce::Rectangle<float>(x - 7.0f, y + 3.0f, 14.0f, 10.0f); };
        // The record head, lit by what it writes.
        {
            const float level = levelShare(levelAt(0.0));
            const auto b = block(geo.recordX);
            g.setColour(c.surface3);
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(c.textSecondary);
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
            g.setColour(hue.withAlpha(0.35f + 0.65f * level));
            g.fillRect(juce::Rectangle<float>(geo.recordX - 0.75f, b.getY(), 1.5f, 4.0f));
            g.setFont(theme.caption());
            g.setColour(c.textSecondary);
            g.drawText("R", labelBox(geo.recordX, y), juce::Justification::centred);
        }
        for (int k = 0; k < te::headCount; ++k)
        {
            const bool on = ((machine_.mask >> k) & 1) != 0;
            const float x = geo.headX(k);
            const auto b = block(x);
            const float units = te::headSpacing[static_cast<size_t>(k)];
            const float level = levelShare(levelAt(units));
            if (on && level > 0.0f)
            {
                // The head flares as an echo passes it.
                g.setColour(hue.withAlpha(0.5f * level));
                g.fillEllipse(juce::Rectangle<float>(22.0f, 22.0f).withCentre({x, y + 4.0f}));
            }
            g.setColour(on ? hue : c.surface2);
            g.fillRoundedRectangle(b, 2.0f);
            g.setColour(on ? hue : c.lineStrong);
            g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
            g.setColour(on ? c.textOnColour : c.textTertiary);
            g.fillRect(juce::Rectangle<float>(x - 0.75f, b.getY(), 1.5f, 4.0f));
            g.setFont(theme.caption());
            g.setColour(on ? c.textPrimary : c.textTertiary);
            g.drawText(juce::String(k + 1), labelBox(x, y), juce::Justification::centred);
            // Each active head's time, inside the loop above it.
            if (on)
            {
                g.setFont(theme.value());
                g.setColour(c.textSecondary);
                g.drawText(msText(machine_.motorSeconds * units),
                           juce::Rectangle<float>(x - 40.0f, y - maxBand * 0.5f - 17.0f, 80.0f, 13.0f),
                           juce::Justification::centred);
            }
        }
    }

    // The heads' feedback, back to the record head: a line from each active head
    // under its label to a bus that runs back to R, as bright as the feedback and
    // warning-coloured near self-oscillation; dashed and faint at no feedback.
    void drawFeedbackPath(juce::Graphics& g, const Geometry& geo) const
    {
        const auto& theme = card_.theme();
        const float fb = card_.param(te::feedback) * 0.01f;
        const float y = geo.loop.getBottom();
        const float top = y + 28.0f, bus = y + 33.0f;
        int last = -1;
        for (int k = 0; k < te::headCount; ++k)
            if (((machine_.mask >> k) & 1) != 0)
                last = k;
        if (last < 0)
            return;
        juce::Path wire;
        for (int k = 0; k <= last; ++k)
            if (((machine_.mask >> k) & 1) != 0)
            {
                wire.startNewSubPath(geo.headX(k), top);
                wire.lineTo(geo.headX(k), bus);
            }
        wire.startNewSubPath(geo.headX(last), bus);
        wire.lineTo(geo.recordX, bus);
        wire.lineTo(geo.recordX, top + 1.0f);
        if (fb <= 0.0f)
        {
            g.setColour(theme.colours.textTertiary.withAlpha(0.5f));
            juce::Path dashed;
            const float dashes[] = {3.0f, 3.0f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, wire, dashes, 2);
            g.fillPath(dashed);
            return;
        }
        const bool warn = machine_.loopGain >= warnLoopGain;
        g.setColour(loopColour(theme, card_.colour(), machine_.loopGain)
                        .withAlpha(warn ? 1.0f : 0.4f + 0.6f * std::min(1.0f, fb)));
        g.strokePath(wire,
                     juce::PathStrokeType(
                         warn ? 1.5f : 1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
        juce::Path arrow;
        arrow.addTriangle(
            geo.recordX - 3.5f, top + 3.0f, geo.recordX + 3.5f, top + 3.0f, geo.recordX, top - 2.0f);
        g.fillPath(arrow);
    }

    // Inside the loop: the motor at the left; the loop gain and the
    // saturation at the right, each a caption with its value over a segment bar.
    void drawReadouts(juce::Graphics& g, const Geometry& geo) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto in = geo.inside;
        const auto caption = [&](juce::Rectangle<float> row, const juce::String& text)
        {
            g.setFont(theme.captionCaps());
            g.setColour(c.textTertiary);
            g.drawText(text, row, juce::Justification::centredLeft);
        };

        // The motor: head 1's time as it runs (gliding after a change), or the
        // note value with the time beside it.
        caption(in.withHeight(12.0f), "MOTOR");
        const auto bigFont = theme.font(Theme::Face::mono, theme.fonts.size.title, theme.fonts.weight.medium);
        auto big = juce::Rectangle<float>(in.getX(), in.getY() + 17.0f, 220.0f, 22.0f);
        const juce::String main =
            machine_.synced ? juce::String(noteName(card_.param(te::sync))) : msText(machine_.motorSeconds);
        g.setFont(bigFont);
        g.setColour(c.textPrimary);
        g.drawText(main, big, juce::Justification::centredLeft);
        if (machine_.synced)
        {
            g.setFont(theme.value());
            g.setColour(machine_.clamped ? c.warning : c.textSecondary);
            g.drawText((machine_.clamped ? "max " : "") + msText(machine_.motorSeconds),
                       big.withTrimmedLeft(textWidth(bigFont, main) + 8.0f).translated(0.0f, 1.0f),
                       juce::Justification::centredLeft);
        }

        // The loop gain and the saturation, from the right.
        constexpr float column = 150.0f, gap = 24.0f;
        auto right = in.withTrimmedLeft(in.getWidth() - column);
        meter(g, right, "SATURATION", saturationRangeDb, card_.reductionReadoutDb(), true);
        right.translate(-(column + gap), 0.0f);
        meter(g, right, "LOOP", 1.2f, machine_.loopGain, false);
    }

    // A caption and value on one row, a bar of twelve segments under them. The
    // loop's bar marks the edge of oscillation (1.0) and colours by it.
    void meter(juce::Graphics& g,
               juce::Rectangle<float> area,
               const juce::String& name,
               float range,
               float value,
               bool saturation) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        auto row = area.withHeight(12.0f);
        g.setFont(theme.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText(name, row, juce::Justification::centredLeft);
        g.setFont(theme.value());
        juce::String text;
        juce::Colour colour = c.textSecondary;
        if (saturation)
        {
            text = value >= 0.1f ? juce::String(value, 1) + " dB" : juce::String::fromUTF8("\xe2\x80\x93");
            colour = value >= 0.1f ? c.textSecondary : c.textTertiary;
        }
        else
        {
            text = juce::String(value, 2);
            if (value >= warnLoopGain)
                colour = loopColour(theme, hue, value);
        }
        g.setColour(colour);
        g.drawText(text, row.translated(0.0f, 0.5f), juce::Justification::centredRight);

        constexpr int segments = 12;
        const auto bar = juce::Rectangle<float>(area.getX(), area.getY() + 24.0f, area.getWidth(), 6.0f);
        const float segGap = 2.0f;
        const float w = (bar.getWidth() - segGap * (segments - 1)) / segments;
        for (int i = 0; i < segments; ++i)
        {
            const float at = range * static_cast<float>(i + 1) / segments;
            const float middle = at - 0.5f * range / segments;
            const auto seg = juce::Rectangle<float>(
                bar.getX() + static_cast<float>(i) * (w + segGap), bar.getY(), w, bar.getHeight());
            const bool lit = value >= middle;
            g.setColour(!lit ? c.surface2 : saturation ? hue : loopColour(theme, hue, middle));
            g.fillRoundedRectangle(seg, 1.0f);
        }
        if (!saturation)
        {
            // The edge: between the segments for 1.0 and 1.1.
            const float edge = bar.getX() + 10.0f * (w + segGap) - segGap * 0.5f;
            g.setColour(c.textSecondary);
            g.fillRect(juce::Rectangle<float>(edge - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f));
        }
    }

    ModuleCard& card_;
    const Machine& machine_;
    double travel_ = 0.0; // head units the tape has moved
    double wowPhase_ = 0.0, flutterPhase_ = 0.0, lastElapsed_ = 16.0;
};

// ----- the card ----------------------------------------------------------------------------

class TapeEchoCard final : public ModuleCard
{
public:
    TapeEchoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), loop_(*this, machine_), sync_(theme, colour())
    {
        addAndMakeVisible(loop_);

        time_ = &addKnob(te::time);
        time_->setTooltip("The motor speed: head 1's time (the other heads are 2, 3 and 4 times it). A "
                          "change glides, bending the echoes' pitch as the motor settles");
        time_->format = [this](double v)
        {
            if (synced())
                return juce::String(noteName(param(te::sync)));
            return juce::String(static_cast<int>(std::lround(v))) + " ms";
        };
        selector(te::heads, "Heads", "Heads");
        feedback_ = &addKnob(te::feedback);
        feedback_->setTooltip("How much of the heads goes back to the record head. Past the edge (a loop "
                              "gain of 1) the echoes build into the tape's saturation and sustain");
        for (auto* k : {time_, feedback_})
            k->setRingSize(bigRing);

        bass_ = &addKnob(te::bass);
        bass_->setTooltip("Low shelf in the loop: each repeat gets more (or less) bass than the one before");
        treble_ = &addKnob(te::treble);
        treble_->setTooltip("High shelf in the loop: each repeat gets brighter (or darker) than the last");

        drive_ = &addKnob(te::drive);
        drive_->setTooltip("The record level into the tape: more saturation, thicker repeats (their level "
                           "is compensated by half)");
        wow_ = &addKnob(te::wow);
        wow_->setTooltip("Slow, wandering speed drift: the echoes' pitch sways");
        flutter_ = &addKnob(te::flutter);
        flutter_->setTooltip("Fast speed jitter from the capstan: a shimmer on the echoes");
        age_ = &addKnob(te::age);
        age_->setTooltip("Worn tape: fewer highs on every pass, hiss under the echoes and the odd dropout");

        selector(te::stereo, "Stereo", "Stereo");
        spread_ = &addKnob(te::spread);
        spread_->setTooltip("How wide the echoes sit: the heads' pan, or the width of ping-pong and stereo");
        spring_ = &addKnob(te::spring);
        spring_->setTooltip("A short spring reverb on the echoes: its chirp and drip, not on the dry signal");

        mix_ = &addKnob(te::mix);
        mix_->setTooltip("The echoes against the dry signal");
        output_ = &addKnob(te::output);
        output_->setTooltip("The level after the machine");
        killDry_ = &addToggle(te::killDry, "Kill");
        killDry_->setTooltip("Kill dry: only the echoes come out, whatever the mix (for a send)");
        killDryLabel_ = makeCaption("Dry");

        for (const auto& name : plugins::sync::choices())
            sync_.choices.emplace_back(name);
        sync_.choices.front() = "Sync off";
        sync_.setTooltip("Sync the motor to the project tempo: head 1 lands on the note value");
        sync_.onChoose = [this](int i)
        {
            beginGesture("Sync");
            setParam(te::sync, static_cast<float>(i));
            endGesture();
        };
        addAndMakeVisible(sync_);
        paramsChanged();
    }

    int preferredWidth() const override
    {
        return 2 * bigCellWidth + 2 * selectorWidth() + 11 * knobCellWidth() +
               4 * (2 * theme_.spacing.sm + 1) + 16;
    }
    int preferredBodyHeight() const override { return 250; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        const auto& c = theme_.colours;
        // Scale marks around the large knobs.
        for (auto* knob : {time_, feedback_})
        {
            const auto b = knob->getBounds().toFloat();
            const juce::Point<float> centre(b.getCentreX(), b.getY() + bigRing * 0.5f);
            for (int i = 0; i <= 10; ++i)
            {
                const float angle =
                    juce::MathConstants<float>::pi * (1.25f + 1.5f * static_cast<float>(i) / 10.0f);
                const auto p = centre.getPointOnCircumference(bigRing * 0.5f + 4.0f, angle);
                const float d = i % 5 == 0 ? 3.0f : 2.0f;
                g.setColour(i % 5 == 0 ? c.textSecondary : c.textTertiary);
                g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(p));
            }
        }
        // Faint rules between the groups.
        g.setColour(c.lineSubtle);
        for (const int x : dividers_)
            g.drawVerticalLine(x, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
    }

private:
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;
    // The heads selector's eight positions reach below its body's centre.
    static constexpr int selectorExtra = 11;

    int selectorWidth() const noexcept
    {
        return selectors_.empty() ? 104 : selectors_.front()->preferredWidth();
    }
    bool synced() const { return plugins::sync::quarters(param(te::sync)) > 0.0; }

    juce::Label* makeCaption(const juce::String& text)
    {
        auto& label = captions_.emplace_back(std::make_unique<juce::Label>());
        label->setText(text, juce::dontSendNotification);
        label->setFont(theme_.caption());
        label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        label->setJustificationType(juce::Justification::centred);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
        return label.get();
    }

    void selector(int index, const juce::String& caption, const juce::String& tooltipName)
    {
        auto& s = selectors_.emplace_back(std::make_unique<RotarySelector>(theme_));
        std::vector<juce::String> labels, full;
        if (index == te::heads)
            for (const auto& m : te::headModes)
            {
                labels.push_back(juce::String::fromUTF8(m.label));
                full.push_back(juce::String::fromUTF8(m.name));
            }
        else
        {
            labels = {"Pan", "Ping", "L/R"};
            full = {"Heads panned", "Ping-pong", "Left and right"};
        }
        s->setPositions(std::move(labels), std::move(full), tooltipName);
        s->setHue(colour());
        s->setDefault(static_cast<int>(std::lround(info(index).defaultValue)));
        s->setSelected(static_cast<int>(std::lround(param(index))), juce::dontSendNotification);
        s->onChange = [this, index](int value) { setParam(index, static_cast<float>(value)); };
        s->onGestureBegin = [this, tooltipName] { beginGesture(tooltipName); };
        s->onGestureEnd = [this] { endGesture(); };
        addAndMakeVisible(*s);
        selectorCaptions_.push_back(makeCaption(caption));
        selectorParams_.push_back(index);
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int valueH = static_cast<int>(theme_.fonts.size.value) + 4;
        const int labelH = static_cast<int>(theme_.fonts.size.caption) + 4;
        const int bigCellH = bigRing + 6 + valueH + labelH;
        const int selectorH = selectors_.front()->preferredHeight() + selectorExtra;
        const int controlsH = std::max({bigCellH, selectorH + labelH, knobCellHeight()});
        auto display = body.removeFromTop(std::max(120, body.getHeight() - controlsH - 6));
        body.removeFromTop(6);
        loop_.setBounds(display);
        sync_.setBounds(loop_.syncChipBounds(sync_.preferredWidth()) + display.getPosition());

        auto controls = body.removeFromTop(controlsH);
        dividers_.clear();
        dividerTop_ = controls.getY() + 6;
        dividerBottom_ = controls.getY() + controlsH - 4;
        const auto rule = [&]
        {
            controls.removeFromLeft(theme_.spacing.sm);
            dividers_.push_back(controls.getX());
            controls.removeFromLeft(theme_.spacing.sm + 1);
        };
        // Every label sits on the row's bottom line, whatever the control above it.
        const auto big = [&](Knob* knob)
        {
            auto cell = controls.removeFromLeft(bigCellWidth);
            if (auto* label = labelFor(*knob))
                label->setBounds(cell.removeFromBottom(labelH));
            knob->setBounds(cell.removeFromBottom(bigCellH - labelH - 6));
        };
        const auto rotary = [&](size_t i)
        {
            auto cell = controls.removeFromLeft(selectorWidth());
            selectorCaptions_[i]->setBounds(cell.removeFromBottom(labelH));
            selectors_[i]->setBounds(cell.removeFromBottom(selectorH));
        };
        const auto knobs = [&](std::vector<Knob*> row)
        {
            const int n = static_cast<int>(row.size());
            layoutKnobs(
                controls.removeFromLeft(n * knobCellWidth()).removeFromBottom(knobCellHeight()), n, row);
        };
        big(time_);
        rotary(0);
        big(feedback_);
        rule();
        knobs({bass_, treble_});
        rule();
        knobs({drive_, wow_, flutter_, age_});
        rule();
        rotary(1);
        knobs({spread_, spring_});
        rule();
        knobs({mix_, output_});
        // Kill dry: a pill where a knob's ring would be, its name on the baseline.
        auto cell = controls.removeFromLeft(knobCellWidth()).removeFromBottom(knobCellHeight());
        killDryLabel_->setBounds(cell.removeFromBottom(labelH));
        const int ring = theme_.sizes.knob;
        killDry_->setBounds(cell.getCentreX() - 24, cell.getY() + ring / 2 - 11, 48, 22);
    }

    // The motor, the loop gain and the mode, from the parameters and the frames.
    void update()
    {
        const double bpm = tempoBpm();
        machine_.targetSeconds = te::headOneSeconds(param(te::time), param(te::sync), bpm);
        const Frame f = historySize() > 0 ? historyAt(historySize() - 1) : Frame{};
        machine_.motorSeconds = f.extra2 > 0.0f ? static_cast<double>(f.extra2) : machine_.targetSeconds;
        machine_.mask = te::headMask(param(te::heads));
        machine_.synced = synced();
        machine_.clamped = te::syncClamped(param(te::sync), bpm);
        machine_.loopGain = te::loopGain(param(te::feedback),
                                         param(te::bass),
                                         param(te::treble),
                                         param(te::age),
                                         machine_.targetSeconds,
                                         sampleRate());
        // The feedback knob's arc warns too.
        if (const auto arc = loopColour(theme_, colour(), machine_.loopGain); arc != feedbackArc_)
        {
            feedbackArc_ = arc;
            feedback_->setArcColour(arc);
        }
    }

    void paramsChanged() override
    {
        for (size_t i = 0; i < selectors_.size(); ++i)
            selectors_[i]->setSelected(static_cast<int>(std::lround(param(selectorParams_[i]))),
                                       juce::dontSendNotification);
        sync_.selected = plugins::sync::choiceOf(param(te::sync));
        sync_.repaint();
        time_->setAlpha(synced() ? 0.5f : 1.0f);
        time_->repaint();
        update();
        loop_.repaint();
    }
    void framesArrived(double elapsedMs) override
    {
        update();
        loop_.advance(elapsedMs);
    }
    juce::String headerStatus() const override
    {
        return isOn() && machine_.loopGain >= 1.0f ? juce::String("Self-oscillating") : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    Machine machine_;
    juce::Colour feedbackArc_;
    TapeLoopView loop_;
    CornerChoice sync_;
    Knob* time_ = nullptr;
    Knob* feedback_ = nullptr;
    Knob* bass_ = nullptr;
    Knob* treble_ = nullptr;
    Knob* drive_ = nullptr;
    Knob* wow_ = nullptr;
    Knob* flutter_ = nullptr;
    Knob* age_ = nullptr;
    Knob* spread_ = nullptr;
    Knob* spring_ = nullptr;
    Knob* mix_ = nullptr;
    Knob* output_ = nullptr;
    PillToggle* killDry_ = nullptr;
    juce::Label* killDryLabel_ = nullptr;
    std::vector<std::unique_ptr<RotarySelector>> selectors_;
    std::vector<std::unique_ptr<juce::Label>> captions_;
    std::vector<juce::Label*> selectorCaptions_;
    std::vector<int> selectorParams_;
    std::vector<int> dividers_;
    int dividerTop_ = 0, dividerBottom_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createTapeEchoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<TapeEchoCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
