// Vesper's card (screens/plugins.md 7.2a): a dusk sky over the machair, behind
// glass. The voice is the land and the reverb the sky: in the Evensong display the
// dry voice stands as hills in front of the tail's afterglow, so the duck shows
// as the sky hidden while the singer sings and the bloom as it filling the gaps,
// a star on the horizon at each line end. Over it the header row: the wordmark,
// the Listener's three lights and the evening star, which is Catch. To the right
// the four controls that make Vesper what it is (Decay, Clarity, Duck, Ends) as
// large bud knobs, the space as pills, the lookahead and the shape of one sung
// line, then the rest in five groups: Time, Duck, Voice, Tone, Output.

#include "plugins/BeatSync.h"
#include "plugins/Vesper.h"
#include "plugins/VocalListener.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/OpticalCentre.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace northlight::ui::strip
{

namespace pv = plugins::vesper;

namespace
{
constexpr double fieldSeconds = 6.0;
constexpr int fieldFrames = static_cast<int>(fieldSeconds / pv::Telemetry::frameSeconds);
constexpr float floorDb = -60.0f;
constexpr double lowestPitchHz = 50.0, highestPitchHz = 2000.0;

float textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text));
}

juce::String dot()
{
    return juce::String::fromUTF8(" \xc2\xb7 ");
}

// The gold low on the horizon: the scene and the wordmark only, never a control.
juce::Colour strawOf(const Theme& theme)
{
    return paletteColour(theme, "Straw");
}

// A four-point star: long points up, down and to the sides, pinched between.
juce::Path fourPointStar(juce::Point<float> centre, float r, float waist = 0.28f)
{
    juce::Path p;
    for (int k = 0; k < 8; ++k)
    {
        const float a = juce::MathConstants<float>::pi * 0.25f * static_cast<float>(k);
        const float rr = k % 2 == 0 ? r : r * waist;
        const auto at = centre.getPointOnCircumference(rr, a);
        if (k == 0)
            p.startNewSubPath(at);
        else
            p.lineTo(at);
    }
    p.closeSubPath();
    return p;
}

int choiceOf(float value, int count)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

bool autoPreDelay(const ModuleCard& card)
{
    return card.param(pv::preDelayAuto) >= 0.5f;
}

bool preDelaySynced(const ModuleCard& card)
{
    return !autoPreDelay(card) && plugins::sync::quarters(card.param(pv::preDelaySync)) > 0.0;
}

bool releaseSynced(const ModuleCard& card)
{
    return plugins::sync::quarters(card.param(pv::releaseSync)) > 0.0;
}

juce::String noteOf(float choice)
{
    return plugins::sync::noteValues[static_cast<size_t>(plugins::sync::choiceOf(choice))].name;
}

const char* spaceName(const ModuleCard& card)
{
    static constexpr std::array<const char*, 3> names{"Plate", "Chamber", "Hall"};
    return names[static_cast<size_t>(choiceOf(card.param(pv::space), 3))];
}

// ----- Evensong ----------------------------------------------------------------------------

// The last six seconds, now at the right: the tail as a sky rising from the
// horizon, the dry voice as land in front of it, a lane under the horizon with
// what the Listener heard (sung runs as bars, what was kept out of the send as
// ticks, breaths dotted), a star at each line end and Follow's cutoff dotted
// through the sky.
class EvensongDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EvensongDisplay(ModuleCard& card) : card_(card)
    {
        setComponentID("evensong");
        setTooltip("The voice and its tail");
        frames_.resize(static_cast<size_t>(fieldFrames));
    }

    // New frames from the processor, oldest first.
    void add(const pv::Telemetry::Frame* frames, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            frames_[static_cast<size_t>(head_)] = frames[i];
            head_ = (head_ + 1) % fieldFrames;
            ++total_;
            count_ = std::min(fieldFrames, count_ + 1);
        }
        if (n > 0)
        {
            const auto& last = frames[n - 1];
            duckDb_ = last[pv::Telemetry::duckDb];
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        g.setColour(c.canvas.withAlpha(0.82f));
        g.fillRoundedRectangle(area, theme.radius.md);

        const auto plot = plotArea();
        const float horizon = plot.getBottom();
        const auto y = [&](float db) { return dbToY(std::max(floorDb, db), 0.0f, floorDb, plot); };
        const int columns = std::max(1, static_cast<int>(plot.getWidth()));
        gather(columns);

        // The grid: -20 and -40 dB, and the beats along the top.
        g.setColour(c.gridBeat);
        for (const float db : {-20.0f, -40.0f})
            g.drawHorizontalLine(static_cast<int>(y(db)), plot.getX(), plot.getRight());
        {
            const double beat = 60.0 / std::max(20.0, card_.tempoBpm());
            g.setColour(c.textPrimary.withAlpha(0.2f));
            for (int b = 0; b * beat <= fieldSeconds; ++b)
            {
                const float x =
                    plot.getRight() - static_cast<float>(b * beat / fieldSeconds) * plot.getWidth();
                g.drawVerticalLine(
                    static_cast<int>(x), plot.getY() - (b % 4 == 0 ? 7.0f : 4.0f), plot.getY());
            }
        }

        const bool held = card_.param(pv::catchTail) >= 0.5f;
        if (count_ > 0)
        {
            const float colW = plot.getWidth() / static_cast<float>(columns);
            const auto xOf = [&](int col) { return plot.getX() + (static_cast<float>(col) + 0.5f) * colW; };

            // The sky: the tail, brightest at the horizon.
            juce::Path sky, top;
            sky.startNewSubPath(plot.getX(), horizon);
            for (int col = 0; col < columns; ++col)
            {
                const auto p = juce::Point<float>(xOf(col), y(toDb(cols_[static_cast<size_t>(col)].tail)));
                sky.lineTo(p);
                if (col == 0)
                    top.startNewSubPath(p);
                else
                    top.lineTo(p);
            }
            sky.lineTo(plot.getRight(), horizon);
            sky.closeSubPath();
            juce::ColourGradient glow(
                hue.withAlpha(0.55f), 0.0f, horizon, hue.withAlpha(0.06f), 0.0f, plot.getY(), false);
            glow.addColour(0.45, hue.withAlpha(0.18f));
            g.setGradientFill(glow);
            g.fillPath(sky);
            g.setColour(held ? c.textPrimary : hue);
            g.strokePath(top, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));

            // Follow's cutoff through the sky, dotted.
            {
                juce::Path pitch;
                bool open = false;
                for (int col = 0; col < columns; ++col)
                {
                    const float hz = cols_[static_cast<size_t>(col)].follow;
                    if (hz <= 0.0f)
                    {
                        open = false;
                        continue;
                    }
                    const float f = static_cast<float>(
                        std::log(std::max(lowestPitchHz, static_cast<double>(hz)) / lowestPitchHz) /
                        std::log(highestPitchHz / lowestPitchHz));
                    const auto p =
                        juce::Point<float>(xOf(col), horizon - std::clamp(f, 0.0f, 1.0f) * plot.getHeight());
                    if (open)
                        pitch.lineTo(p);
                    else
                        pitch.startNewSubPath(p);
                    open = true;
                }
                juce::Path dotted;
                const float pattern[] = {1.0f, 3.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(dotted, pitch, pattern, 2);
                g.setColour(c.textPrimary.withAlpha(0.2f));
                g.fillPath(dotted);
            }

            // The land: the dry voice, in front of the sky.
            juce::Path land, ridge;
            land.startNewSubPath(plot.getX(), horizon);
            for (int col = 0; col < columns; ++col)
            {
                const auto p = juce::Point<float>(xOf(col), y(toDb(cols_[static_cast<size_t>(col)].dry)));
                land.lineTo(p);
                if (col == 0)
                    ridge.startNewSubPath(p);
                else
                    ridge.lineTo(p);
            }
            land.lineTo(plot.getRight(), horizon);
            land.closeSubPath();
            g.setColour(c.canvas.interpolatedWith(hue, 0.04f).withAlpha(0.97f));
            g.fillPath(land);
            g.setColour(c.textSecondary.withAlpha(0.6f));
            g.strokePath(ridge, juce::PathStrokeType(1.0f));

            // The lane: sung runs, what was kept out, breaths.
            const float lane = horizon + 8.0f;
            for (int col = 0; col < columns;)
            {
                if (cols_[static_cast<size_t>(col)].voiced)
                {
                    int end = col;
                    while (end < columns && cols_[static_cast<size_t>(end)].voiced)
                        ++end;
                    g.setColour(hue.withAlpha(0.85f));
                    g.fillRoundedRectangle(
                        juce::Rectangle<float>(xOf(col) - 0.5f * colW, lane, (end - col) * colW, 3.0f), 1.5f);
                    col = end;
                }
                else
                    ++col;
            }
            // Breaths dotted and what was kept out as ticks, one per three columns:
            // each column keeps its place in time, so they scroll whole and do not shimmer.
            g.setColour(c.textTertiary);
            for (int col = 0; col < columns; ++col)
            {
                const auto& k = cols_[static_cast<size_t>(col)];
                if (k.breath && k.at % 4 == 0)
                    g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f).withCentre({xOf(col), lane + 1.5f}));
                if (k.at % tickColumns != 0)
                    continue;
                float kept = 0.0f;
                for (int i = col; i < std::min(columns, col + tickColumns); ++i)
                    kept = std::max(kept, cols_[static_cast<size_t>(i)].keptOut);
                if (kept > 1.5f)
                {
                    const float h = 2.0f + 8.0f * std::min(1.0f, kept / pv::sendCutDb);
                    g.fillRect(juce::Rectangle<float>(xOf(col) - 0.75f, lane + 6.0f, 1.5f, h));
                }
            }

            // A star at each line end, its glow as large as Ends lifts the tail.
            const float lift = card_.param(pv::ends) * 0.01f;
            for (int col = 0; col < columns; ++col)
                if (cols_[static_cast<size_t>(col)].lineEnd)
                {
                    const auto at = juce::Point<float>(xOf(col), horizon);
                    const float r = 8.0f + 12.0f * lift;
                    g.setGradientFill(juce::ColourGradient(
                        hue.withAlpha(0.3f), at, hue.withAlpha(0.0f), at.translated(r, 0.0f), true));
                    g.fillEllipse(juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(at));
                    g.setColour(c.textPrimary);
                    g.fillPath(fourPointStar(at, 7.0f));
                }
        }
        g.setColour(c.textPrimary.withAlpha(0.22f));
        g.drawHorizontalLine(static_cast<int>(horizon), plot.getX(), plot.getRight());

        drawCorners(g, area);
    }

    // The plot inside the well: room above for the readouts, below for the lane.
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds()
            .toFloat()
            .reduced(14.0f, 0.0f)
            .withTrimmedTop(70.0f)
            .withTrimmedBottom(laneDepth);
    }

private:
    // Below the horizon: the lane and its ticks.
    static constexpr float laneDepth = 30.0f;
    static constexpr int tickColumns = 3;

    struct Column
    {
        float tail = 0.0f, dry = 0.0f, keptOut = 0.0f, follow = 0.0f;
        bool voiced = false, breath = false, lineEnd = false;
        std::int64_t at = 0; // the column's place in time, counted in columns since the first frame
    };

    // The frames under each column: the loudest, and whether anything happened.
    // A frame's column comes from its place in time, not from where it sits in the
    // field, so the same frames share a column from one repaint to the next and the
    // picture scrolls a whole column at a time.
    void gather(int columns)
    {
        cols_.assign(static_cast<size_t>(columns), {});
        const auto columnOf = [columns](std::int64_t frame) { return frame * columns / fieldFrames; };
        const std::int64_t newest = columnOf(total_ - 1);
        for (int col = 0; col < columns; ++col)
            cols_[static_cast<size_t>(col)].at = newest - (columns - 1 - col);
        for (int i = 0; i < count_; ++i)
        {
            const auto& f = frames_[static_cast<size_t>((head_ - count_ + i + fieldFrames) % fieldFrames)];
            const std::int64_t col = columns - 1 - (newest - columnOf(total_ - count_ + i));
            if (col < 0)
                continue;
            auto& k = cols_[static_cast<size_t>(col)];
            k.tail = std::max(k.tail, f[pv::Telemetry::tailRms]);
            k.dry = std::max(k.dry, f[pv::Telemetry::dryRms]);
            k.keptOut = std::max(k.keptOut, f[pv::Telemetry::keptOutDb]);
            k.follow = f[pv::Telemetry::followHz];
            k.voiced = k.voiced || f[pv::Telemetry::voiced] > 0.5f;
            k.breath = k.breath || f[pv::Telemetry::breath] > 0.5f;
            k.lineEnd = k.lineEnd || f[pv::Telemetry::lineEnd] > 0.5f;
        }
    }

    // Top left the space and decay over the pre-delay, top right the duck now.
    void drawCorners(juce::Graphics& g, juce::Rectangle<float> area) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto caps = theme.captionCaps();
        auto top = area.reduced(14.0f, 10.0f);
        const auto big =
            theme.font(Theme::Face::mono, theme.fonts.size.title + 1.0f, theme.fonts.weight.medium);
        {
            auto r = top;
            g.setFont(caps);
            g.setColour(c.textTertiary);
            g.drawText("SPACE", r.removeFromTop(13.0f), juce::Justification::centredLeft);
            g.setFont(big);
            g.setColour(c.textPrimary);
            g.drawText(juce::String(spaceName(card_)) + dot() + secondsText(card_.param(pv::decay)),
                       r.removeFromTop(22.0f),
                       juce::Justification::centredLeft);
            g.setFont(theme.value());
            g.setColour(c.textSecondary);
            juce::String pre;
            if (autoPreDelay(card_))
                pre = "Auto pre-delay";
            else if (preDelaySynced(card_))
                pre = noteOf(card_.param(pv::preDelaySync)) + " before";
            else
                pre = secondsText(card_.param(pv::preDelay) * 0.001) + " before";
            g.drawText(pre, r.removeFromTop(15.0f), juce::Justification::centredLeft);
        }
        {
            auto r = top;
            g.setFont(caps);
            g.setColour(c.textTertiary);
            g.drawText("DUCK", r.removeFromTop(13.0f), juce::Justification::centredRight);
            g.setFont(big);
            const bool ducking = duckDb_ >= 0.1f;
            g.setColour(ducking ? card_.colour() : c.textSecondary);
            g.drawText(ducking ? minusSign() + fixedText(duckDb_, 1) + " dB"
                               : juce::String::fromUTF8("\xe2\x80\x93"),
                       r.removeFromTop(22.0f),
                       juce::Justification::centredRight);
        }
    }

    ModuleCard& card_;
    std::vector<pv::Telemetry::Frame> frames_; // a ring of the last six seconds
    int head_ = 0, count_ = 0;
    std::int64_t total_ = 0; // frames received
    std::vector<Column> cols_;
    float duckDb_ = 0.0f;
};

// ----- one sung line -----------------------------------------------------------------------

// What the settings do to one sung line: the voice as a block, the tail ducked
// under it, coming back up in the gap (Release), lifted where the line ends (a
// star, Gap after the voice) and falling over the decay, longer by Ends.
class LineGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LineGraph(ModuleCard& card) : card_(card) { setTooltip("One sung line"); }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        g.setColour(c.canvas.withAlpha(0.82f));
        g.fillRoundedRectangle(area, theme.radius.sm);
        const auto p = area.reduced(6.0f, 5.0f);

        const double voice = 1.2;
        const double gap = card_.param(pv::gap) * 0.001;
        const double ends = card_.param(pv::ends) * 0.01;
        const double decay = card_.param(pv::decay) * (1.0 + (pv::endsDecayRatio - 1.0) * ends);
        const double release = releaseSynced(card_)
                                   ? plugins::sync::seconds(card_.param(pv::releaseSync), card_.tempoBpm())
                                   : card_.param(pv::release) * 0.001;
        const double duck = card_.param(pv::duck);
        const double span = voice + gap + std::min(decay, 4.0) * 0.8 + 0.2;
        const auto x = [&](double t) { return p.getX() + static_cast<float>(t / span) * p.getWidth(); };
        const auto y = [&](double db)
        { return dbToY(static_cast<float>(std::max(-60.0, db)), 0.0f, -60.0f, p); };
        const double base = -14.0;
        const auto level = [&](double t)
        {
            if (t < voice)
                return base - duck * std::min(1.0, t / 0.05);
            const double back = base - duck * std::exp(-(t - voice) / std::max(0.02, release / 3.0));
            if (t < voice + gap)
                return back;
            const double lifted = back + pv::endsLiftDb * ends * std::min(1.0, (t - voice - gap) / 0.08);
            return lifted - 60.0 * std::max(0.0, t - voice - gap - 0.1) / std::max(0.1, decay);
        };

        g.setColour(c.textSecondary.withAlpha(0.18f));
        const auto block =
            juce::Rectangle<float>(x(0.0), y(-4.0), x(voice) - x(0.0), p.getBottom() - y(-4.0));
        g.fillRect(block);
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        g.setColour(c.textSecondary);
        g.drawText("voice", block, juce::Justification::centred);

        juce::Path curve, fill;
        const int steps = std::max(2, static_cast<int>(p.getWidth()));
        fill.startNewSubPath(p.getX(), p.getBottom());
        for (int i = 0; i <= steps; ++i)
        {
            const double t = span * i / steps;
            const auto at = juce::Point<float>(x(t), y(level(t)));
            if (i == 0)
                curve.startNewSubPath(at);
            else
                curve.lineTo(at);
            fill.lineTo(at);
        }
        fill.lineTo(p.getRight(), p.getBottom());
        fill.closeSubPath();
        g.setColour(hue.withAlpha(0.14f));
        g.fillPath(fill);
        g.setColour(hue);
        g.strokePath(curve, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
        g.setColour(c.textPrimary);
        g.fillPath(fourPointStar({x(voice + gap), y(level(voice + gap + 0.08))}, 4.5f));
    }

private:
    ModuleCard& card_;
};

// ----- the evening star: Catch -------------------------------------------------------------

// The star in the header's right: its glow grows with the voice and its points
// flare at each line end. It is Catch: held, it grows warm and still, ringed in
// gold, and reads HELD.
class EveningStar final : public juce::Button
{
public:
    EveningStar(const Theme& theme, juce::Colour hue) : juce::Button("Catch"), theme_(theme), hue_(hue)
    {
        setComponentID("catch");
        setWantsKeyboardFocus(true);
        setTitle("Catch");
        setTooltip("Catch: hold the tail");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    // The voice's level (0..1) and a flare at a line end; repaints only on a visible change.
    void setLevel(float level)
    {
        if (std::abs(level - level_) < 0.03f)
            return;
        level_ = level;
        repaint();
    }
    void flare()
    {
        if (theme_.motion.reduceMotion)
            return;
        flare_ = 1.0f;
        repaint();
    }
    void tick(double elapsedMs)
    {
        if (flare_ <= 0.0f)
            return;
        flare_ = std::max(0.0f, flare_ - static_cast<float>(elapsedMs / 300.0));
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (isEnabled() && key.isKeyCode(juce::KeyPress::spaceKey))
        {
            triggerClick();
            return true;
        }
        return juce::Button::keyPressed(key);
    }

    bool hitTest(int x, int y) override
    {
        return centre().getDistanceFrom({static_cast<float>(x), static_cast<float>(y)}) <= 22.0f;
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto& c = theme_.colours;
        const bool on = getToggleState();
        const auto straw = strawOf(theme_);
        const auto mid = centre();
        // The glow: the voice's level, brighter under the pointer.
        const float glow = on ? 0.4f : 0.16f + 0.3f * level_ + (over ? 0.14f : 0.0f);
        g.setGradientFill(juce::ColourGradient((on ? straw : hue_).withAlpha(glow),
                                               mid,
                                               hue_.withAlpha(0.0f),
                                               mid.translated(28.0f, 0.0f),
                                               true));
        g.fillEllipse(juce::Rectangle<float>(56.0f, 56.0f).withCentre(mid));
        if (on)
        {
            g.setColour(straw.withAlpha(0.35f));
            g.drawEllipse(juce::Rectangle<float>(28.0f, 28.0f).withCentre(mid), 1.0f);
        }
        // The long points, thin and fading out.
        const float reach = on ? 26.0f : 22.0f * (1.0f + 0.4f * flare_);
        const auto core = on ? c.textPrimary.interpolatedWith(straw, 0.2f) : c.textPrimary;
        juce::ColourGradient across(
            core.withAlpha(0.0f), mid.x - reach, mid.y, core.withAlpha(0.0f), mid.x + reach, mid.y, false);
        across.addColour(0.5, core);
        g.setGradientFill(across);
        g.fillRect(juce::Rectangle<float>(2.0f * reach, 1.0f).withCentre(mid));
        juce::ColourGradient upDown(
            core.withAlpha(0.0f), mid.x, mid.y - reach, core.withAlpha(0.0f), mid.x, mid.y + reach, false);
        upDown.addColour(0.5, core);
        g.setGradientFill(upDown);
        g.fillRect(juce::Rectangle<float>(1.0f, 2.0f * reach).withCentre(mid));
        g.setColour(down ? core.darker(0.2f) : core);
        g.fillPath(fourPointStar(mid, on ? 9.0f : 7.5f, 0.2f));
        g.fillEllipse(juce::Rectangle<float>(on ? 6.0f : 4.0f, on ? 6.0f : 4.0f).withCentre(mid));

        g.setFont(theme_.captionCaps());
        g.setColour(on ? c.textPrimary : over ? c.textSecondary : c.textTertiary);
        g.drawText(on ? "HELD" : "CATCH",
                   getLocalBounds().toFloat().removeFromBottom(13.0f),
                   juce::Justification::centred);
        if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
            laf->drawFocusRing(g, *this, juce::Rectangle<float>(40.0f, 40.0f).withCentre(mid), 20.0f);
    }

private:
    juce::Point<float> centre() const
    {
        return {static_cast<float>(getWidth()) * 0.5f, (static_cast<float>(getHeight()) - 13.0f) * 0.5f};
    }

    const Theme& theme_;
    juce::Colour hue_;
    float level_ = 0.0f, flare_ = 0.0f;
};

// ----- the card ----------------------------------------------------------------------------

class VesperCard final : public ModuleCard
{
public:
    VesperCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), evensong_(*this), line_(*this), star_(theme, colour()),
          space_(theme, colour(), "Space"), preDelayMode_(theme, colour()), releaseMode_(theme, colour())
    {
        addAndMakeVisible(evensong_);
        addAndMakeVisible(line_);

        star_.onClick = [this]
        {
            beginGesture("Catch");
            setParam(pv::catchTail, param(pv::catchTail) >= 0.5f ? 0.0f : 1.0f);
            endGesture();
        };
        addAndMakeVisible(star_);

        space_.labels = {"Plate", "Chamber", "Hall"};
        space_.words = true;
        space_.setComponentID("space");
        space_.onChange = [this](int value)
        {
            beginGesture("Space");
            setParam(pv::space, static_cast<float>(value));
            endGesture();
        };
        addAndMakeVisible(space_);
        lookahead_ = &addChoice(pv::lookahead);
        lookahead_->setTooltip("Lookahead");
        lookahead_->setTrackColour(theme.colours.surface0);

        // The four that make Vesper what it is.
        decay_ = &addKnob(pv::decay);
        decay_->format = [](double s) { return secondsText(s); };
        clarity_ = &addKnob(pv::clarity);
        duck_ = &addKnob(pv::duck);
        duck_->format = [](double db) { return fixedText(db, db < 10.0 ? 1 : 0) + " dB"; };
        ends_ = &addKnob(pv::ends);
        for (auto* k : {decay_, clarity_, duck_, ends_})
            k->setRingSize(bigRing);

        // The rest, in groups.
        const auto knob = [this](int index, const juce::String& label) { return &addKnob(index, label); };
        preDelay_ = knob(pv::preDelay, "Pre-delay");
        preDelay_->format = [this](double ms)
        {
            if (autoPreDelay(*this))
                return juce::String("Auto");
            if (preDelaySynced(*this))
                return noteOf(param(pv::preDelaySync));
            return secondsText(ms * 0.001);
        };
        swell_ = knob(pv::swell, "Swell");
        release_ = knob(pv::release, "Release");
        release_->format = [this](double ms)
        { return releaseSynced(*this) ? noteOf(param(pv::releaseSync)) : secondsText(ms * 0.001); };
        gap_ = knob(pv::gap, "Gap");
        gap_->format = [](double ms) { return secondsText(ms * 0.001); };
        breaths_ = knob(pv::breaths, "Breaths");
        follow_ = knob(pv::follow, "Follow");
        follow_->format = [](double oct)
        { return oct <= pv::followOffOctaves ? juce::String("Off") : fixedText(oct, 1) + " oct"; };
        mud_ = knob(pv::mud, "Mud");
        size_ = knob(pv::size, "Size");
        lowCut_ = knob(pv::lowCut, "Low cut");
        lowCut_->format = [](double hz) { return compactHzText(hz); };
        damping_ = knob(pv::damping, "Damping");
        damping_->format = [](double hz) { return compactHzText(hz); };
        width_ = knob(pv::width, "Width");
        centre_ = knob(pv::centre, "Centre");
        mix_ = knob(pv::mix, "Mix");
        output_ = knob(pv::output, "Output");
        output_->format = [](double db) { return dbText(static_cast<float>(db)); };
        for (auto* k : allKnobs())
            k->setStyle(Knob::Style::bud);

        // The pre-delay's chip: Auto, a free time, or a note value.
        preDelayMode_.choices = {"Auto", "Free"};
        for (int i = 1; i < plugins::sync::choiceCount; ++i)
            preDelayMode_.choices.emplace_back(plugins::sync::noteValues[static_cast<size_t>(i)].name);
        preDelayMode_.setTooltip("Pre-delay mode");
        preDelayMode_.setComponentID("predelay-mode");
        preDelayMode_.onChoose = [this](int i)
        {
            beginGesture("Pre-delay mode");
            setParam(pv::preDelayAuto, i == 0 ? 1.0f : 0.0f);
            if (i > 0)
                setParam(pv::preDelaySync, static_cast<float>(i - 1));
            endGesture();
        };
        addAndMakeVisible(preDelayMode_);
        // The release's chip: a free time or a note value.
        for (const auto& c : plugins::sync::choices())
            releaseMode_.choices.emplace_back(c);
        releaseMode_.choices.front() = "Free";
        releaseMode_.setTooltip("Release sync");
        releaseMode_.setComponentID("release-mode");
        releaseMode_.onChoose = [this](int i)
        {
            beginGesture("Release sync");
            setParam(pv::releaseSync, static_cast<float>(i));
            endGesture();
        };
        addAndMakeVisible(releaseMode_);
        paramsChanged();
    }

    int preferredWidth() const override
    {
        return evensongWidth + columnGap + controlsWidth + 2 * (theme_.spacing.md + theme_.spacing.xs);
    }
    // The panels need their height; the land along the foot gives way first, so a
    // short dock still shows every control.
    int minimumBodyHeight() const override
    {
        return headerH + headerGap + panelsHeight() + 2 * theme_.spacing.xs;
    }
    int preferredBodyHeight() const override { return minimumBodyHeight() + landH; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        paintScene(g);
    }

private:
    static constexpr int bigRing = 52;
    static constexpr int columnGap = 14;
    static constexpr int smallCellWidth = 48;
    static constexpr int groupGap = 14;
    static constexpr int controlsWidth = 14 * smallCellWidth + 4 * groupGap;
    static constexpr int evensongWidth = 476;
    // The header: the wordmark's baseline 30 px down, the subtitle's 20 under it
    // (clear of the italic's descenders), then 10 px clear to the panels. The lights sit on the wordmark's
    // baseline, the star's caption on the subtitle's line.
    static constexpr int headerH = 56, headerGap = 10;
    static constexpr int wordmarkBaseline = 30, subtitleBaseline = 50;
    static constexpr int landH = 26;
    static constexpr int maxStars = 24;

    // A glass panel, captioned or not; a group's caption over its hairline; a
    // star in the sky.
    struct Panel
    {
        juce::Rectangle<int> bounds;
        const char* title = nullptr;
    };
    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
        float ruleEnd = 0.0f;
    };
    struct Star
    {
        juce::Point<float> at;
        float size = 2.0f, alpha = 0.0f;
        double age = 0.0, life = 0.0; // life 0: not in the sky
    };
    // A captioned panel: its caption 8 px down, the content 8 px under the caption
    // and 8 px clear of the foot (captionH is the caption and the gap under it).
    static constexpr int captionH = 22;
    static constexpr int choiceH = 28;
    static constexpr int choicesRowH = 8 + captionH + choiceH + 8;

    int bigTileHeight() const { return theme_.spacing.sm + knobCellHeight(bigRing, theme_.fonts.size.label); }
    int groupsHeight() const { return 2 * theme_.spacing.sm + 16 + theme_.spacing.xs + knobCellHeight(); }
    // The big knobs, the row of choices and the groups, with the gaps between.
    int panelsHeight() const
    {
        return bigTileHeight() + theme_.spacing.sm + choicesRowH + theme_.spacing.sm + groupsHeight();
    }

    std::vector<Knob*> allKnobs() const
    {
        return {decay_,
                clarity_,
                duck_,
                ends_,
                preDelay_,
                swell_,
                release_,
                gap_,
                breaths_,
                follow_,
                mud_,
                size_,
                lowCut_,
                damping_,
                width_,
                centre_,
                mix_,
                output_};
    }

    // ----- layout ------------------------------------------------------------------------------

    void layoutBody(juce::Rectangle<int> body) override
    {
        face_ = body.expanded(theme_.spacing.sm, theme_.spacing.xs);
        panels_.clear();
        captions_.clear();
        auto area = body.reduced(theme_.spacing.xs, theme_.spacing.xs);

        // The header row: the wordmark, the lights, the sky where stars come out, the star.
        auto header = area.removeFromTop(headerH);
        wordmarkAt_ = {static_cast<float>(header.getX()) + 2.0f,
                       static_cast<float>(header.getY() + wordmarkBaseline)};
        subtitleBaseline_ = static_cast<float>(header.getY() + subtitleBaseline);
        buildWordmark();
        fitSubtitle();
        lightsX_ = wordmark_.getBounds().getRight() + 32.0f;
        lights_ = juce::Rectangle<float>(lightsX_ - 4.0f, wordmarkAt_.y - 14.0f, lightsWidth() + 8.0f, 20.0f)
                      .getSmallestIntegerContainer();
        // The star's cell reaches above the header so its glow has room; its caption
        // ends on the header's foot, level with the subtitle.
        auto starCell = header.removeFromRight(72);
        star_.setBounds(starCell.withTop(starCell.getY() - theme_.spacing.sm)
                            .withBottom(starCell.getY() + subtitleBaseline + 3));
        skyZone_ = header.withLeft(lights_.getRight() + theme_.spacing.md)
                       .withRight(star_.getX() - theme_.spacing.md)
                       .withTrimmedTop(4)
                       .withHeight(36);

        // The land along the foot, as much as the panels leave (they stand on it).
        area.removeFromTop(headerGap);
        const int land = std::clamp(area.getHeight() - panelsHeight(), 0, landH);
        landArea_ = face_.toFloat().removeFromBottom(static_cast<float>(land + 14));
        buildLand();
        area.removeFromBottom(land);

        // Evensong, in its glass.
        auto song = area.removeFromLeft(std::min(evensongWidth, area.getWidth() - controlsWidth - columnGap));
        panels_.push_back({song, "EVENSONG"});
        evensong_.setBounds(song.reduced(theme_.spacing.sm).withTrimmedTop(captionH));
        area.removeFromLeft(columnGap);
        auto column = area.removeFromLeft(std::min(controlsWidth, area.getWidth()));
        const int gap = theme_.spacing.sm;

        // Four tiles: the big knobs.
        {
            auto row = column.removeFromTop(bigTileHeight());
            const int tileW = (row.getWidth() - 3 * gap) / 4;
            for (auto* k : {decay_, clarity_, duck_, ends_})
            {
                auto tile = row.removeFromLeft(tileW);
                row.removeFromLeft(gap);
                panels_.push_back({tile, nullptr});
                if (auto* label = labelFor(*k))
                    label->setFont(theme_.label());
                placeKnob(*k,
                          tile.reduced(theme_.spacing.xs, 0).withTrimmedTop(theme_.spacing.sm),
                          theme_.fonts.size.label);
            }
        }
        column.removeFromTop(gap);

        // The space, the lookahead, one sung line.
        {
            auto row = column.removeFromTop(choicesRowH);
            const int spaceW = 252, aheadW = 176;
            auto space = row.removeFromLeft(spaceW);
            row.removeFromLeft(gap);
            auto ahead = row.removeFromLeft(aheadW);
            row.removeFromLeft(gap);
            auto line = row;
            for (const auto& [tile, title] :
                 {std::pair{space, "SPACE"}, std::pair{ahead, "LOOKAHEAD"}, std::pair{line, "ONE LINE"}})
                panels_.push_back({tile, title});
            const auto content = [&](juce::Rectangle<int> tile, int inset)
            {
                return tile.reduced(inset, 0)
                    .withTrimmedTop(theme_.spacing.sm + captionH)
                    .withHeight(choiceH);
            };
            space_.setBounds(content(space, theme_.spacing.md));
            lookahead_->setBounds(content(ahead, theme_.spacing.md));
            line_.setBounds(content(line, theme_.spacing.md));
        }
        column.removeFromTop(gap);

        // The rest in one glass tile, in groups under captions and hairlines.
        auto groups = column;
        panels_.push_back({groups, nullptr});
        auto row = groups.reduced(theme_.spacing.sm, theme_.spacing.sm);
        struct Group
        {
            const char* title;
            std::vector<Knob*> knobs;
            CornerChoice* chip;
        };
        const std::array<Group, 5> list{Group{"TIME", {preDelay_, swell_}, &preDelayMode_},
                                        Group{"DUCK", {release_, gap_}, &releaseMode_},
                                        Group{"VOICE", {breaths_, follow_, mud_}, nullptr},
                                        Group{"TONE", {size_, lowCut_, damping_}, nullptr},
                                        Group{"OUTPUT", {width_, centre_, mix_, output_}, nullptr}};
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (i > 0)
                row.removeFromLeft(groupGap - 2);
            const int count = static_cast<int>(list[i].knobs.size());
            auto cell = row.removeFromLeft(count * smallCellWidth);
            auto caption = cell.removeFromTop(16);
            float ruleEnd = 0.0f;
            if (list[i].chip != nullptr)
            {
                const int w = std::min(caption.getWidth() / 2 + 8, list[i].chip->preferredWidth());
                list[i].chip->setBounds(caption.removeFromRight(w).withSizeKeepingCentre(w, 16));
                ruleEnd = static_cast<float>(list[i].chip->getX() - 6);
            }
            captions_.push_back({list[i].title, caption.toFloat(), ruleEnd});
            cell.removeFromTop(theme_.spacing.xs);
            layoutKnobs(cell, count, list[i].knobs);
        }
    }

    // The subtitle's tracking and start, so its ink spans the wordmark's exactly:
    // from the italic V's top serif to the r's end. Found by halving, once per layout.
    static constexpr const char* subtitle = "VOCAL REVERB";
    void fitSubtitle()
    {
        const auto box = wordmark_.getBounds();
        const auto inkOf = [this](float tracking)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText(theme_.captionCaps().withExtraKerningFactor(tracking), subtitle, 0.0f, 0.0f);
            juce::Path outline;
            glyphs.createPath(outline);
            return outline.getBounds();
        };
        float low = 0.0f, high = 2.0f;
        for (int i = 0; i < 24; ++i)
        {
            const float mid = 0.5f * (low + high);
            (inkOf(mid).getWidth() < box.getWidth() ? low : high) = mid;
        }
        subtitleTracking_ = 0.5f * (low + high);
        subtitleX_ = box.getX() - inkOf(subtitleTracking_).getX();
    }

    // The wordmark's outline, made once per layout: "Vesper" in the display serif.
    void buildWordmark()
    {
        juce::GlyphArrangement glyphs;
        const auto font = theme_.displayFont(36.0f, true);
        glyphs.addLineOfText(font, "Vesper", wordmarkAt_.x, wordmarkAt_.y);
        wordmark_.clear();
        glyphs.createPath(wordmark_);
    }

    // The machair along the foot: low dunes, the same in every card.
    void buildLand()
    {
        landPath_.clear();
        const auto b = landArea_;
        landPath_.startNewSubPath(b.getX(), b.getBottom());
        const float base = b.getY() + 10.0f;
        for (float x = b.getX(); x <= b.getRight(); x += 6.0f)
        {
            const float u = x - b.getX();
            const float yy = base + 5.0f * std::sin(u / 140.0f) + 3.0f * std::sin(u / 53.0f + 1.0f) +
                             2.0f * std::sin(u / 23.0f + 2.0f);
            landPath_.lineTo(x, yy);
        }
        landPath_.lineTo(b.getRight(), b.getBottom());
        landPath_.closeSubPath();
    }

    // ----- the scene ---------------------------------------------------------------------------

    void paintScene(juce::Graphics& g)
    {
        const auto& c = theme_.colours;
        const auto hue = colour();
        const auto straw = strawOf(theme_);
        const auto face = face_.toFloat();
        // The Evensong display's frames repaint inside it: every layer below skips
        // itself when the repaint does not reach it (the land's and the
        // wordmark's paths, the panels and the captions are the costly ones).
        const auto painted = g.getClipBounds().toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(face, theme_.radius.md);
        g.reduceClipRegion(clip);

        // Dusk: the canvas at the top, warming toward the horizon.
        juce::ColourGradient dusk(c.canvas,
                                  0.0f,
                                  face.getY(),
                                  c.canvas.interpolatedWith(straw, 0.10f),
                                  0.0f,
                                  face.getBottom(),
                                  false);
        dusk.addColour(0.62, c.canvas.interpolatedWith(hue, 0.09f));
        dusk.addColour(0.9, c.canvas.interpolatedWith(straw, 0.10f));
        g.setGradientFill(dusk);
        g.fillRect(face);

        // The stars that came out at line ends.
        for (const auto& s : stars_)
            if (s.alpha > 0.0f)
            {
                g.setColour(c.textPrimary.withAlpha(0.08f * s.alpha));
                g.fillEllipse(juce::Rectangle<float>(6.0f * s.size, 6.0f * s.size).withCentre(s.at));
                g.setColour(c.textPrimary.withAlpha(s.alpha));
                g.fillEllipse(juce::Rectangle<float>(s.size, s.size).withCentre(s.at));
            }

        // The land, its crest catching the last light.
        if (landPath_.getBounds().expanded(1.0f).intersects(painted))
        {
            g.setColour(c.canvas.darker(0.3f));
            g.fillPath(landPath_);
            g.setColour(straw.withAlpha(0.3f));
            g.strokePath(landPath_, juce::PathStrokeType(1.0f));
        }

        // The wordmark: gold into the hue, and what it is under it.
        if (!wordmark_.isEmpty() && wordmark_.getBounds()
                                        .withBottom(static_cast<float>(subtitleBaseline_) + 4.0f)
                                        .intersects(painted))
        {
            const auto box = wordmark_.getBounds();
            g.setGradientFill(
                juce::ColourGradient(straw, box.getX(), 0.0f, hue, box.getRight(), 0.0f, false));
            g.fillPath(wordmark_);
            // The subtitle spans the wordmark's ink exactly (fitSubtitle).
            g.setFont(theme_.captionCaps().withExtraKerningFactor(subtitleTracking_));
            g.setColour(c.textTertiary);
            g.drawSingleLineText(subtitle, juce::roundToInt(subtitleX_), juce::roundToInt(subtitleBaseline_));
        }
        if (lights_.toFloat().intersects(painted))
            paintLights(g);

        // The glass panels.
        for (const auto& p : panels_)
        {
            const auto r = p.bounds.toFloat();
            if (!r.intersects(painted))
                continue;
            g.setColour(c.surface1.withAlpha(0.72f));
            g.fillRoundedRectangle(r, theme_.radius.lg);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.drawHorizontalLine(
                static_cast<int>(r.getY()) + 1, r.getX() + theme_.radius.lg, r.getRight() - theme_.radius.lg);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.lg, 1.0f);
            if (p.title != nullptr)
            {
                auto caption = p.bounds.reduced(theme_.spacing.md, 0)
                                   .withTrimmedTop(theme_.spacing.sm)
                                   .withHeight(captionH - theme_.spacing.sm);
                g.setColour(hue);
                g.fillEllipse(caption.removeFromLeft(6).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
                caption.removeFromLeft(theme_.spacing.xs + 2);
                g.setFont(theme_.captionCaps());
                g.setColour(c.textSecondary);
                g.drawText(p.title, caption, juce::Justification::centredLeft);
            }
        }
        g.setFont(theme_.captionCaps());
        for (const auto& cap : captions_)
        {
            if (!cap.area.withRight(cap.ruleEnd > 0.0f ? cap.ruleEnd : cap.area.getRight())
                     .intersects(painted))
                continue;
            g.setColour(c.textTertiary);
            g.drawText(cap.text, cap.area, juce::Justification::centredLeft);
            const float x = cap.area.getX() + textWidth(theme_.captionCaps(), cap.text) + 8.0f;
            const float right = cap.ruleEnd > 0.0f ? cap.ruleEnd : cap.area.getRight();
            if (right > x)
            {
                g.setColour(c.textPrimary.withAlpha(0.08f));
                g.fillRect(juce::Rectangle<float>(x, cap.area.getCentreY(), right - x, 1.0f));
            }
        }
    }

    // The lights' run: each a lamp, its caption 14 px on, 24 px to the next.
    static constexpr float lampGap = 14.0f, lightGap = 24.0f;
    float lightsWidth() const
    {
        float w = 0.0f;
        for (const char* name : {"SINGING", "KEPT OUT", "LINE END"})
            w += lampGap + textWidth(theme_.captionCaps(), name) + lightGap;
        return w - lightGap;
    }

    // The Listener's three lights: what it hears now.
    void paintLights(juce::Graphics& g) const
    {
        const auto& c = theme_.colours;
        static constexpr std::array<const char*, 3> names{"SINGING", "KEPT OUT", "LINE END"};
        const std::array<juce::Colour, 3> colours{colour(), c.textSecondary, c.textPrimary};
        const auto font = theme_.captionCaps();
        g.setFont(font);
        // On the wordmark's baseline, each lamp on its caption's middle.
        const float baseline = wordmarkAt_.y;
        const float middle = baseline - 0.5f * letterHeights(font).first;
        float x = lightsX_;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const auto lamp = juce::Rectangle<float>(6.0f, 6.0f).withCentre({x + 3.0f, middle});
            const float on = lit_[i];
            if (on > 0.0f)
            {
                g.setColour(colours[i].withAlpha(0.3f * on));
                g.fillEllipse(lamp.expanded(3.0f));
            }
            g.setColour(c.surface3.interpolatedWith(colours[i], on));
            g.fillEllipse(lamp);
            g.setColour(on > 0.5f ? c.textSecondary : c.textTertiary);
            g.drawSingleLineText(names[i], juce::roundToInt(x + lampGap), juce::roundToInt(baseline));
            x += lampGap + textWidth(font, names[i]) + lightGap;
        }
    }

    // ----- live --------------------------------------------------------------------------------

    void framesArrived(double elapsedMs) override
    {
        star_.tick(elapsedMs);
        const auto* telemetry = pv::telemetryOf(liveProcessor());
        if (telemetry == nullptr)
            return;
        const int n = telemetry->read(cursor_, buffer_.data(), static_cast<int>(buffer_.size()));
        clock_ += elapsedMs * 0.001;
        std::array<float, 3> target{};
        float dry = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto& f = buffer_[static_cast<size_t>(i)];
            if (f[pv::Telemetry::voiced] > 0.5f)
                target[0] = 1.0f;
            if (f[pv::Telemetry::keptOutDb] > 1.5f)
                target[1] = 1.0f;
            if (f[pv::Telemetry::lineEnd] > 0.5f)
            {
                target[2] = 1.0f;
                lineEndAt_ = clock_;
                star_.flare();
                addStar();
            }
            dry = std::max(dry, f[pv::Telemetry::dryRms]);
        }
        if (n > 0)
        {
            evensong_.add(buffer_.data(), n);
            star_.setLevel(std::clamp((toDb(dry) + 48.0f) / 42.0f, 0.0f, 1.0f));
        }
        // The lights fall over 120 ms, the line end's over 600.
        bool changed = false;
        for (size_t i = 0; i < lit_.size(); ++i)
        {
            const float fall = static_cast<float>(elapsedMs / (i == 2 ? 600.0 : 120.0));
            const float next = std::max(target[i], lit_[i] - fall);
            changed = changed || std::abs(next - lit_[i]) > 0.02f;
            lit_[i] = next;
        }
        if (changed)
            repaint(lights_.expanded(4));
        fadeStars(elapsedMs);
    }

    // A line end lights a star in the sky, at a place from a fixed sequence.
    void addStar()
    {
        if (skyZone_.isEmpty())
            return;
        // A free place, else the oldest star's.
        Star* slot = &stars_[0];
        for (auto& s : stars_)
        {
            if (s.life <= 0.0)
            {
                slot = &s;
                break;
            }
            if (s.age > slot->age)
                slot = &s;
        }
        if (slot->alpha > 0.0f)
            repaint(starBounds(*slot));
        const float fx = std::fmod(static_cast<float>(placed_) * 0.6180339f + 0.21f, 1.0f);
        const float fy = std::fmod(static_cast<float>(placed_) * 0.4142135f + 0.37f, 1.0f);
        ++placed_;
        const auto zone = skyZone_.toFloat();
        slot->at = {zone.getX() + fx * zone.getWidth(), zone.getY() + fy * zone.getHeight()};
        slot->size = 2.0f + 1.5f * param(pv::ends) * 0.01f;
        slot->age = 0.0;
        slot->life = std::max(1.0, static_cast<double>(param(pv::decay)) * (1.0 + param(pv::ends) * 0.01));
        slot->alpha = theme_.motion.reduceMotion ? 1.0f : 0.0f;
        repaint(starBounds(*slot));
    }

    // The stars fade in over 400 ms and out over the decay; each repaints only round itself.
    void fadeStars(double elapsedMs)
    {
        for (auto& s : stars_)
        {
            if (s.life <= 0.0)
                continue;
            s.age += elapsedMs * 0.001;
            float alpha = 0.0f;
            if (s.age < s.life)
                alpha = theme_.motion.reduceMotion
                            ? 1.0f
                            : static_cast<float>(std::min(1.0, s.age / 0.4) * (1.0 - s.age / s.life)) * 0.9f;
            else
                s.life = 0.0;
            if (std::abs(alpha - s.alpha) > 0.02f || (alpha == 0.0f) != (s.alpha == 0.0f))
            {
                s.alpha = alpha;
                repaint(starBounds(s));
            }
        }
    }

    juce::Rectangle<int> starBounds(const Star& s) const
    {
        return juce::Rectangle<float>(8.0f * s.size, 8.0f * s.size)
            .withCentre(s.at)
            .getSmallestIntegerContainer();
    }

    void paramsChanged() override
    {
        space_.selected = choiceOf(param(pv::space), 3);
        space_.repaint();
        const bool held = param(pv::catchTail) >= 0.5f;
        star_.setToggleState(held, juce::dontSendNotification);
        preDelayMode_.selected =
            autoPreDelay(*this) ? 0 : 1 + plugins::sync::choiceOf(param(pv::preDelaySync));
        preDelayMode_.repaint();
        releaseMode_.selected = plugins::sync::choiceOf(param(pv::releaseSync));
        releaseMode_.repaint();
        preDelay_->setAlpha(autoPreDelay(*this) || preDelaySynced(*this) ? 0.5f : 1.0f);
        preDelay_->repaint();
        release_->setAlpha(releaseSynced(*this) ? 0.5f : 1.0f);
        release_->repaint();
        // Swell needs Long lookahead.
        swell_->setAlpha(choiceOf(param(pv::lookahead), 3) == 2 ? 1.0f : 0.5f);
        swell_->setTooltip(choiceOf(param(pv::lookahead), 3) == 2 ? "Swell" : "Swell needs Long lookahead");
        evensong_.repaint();
        line_.repaint();
    }

    juce::String headerStatus() const override
    {
        if (!isOn())
            return {};
        if (param(pv::catchTail) >= 0.5f)
            return "Held";
        if (clock_ - lineEndAt_ < 0.6)
            return "Line end";
        const float db = reductionReadoutDb();
        return db >= 0.1f ? "Duck " + minusSign() + juce::String(db, 1) + " dB" : juce::String();
    }
    juce::Colour headerStatusColour() const override
    {
        if (param(pv::catchTail) >= 0.5f)
            return theme_.colours.textPrimary;
        return clock_ - lineEndAt_ < 0.6 ? colour() : theme_.colours.textSecondary;
    }

    EvensongDisplay evensong_;
    LineGraph line_;
    EveningStar star_;
    PillRow space_;
    CornerChoice preDelayMode_, releaseMode_;
    SegmentedControl* lookahead_ = nullptr;
    Knob *decay_ = nullptr, *clarity_ = nullptr, *duck_ = nullptr, *ends_ = nullptr;
    Knob *preDelay_ = nullptr, *swell_ = nullptr, *release_ = nullptr, *gap_ = nullptr;
    Knob *breaths_ = nullptr, *follow_ = nullptr, *mud_ = nullptr;
    Knob *size_ = nullptr, *lowCut_ = nullptr, *damping_ = nullptr;
    Knob *width_ = nullptr, *centre_ = nullptr, *mix_ = nullptr, *output_ = nullptr;
    juce::Rectangle<int> face_, lights_, skyZone_;
    juce::Point<float> wordmarkAt_; // the wordmark's left end on its baseline
    float subtitleBaseline_ = 0.0f, subtitleX_ = 0.0f, subtitleTracking_ = 0.0f, lightsX_ = 0.0f;
    juce::Rectangle<float> landArea_;
    juce::Path wordmark_, landPath_;
    std::vector<Panel> panels_;
    std::vector<Caption> captions_;
    std::array<float, 3> lit_{};
    std::array<Star, maxStars> stars_{};
    int placed_ = 0;
    std::array<pv::Telemetry::Frame, 512> buffer_{};
    std::uint64_t cursor_ = 0;
    double clock_ = 0.0, lineEndAt_ = -10.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createVesperCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<VesperCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
