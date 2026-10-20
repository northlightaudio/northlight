// The built-ins' inline editors (07-plugins.md 7.1): a display to read and
// drag for each processor, then the knobs. The dynamics reuse the channel
// strip's displays; the reverb, delay and synth have their own here.

#include "ui/strip/BuiltInCards.h"

#include "plugins/Biquad.h"
#include "plugins/BuiltIns.h"
#include "ui/strip/AuroraEditor.h"
#include "ui/strip/EqGraph.h"
#include "ui/strip/Graphs.h"
#include "ui/strip/NebulaEditor.h"
#include "ui/strip/PulsarEditor.h"
#include "ui/strip/StarbitEditor.h"
#include "ui/strip/StripStyle.h"

#include <cmath>

namespace aural::ui::strip
{

namespace
{
namespace types = plugins::types;

// The built-ins' parameter indices (BuiltIns.cpp).
namespace gainp
{
constexpr int gain = 0;
}
namespace eqp
{
constexpr int highPass = 0, lowHz = 1, lowDb = 2, midHz = 3, midDb = 4, midQ = 5, highHz = 6, highDb = 7;
}
namespace limp
{
constexpr int ceiling = 0, release = 1, lookahead = 2;
}
namespace rev = plugins::reverb;
namespace del = plugins::delay;

// The display takes the body above `controlsHeight` of controls.
juce::Rectangle<int> takeDisplay(juce::Rectangle<int>& body, int controlsHeight)
{
    auto display = body.removeFromTop(std::max(40, body.getHeight() - controlsHeight - 6));
    body.removeFromTop(6);
    return display;
}

juce::String reductionText(float db)
{
    return db >= 0.1f ? "-" + juce::String(db, 1) + " dB" : juce::String();
}

// A draggable point on a display, the same everywhere: a 9 px textPrimary
// disc with a ring in the module's hue.
void drawHandle(juce::Graphics& g, const Theme& theme, juce::Colour hue, juce::Point<float> at, bool active)
{
    const auto r = juce::Rectangle<float>(9.0f, 9.0f).withCentre(at);
    if (active)
    {
        g.setColour(hue.withAlpha(0.25f));
        g.fillEllipse(r.expanded(4.0f));
    }
    g.setColour(theme.colours.textPrimary.withAlpha(active ? 1.0f : 0.85f));
    g.fillEllipse(r);
    g.setColour(hue);
    g.drawEllipse(r, 1.5f);
}

// A readout in the display's top-right corner (the handles start at the left).
void drawReadout(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> plot, const juce::String& text)
{
    g.setFont(theme.value());
    g.setColour(theme.colours.textSecondary);
    g.drawText(text, plot.reduced(8.0f, 6.0f).removeFromTop(14.0f), juce::Justification::centredRight);
}

// A time axis's labels and grid at a sensible step for `span` seconds.
void drawTimeGrid(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> p, double span)
{
    const auto& c = theme.colours;
    double step = 0.001;
    for (const double s : {0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0})
        if (span / s <= 6.0)
        {
            step = s;
            break;
        }
    if (step == 0.001)
        step = 10.0;
    g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
    for (double t = step; t < span; t += step)
    {
        const float x = p.getX() + static_cast<float>(t / span) * p.getWidth();
        g.setColour(c.gridBeat);
        g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
        if (x > p.getRight() - 24.0f)
            continue; // a label there would be cut off
        g.setColour(c.textTertiary);
        const auto label = t < 1.0 ? juce::String(static_cast<int>(std::lround(t * 1000.0))) + " ms"
                                   : juce::String(t, 1) + " s";
        g.drawText(label,
                   juce::Rectangle<float>(x - 24.0f, p.getBottom() + 1.0f, 48.0f, 12.0f),
                   juce::Justification::centred);
    }
}

// ----- gain ---------------------------------------------------------------------------------------

class GainCard final : public ModuleCard
{
public:
    GainCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert),
          graph_(*this, HistoryGraph::Style{12.0f, -60.0f, false, true, false, -1, false, 3.0f}),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::output})
    {
        graph_.setTooltip("Input (grey) and output");
        addAndMakeVisible(graph_);
        addAndMakeVisible(bars_);
        knobs_.push_back(&addKnob(gainp::gain));
    }

    int preferredWidth() const override { return 300; }
    int preferredBodyHeight() const override { return 150 + knobCellHeight(); }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        graph_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()).removeFromLeft(knobCellWidth()), 1, knobs_);
    }
    void framesArrived(double) override
    {
        graph_.repaint();
        bars_.repaint();
    }

    HistoryGraph graph_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
};

// ----- EQ -----------------------------------------------------------------------------------------

// A built-in EQ band's hue: the strip EQ's low cut, band 1, band 2 and band 4.
juce::Colour eqHue(const Theme& theme, int band)
{
    return bandColour(theme, band == 0 ? 0 : band == 1 ? 1 : band == 2 ? 2 : 4);
}

// The built-in EQ's four bands on the strip's graph: a high-pass (off at 0 Hz),
// a low shelf, a peaking mid and a high shelf, designed as the processor does.
EqBands builtInEqBands()
{
    using F = EqBands::Field;
    EqBands b;
    b.count = 4;
    b.selected = 2;
    b.lowestNodeHz = 28.0;
    b.index = [](int band, F field) -> int
    {
        switch (band)
        {
            case 0:
                return field == F::frequency ? eqp::highPass : -1;
            case 1:
                return field == F::frequency ? eqp::lowHz : field == F::gain ? eqp::lowDb : -1;
            case 2:
                return field == F::frequency ? eqp::midHz
                       : field == F::gain    ? eqp::midDb
                       : field == F::q       ? eqp::midQ
                                             : -1;
            case 3:
                return field == F::frequency ? eqp::highHz : field == F::gain ? eqp::highDb : -1;
            default:
                return -1;
        }
    };
    b.isCut = [](int band) { return band == 0; };
    b.colour = eqHue;
    b.name = [](int band)
    {
        static const char* names[] = {"High-pass", "Low", "Mid", "High"};
        return juce::String(names[juce::jlimit(0, 3, band)]);
    };
    b.label = [](int band)
    {
        static const char* labels[] = {"HP", "L", "M", "H"};
        return juce::String(labels[juce::jlimit(0, 3, band)]);
    };
    // The high-pass is on when its frequency is; the shelves and the mid always are.
    b.isOn = [](const ModuleCard& card, int band) { return band != 0 || card.param(eqp::highPass) > 0.0f; };
    b.setOn = [](ModuleCard& card, int band, bool on)
    {
        if (band == 0)
            card.setParam(eqp::highPass, on ? std::max(30.0f, card.param(eqp::highPass)) : 0.0f);
    };
    b.design = [](const float* params, double rate, plugins::Biquad* out, int band)
    {
        for (int i = 0; i < EqBands::maxSections; ++i)
            out[i].bypass();
        switch (band)
        {
            case 0:
                if (params[eqp::highPass] > 0.0f)
                    out[0].setHighPass(rate, params[eqp::highPass]);
                break;
            case 1:
                out[0].setLowShelf(rate, params[eqp::lowHz], params[eqp::lowDb]);
                break;
            case 2:
                out[0].setPeaking(rate, params[eqp::midHz], params[eqp::midQ], params[eqp::midDb]);
                break;
            case 3:
                out[0].setHighShelf(rate, params[eqp::highHz], params[eqp::highDb]);
                break;
            default:
                break;
        }
    };
    return b;
}

class EqCard final : public ModuleCard
{
public:
    EqCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), graph_(*this, builtInEqBands())
    {
        addAndMakeVisible(graph_);
        graph_.onSelect = [this](int band) { select(band); };
        // Every band's knobs exist; the selected band's are shown.
        const std::vector<std::vector<int>> bands{{eqp::highPass},
                                                  {eqp::lowHz, eqp::lowDb},
                                                  {eqp::midHz, eqp::midDb, eqp::midQ},
                                                  {eqp::highHz, eqp::highDb}};
        for (size_t b = 0; b < bands.size(); ++b)
        {
            std::vector<Knob*> knobs;
            for (const int p : bands[b])
            {
                auto& k = addKnob(p,
                                  p == eqp::midQ         ? juce::String("Q")
                                  : info(p).unit == "dB" ? juce::String("Gain")
                                                         : juce::String("Frequency"));
                k.setArcColour(eqHue(theme, static_cast<int>(b)));
                knobs.push_back(&k);
            }
            knobs_.push_back(knobs);
        }
        select(2);
    }

    int preferredWidth() const override { return 400; }
    int preferredBodyHeight() const override { return 170 + 20 + knobCellHeight(); }

private:
    void select(int band)
    {
        selected_ = band;
        graph_.setSelectedBand(band);
        for (size_t b = 0; b < knobs_.size(); ++b)
            for (auto* k : knobs_[b])
            {
                k->setVisible(static_cast<int>(b) == band);
                if (auto* l = labelFor(*k))
                    l->setVisible(static_cast<int>(b) == band);
            }
        resized();
        repaint();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int captionH = 18;
        graph_.setBounds(takeDisplay(body, captionH + knobCellHeight()));
        caption_ = body.removeFromTop(captionH);
        layoutKnobs(body.removeFromTop(knobCellHeight()).removeFromLeft(3 * knobCellWidth()),
                    3,
                    knobs_[static_cast<size_t>(selected_)]);
    }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        // The selected band's name over its knobs, in its hue.
        static const char* names[] = {"High-pass", "Low shelf", "Mid", "High shelf"};
        g.setFont(theme_.captionCaps());
        g.setColour(eqHue(theme_, selected_));
        g.drawText(names[juce::jlimit(0, 3, selected_)],
                   caption_.withTrimmedLeft(theme_.spacing.xs),
                   juce::Justification::centredLeft);
    }

    void paramsChanged() override { graph_.invalidateCurve(); }
    void framesArrived(double elapsedMs) override { graph_.update(elapsedMs); }

    EqGraph graph_;
    std::vector<std::vector<Knob*>> knobs_;
    juce::Rectangle<int> caption_;
    int selected_ = 2;
};

// ----- limiter ------------------------------------------------------------------------------------

class LimiterCard final : public ModuleCard
{
public:
    LimiterCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert),
          graph_(*this, HistoryGraph::Style{0.0f, -36.0f, false, true, true, limp::ceiling, false, 3.0f}),
          bars_(*this, {LevelBars::Kind::reduction, LevelBars::Kind::output}),
          ceiling_(*this, limp::ceiling, ThresholdMeter::Level::input, -36.0f, "IN", theme.colours.meterHot)
    {
        graph_.setTooltip("Input (grey), output and gain reduction; drag to set the ceiling");
        for (auto* c : std::initializer_list<juce::Component*>{&graph_, &bars_, &ceiling_})
            addAndMakeVisible(c);
        for (int p : {limp::release, limp::lookahead})
            knobs_.push_back(&addKnob(p));
    }

    int preferredWidth() const override { return 320; }
    int preferredBodyHeight() const override { return 150 + knobCellHeight(); }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        ceiling_.setBounds(display.removeFromRight(ThresholdMeter::preferredWidth));
        display.removeFromRight(4);
        graph_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()).removeFromLeft(2 * knobCellWidth()), 2, knobs_);
    }
    void paramsChanged() override
    {
        graph_.repaint();
        ceiling_.repaint();
    }
    void framesArrived(double) override
    {
        graph_.repaint();
        bars_.repaint();
        ceiling_.repaint();
    }

    HistoryGraph graph_;
    LevelBars bars_;
    ThresholdMeter ceiling_;
    std::vector<Knob*> knobs_;
};

// ----- reverb -------------------------------------------------------------------------------------

// The reverb's decay: time across, level down to -60 dB. The dry sound at 0,
// the pre-delay gap, a cluster of early reflections (denser and longer for a
// bigger room), then the tail falling 60 dB over the decay time, and the highs'
// faster fall above the damping frequency as a dashed line. Drag the handle at
// the tail's start for the pre-delay and the one at its end for the decay.
class ReverbGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ReverbGraph(ModuleCard& card) : card_(card)
    {
        setTooltip("Drag the tail's start for the pre-delay, its end for the decay time");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto bounds = getLocalBounds().toFloat();
        drawWell(g, theme, bounds);
        const auto p = plot();
        const double span = spanSeconds();
        const float pre = card_.param(rev::preDelay) * 0.001f;
        const float decay = card_.param(rev::decay);
        const auto x = [&](double t) { return p.getX() + static_cast<float>(t / span) * p.getWidth(); };
        const auto y = [&](float db) { return dbToY(db, 0.0f, floorDb, p); };
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(bounds.reduced(1.0f).toNearestInt());
            drawTimeGrid(g, theme, p, span);
            for (float db = -12.0f; db > floorDb; db -= 12.0f)
            {
                g.setColour(c.gridBeat);
                g.drawHorizontalLine(static_cast<int>(y(db)), p.getX(), p.getRight());
            }
            g.reduceClipRegion(p.toNearestInt());
            // The dry sound.
            g.setColour(c.textTertiary);
            g.fillRect(juce::Rectangle<float>(p.getX() + 1.0f, y(0.0f), 2.0f, p.getBottom() - y(0.0f)));
            // The tail: from the start level at the pre-delay down 60 dB over the decay.
            const float start = startDb;
            juce::Path tail;
            tail.startNewSubPath(x(pre), p.getBottom());
            tail.lineTo(x(pre), y(start));
            tail.lineTo(x(pre + decay), y(start - 60.0f));
            tail.lineTo(x(pre + decay), p.getBottom());
            tail.closeSubPath();
            g.setGradientFill(juce::ColourGradient(
                hue.withAlpha(0.35f), 0.0f, y(start), hue.withAlpha(0.04f), 0.0f, p.getBottom(), false));
            g.fillPath(tail);
            // Early reflections: taps with a fixed scatter, as many and as long
            // as the room is big.
            const float size = card_.param(rev::size) * 0.01f;
            const int taps = 6 + static_cast<int>(size * 10.0f);
            const float spread = 0.008f + 0.07f * size;
            for (int i = 0; i < taps; ++i)
            {
                const float u = std::fmod(static_cast<float>(i) * 0.618034f, 1.0f);
                const float t = pre + spread * (static_cast<float>(i) + u) / static_cast<float>(taps);
                const float level = start + 2.0f - 14.0f * static_cast<float>(i) / static_cast<float>(taps) -
                                    4.0f * std::fmod(u * 7.0f, 1.0f);
                g.setColour(hue.withAlpha(0.8f));
                g.drawLine(x(t), p.getBottom(), x(t), y(level), 1.2f);
            }
            g.setColour(hue);
            g.drawLine(x(pre), y(start), x(pre + decay), y(start - 60.0f), 2.0f);
            // The highs fade sooner the lower the damping.
            const float damping = card_.param(rev::damping);
            const float highs = decay * juce::jlimit(0.15f, 1.0f, std::pow(damping / 20000.0f, 0.6f));
            const float dashes[] = {4.0f, 3.0f};
            g.setColour(hue.brighter(0.5f).withAlpha(0.8f));
            g.drawDashedLine(
                juce::Line<float>(x(pre), y(start), x(pre + highs), y(start - 60.0f)), dashes, 2, 1.2f);
            // The live output against the tail: how loud the room is now.
            const float out = card_.outputDb();
            if (out > floorDb)
            {
                g.setColour(c.textSecondary.withAlpha(0.5f));
                g.fillRect(juce::Rectangle<float>(p.getRight() - 5.0f, y(out), 3.0f, p.getBottom() - y(out)));
            }
        }
        drawWellFrame(g, theme, bounds);
        drawHandle(g, theme, hue, {x(pre), y(startDb)}, drag_ == Drag::preDelay);
        drawHandle(g, theme, hue, {x(pre + decay), y(startDb - 60.0f)}, drag_ == Drag::decay);
        drawReadout(g,
                    theme,
                    p,
                    "RT60 " + juce::String(decay, decay < 10.0f ? 2 : 1) + " s   Pre " +
                        juce::String(static_cast<int>(std::lround(pre * 1000.0f))) + " ms");
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p = plot();
        const double span = spanSeconds();
        const float pre = card_.param(rev::preDelay) * 0.001f;
        const float preX = p.getX() + static_cast<float>(pre / span) * p.getWidth();
        frozenSpan_ = span;
        drag_ = std::abs(e.position.x - preX) <= 10.0f ? Drag::preDelay : Drag::decay;
        card_.beginGesture(drag_ == Drag::preDelay ? "Pre-delay" : "Decay");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (drag_ == Drag::none)
            return;
        const auto p = plot();
        const double t = juce::jmax(0.0, (e.position.x - p.getX()) / p.getWidth() * frozenSpan_);
        if (drag_ == Drag::preDelay)
            card_.setParam(rev::preDelay, static_cast<float>(t * 1000.0));
        else
            card_.setParam(rev::decay, static_cast<float>(t - card_.param(rev::preDelay) * 0.001));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ != Drag::none)
            card_.endGesture();
        drag_ = Drag::none;
        frozenSpan_ = 0.0;
        repaint();
    }
    void mouseMove(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }

private:
    enum class Drag
    {
        none,
        preDelay,
        decay
    };
    static constexpr float floorDb = -66.0f, startDb = -4.0f;
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(14.0f);
    }
    // The time across: the tail and a little after (held still while dragging).
    double spanSeconds() const
    {
        if (frozenSpan_ > 0.0)
            return frozenSpan_;
        const double tail = card_.param(rev::preDelay) * 0.001 + card_.param(rev::decay);
        return juce::jlimit(0.5, 14.0, tail * 1.25);
    }

    ModuleCard& card_;
    Drag drag_ = Drag::none;
    double frozenSpan_ = 0.0;
};

class ReverbCard final : public ModuleCard
{
public:
    ReverbCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), graph_(*this),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::output})
    {
        addAndMakeVisible(graph_);
        addAndMakeVisible(bars_);
        for (int p : {rev::size, rev::damping, rev::lowCut, rev::width, rev::mix})
            knobs_.push_back(&addKnob(p));
    }

    int preferredWidth() const override { return 5 * knobCellWidth() + 90; }
    int preferredBodyHeight() const override { return 160 + knobCellHeight(); }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        graph_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 5, knobs_);
    }
    void paramsChanged() override { graph_.repaint(); }
    void framesArrived(double) override
    {
        graph_.repaint();
        bars_.repaint();
    }

    ReverbGraph graph_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
};

// ----- delay --------------------------------------------------------------------------------------

// The delay's echoes: time across, each repeat a bar as loud as the feedback
// leaves it (dB, up from the centre line); ping-pong puts them on alternate
// sides (left up, right down), otherwise each is both. The tone darkens the
// later repeats. Drag the first echo across for the time, the second up and
// down for the feedback.
class DelayGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DelayGraph(ModuleCard& card) : card_(card)
    {
        setTooltip("Drag the first echo for the time, the second for the feedback");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto bounds = getLocalBounds().toFloat();
        drawWell(g, theme, bounds);
        const auto p = plot();
        const double span = spanSeconds();
        const double time = card_.param(del::time) * 0.001;
        const float feedback = card_.param(del::feedback) * 0.01f;
        const bool pingPong = card_.param(del::pingPong) >= 0.5f;
        const float tone =
            juce::jlimit(0.0f, 1.0f, std::log(card_.param(del::tone) / 500.0f) / std::log(40.0f));
        const float mid = p.getCentreY();
        const float half = p.getHeight() * 0.5f - 4.0f;
        const auto height = [&](float db)
        { return juce::jlimit(0.0f, 1.0f, (db - floorDb) / -floorDb) * half; };
        const auto x = [&](double t) { return p.getX() + static_cast<float>(t / span) * p.getWidth(); };
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(bounds.reduced(1.0f).toNearestInt());
            drawTimeGrid(g, theme, p, span);
            g.setColour(c.lineStrong);
            g.drawHorizontalLine(static_cast<int>(mid), p.getX(), p.getRight());
            g.setFont(
                theme.font(Theme::Face::ui, theme.fonts.size.caption - 1.0f, theme.fonts.weight.medium));
            g.setColour(c.textTertiary);
            g.drawText("L",
                       juce::Rectangle<float>(p.getRight() - 14.0f, p.getY() + 2.0f, 10.0f, 12.0f),
                       juce::Justification::centred);
            g.drawText("R",
                       juce::Rectangle<float>(p.getRight() - 14.0f, p.getBottom() - 14.0f, 10.0f, 12.0f),
                       juce::Justification::centred);
            g.reduceClipRegion(p.toNearestInt());
            // The dry sound, both sides.
            g.setColour(c.textTertiary);
            g.fillRect(juce::Rectangle<float>(p.getX() + 1.0f, mid - half, 2.0f, 2.0f * half));
            // The repeats.
            float db = 0.0f;
            for (int n = 1; x(n * time) < p.getRight() && db > floorDb && n < 200; ++n)
            {
                const float h = height(db);
                const float fade = std::pow(0.55f + 0.45f * tone, static_cast<float>(n - 1));
                g.setColour(hue.withAlpha(juce::jlimit(0.25f, 1.0f, fade)));
                const float bx = x(n * time) - 1.5f;
                const bool up = !pingPong || n % 2 == 1, down = !pingPong || n % 2 == 0;
                if (up)
                    g.fillRoundedRectangle(bx, mid - h, 3.0f, h, 1.5f);
                if (down)
                    g.fillRoundedRectangle(bx, mid, 3.0f, h, 1.5f);
                db += feedback > 0.0f ? 20.0f * std::log10(feedback) : floorDb;
            }
        }
        drawWellFrame(g, theme, bounds);
        // The handles: the first echo's time, the second's level (the feedback).
        const float secondDb = feedback > 0.0f ? 20.0f * std::log10(feedback) : floorDb;
        drawHandle(g, theme, hue, {x(time), mid - height(0.0f)}, drag_ == Drag::time);
        if (x(2.0 * time) < p.getRight())
            drawHandle(g,
                       theme,
                       hue,
                       {x(2.0 * time), pingPong ? mid + height(secondDb) : mid - height(secondDb)},
                       drag_ == Drag::feedback);
        juce::String text = juce::String(static_cast<int>(std::lround(time * 1000.0))) + " ms   " +
                            juce::String(static_cast<int>(std::lround(feedback * 100.0f))) + "% feedback";
        if (pingPong)
            text << "   ping-pong";
        drawReadout(g, theme, p, text);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p = plot();
        frozenSpan_ = spanSeconds();
        const double time = card_.param(del::time) * 0.001;
        const float secondX = p.getX() + static_cast<float>(2.0 * time / frozenSpan_) * p.getWidth();
        drag_ = std::abs(e.position.x - secondX) <= 10.0f ? Drag::feedback : Drag::time;
        card_.beginGesture(drag_ == Drag::time ? "Delay time" : "Feedback");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto p = plot();
        if (drag_ == Drag::time)
        {
            const double t = (e.position.x - p.getX()) / p.getWidth() * frozenSpan_;
            card_.setParam(del::time, static_cast<float>(t * 1000.0));
        }
        else if (drag_ == Drag::feedback)
        {
            const float mid = p.getCentreY(), half = p.getHeight() * 0.5f - 4.0f;
            const float fraction = juce::jlimit(0.0f, 1.0f, std::abs(e.position.y - mid) / half);
            const float db = floorDb * (1.0f - fraction);
            card_.setParam(del::feedback, fraction <= 0.0f ? 0.0f : 100.0f * std::pow(10.0f, db / 20.0f));
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ != Drag::none)
            card_.endGesture();
        drag_ = Drag::none;
        frozenSpan_ = 0.0;
        repaint();
    }

private:
    enum class Drag
    {
        none,
        time,
        feedback
    };
    static constexpr float floorDb = -48.0f;
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(14.0f);
    }
    // Long enough for six repeats (held still while dragging).
    double spanSeconds() const
    {
        if (frozenSpan_ > 0.0)
            return frozenSpan_;
        return juce::jlimit(0.1, 6.0, card_.param(del::time) * 0.001 * 6.5);
    }

    ModuleCard& card_;
    Drag drag_ = Drag::none;
    double frozenSpan_ = 0.0;
};

class DelayCard final : public ModuleCard
{
public:
    DelayCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), graph_(*this),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::output})
    {
        addAndMakeVisible(graph_);
        addAndMakeVisible(bars_);
        for (int p : {del::time, del::feedback, del::tone, del::mix})
            knobs_.push_back(&addKnob(p));
        pingPong_ = &addToggle(del::pingPong, "Ping-pong");
    }

    int preferredWidth() const override { return 4 * knobCellWidth() + 110 + 90; }
    int preferredBodyHeight() const override { return 150 + knobCellHeight(); }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        graph_.setBounds(display);
        auto row = body.removeFromTop(knobCellHeight());
        layoutKnobs(row.removeFromLeft(4 * knobCellWidth()), 4, knobs_);
        row.removeFromLeft(theme_.spacing.sm);
        pingPong_->setBounds(row.removeFromLeft(96).withSizeKeepingCentre(96, theme_.sizes.controlCompact));
    }
    void paramsChanged() override { graph_.repaint(); }
    void framesArrived(double) override { bars_.repaint(); }

    DelayGraph graph_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
    PillToggle* pingPong_ = nullptr;
};

} // namespace

std::unique_ptr<ModuleCard>
createBuiltInCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    if (insert.type == types::gain)
        return std::make_unique<GainCard>(theme, host, channelId, insert);
    if (insert.type == types::eq)
        return std::make_unique<EqCard>(theme, host, channelId, insert);
    if (insert.type == types::compressor)
        return createCompressorCard(theme, host, channelId, insert); // the strip compressor's
    if (insert.type == types::limiter)
        return std::make_unique<LimiterCard>(theme, host, channelId, insert);
    if (insert.type == types::reverb)
        return std::make_unique<ReverbCard>(theme, host, channelId, insert);
    if (insert.type == types::delay)
        return std::make_unique<DelayCard>(theme, host, channelId, insert);
    if (insert.type == "aural.aurora")
        return createAuroraEditor(theme, host, channelId, insert);
    if (insert.type == "aural.pulsar")
        return createPulsarEditor(theme, host, channelId, insert);
    if (insert.type == "aural.nebula")
        return createNebulaEditor(theme, host, channelId, insert);
    if (insert.type == "aural.starbit")
        return createStarbitEditor(theme, host, channelId, insert);
    return nullptr;
}

} // namespace aural::ui::strip
