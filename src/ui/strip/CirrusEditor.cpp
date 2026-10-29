// Cirrus's editor (screens/plugins.md 7.4h). An FM synth's face: an evening sky
// in which the sound is the light on the cloud. A low sun sits in the header
// band under cirrus streaks that take colour as it plays, with a sundog either
// side, further out the harder the modulators work. Under it five glass panels:
// the algorithm (its routing drawn large, a picker of all eight and Feedback),
// the four operators as columns, the selected operator's envelope and knobs, the
// tone (the output's spectrum, the macros and the output) and pitch and the LFO.
// Each operator keeps one hue wherever it appears.

#include "ui/strip/CirrusEditor.h"

#include "plugins/BeatSync.h"
#include "plugins/Cirrus.h"
#include "plugins/fm/Fm.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/strip/AdsrDisplay.h"
#include "ui/strip/CardParts.h"
#include "ui/strip/PresetBar.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"
#include "ui/strip/TapSpectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace northlight::ui::strip
{

namespace
{
namespace cr = plugins::cirrus;
namespace fm = plugins::fm;
constexpr int ops = fm::operatorCount;

// The hues: one per operator wherever it appears, then the panels'.
struct Hues
{
    std::array<juce::Colour, ops> op;
    juce::Colour algorithm, tone, motion, dusk;
    explicit Hues(const Theme& t)
        : op{{paletteColour(t, "Thrift"),
              paletteColour(t, "Gorse"),
              paletteColour(t, "Sea grass"),
              paletteColour(t, "Loch")}},
          algorithm(paletteColour(t, "Heather")), tone(paletteColour(t, "Straw")),
          motion(paletteColour(t, "Lichen")), dusk(paletteColour(t, "Gorse"))
    {
    }
};

int algorithmIndex(const ModuleCard& card)
{
    return juce::jlimit(0, fm::algorithmCount - 1, static_cast<int>(std::lround(card.param(cr::algorithm))));
}

const fm::Algorithm& algorithmOf(const ModuleCard& card)
{
    return fm::algorithms[static_cast<size_t>(algorithmIndex(card))];
}

float opValue(const ModuleCard& card, int op, cr::OpField field)
{
    return card.param(cr::opParam(op, field));
}

bool isMuted(const ModuleCard& card, int op)
{
    return opValue(card, op, cr::level) <= 0.0f;
}

bool isFixed(const ModuleCard& card, int op)
{
    return opValue(card, op, cr::fixed) >= 0.5f;
}

// The operators a given one modulates, as "1" or "1, 2 and 3".
juce::String targetsOf(const fm::Algorithm& a, int op)
{
    juce::StringArray targets;
    for (int t = 0; t < op; ++t)
        if (((a.modulators[static_cast<size_t>(t)] >> op) & 1u) != 0)
            targets.add(juce::String(t + 1));
    if (targets.size() <= 1)
        return targets.joinIntoString("");
    const auto last = targets[targets.size() - 1];
    targets.remove(targets.size() - 1);
    return targets.joinIntoString(", ") + " and " + last;
}

// "2 into 1, 4 into 3"; "all four heard" for the additive one.
juce::String describe(const fm::Algorithm& a)
{
    juce::StringArray parts;
    for (int t = 0; t < ops; ++t)
    {
        juce::StringArray from;
        for (int m = t + 1; m < ops; ++m)
            if (((a.modulators[static_cast<size_t>(t)] >> m) & 1u) != 0)
                from.add(juce::String(m + 1));
        if (!from.isEmpty())
            parts.add(from.joinIntoString(" and ") + " into " + juce::String(t + 1));
    }
    return parts.isEmpty() ? juce::String("all four heard") : parts.joinIntoString(", ");
}

// An operator's ratio or fixed frequency as its column reads it: "×1", "×3.51",
// "2.35k".
juce::String readoutOf(const ModuleCard& card, int op)
{
    if (isFixed(card, op))
    {
        const double hz = opValue(card, op, cr::fixedHz);
        return hz >= 1000.0 ? fixedText(hz / 1000.0, hz >= 10000.0 ? 1 : 2) + "k"
                            : juce::String(static_cast<int>(std::lround(hz)));
    }
    const double ratio =
        cr::ratioOf(opValue(card, op, cr::ratio)) * (1.0 + opValue(card, op, cr::fine) * 0.01);
    const bool whole = std::abs(ratio - std::round(ratio)) < 0.005;
    return juce::String::fromUTF8("\xc3\x97") +
           (whole ? juce::String(static_cast<int>(std::lround(ratio))) : juce::String(ratio, 2));
}

// A live level (a linear gain, 0..1) on the level's own 0..99 scale.
float levelSteps(float gain)
{
    return gain <= 1.0e-6f ? 0.0f : juce::jlimit(0.0f, 99.0f, 99.0f + 8.0f * std::log2(gain));
}

// An ADSR's shape in `r`, as AdsrDisplay draws it: each stage a quarter of the
// width, its time on a log scale up to its parameter's maximum.
juce::Path envelopeShape(const ModuleCard& card, int op, juce::Rectangle<float> r)
{
    const auto span = [&](cr::OpField f)
    {
        const auto& i = card.info(cr::opParam(op, f));
        return std::log1p(opValue(card, op, f)) / std::log1p(i.maximum);
    };
    const float q = r.getWidth() * 0.25f;
    const float sustain = juce::jlimit(0.0f, 1.0f, opValue(card, op, cr::sustain));
    const float peakX = r.getX() + q * span(cr::attack);
    const float decayX = peakX + q * span(cr::decay);
    const float holdX = r.getX() + 3.0f * q;
    const float endX = holdX + q * span(cr::release);
    const float sustainY = r.getBottom() - sustain * r.getHeight();
    juce::Path p;
    p.startNewSubPath(r.getX(), r.getBottom());
    p.lineTo(peakX, r.getY());
    p.lineTo(decayX, sustainY);
    p.lineTo(holdX, sustainY);
    p.lineTo(endX, r.getBottom());
    return p;
}

// One cycle of a wave in `r`.
juce::Path waveShape(fm::Wave wave, juce::Rectangle<float> r)
{
    juce::Path p;
    const int n = std::max(8, static_cast<int>(r.getWidth()));
    for (int i = 0; i <= n; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(n);
        const float x = r.getX() + phase * r.getWidth();
        const float y = r.getCentreY() - fm::waveAt(wave, phase) * r.getHeight() * 0.5f;
        if (i == 0)
            p.startNewSubPath(x, y);
        else
            p.lineTo(x, y);
    }
    return p;
}

fm::Wave waveOf(const ModuleCard& card, int op)
{
    return static_cast<fm::Wave>(
        juce::jlimit(0, fm::waveCount - 1, static_cast<int>(std::lround(opValue(card, op, cr::wave)))));
}

void drawFocus(juce::Graphics& g, const juce::Component& c, juce::Rectangle<float> r, float radius)
{
    if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&c.getLookAndFeel()))
        laf->drawFocusRing(g, c, r, radius);
}

// ----- the routing diagram (design-system.md "Routing diagram") --------------------------------

// Each algorithm's discs on a grid: a column (halves allowed) and a row up from
// the output, so the signal runs down the well.
struct Place
{
    float x, row;
};
constexpr std::array<std::array<Place, ops>, fm::algorithmCount> places{{
    {{{1.5f, 0}, {1.5f, 1}, {1.5f, 2}, {1.5f, 3}}}, // 4 > 3 > 2 > 1
    {{{1.5f, 0}, {1.5f, 1}, {1.0f, 2}, {2.0f, 2}}}, // (3 + 4) > 2 > 1
    {{{1.5f, 0}, {1.0f, 1}, {1.0f, 2}, {2.0f, 1}}}, // 3 > 2 > 1, 4 > 1
    {{{1.5f, 0}, {1.0f, 1}, {2.0f, 1}, {2.0f, 2}}}, // 4 > 3 > 1, 2 > 1
    {{{1.0f, 0}, {1.0f, 1}, {2.0f, 0}, {2.0f, 1}}}, // 2 > 1, 4 > 3
    {{{0.5f, 0}, {1.5f, 0}, {2.5f, 0}, {1.5f, 1}}}, // 4 > 1, 2, 3
    {{{0.5f, 0}, {1.5f, 0}, {2.5f, 0}, {2.5f, 1}}}, // 4 > 3; 1, 2, 3 heard
    {{{0.0f, 0}, {1.0f, 0}, {2.0f, 0}, {3.0f, 0}}}, // all four heard
}};

struct Routing
{
    std::array<juce::Point<float>, ops> centre{};
    float radius = 0.0f, outputY = 0.0f;
};

// Where an algorithm's discs sit in `r`, with room for op 4's loop above and to
// the right and the output line at the foot.
Routing routingIn(juce::Rectangle<float> r, int algorithm, float disc)
{
    Routing out;
    out.radius = disc * 0.5f;
    const auto& at = places[static_cast<size_t>(algorithm)];
    float minX = 9.0f, maxX = -9.0f, rows = 0.0f;
    for (const auto& p : at)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        rows = std::max(rows, p.row);
    }
    const float loop = out.radius * 2.6f;
    out.outputY = r.getBottom() - 1.5f;
    const float bottomRow = out.outputY - disc * 0.45f - out.radius;
    const float stepY = rows > 0.0f ? std::min(disc * 2.1f, (bottomRow - (r.getY() + loop)) / rows) : 0.0f;
    const float stepX =
        maxX > minX ? std::min(disc * 2.3f, (r.getWidth() - 2.0f * loop) / (maxX - minX)) : 0.0f;
    const float mid = 0.5f * (minX + maxX);
    for (int op = 0; op < ops; ++op)
    {
        const auto& p = at[static_cast<size_t>(op)];
        out.centre[static_cast<size_t>(op)] = {r.getCentreX() + (p.x - mid) * stepX,
                                               bottomRow - p.row * stepY};
    }
    return out;
}

// The loop from op 4's right side back to its top.
juce::Path loopPath(juce::Point<float> c, float radius)
{
    juce::Path p;
    p.startNewSubPath(c.x + radius, c.y);
    p.cubicTo(c.x + radius * 2.5f, c.y, c.x + radius * 2.5f, c.y - radius * 2.4f, c.x, c.y - radius * 2.4f);
    p.lineTo(c.x, c.y - radius);
    return p;
}

struct RoutingLook
{
    bool small = false;
    bool chosen = false; // a picker cell that is the chosen algorithm: its lines in textPrimary
    std::array<float, ops> live{};
    std::array<bool, ops> muted{};
    int selected = -1, hovered = -1;
    float feedback = 0.0f;
};

void drawRouting(juce::Graphics& g,
                 const Theme& theme,
                 const Hues& hues,
                 juce::Rectangle<float> r,
                 int algorithm,
                 const RoutingLook& look)
{
    const auto& c = theme.colours;
    const auto& a = fm::algorithms[static_cast<size_t>(algorithm)];
    const float disc = look.small ? 6.0f : 22.0f;
    const auto routing = routingIn(r, algorithm, disc);
    const float rad = routing.radius;
    const float stroke = look.small ? 1.0f : 1.5f;
    const auto fade = [&](int op) { return look.muted[static_cast<size_t>(op)] ? 0.4f : 1.0f; };
    const auto lineColour = [&](int op)
    {
        if (look.chosen)
            return c.textPrimary.withAlpha(0.85f * fade(op));
        const float live = look.small ? 0.0f : look.live[static_cast<size_t>(op)];
        return hues.op[static_cast<size_t>(op)].withAlpha((0.55f + 0.4f * live) * fade(op));
    };

    // The output line under the carriers, and each carrier's drop to it.
    float left = r.getRight(), right = r.getX();
    for (int op = 0; op < ops; ++op)
        if (a.isCarrier(op))
        {
            const auto p = routing.centre[static_cast<size_t>(op)];
            left = std::min(left, p.x - rad);
            right = std::max(right, p.x + rad);
            g.setColour(lineColour(op));
            g.drawLine(p.x, p.y + rad, p.x, routing.outputY, stroke);
        }
    g.setColour(look.chosen ? c.textPrimary : c.lineStrong);
    g.drawLine(left, routing.outputY, right, routing.outputY, stroke);

    // The connections: down from the feeder, across, down into the one it feeds.
    for (int t = 0; t < ops; ++t)
    {
        std::vector<int> from;
        for (int m = t + 1; m < ops; ++m)
            if (((a.modulators[static_cast<size_t>(t)] >> m) & 1u) != 0)
                from.push_back(m);
        std::sort(
            from.begin(),
            from.end(),
            [&](int x, int y)
            { return routing.centre[static_cast<size_t>(x)].x < routing.centre[static_cast<size_t>(y)].x; });
        const auto target = routing.centre[static_cast<size_t>(t)];
        for (size_t i = 0; i < from.size(); ++i)
        {
            const auto source = routing.centre[static_cast<size_t>(from[i])];
            const float offset =
                from.size() > 1
                    ? (static_cast<float>(i) - 0.5f * static_cast<float>(from.size() - 1)) * rad * 0.7f
                    : 0.0f;
            const juce::Point<float> start{source.x, source.y + rad};
            const juce::Point<float> end{target.x + offset, target.y - rad};
            juce::Path link;
            link.startNewSubPath(start);
            if (std::abs(start.x - end.x) > 0.5f)
            {
                const float midY = 0.5f * (start.y + end.y);
                link.lineTo(start.x, midY);
                link.lineTo(end.x, midY);
            }
            link.lineTo(end);
            g.setColour(lineColour(from[i]));
            g.strokePath(link.createPathWithRoundedCorners(look.small ? 1.5f : 4.0f),
                         juce::PathStrokeType(stroke));
        }
    }

    // Op 4's feedback loop: thicker and brighter with the amount, dotted at 0.
    {
        const auto p = routing.centre[ops - 1];
        const auto loop = loopPath(p, rad);
        const auto hue = look.chosen ? c.textPrimary : hues.op[ops - 1];
        if (look.feedback <= 0.0f)
        {
            juce::Path dotted;
            const float dashes[] = {1.5f, 2.5f};
            juce::PathStrokeType(look.small ? 0.8f : 1.2f).createDashedStroke(dotted, loop, dashes, 2);
            g.setColour(hue.withAlpha(0.45f * fade(ops - 1)));
            g.fillPath(dotted);
        }
        else
        {
            g.setColour(hue.withAlpha((0.5f + 0.5f * look.feedback) * fade(ops - 1)));
            g.strokePath(loop, juce::PathStrokeType(stroke * (1.0f + look.feedback)));
        }
    }

    // The discs, with their halos following the live levels.
    for (int op = 0; op < ops; ++op)
    {
        const auto o = static_cast<size_t>(op);
        const auto p = routing.centre[o];
        const auto body = juce::Rectangle<float>(disc, disc).withCentre(p);
        const auto hue = hues.op[o];
        if (!look.small && look.live[o] > 0.01f)
        {
            g.setColour(hue.withAlpha(0.45f * look.live[o] * fade(op)));
            g.fillEllipse(body.expanded(rad * 1.2f));
        }
        g.setColour(hue.withAlpha(0.85f * fade(op)));
        g.fillEllipse(body);
        if (look.small)
            continue;
        g.setColour(c.textOnColour.withAlpha(fade(op)));
        g.setFont(theme.label());
        g.drawText(juce::String(op + 1), body, juce::Justification::centred);
        if (op == look.hovered && op != look.selected)
        {
            g.setColour(c.textPrimary.withAlpha(0.3f));
            g.drawEllipse(body, 1.0f);
        }
        if (op == look.selected)
        {
            g.setColour(c.textPrimary);
            g.drawEllipse(body.expanded(2.75f), 1.5f);
        }
    }
}

// The chosen algorithm drawn large: click a disc to select its operator, drag
// the loop for Feedback.
class RoutingDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    RoutingDisplay(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setWantsKeyboardFocus(true);
        setTitle("Algorithm");
    }
    std::function<int()> selected;
    std::function<void(int)> onSelect;
    std::array<float, ops> live{};

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        RoutingLook look;
        look.live = live;
        for (int op = 0; op < ops; ++op)
            look.muted[static_cast<size_t>(op)] = isMuted(card_, op);
        look.selected = selected ? selected() : -1;
        look.hovered = hovered_;
        look.feedback = card_.param(cr::feedback);
        drawRouting(g, theme, hues_, area(), algorithmIndex(card_), look);
        drawWellFrame(g, theme, b);
        if (look.selected >= 0)
        {
            const auto routing = routingIn(area(), algorithmIndex(card_), 22.0f);
            const auto p = routing.centre[static_cast<size_t>(look.selected)];
            drawFocus(g, *this, juce::Rectangle<float>(28.0f, 28.0f).withCentre(p), 14.0f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int disc = discAt(e.position);
        const bool loop = disc < 0 && overLoop(e.position);
        if (disc != hovered_)
        {
            hovered_ = disc;
            repaint();
        }
        setMouseCursor(loop        ? juce::MouseCursor::UpDownResizeCursor
                       : disc >= 0 ? juce::MouseCursor::PointingHandCursor
                                   : juce::MouseCursor::NormalCursor);
        if (loop)
            setTooltip("Feedback: drag up or down");
        else if (disc >= 0)
        {
            const auto& a = algorithmOf(card_);
            setTooltip("Operator " + juce::String(disc + 1) + ": " +
                       (a.isCarrier(disc) ? juce::String("heard") : "modulates " + targetsOf(a, disc)) +
                       ". Click to edit.");
        }
        else
            setTooltip({});
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (hovered_ >= 0)
        {
            hovered_ = -1;
            repaint();
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (const int disc = discAt(e.position); disc >= 0)
        {
            if (onSelect)
                onSelect(disc);
            return;
        }
        if (overLoop(e.position))
        {
            dragging_ = true;
            start_ = card_.param(cr::feedback);
            card_.beginGesture("Feedback");
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging_)
            return;
        const float rate = e.mods.isShiftDown() ? 0.0005f : 0.005f;
        card_.setParam(cr::feedback, juce::jlimit(0.0f, 1.0f, start_ - e.getDistanceFromDragStartY() * rate));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_)
            card_.endGesture();
        dragging_ = false;
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const int now = selected ? selected() : 0;
        if (key == juce::KeyPress::leftKey && now > 0 && onSelect)
            onSelect(now - 1);
        else if (key == juce::KeyPress::rightKey && now < ops - 1 && onSelect)
            onSelect(now + 1);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(8.0f, 6.0f); }
    int discAt(juce::Point<float> at) const
    {
        const auto routing = routingIn(area(), algorithmIndex(card_), 22.0f);
        for (int op = 0; op < ops; ++op)
            if (routing.centre[static_cast<size_t>(op)].getDistanceFrom(at) <= routing.radius + 3.0f)
                return op;
        return -1;
    }
    bool overLoop(juce::Point<float> at) const
    {
        const auto routing = routingIn(area(), algorithmIndex(card_), 22.0f);
        return loopPath(routing.centre[ops - 1], routing.radius).getBounds().expanded(4.0f).contains(at);
    }

    ModuleCard& card_;
    const Hues& hues_;
    int hovered_ = -1;
    bool dragging_ = false;
    float start_ = 0.0f;
};

// The eight algorithms as small diagrams, 4 by 2; a click or an arrow chooses.
class AlgorithmPicker final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int cellW = 36, cellH = 30, cellGap = 4;
    static constexpr int width = 4 * cellW + 3 * cellGap, height = 2 * cellH + cellGap;

    AlgorithmPicker(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setWantsKeyboardFocus(true);
        setTitle("Algorithms");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const int chosen = algorithmIndex(card_);
        for (int i = 0; i < fm::algorithmCount; ++i)
        {
            const auto r = cell(i).toFloat();
            if (i == chosen)
            {
                g.setColour(theme.colours.surface2);
                g.fillRoundedRectangle(r, theme.radius.sm);
                g.setColour(hues_.algorithm);
                g.drawRoundedRectangle(r.reduced(0.75f), theme.radius.sm, 1.5f);
            }
            else if (i == hovered_)
            {
                g.setColour(theme.colours.surface2.withAlpha(0.5f));
                g.fillRoundedRectangle(r, theme.radius.sm);
            }
            RoutingLook look;
            look.small = true;
            look.chosen = i == chosen;
            look.feedback = card_.param(cr::feedback);
            drawRouting(g, theme, hues_, r.reduced(4.0f, 3.0f), i, look);
        }
        drawFocus(g, *this, cell(chosen).toFloat(), theme.radius.sm);
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        const int i = cellAt(e.getPosition());
        if (i != hovered_)
        {
            hovered_ = i;
            repaint();
        }
        setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        setTooltip(i >= 0 ? "Algorithm " + juce::String(i + 1) + ": " +
                                describe(fm::algorithms[static_cast<size_t>(i)])
                          : juce::String());
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hovered_ = -1;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (const int i = cellAt(e.getPosition()); i >= 0)
            choose(i);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const int now = algorithmIndex(card_);
        int next = now;
        if (key == juce::KeyPress::leftKey)
            next = now - 1;
        else if (key == juce::KeyPress::rightKey)
            next = now + 1;
        else if (key == juce::KeyPress::upKey)
            next = now - 4;
        else if (key == juce::KeyPress::downKey)
            next = now + 4;
        else
            return false;
        if (next >= 0 && next < fm::algorithmCount)
            choose(next);
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    juce::Rectangle<int> cell(int i) const
    {
        return {(i % 4) * (cellW + cellGap), (i / 4) * (cellH + cellGap), cellW, cellH};
    }
    int cellAt(juce::Point<int> p) const
    {
        for (int i = 0; i < fm::algorithmCount; ++i)
            if (cell(i).contains(p))
                return i;
        return -1;
    }
    void choose(int i)
    {
        if (i == algorithmIndex(card_))
            return;
        card_.beginGesture("Algorithm " + juce::String(i + 1));
        card_.setParam(cr::algorithm, static_cast<float>(i));
        card_.endGesture();
    }

    ModuleCard& card_;
    const Hues& hues_;
    int hovered_ = -1;
};

// ----- the operator columns (design-system.md "Operator column") -------------------------------

// An operator's level as a groove with the live level inside it, its value
// under it; drag, the wheel or the arrows (from its column) set it.
class LevelBar final : public juce::Component
{
public:
    LevelBar(ModuleCard& card, int op, juce::Colour hue) : card_(card), op_(op), hue_(hue)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    float live = 0.0f; // 0..99

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const float level = opValue(card_, op_, cr::level);
        const float fade = level <= 0.0f ? 0.4f : 1.0f;
        const auto groove = grooveArea();
        g.setColour(c.surface0);
        g.fillRoundedRectangle(groove, groove.getWidth() * 0.5f);
        const auto fill = [&](float steps)
        {
            return groove.withTrimmedTop(groove.getHeight() *
                                         (1.0f - juce::jlimit(0.0f, 99.0f, steps) / 99.0f));
        };
        if (level > 0.0f)
        {
            g.setColour(hue_.withAlpha(0.4f * fade));
            g.fillRoundedRectangle(fill(level), groove.getWidth() * 0.5f);
        }
        if (live > 0.5f)
        {
            g.setColour(hue_.withAlpha(fade));
            g.fillRoundedRectangle(fill(std::min(live, level)).reduced(1.5f, 0.0f), groove.getWidth() * 0.5f);
        }
        g.setFont(theme.value());
        g.setColour(c.textPrimary.withAlpha(fade));
        g.drawText(juce::String(static_cast<int>(std::lround(level))),
                   getLocalBounds().removeFromBottom(valueH),
                   juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        start_ = opValue(card_, op_, cr::level);
        card_.beginGesture(name());
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float rate = e.mods.isShiftDown() ? 0.1f : 1.0f;
        set(start_ - static_cast<float>(e.getDistanceFromDragStartY()) * rate);
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        card_.beginGesture(name());
        set(card_.info(cr::opParam(op_, cr::level)).defaultValue);
        card_.endGesture();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        step(wheel.deltaY > 0.0f ? 1.0f : -1.0f);
    }
    // One step up or down, as one undo step.
    void step(float by)
    {
        card_.beginGesture(name());
        set(std::round(opValue(card_, op_, cr::level)) + by);
        card_.endGesture();
    }

    static constexpr int valueH = 16;

private:
    juce::String name() const { return "Op " + juce::String(op_ + 1) + " level"; }
    juce::Rectangle<float> grooveArea() const
    {
        return getLocalBounds()
            .withTrimmedBottom(valueH + 2)
            .toFloat()
            .withSizeKeepingCentre(8.0f, static_cast<float>(getHeight() - valueH - 2));
    }
    void set(float v)
    {
        card_.setParam(cr::opParam(op_, cr::level), juce::jlimit(0.0f, 99.0f, v));
        repaint();
    }

    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
    float start_ = 0.0f;
};

// One operator's column: its number and role, its ratio, its wave, its level
// and its envelope. A click selects it.
class OperatorColumn final : public juce::Component, public juce::SettableTooltipClient
{
public:
    OperatorColumn(ModuleCard& card, int op, juce::Colour hue)
        : bar(card, op, hue), card_(card), op_(op), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setTitle("Operator " + juce::String(op + 1));
        addAndMakeVisible(bar);
    }
    LevelBar bar;
    bool selected = false;
    std::function<void(int)> onSelect;

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop(tagH + readoutH + waveH);
        r.removeFromBottom(envelopeH + 18);
        bar.setBounds(r.reduced(0, 2));
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto b = getLocalBounds().toFloat();
        if (selected)
        {
            g.setColour(c.surface2);
            g.fillRoundedRectangle(b, theme.radius.md);
            g.setColour(hue_);
            g.drawRoundedRectangle(b.reduced(0.75f), theme.radius.md, 1.5f);
        }
        else if (isMouseOver(true))
        {
            g.setColour(c.surface2.withAlpha(0.5f));
            g.fillRoundedRectangle(b, theme.radius.md);
        }
        const bool muted = isMuted(card_, op_);
        const float fade = muted ? 0.4f : 1.0f;
        const bool carrier = algorithmOf(card_).isCarrier(op_);
        auto r = getLocalBounds();

        // The tag: the disc and the role.
        auto tag = r.removeFromTop(tagH);
        const auto disc =
            tag.removeFromTop(22).toFloat().withSizeKeepingCentre(16.0f, 16.0f).translated(0.0f, 3.0f);
        g.setColour(hue_.withAlpha(0.85f * fade));
        g.fillEllipse(disc);
        g.setColour(c.textOnColour.withAlpha(fade));
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.micro, theme.fonts.weight.semibold));
        g.drawText(juce::String(op_ + 1), disc, juce::Justification::centred);
        g.setFont(theme.captionCaps());
        g.setColour(c.textTertiary.withAlpha(fade));
        const juce::String role = carrier ? "OUT" : "MOD";
        g.drawText(role, tag, juce::Justification::centredTop);
        if (carrier && muted)
        {
            const float w =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.captionCaps(), role));
            const float y =
                static_cast<float>(tag.getY()) + theme.lineHeight(theme.fonts.size.caption) * 0.5f;
            g.drawLine(static_cast<float>(tag.getCentreX()) - w * 0.5f - 1.0f,
                       y,
                       static_cast<float>(tag.getCentreX()) + w * 0.5f + 1.0f,
                       y,
                       1.0f);
        }

        // The readout: the ratio or the fixed frequency, then an offset.
        auto readout = r.removeFromTop(readoutH);
        g.setFont(theme.value());
        g.setColour(c.textPrimary.withAlpha(fade));
        g.drawText(readoutOf(card_, op_), readout.removeFromTop(16), juce::Justification::centred);
        juce::String under;
        if (isFixed(card_, op_))
            under = "Hz";
        else if (const float cents = opValue(card_, op_, cr::detune); std::abs(cents) >= 0.5f)
            under = signedText(cents, 0) + " ct";
        g.setFont(theme.caption());
        g.setColour(c.textTertiary.withAlpha(fade));
        g.drawText(under, readout.removeFromTop(14), juce::Justification::centred);

        // The wave.
        const auto wave = r.removeFromTop(waveH).toFloat().withSizeKeepingCentre(36.0f, 12.0f);
        g.setColour(hue_.withAlpha(fade));
        g.strokePath(waveShape(waveOf(card_, op_), wave), juce::PathStrokeType(1.25f));

        // The envelope at the foot.
        const auto envelope = getLocalBounds()
                                  .removeFromBottom(envelopeH + 6)
                                  .toFloat()
                                  .withSizeKeepingCentre(40.0f, envelopeH)
                                  .translated(0.0f, -2.0f);
        g.setColour(hue_.withAlpha(0.7f * fade));
        g.strokePath(
            envelopeShape(card_, op_, envelope),
            juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        drawFocus(g, *this, b, theme.radius.md);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect(op_);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey)
            bar.step(1.0f);
        else if (key == juce::KeyPress::downKey)
            bar.step(-1.0f);
        else if (key == juce::KeyPress::returnKey && onSelect)
            onSelect(op_);
        else if (key == juce::KeyPress::leftKey && op_ > 0 && onSelect)
            onSelect(op_ - 1);
        else if (key == juce::KeyPress::rightKey && op_ < ops - 1 && onSelect)
            onSelect(op_ + 1);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    void updateTooltip()
    {
        const auto& a = algorithmOf(card_);
        setTooltip("Operator " + juce::String(op_ + 1) + ": " +
                   (a.isCarrier(op_) ? juce::String("heard") : "modulates " + targetsOf(a, op_)) +
                   ". Click to edit.");
    }

private:
    static constexpr int tagH = 36, readoutH = 32, waveH = 20, envelopeH = 20;
    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
};

// ----- the selected operator --------------------------------------------------------------------

// The selected operator's envelope, to drag; the fill brightens with its live level.
class OperatorEnvelope final : public AdsrDisplay
{
public:
    OperatorEnvelope(ModuleCard& card, const Hues& hues) : AdsrDisplay(card), hues_(hues)
    {
        setTooltip("Drag the handles: attack, decay and sustain, release");
    }
    int op = 0;
    float live = 0.0f; // 0..1

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = plot();
        const auto hue = hues_.op[static_cast<size_t>(op)];
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            for (int i = 1; i < 4; ++i)
            {
                g.setColour(theme.colours.gridBeat);
                g.drawVerticalLine(static_cast<int>(p.getX() + p.getWidth() * static_cast<float>(i) / 4.0f),
                                   p.getY(),
                                   p.getBottom());
            }
            const auto at = points(editedAttack());
            const auto line = path(at);
            juce::Path area = line;
            area.lineTo(at.back().x, p.getBottom());
            area.closeSubPath();
            g.setColour(hue.withAlpha(0.18f + 0.22f * juce::jlimit(0.0f, 1.0f, live)));
            g.fillPath(area);
            g.setColour(hue);
            g.strokePath(
                line,
                juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
        drawWellFrame(g, theme, b);
        drawHandles(g, hue);
    }

private:
    int editedAttack() const override { return cr::opParam(op, cr::attack); }
    juce::Rectangle<float> plot() const override { return getLocalBounds().toFloat().reduced(8.0f, 8.0f); }

    const Hues& hues_;
};

// The selected operator's wave: its name and a tiny cycle, opening a menu of
// the eight waves, each drawn.
class WaveChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveChip(ModuleCard& card) : card_(card)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity(true);
        setTooltip("Wave");
    }
    int op = 0;
    juce::Colour hue;

    int preferredWidth() const
    {
        int w = 0;
        for (const auto& c : card_.info(cr::opParam(0, cr::wave)).choices)
            w = std::max(w, juce::GlyphArrangement::getStringWidthInt(font(), juce::String(c)));
        return w + 44;
    }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(hue.withAlpha(isMouseOver() ? 0.32f : 0.2f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        auto inner = getLocalBounds().reduced(7, 0);
        const auto wave = waveOf(card_, op);
        g.setColour(hue);
        g.strokePath(waveShape(wave, inner.removeFromLeft(12).toFloat().withSizeKeepingCentre(12.0f, 7.0f)),
                     juce::PathStrokeType(1.0f));
        inner.removeFromLeft(4);
        g.setFont(font());
        const auto& names = card_.info(cr::opParam(op, cr::wave)).choices;
        g.drawText(juce::String(names[static_cast<size_t>(wave)]) + juce::String::fromUTF8(" \xe2\x96\xbe"),
                   inner,
                   juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        const auto& names = card_.info(cr::opParam(op, cr::wave)).choices;
        const int current = static_cast<int>(waveOf(card_, op));
        for (int w = 0; w < fm::waveCount; ++w)
        {
            juce::PopupMenu::Item item;
            item.text = juce::String(names[static_cast<size_t>(w)]);
            item.itemID = w + 1;
            item.isTicked = w == current;
            auto icon = std::make_unique<juce::DrawablePath>();
            icon->setPath(waveShape(static_cast<fm::Wave>(w), {0.0f, 0.0f, 18.0f, 10.0f}));
            icon->setFill(juce::FillType(juce::Colours::transparentBlack));
            icon->setStrokeFill(juce::FillType(hue));
            icon->setStrokeType(juce::PathStrokeType(1.25f));
            item.image = std::move(icon);
            menu.addItem(std::move(item));
        }
        juce::Component::SafePointer<WaveChip> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe](int r)
                           {
                               if (safe == nullptr || r <= 0)
                                   return;
                               auto& card = safe->card_;
                               card.beginGesture("Op " + juce::String(safe->op + 1) + " wave");
                               card.setParam(cr::opParam(safe->op, cr::wave), static_cast<float>(r - 1));
                               card.endGesture();
                           });
    }

private:
    juce::Font font() const
    {
        const auto& t = card_.theme();
        return t.font(Theme::Face::ui, t.fonts.size.micro, t.fonts.weight.semibold);
    }
    ModuleCard& card_;
};

// ----- tone -------------------------------------------------------------------------------------

// The output's spectrum, live, with the high cut drawn over it to drag across.
class SpectrumDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTitle("Spectrum");
        setTooltip("Drag across for the high cut");
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    ~SpectrumDisplay() override
    {
        if (auto* fb = card_.liveFeedback())
            fb->setTapsEnabled(false);
    }

    // Each tick: the analyser runs about 30 times a second while the card shows.
    void update(double elapsedMs)
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr)
            return;
        if (!fb->tapsEnabled())
            fb->setTapsEnabled(true);
        sinceAnalysisMs_ += elapsedMs;
        if (sinceAnalysisMs_ < 30.0)
            return;
        const bool had = showing();
        spectrum_.analyse(*fb, 1, plot(), sinceAnalysisMs_);
        sinceAnalysisMs_ = 0.0;
        if (had || showing())
            repaint();
    }
    void resized() override { spectrum_.clear(); }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = plot();
        // Three lines: the plot is too narrow to label more.
        static constexpr std::array<double, 3> lines{100.0, 1000.0, 10000.0};
        drawFrequencyGrid(g, theme, p, lines);
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(p.toNearestInt());
            const auto& columns = spectrum_.spectrum(1);
            if (!columns.empty())
            {
                juce::Path line, area;
                for (size_t i = 0; i < columns.size(); ++i)
                {
                    const float x = p.getX() + static_cast<float>(i);
                    const float y = dbToY(columns[i], TapSpectrum::topDb, TapSpectrum::bottomDb, p);
                    if (i == 0)
                    {
                        line.startNewSubPath(x, y);
                        area.startNewSubPath(x, p.getBottom());
                    }
                    else
                        line.lineTo(x, y);
                    area.lineTo(x, y);
                }
                area.lineTo(p.getX() + static_cast<float>(columns.size() - 1), p.getBottom());
                area.closeSubPath();
                g.setColour(hue_.withAlpha(0.28f));
                g.fillPath(area);
                g.setColour(hue_);
                g.strokePath(line, juce::PathStrokeType(1.5f));
            }
            // The high cut: flat to its corner, then 12 dB an octave.
            const float hc = card_.param(cr::highCut);
            const float corner = hzToX(hc, p);
            const float y0 = cutY(p);
            juce::Path cut;
            cut.startNewSubPath(p.getX(), y0);
            cut.lineTo(corner, y0);
            for (float x = corner; x <= p.getRight(); x += 2.0f)
            {
                const double hz = xToHz(x, p);
                const float drop = 12.0f * static_cast<float>(std::log2(hz / hc));
                cut.lineTo(x, y0 + drop * p.getHeight() / 96.0f);
            }
            juce::Path dashed;
            const float dashes[] = {4.0f, 3.0f};
            juce::PathStrokeType(1.25f).createDashedStroke(dashed, cut, dashes, 2);
            g.setColour(theme.colours.textTertiary);
            g.fillPath(dashed);
        }
        drawWellFrame(g, theme, b);
        drawHandle(g, theme, hue_, {hzToX(card_.param(cr::highCut), p), cutY(p)}, dragging_);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = true;
        card_.beginGesture("High cut");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto& info = card_.info(cr::highCut);
        card_.setParam(
            cr::highCut,
            juce::jlimit(info.minimum, info.maximum, static_cast<float>(xToHz(e.position.x, plot()))));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        dragging_ = false;
        card_.endGesture();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        card_.beginGesture("High cut");
        card_.setParam(cr::highCut, card_.info(cr::highCut).defaultValue);
        card_.endGesture();
        repaint();
    }

private:
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(6.0f, 6.0f).withTrimmedBottom(12.0f);
    }
    static float cutY(juce::Rectangle<float> p) { return p.getY() + p.getHeight() * 0.18f; }
    bool showing() const
    {
        const auto& s = spectrum_.spectrum(1);
        return std::any_of(s.begin(), s.end(), [](float v) { return v > TapSpectrum::bottomDb + 3.0f; });
    }

    ModuleCard& card_;
    juce::Colour hue_;
    TapSpectrum spectrum_{2};
    double sinceAnalysisMs_ = 0.0;
    bool dragging_ = false;
};

// ----- pitch and the LFO ------------------------------------------------------------------------

// A note's first second: the bend settling back to the note, the vibrato riding
// on it (shown four times as deep), the LFO's live value at the right edge.
class PitchDisplay final : public juce::Component
{
public:
    PitchDisplay(ModuleCard& card, juce::Colour hue)
        : card_(card), hue_(hue), shape_(card.theme(), hue), sync_(card.theme(), hue)
    {
        setTitle("Pitch");
        shape_.setChoices(card.info(cr::lfoShape));
        shape_.setTooltip("LFO shape");
        shape_.onChoose = [this](int s)
        {
            card_.beginGesture("LFO shape");
            card_.setParam(cr::lfoShape, static_cast<float>(s));
            card_.endGesture();
        };
        for (const auto& c : plugins::sync::choices())
            sync_.choices.emplace_back(c);
        sync_.choices.front() = "Free";
        sync_.setTooltip("Sync the LFO to the project tempo");
        sync_.onChoose = [this](int i)
        {
            card_.beginGesture("LFO sync");
            card_.setParam(cr::lfoSync, static_cast<float>(i));
            card_.endGesture();
        };
        addAndMakeVisible(shape_);
        addAndMakeVisible(sync_);
    }
    // The LFO now (-1..1), or below -1 while Cirrus is silent.
    float live = -2.0f;

    void resized() override
    {
        auto r = getLocalBounds().reduced(5);
        shape_.setBounds(r.removeFromTop(16).removeFromRight(shape_.preferredWidth()));
        sync_.setBounds(r.removeFromBottom(16).removeFromRight(sync_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        shape_.selected = juce::jlimit(0, 4, static_cast<int>(std::lround(card_.param(cr::lfoShape))));
        sync_.selected = plugins::sync::choiceOf(card_.param(cr::lfoSync));
        const auto p = plot();
        const auto yOf = [&](float semitones)
        { return p.getCentreY() - juce::jlimit(-12.0f, 12.0f, semitones) / 12.0f * p.getHeight() * 0.5f; };
        g.setColour(theme.colours.textTertiary);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());

        const float bend = card_.param(cr::pitchEnvelope);
        const float timeScale = std::exp2(2.0f * card_.param(cr::time) * 0.01f);
        const float settle = std::max(0.001f, card_.param(cr::pitchTime) * timeScale * 0.001f);
        const float depth = card_.param(cr::lfoPitch) * 0.01f * 4.0f;
        const int shape = shape_.selected;
        const double rate = lfoRate();
        juce::Path curve;
        const int n = std::max(2, static_cast<int>(p.getWidth()));
        for (int i = 0; i <= n; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(n); // seconds
            const float bent = bend * std::pow(10.0f, -3.0f * t / settle);
            const double phase = std::fmod(rate * t, 1.0);
            const float semis = bent + depth * lfoAt(shape, static_cast<float>(phase));
            const float x = p.getX() + t * p.getWidth();
            if (i == 0)
                curve.startNewSubPath(x, yOf(semis));
            else
                curve.lineTo(x, yOf(semis));
        }
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            g.setColour(hue_.withAlpha(0.25f));
            g.strokePath(curve, juce::PathStrokeType(4.0f));
            g.setColour(hue_);
            g.strokePath(curve, juce::PathStrokeType(1.5f));
        }
        if (live >= -1.0f)
        {
            const float y = yOf(live * std::max(0.5f, depth));
            g.setColour(theme.colours.textPrimary);
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({p.getRight() - 2.0f, y}));
        }
        drawWellFrame(g, theme, b);
    }

private:
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(16.0f).withTrimmedBottom(14.0f);
    }
    double lfoRate() const
    {
        const float choice = card_.param(cr::lfoSync);
        if (plugins::sync::quarters(choice) > 0.0)
            return 1.0 / std::max(1.0e-3, plugins::sync::seconds(choice, card_.tempoBpm()));
        return card_.param(cr::lfoRate);
    }
    static float lfoAt(int shape, float phase)
    {
        switch (shape)
        {
            case 0:
                return std::sin(juce::MathConstants<float>::twoPi * phase);
            case 1:
                return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
            case 2:
                return 2.0f * phase - 1.0f;
            case 3:
                return phase < 0.5f ? 1.0f : -1.0f;
            default:
            {
                // A fixed staircase stands for sample and hold.
                static const float steps[] = {0.2f, -0.6f, 0.9f, -0.1f, 0.5f, -0.9f, 0.3f, -0.4f};
                return steps[juce::jlimit(0, 7, static_cast<int>(phase * 8.0f))];
            }
        }
    }

    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice shape_, sync_;
};

// The operator each Cirrus card had selected, kept while the app runs.
std::map<core::Id, int>& selections()
{
    static std::map<core::Id, int> chosen;
    return chosen;
}

// ----- the editor -------------------------------------------------------------------------------

class CirrusCard final : public ModuleCard
{
public:
    CirrusCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), routing_(*this, hues_),
          picker_(*this, hues_), columns_{{{*this, 0, hues_.op[0]},
                                           {*this, 1, hues_.op[1]},
                                           {*this, 2, hues_.op[2]},
                                           {*this, 3, hues_.op[3]}}},
          envelope_(*this, hues_), wave_(*this), spectrum_(*this, hues_.tone), pitch_(*this, hues_.motion),
          quality_(theme, hues_.tone)
    {
        routing_.selected = [this] { return selected_; };
        routing_.onSelect = [this](int op) { select(op); };
        for (auto& c : columns_)
        {
            c.onSelect = [this](int op) { select(op); };
            addAndMakeVisible(c);
        }
        for (auto* c : std::initializer_list<juce::Component*>{
                 &routing_, &picker_, &envelope_, &wave_, &spectrum_, &pitch_})
            addAndMakeVisible(c);
        // Named for the tests and the screen reader's tree.
        routing_.setComponentID("routing");
        picker_.setComponentID("algorithms");
        envelope_.setComponentID("envelope");
        spectrum_.setComponentID("spectrum");
        pitch_.setComponentID("pitch");
        for (int op = 0; op < ops; ++op)
            columns_[static_cast<size_t>(op)].setComponentID("operator" + juce::String(op + 1));

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
        const auto integer = [](const char* suffix, bool sign)
        {
            return [suffix, sign](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                return (sign && n > 0 ? "+" : "") + juce::String(n) + suffix;
            };
        };
        const auto percent01 = [](double v) { return percentText(v * 100.0); };
        const auto ratio = [](double v)
        {
            const double r = cr::ratioOf(static_cast<float>(v));
            return juce::String::fromUTF8("\xc3\x97") +
                   (r < 1.0 ? juce::String("0.5") : juce::String(static_cast<int>(std::lround(r))));
        };
        for (int op = 0; op < ops; ++op)
        {
            const auto hue = hues_.op[static_cast<size_t>(op)];
            auto& k = opKnobs_[static_cast<size_t>(op)];
            k.ratio = knob(cr::opParam(op, cr::ratio), "Ratio", hue, ratio);
            k.fine = knob(cr::opParam(op, cr::fine), "Fine", hue, integer("%", true));
            k.detune = knob(cr::opParam(op, cr::detune), "Detune", hue, integer(" ct", true));
            k.level = knob(cr::opParam(op, cr::level), "Level", hue, integer("", false));
            k.attack = knob(cr::opParam(op, cr::attack), "Attack", hue);
            k.decay = knob(cr::opParam(op, cr::decay), "Decay", hue);
            k.sustain = knob(cr::opParam(op, cr::sustain), "Sustain", hue, percent01);
            k.release = knob(cr::opParam(op, cr::release), "Release", hue);
            k.fixedHz =
                knob(cr::opParam(op, cr::fixedHz), "Freq", hue, [](double v) { return compactHzText(v); });
            k.velocity = knob(cr::opParam(op, cr::velocity), "Velocity", hue, percent01);
            k.keyScale = knob(cr::opParam(op, cr::keyScale),
                              "Key scale",
                              hue,
                              [](double v) { return signedPercentText(v * 100.0); });
            k.fixed = &addToggle(cr::opParam(op, cr::fixed), "On");
            k.fixed->setTooltip("Fixed: a frequency of its own, whatever the note");
            k.fixedLabel = &addLabel("Fixed");
        }
        feedback_ = knob(cr::feedback, "Feedback", hues_.op[ops - 1], percent01);
        const auto signedPercent = [](double v) { return signedPercentText(v); };
        tone_ = knob(cr::tone, "Tone", hues_.tone, signedPercent);
        time_ = knob(cr::time, "Time", hues_.tone, signedPercent);
        tone_->setRingSize(46);
        time_->setRingSize(46);
        highCut_ = knob(cr::highCut,
                        "High cut",
                        hues_.tone,
                        [](double v) { return v >= 19500.0 ? juce::String("Off") : compactHzText(v); });
        width_ = knob(cr::width, "Width", hues_.tone, [](double v) { return percentText(v); });
        level_ = knob(cr::outputLevel, "Level", hues_.tone);
        rate_ = knob(cr::lfoRate,
                     "Rate",
                     hues_.motion,
                     [this](double v)
                     {
                         if (const int c = plugins::sync::choiceOf(param(cr::lfoSync)); c > 0)
                             return juce::String(plugins::sync::noteValues[static_cast<size_t>(c)].name);
                         return v < 10.0 ? fixedText(v, 2) + " Hz" : fixedText(v, 1) + " Hz";
                     });
        motionKnobs_ = {rate_,
                        knob(cr::lfoPitch, "Pitch", hues_.motion, integer(" ct", false)),
                        knob(cr::lfoTone, "Bright", hues_.motion, percent01),
                        knob(cr::pitchEnvelope, "Bend", hues_.motion, integer(" st", true)),
                        knob(cr::pitchTime, "Bend time", hues_.motion),
                        nullptr};
        glide_ = knob(cr::glide, "Glide", hues_.motion);
        motionKnobs_.back() = glide_;
        mono_ = &addToggle(cr::voiceMode, "Mono");
        mono_->setTooltip("Mono: one voice, legato");

        quality_.setChoices(info(cr::quality));
        quality_.setTooltip("High: twice the rate, cleaner high notes. Eco: half the CPU.");
        quality_.onChoose = [this](int q)
        {
            beginGesture("Quality");
            setParam(cr::quality, static_cast<float>(q));
            endGesture();
        };
        addAndMakeVisible(quality_);

        presets_.onApply = [this](const std::vector<float>& values, const juce::String& name)
        { applyValues(values, "Preset " + name); };
        presets_.setValues(values());
        addAndMakeVisible(presets_);

        const auto known = selections().find(moduleId());
        select(known != selections().end() ? known->second : 0);
    }

    // Thirteen knob columns and four operator columns in five panels.
    int preferredWidth() const override
    {
        return 13 * cellW + ops * columnW + 5 * 2 * pad + 4 * gap + 2 * theme_.spacing.sm;
    }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, captionH = 18, cellW = 52, columnW = 48, gap = 8, pad = 8;

    struct OpKnobs
    {
        Knob *ratio = nullptr, *fine = nullptr, *detune = nullptr, *level = nullptr;
        Knob *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr;
        Knob *fixedHz = nullptr, *velocity = nullptr, *keyScale = nullptr;
        PillToggle* fixed = nullptr;
        juce::Label* fixedLabel = nullptr;
        std::vector<Knob*> all() const
        {
            return {ratio, fine, detune, level, attack, decay, sustain, release, fixedHz, velocity, keyScale};
        }
    };

    struct Section
    {
        juce::String title;
        juce::Colour hue;
        juce::Rectangle<int> bounds;
    };

    void select(int op)
    {
        selected_ = juce::jlimit(0, ops - 1, op);
        selections()[moduleId()] = selected_;
        for (int o = 0; o < ops; ++o)
        {
            const bool shown = o == selected_;
            auto& k = opKnobs_[static_cast<size_t>(o)];
            for (auto* knob : k.all())
            {
                knob->setVisible(shown);
                if (auto* l = labelFor(*knob))
                    l->setVisible(shown);
            }
            k.fixed->setVisible(shown);
            k.fixedLabel->setVisible(shown);
            columns_[static_cast<size_t>(o)].selected = shown;
            columns_[static_cast<size_t>(o)].repaint();
        }
        envelope_.op = selected_;
        wave_.op = selected_;
        wave_.hue = hues_.op[static_cast<size_t>(selected_)];
        updateStates();
        resized();
        repaint();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        sky_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        wordmark_ = header.removeFromLeft(300);
        voices_ = header.removeFromRight(8 * 9 + 4).withSizeKeepingCentre(8 * 9 + 4, 15);
        header.removeFromRight(theme_.spacing.lg);
        auto preset = header.removeFromRight(260).withSizeKeepingCentre(260, theme_.sizes.controlCompact);
        presetBar_ = preset;
        presets_.setBounds(preset);
        header.removeFromRight(theme_.spacing.lg);
        band_ = header.withTop(sky_.getY()).withBottom(header.getBottom() + 4);
        sun_ = {static_cast<float>(header.getCentreX()), static_cast<float>(header.getBottom()) - 3.0f};
        buildWordmark();
        body.removeFromTop(4);

        sections_.clear();
        int x = body.getX();
        const int cellH = knobCellHeight();
        const auto panel = [&](const juce::String& title, juce::Colour hue, int width) -> juce::Rectangle<int>
        {
            const juce::Rectangle<int> r(x, body.getY(), width + 2 * pad, body.getHeight());
            sections_.push_back({title, hue, r});
            x += r.getWidth() + gap;
            auto inner = r.reduced(pad);
            inner.removeFromTop(captionH);
            return inner;
        };
        const auto captionOf = [&](const Section& s)
        { return s.bounds.reduced(pad).removeFromTop(captionH - 2); };

        // ALGORITHM: the routing, the picker, Feedback.
        {
            auto inner = panel("ALGORITHM", hues_.algorithm, 3 * cellW);
            auto feedbackRow = inner.removeFromBottom(cellH);
            placeKnob(*feedback_, feedbackRow.withSizeKeepingCentre(cellW, cellH));
            inner.removeFromBottom(2);
            picker_.setBounds(inner.removeFromBottom(AlgorithmPicker::height)
                                  .withSizeKeepingCentre(AlgorithmPicker::width, AlgorithmPicker::height));
            inner.removeFromBottom(pad);
            routing_.setBounds(inner);
        }
        // OPERATORS: four columns.
        {
            auto inner = panel("OPERATORS", hues_.algorithm, ops * columnW);
            for (int op = 0; op < ops; ++op)
                columns_[static_cast<size_t>(op)].setBounds(inner.removeFromLeft(columnW));
        }
        // OPERATOR: the selected one's envelope and knobs.
        {
            auto inner = panel({}, hues_.op[static_cast<size_t>(selected_)], 4 * cellW);
            operatorSection_ = static_cast<int>(sections_.size()) - 1;
            auto chip = captionOf(sections_.back());
            wave_.setBounds(chip.removeFromRight(wave_.preferredWidth())
                                .withSizeKeepingCentre(wave_.preferredWidth(), 16));
            auto knobs = inner.removeFromBottom(3 * cellH);
            inner.removeFromBottom(pad);
            envelope_.setBounds(inner);
            const auto& k = opKnobs_[static_cast<size_t>(selected_)];
            const auto cell = [&](int row, int col)
            {
                return juce::Rectangle<int>(
                    knobs.getX() + col * cellW, knobs.getY() + row * cellH, cellW, cellH);
            };
            const std::array<Knob*, 11> placed{k.ratio,
                                               k.fine,
                                               k.detune,
                                               k.level,
                                               k.attack,
                                               k.decay,
                                               k.sustain,
                                               k.release,
                                               k.fixedHz,
                                               k.velocity,
                                               k.keyScale};
            for (size_t i = 0; i < placed.size(); ++i)
            {
                const int slot =
                    i < 8 ? static_cast<int>(i) : static_cast<int>(i) + 1; // row 3 starts with Fixed
                placeKnob(*placed[i], cell(slot / 4, slot % 4));
            }
            // Fixed: the pill where a ring sits, its name on the labels' line.
            auto fixedCell = cell(2, 0);
            k.fixedLabel->setBounds(labelLine(fixedCell));
            k.fixed->setBounds(fixedCell.removeFromTop(theme_.sizes.knob)
                                   .withSizeKeepingCentre(cellW - theme_.spacing.sm, 20));
        }
        // TONE: the spectrum, the macros, the output.
        {
            auto inner = panel("TONE", hues_.tone, 3 * cellW);
            auto chip = captionOf(sections_.back());
            quality_.setBounds(chip.removeFromRight(quality_.preferredWidth())
                                   .withSizeKeepingCentre(quality_.preferredWidth(), 16));
            layoutKnobs(inner.removeFromBottom(cellH), 3, {highCut_, width_, level_});
            // The macros' labels sit on the second row's line; the spectrum takes
            // the room above their rings.
            auto macros = inner.removeFromBottom(2 * cellH);
            const int stack = knobCellHeight(tone_->ringSize(), theme_.fonts.size.caption);
            const int spare = std::max(0, macros.getHeight() - stack - pad);
            inner.setBottom(inner.getBottom() + spare);
            macros.removeFromTop(spare);
            placeKnob(*tone_, macros.removeFromLeft(macros.getWidth() / 2), theme_.fonts.size.caption);
            placeKnob(*time_, macros, theme_.fonts.size.caption);
            inner.removeFromBottom(pad);
            spectrum_.setBounds(inner);
        }
        // PITCH · LFO: the first second of a note, two rows of knobs.
        {
            auto inner = panel(juce::String::fromUTF8("PITCH  \xc2\xb7  LFO"), hues_.motion, 3 * cellW);
            auto chip = captionOf(sections_.back());
            mono_->setBounds(chip.removeFromRight(52).withSizeKeepingCentre(52, 16));
            layoutKnobs(inner.removeFromBottom(2 * cellH), 3, motionKnobs_);
            inner.removeFromBottom(pad);
            pitch_.setBounds(inner);
        }
    }

    // ----- drawing ----------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = sky_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        // Dusk: Loch at the top through Heather and Thrift to Gorse at the foot.
        juce::ColourGradient sky(c.canvas.interpolatedWith(hues_.op[3], 0.10f),
                                 0.0f,
                                 bounds.getY(),
                                 c.canvas.interpolatedWith(hues_.dusk, 0.18f),
                                 0.0f,
                                 bounds.getBottom(),
                                 false);
        sky.addColour(0.35, c.canvas.interpolatedWith(hues_.algorithm, 0.14f));
        sky.addColour(0.7, c.canvas.interpolatedWith(hues_.op[0], 0.14f));
        g.setGradientFill(sky);
        g.fillRect(bounds);
        paintBand(g);
        paintWordmark(g);
        paintVoices(g);
        for (size_t i = 0; i < sections_.size(); ++i)
        {
            const auto& s = sections_[i];
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1.withAlpha(0.72f));
            g.fillRoundedRectangle(r, theme_.radius.md);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.drawHorizontalLine(
                static_cast<int>(r.getY()) + 1, r.getX() + theme_.radius.md, r.getRight() - theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.md, 1.0f);
            const bool selectedPanel = static_cast<int>(i) == operatorSection_;
            const auto hue = selectedPanel ? hues_.op[static_cast<size_t>(selected_)] : s.hue;
            auto caption = s.bounds.reduced(pad).removeFromTop(captionH - 4);
            g.setColour(hue);
            g.fillEllipse(caption.removeFromLeft(8).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
            caption.removeFromLeft(theme_.spacing.xs);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textSecondary);
            const auto title =
                selectedPanel ? "OP " + juce::String(selected_ + 1) + juce::String::fromUTF8("  \xc2\xb7  ") +
                                    (algorithmOf(*this).isCarrier(selected_) ? "CARRIER" : "MODULATOR")
                              : s.title;
            g.drawText(title, caption, juce::Justification::centredLeft);
        }
        g.setColour(c.surface1.withAlpha(0.8f));
        g.fillRoundedRectangle(presetBar_.toFloat(), presetBar_.getHeight() * 0.5f);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(presetBar_.toFloat().reduced(0.5f), presetBar_.getHeight() * 0.5f, 1.0f);
    }

    // The header band: the low sun, the cirrus streaks catching its colour as
    // Cirrus plays, and a sundog either side, further out the deeper the
    // modulation.
    void paintBand(juce::Graphics& g) const
    {
        const auto& c = theme_.colours;
        const auto band = band_.toFloat();
        const float glow = theme_.motion.reduceMotion ? 0.0f : glow_;
        // The sun: a soft Gorse halo round a Straw core.
        const float sunR = 8.0f;
        juce::ColourGradient halo(hues_.dusk.withAlpha(0.35f),
                                  sun_.x,
                                  sun_.y,
                                  hues_.dusk.withAlpha(0.0f),
                                  sun_.x + 42.0f,
                                  sun_.y,
                                  true);
        g.setGradientFill(halo);
        g.fillEllipse(juce::Rectangle<float>(84.0f, 84.0f).withCentre(sun_));
        g.setColour(hues_.tone.withAlpha(0.9f));
        g.fillEllipse(juce::Rectangle<float>(2.0f * sunR, 2.0f * sunR).withCentre(sun_));

        // Five long streaks across the band, tinted by the sun as it plays.
        for (int i = 0; i < 5; ++i)
        {
            const float f = static_cast<float>(i) / 4.0f;
            const float y = band.getY() + 7.0f + f * (band.getHeight() - 22.0f);
            const float drift = 6.0f * std::sin(skyTime_ * 0.08f + static_cast<float>(i) * 1.7f);
            const float start =
                band.getX() +
                band.getWidth() * (0.04f + 0.13f * std::fmod(static_cast<float>(i) * 0.618f, 1.0f)) + drift;
            const float length = band.getWidth() * (0.55f + 0.08f * static_cast<float>(i % 3));
            juce::Path streak;
            streak.startNewSubPath(start, y + 3.0f);
            streak.quadraticTo(start + length * 0.5f, y - 3.0f, start + length, y + 1.0f);
            const auto tint = (i % 2 == 0 ? hues_.dusk : hues_.op[0]);
            const auto colour = c.textPrimary.interpolatedWith(tint, 0.3f + 0.4f * glow);
            g.setColour(colour.withAlpha(0.06f + 0.04f * static_cast<float>(i % 2) + 0.18f * glow));
            g.strokePath(streak,
                         juce::PathStrokeType(1.5f + 0.5f * static_cast<float>(i % 2),
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }

        // The sundogs: a bright spot either side with a short vertical flare.
        if (glow > 0.01f)
        {
            const float spread = juce::jlimit(0.0f, 1.0f, depth_ / 12.0f);
            const float distance = sunR * 2.5f + spread * std::min(70.0f, band.getWidth() * 0.4f);
            for (const float side : {-1.0f, 1.0f})
            {
                const juce::Point<float> at{sun_.x + side * distance, sun_.y - 2.0f};
                g.setColour(hues_.tone.withAlpha(0.22f * glow));
                g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(at));
                g.setColour(c.textPrimary.withAlpha(0.3f * glow));
                g.drawLine(at.x, at.y - 9.0f, at.x, at.y + 5.0f, 1.0f);
                g.setColour(hues_.tone.withAlpha(0.8f * glow));
                g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(at));
            }
        }
    }

    // CIRRUS, tracked wide, as an outline made once per layout.
    void buildWordmark()
    {
        const auto area = wordmark_.toFloat();
        const auto font = theme_.font(Theme::Face::ui, 26.0f, theme_.fonts.weight.regular);
        juce::GlyphArrangement glyphs;
        float x = area.getX() + 4.0f;
        const float baseline = area.getCentreY() + 4.0f;
        for (const auto ch : juce::String("CIRRUS"))
        {
            juce::GlyphArrangement one;
            one.addLineOfText(font, juce::String::charToString(ch), x, baseline);
            const float w = one.getBoundingBox(0, -1, true).getWidth();
            glyphs.addGlyphArrangement(one);
            x += w + 7.0f;
        }
        wordmarkPath_.clear();
        glyphs.createPath(wordmarkPath_);
        wordmarkBaseline_ = baseline;
    }

    void paintWordmark(juce::Graphics& g) const
    {
        const auto box = wordmarkPath_.getBounds();
        g.setGradientFill(
            juce::ColourGradient(hues_.dusk, box.getX(), 0.0f, hues_.algorithm, box.getRight(), 0.0f, false));
        g.fillPath(wordmarkPath_);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.micro, theme_.fonts.weight.medium));
        g.setColour(theme_.colours.textTertiary);
        g.drawText(juce::String::fromUTF8("FM SYNTH  \xc2\xb7  NORTHLIGHT"),
                   juce::Rectangle<float>(box.getRight() + 12.0f, wordmarkBaseline_ - 10.0f, 140.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // Sixteen lights in two rows of eight: how many voices sound.
    void paintVoices(juce::Graphics& g) const
    {
        for (int i = 0; i < cr::voiceCount; ++i)
        {
            const float x = static_cast<float>(voices_.getX()) + 4.0f + static_cast<float>(i % 8) * 9.0f;
            const float y = static_cast<float>(voices_.getCentreY()) + (i < 8 ? -4.5f : 4.5f);
            const auto r = juce::Rectangle<float>(6.0f, 6.0f).withCentre({x, y});
            const bool lit = i < activeVoices_;
            if (lit)
            {
                g.setColour(hues_.tone.withAlpha(0.35f));
                g.fillEllipse(r.expanded(2.5f));
            }
            g.setColour(lit ? theme_.colours.textPrimary : theme_.colours.surface3);
            g.fillEllipse(r);
        }
    }

    // ----- live -------------------------------------------------------------------------------

    // What can be used depends on other values: Ratio and Fine or Frequency by
    // Fixed, Glide by Mono, the Rate knob by Sync.
    void updateStates()
    {
        for (int op = 0; op < ops; ++op)
        {
            const bool fixed = isFixed(*this, op);
            auto& k = opKnobs_[static_cast<size_t>(op)];
            for (auto* knob : {k.ratio, k.fine})
                enable(*knob, !fixed);
            enable(*k.fixedHz, fixed);
        }
        enable(*glide_, param(cr::voiceMode) >= 0.5f);
        rate_->setAlpha(plugins::sync::choiceOf(param(cr::lfoSync)) > 0 ? 0.5f : 1.0f);
        quality_.selected = param(cr::quality) >= 0.5f ? 1 : 0;
        for (auto& c : columns_)
            c.updateTooltip();
    }

    void enable(Knob& knob, bool on)
    {
        if (knob.isEnabled() == on)
            return;
        knob.setEnabled(on);
        if (auto* l = labelFor(knob))
            l->setColour(juce::Label::textColourId,
                         on ? theme_.colours.textSecondary : theme_.colours.textDisabled);
    }

    bool repaintsChangedParameters() const noexcept override { return true; }

    // Repaints what a change of values moved, not the card.
    void paramsChanged() override
    {
        presets_.setValues(values());
        const auto& now = values();
        const bool first = last_.size() != now.size();
        const auto changed = [&](int i)
        { return first || now[static_cast<size_t>(i)] != last_[static_cast<size_t>(i)]; };
        bool routing = false, picker = false, envelope = false, pitch = false, spectrum = false,
             chips = false, caption = false;
        std::array<bool, ops> column{};
        for (int op = 0; op < ops; ++op)
            for (int f = 0; f < cr::fieldCount; ++f)
                if (changed(cr::opParam(op, static_cast<cr::OpField>(f))))
                {
                    column[static_cast<size_t>(op)] = true;
                    if (f == cr::level)
                        routing = true;
                    if (op == selected_ && f >= cr::attack && f <= cr::release)
                        envelope = true;
                    if (op == selected_ && f == cr::wave)
                        wave_.repaint();
                }
        if (changed(cr::algorithm))
        {
            routing = picker = caption = true;
            column.fill(true);
        }
        if (changed(cr::feedback))
            routing = picker = true;
        for (const int p : {cr::pitchEnvelope,
                            cr::pitchTime,
                            cr::lfoShape,
                            cr::lfoRate,
                            cr::lfoSync,
                            cr::lfoPitch,
                            cr::time})
            pitch = pitch || changed(p);
        spectrum = changed(cr::highCut);
        chips = changed(cr::quality) || changed(cr::lfoSync) || changed(cr::voiceMode);
        last_ = now;
        updateStates();
        if (routing)
            routing_.repaint();
        if (picker)
            picker_.repaint();
        for (int op = 0; op < ops; ++op)
            if (column[static_cast<size_t>(op)])
                columns_[static_cast<size_t>(op)].repaint();
        if (envelope)
            envelope_.repaint();
        if (pitch)
            pitch_.repaint();
        if (spectrum)
            spectrum_.repaint();
        if (chips)
            quality_.repaint();
        if (caption && operatorSection_ >= 0)
            repaint(
                sections_[static_cast<size_t>(operatorSection_)].bounds.reduced(pad).removeFromTop(captionH));
        if (changed(cr::lfoSync))
            rate_->repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        // The newest frame carries the voice count and the LFO.
        bool sounding = false;
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            const int voices = static_cast<int>(std::lround(last.reduction));
            sounding = voices > 0;
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(voices_.expanded(4));
            }
            const float lfo =
                sounding ? juce::jlimit(-1.0f, 1.0f, 2.0f * (last.extra - 1.0e-3f) - 1.0f) : -2.0f;
            if (std::abs(lfo - pitch_.live) > 0.01f)
            {
                pitch_.live = lfo;
                pitch_.repaint();
            }
        }
        // The operators' live levels and the modulation depth.
        float depth = 0.0f;
        if (const auto* live = liveProcessor())
            if (const auto* activity = cr::activity(*live))
            {
                depth = activity->depth.load(std::memory_order_relaxed);
                bool routingMoved = false;
                for (int op = 0; op < ops; ++op)
                {
                    const auto o = static_cast<size_t>(op);
                    const float gain = activity->level[o].load(std::memory_order_relaxed);
                    const float steps = levelSteps(gain);
                    auto& bar = columns_[o].bar;
                    if (std::abs(steps - bar.live) > 0.5f)
                    {
                        bar.live = steps;
                        bar.repaint();
                    }
                    const float shown = steps / 99.0f;
                    if (std::abs(shown - routing_.live[o]) > 0.01f)
                    {
                        routing_.live[o] = shown;
                        routingMoved = true;
                    }
                    if (op == selected_ && std::abs(shown - envelope_.live) > 0.01f)
                    {
                        envelope_.live = shown;
                        envelope_.repaint();
                    }
                }
                if (routingMoved)
                    routing_.repaint();
            }
        spectrum_.update(elapsedMs);

        // The sky: the streaks' colour follows the output's level, the sundogs'
        // distance the depth. Still with reduced motion, or silent.
        const float level = juce::jlimit(0.0f, 1.0f, (outputDb() + 48.0f) / 48.0f);
        glow_ += (level - glow_) * 0.15f;
        depth_ += (depth - depth_) * 0.2f;
        if (glow_ < 0.002f)
            glow_ = 0.0f;
        if (theme_.motion.reduceMotion)
            return;
        sinceSky_ += elapsedMs;
        if (sinceSky_ >= 33.0 && (glow_ > 0.0f || paintedGlow_ > 0.0f))
        {
            skyTime_ += static_cast<float>(sinceSky_ * 0.001);
            sinceSky_ = 0.0;
            paintedGlow_ = glow_;
            repaint(band_);
        }
    }

    Hues hues_;
    RoutingDisplay routing_;
    AlgorithmPicker picker_;
    std::array<OperatorColumn, ops> columns_;
    OperatorEnvelope envelope_;
    WaveChip wave_;
    SpectrumDisplay spectrum_;
    PitchDisplay pitch_;
    CornerChoice quality_;
    std::array<OpKnobs, ops> opKnobs_{};
    Knob *feedback_ = nullptr, *tone_ = nullptr, *time_ = nullptr, *highCut_ = nullptr, *width_ = nullptr,
         *level_ = nullptr, *rate_ = nullptr, *glide_ = nullptr;
    std::vector<Knob*> motionKnobs_;
    PillToggle* mono_ = nullptr;
    PresetBar presets_{theme_, cr::type, true};
    std::vector<Section> sections_;
    int operatorSection_ = -1;
    juce::Rectangle<int> sky_, wordmark_, voices_, presetBar_, band_;
    juce::Point<float> sun_;
    juce::Path wordmarkPath_;
    float wordmarkBaseline_ = 0.0f;
    int selected_ = 0, activeVoices_ = 0;
    float skyTime_ = 0.0f, glow_ = 0.0f, paintedGlow_ = 0.0f, depth_ = 0.0f;
    double sinceSky_ = 0.0;
    std::vector<float> last_;
};
} // namespace

std::unique_ptr<ModuleCard>
createCirrusEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<CirrusCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
