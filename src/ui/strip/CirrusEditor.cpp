// Cirrus's editor (screens/plugins.md 7.4h). A precise instrument rather than a
// scene: flat and ruled, built round a display of what FM does to a note (the
// carrier and its sidebands, or the selected operator's envelope) and a table that
// shows all four operators at once. Round them: the algorithm buttons in the top
// bar, the routing diagram with Feedback, pitch and the LFO, and tone and the
// output. Each operator keeps one hue wherever it appears.

#include "ui/strip/CirrusEditor.h"

#include "core/NoteNames.h"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <string>
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
    juce::Colour face, tone, motion;
    explicit Hues(const Theme& t)
        : op{{paletteColour(t, "Thrift"),
              paletteColour(t, "Gorse"),
              paletteColour(t, "Sea grass"),
              paletteColour(t, "Loch")}},
          face(paletteColour(t, "Heather")), tone(paletteColour(t, "Straw")),
          motion(paletteColour(t, "Lichen"))
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

// "2 into 1, 4 into 3", or with `arrows` "2 → 1 · 4 → 3"; "all four heard" for
// the additive one.
juce::String describe(const fm::Algorithm& a, bool arrows)
{
    juce::StringArray parts;
    for (int t = 0; t < ops; ++t)
    {
        juce::StringArray from;
        for (int m = t + 1; m < ops; ++m)
            if (((a.modulators[static_cast<size_t>(t)] >> m) & 1u) != 0)
                from.add(juce::String(m + 1));
        if (!from.isEmpty())
            parts.add(from.joinIntoString(arrows ? " + " : " and ") +
                      (arrows ? juce::String::fromUTF8(" \xe2\x86\x92 ") : juce::String(" into ")) +
                      juce::String(t + 1));
    }
    if (parts.isEmpty())
        return "all four heard";
    return parts.joinIntoString(arrows ? juce::String::fromUTF8("  \xc2\xb7  ") : juce::String(", "));
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

// A number on a small square in an operator's hue (the table's OP column, the
// routing's nodes).
void drawOpSquare(
    juce::Graphics& g, const Theme& theme, juce::Rectangle<float> r, juce::Colour hue, int op, float alpha)
{
    g.setColour(hue.withAlpha(alpha));
    g.fillRoundedRectangle(r, theme.radius.sm);
    g.setColour(theme.colours.textOnColour.withAlpha(alpha));
    g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption, theme.fonts.weight.semibold));
    g.drawText(juce::String(op + 1), r, juce::Justification::centred);
}

// ----- the routing diagram (design-system.md "Routing diagram") --------------------------------

// Each algorithm's nodes on a grid: a column (halves allowed) and a row up from
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

constexpr float nodeW = 24.0f, nodeH = 20.0f;

struct Routing
{
    std::array<juce::Point<float>, ops> centre{};
    float outputY = 0.0f;
};

// Where an algorithm's nodes sit in `r`, with room for op 4's loop above and to
// the right and the output line at the foot.
Routing routingIn(juce::Rectangle<float> r, int algorithm)
{
    Routing out;
    const auto& at = places[static_cast<size_t>(algorithm)];
    float minX = 9.0f, maxX = -9.0f, rows = 0.0f;
    for (const auto& p : at)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        rows = std::max(rows, p.row);
    }
    const float loop = 16.0f;
    out.outputY = r.getBottom() - 1.5f;
    const float bottomRow = out.outputY - 9.0f - nodeH * 0.5f;
    const float stepY =
        rows > 0.0f ? std::min(nodeH * 2.6f, (bottomRow - (r.getY() + loop + nodeH * 0.5f)) / rows) : 0.0f;
    const float stepX =
        maxX > minX ? std::min(nodeW * 2.4f, (r.getWidth() - nodeW - 2.0f * loop) / (maxX - minX)) : 0.0f;
    const float mid = 0.5f * (minX + maxX);
    for (int op = 0; op < ops; ++op)
    {
        const auto& p = at[static_cast<size_t>(op)];
        out.centre[static_cast<size_t>(op)] = {r.getCentreX() + (p.x - mid) * stepX,
                                               bottomRow - p.row * stepY};
    }
    return out;
}

juce::Rectangle<float> nodeAt(juce::Point<float> centre)
{
    return juce::Rectangle<float>(nodeW, nodeH).withCentre(centre);
}

// The loop from op 4's right side back to its top.
juce::Path loopPath(juce::Point<float> c)
{
    const float w = nodeW * 0.5f, h = nodeH * 0.5f;
    juce::Path p;
    p.startNewSubPath(c.x + w, c.y);
    p.cubicTo(c.x + w + 14.0f, c.y, c.x + w + 14.0f, c.y - h - 14.0f, c.x, c.y - h - 14.0f);
    p.lineTo(c.x, c.y - h);
    return p;
}

// The chosen algorithm drawn as a routing diagram: click a node to select its
// operator, drag the loop for Feedback.
class RoutingDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    RoutingDisplay(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setWantsKeyboardFocus(true);
        setTitle("Routing");
    }
    std::function<int()> selected;
    std::function<void(int)> onSelect;
    std::array<float, ops> live{}; // 0..1

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto& a = algorithmOf(card_);
        const auto routing = routingIn(area(), algorithmIndex(card_));
        const int chosen = selected ? selected() : -1;
        const auto fade = [&](int op) { return isMuted(card_, op) ? 0.4f : 1.0f; };
        const auto lineColour = [&](int op)
        {
            return hues_.op[static_cast<size_t>(op)].withAlpha(
                (0.55f + 0.4f * live[static_cast<size_t>(op)]) * fade(op));
        };
        // The output line under the carriers, and each carrier's drop to it.
        float left = area().getRight(), right = area().getX();
        for (int op = 0; op < ops; ++op)
            if (a.isCarrier(op))
            {
                const auto p = routing.centre[static_cast<size_t>(op)];
                left = std::min(left, p.x - nodeW * 0.5f);
                right = std::max(right, p.x + nodeW * 0.5f);
                g.setColour(lineColour(op));
                g.drawLine(p.x, p.y + nodeH * 0.5f, p.x, routing.outputY, 1.5f);
            }
        g.setColour(c.lineStrong);
        g.drawLine(left, routing.outputY, right, routing.outputY, 1.5f);
        // The connections: down from the feeder, across, down into the one it feeds.
        for (int t = 0; t < ops; ++t)
        {
            std::vector<int> from;
            for (int m = t + 1; m < ops; ++m)
                if (((a.modulators[static_cast<size_t>(t)] >> m) & 1u) != 0)
                    from.push_back(m);
            std::sort(from.begin(),
                      from.end(),
                      [&](int x, int y)
                      {
                          return routing.centre[static_cast<size_t>(x)].x <
                                 routing.centre[static_cast<size_t>(y)].x;
                      });
            const auto target = routing.centre[static_cast<size_t>(t)];
            for (size_t i = 0; i < from.size(); ++i)
            {
                const auto source = routing.centre[static_cast<size_t>(from[i])];
                const float offset =
                    from.size() > 1
                        ? (static_cast<float>(i) - 0.5f * static_cast<float>(from.size() - 1)) * 8.0f
                        : 0.0f;
                const juce::Point<float> start{source.x, source.y + nodeH * 0.5f};
                const juce::Point<float> end{target.x + offset, target.y - nodeH * 0.5f};
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
                g.strokePath(link.createPathWithRoundedCorners(4.0f), juce::PathStrokeType(1.5f));
            }
        }
        // Op 4's feedback loop: thicker and brighter with the amount, dotted at 0.
        {
            const float feedback = card_.param(cr::feedback);
            const auto loop = loopPath(routing.centre[ops - 1]);
            const auto hue = hues_.op[ops - 1];
            if (feedback <= 0.0f)
            {
                juce::Path dotted;
                const float dashes[] = {1.5f, 2.5f};
                juce::PathStrokeType(1.2f).createDashedStroke(dotted, loop, dashes, 2);
                g.setColour(hue.withAlpha(0.45f * fade(ops - 1)));
                g.fillPath(dotted);
            }
            else
            {
                g.setColour(hue.withAlpha((0.5f + 0.5f * feedback) * fade(ops - 1)));
                g.strokePath(loop, juce::PathStrokeType(1.5f * (1.0f + feedback)));
            }
        }
        // The nodes, their halos following the live levels.
        for (int op = 0; op < ops; ++op)
        {
            const auto o = static_cast<size_t>(op);
            const auto body = nodeAt(routing.centre[o]);
            if (live[o] > 0.01f)
            {
                g.setColour(hues_.op[o].withAlpha(0.45f * live[o] * fade(op)));
                g.fillRoundedRectangle(body.expanded(6.0f), theme.radius.sm + 6.0f);
            }
            drawOpSquare(g, theme, body, hues_.op[o], op, fade(op));
            if (op == hovered_ && op != chosen)
            {
                g.setColour(c.textPrimary.withAlpha(0.3f));
                g.drawRoundedRectangle(body, theme.radius.sm, 1.0f);
            }
            if (op == chosen)
            {
                g.setColour(c.textPrimary);
                g.drawRoundedRectangle(body.expanded(2.75f), theme.radius.sm + 2.0f, 1.5f);
                drawFocus(g, *this, body.expanded(4.0f), theme.radius.sm + 3.0f);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int node = nodeUnder(e.position);
        const bool loop = node < 0 && overLoop(e.position);
        if (node != hovered_)
        {
            hovered_ = node;
            repaint();
        }
        setMouseCursor(loop        ? juce::MouseCursor::UpDownResizeCursor
                       : node >= 0 ? juce::MouseCursor::PointingHandCursor
                                   : juce::MouseCursor::NormalCursor);
        if (loop)
            setTooltip("Feedback: drag up or down");
        else if (node >= 0)
        {
            const auto& a = algorithmOf(card_);
            setTooltip("Operator " + juce::String(node + 1) + ": " +
                       (a.isCarrier(node) ? juce::String("heard") : "modulates " + targetsOf(a, node)) +
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
        if (const int node = nodeUnder(e.position); node >= 0)
        {
            if (onSelect)
                onSelect(node);
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
        setTooltip("Feedback: " + percentText(card_.param(cr::feedback) * 100.0));
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
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced(6.0f, 4.0f); }
    int nodeUnder(juce::Point<float> at) const
    {
        const auto routing = routingIn(area(), algorithmIndex(card_));
        for (int op = 0; op < ops; ++op)
            if (nodeAt(routing.centre[static_cast<size_t>(op)]).expanded(3.0f).contains(at))
                return op;
        return -1;
    }
    bool overLoop(juce::Point<float> at) const
    {
        const auto routing = routingIn(area(), algorithmIndex(card_));
        return loopPath(routing.centre[ops - 1]).getBounds().expanded(4.0f).contains(at);
    }

    ModuleCard& card_;
    const Hues& hues_;
    int hovered_ = -1;
    bool dragging_ = false;
    float start_ = 0.0f;
};

// The eight algorithm buttons: a radio row in the top bar.
class AlgorithmButtons final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int size = 22, spacing = 4;
    static constexpr int width = fm::algorithmCount * size + (fm::algorithmCount - 1) * spacing;

    AlgorithmButtons(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setTitle("Algorithm");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const int chosen = algorithmIndex(card_);
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption, theme.fonts.weight.semibold));
        for (int i = 0; i < fm::algorithmCount; ++i)
        {
            const auto r = cell(i).toFloat();
            if (i == chosen)
            {
                g.setColour(hue_);
                g.fillRoundedRectangle(r, theme.radius.sm);
                g.setColour(c.textOnColour);
            }
            else
            {
                if (i == hovered_)
                {
                    g.setColour(c.surface2);
                    g.fillRoundedRectangle(r, theme.radius.sm);
                }
                g.setColour(c.lineStrong);
                g.drawRoundedRectangle(r.reduced(0.5f), theme.radius.sm, 1.0f);
                g.setColour(i == hovered_ ? c.textPrimary : c.textSecondary);
            }
            g.drawText(juce::String(i + 1), r, juce::Justification::centred);
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
                                describe(fm::algorithms[static_cast<size_t>(i)], false)
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
        if (key == juce::KeyPress::leftKey && now > 0)
            choose(now - 1);
        else if (key == juce::KeyPress::rightKey && now < fm::algorithmCount - 1)
            choose(now + 1);
        else if (key == juce::KeyPress::homeKey)
            choose(0);
        else if (key == juce::KeyPress::endKey)
            choose(fm::algorithmCount - 1);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    juce::Rectangle<int> cell(int i) const
    {
        return {i * (size + spacing), (getHeight() - size) / 2, size, size};
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
    juce::Colour hue_;
    int hovered_ = -1;
};

// ----- the parameter table (design-system.md "Parameter table") --------------------------------

// One value in the table: drag up or down, the wheel or the arrows to change it,
// double-click to reset it.
class TableCell final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TableCell(ModuleCard& card) : card_(card)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    // Which parameter the cell edits and how it reads; `secondary` values read
    // in textSecondary.
    void bind(int index, std::function<juce::String(float)> format, bool secondary = false)
    {
        index_ = index;
        format_ = std::move(format);
        secondary_ = secondary;
        setTitle(juce::String(card_.info(index).name));
        repaint();
    }
    int index() const noexcept { return index_; }

    void paint(juce::Graphics& g) override
    {
        if (index_ < 0)
            return;
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto b = getLocalBounds().toFloat();
        if (isEnabled() && (isMouseOver(true) || dragging_))
        {
            g.setColour(c.surface2.withAlpha(0.5f));
            g.fillRoundedRectangle(b, theme.radius.sm);
        }
        g.setFont(theme.value());
        g.setColour(!isEnabled() ? c.textDisabled : secondary_ ? c.textSecondary : c.textPrimary);
        g.drawText(text(), getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
        drawFocus(g, *this, b, theme.radius.sm);
    }
    void mouseEnter(const juce::MouseEvent&) override { updateTooltip(); }
    void mouseDown(const juce::MouseEvent&) override
    {
        if (!isEnabled() || index_ < 0)
            return;
        dragging_ = true;
        startValue_ = card_.param(index_);
        start_ = normal(startValue_);
        card_.beginGesture(juce::String(card_.info(index_).name));
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging_)
            return;
        const float dy = static_cast<float>(-e.getDistanceFromDragStartY());
        if (!card_.info(index_).choices.empty())
            set(std::round(startValue_) + std::round(dy / (e.mods.isShiftDown() ? 24.0f : 8.0f)));
        else
            set(denormal(start_ + dy / (e.mods.isShiftDown() ? 2000.0f : 200.0f)));
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_)
            card_.endGesture();
        dragging_ = false;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (!isEnabled() || index_ < 0)
            return;
        card_.beginGesture(juce::String(card_.info(index_).name));
        set(card_.info(index_).defaultValue);
        card_.endGesture();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        step(wheel.deltaY > 0.0f ? 1.0f : -1.0f);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey)
            step(1.0f);
        else if (key == juce::KeyPress::downKey)
            step(-1.0f);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void enablementChanged() override { repaint(); }

private:
    juce::String text() const
    {
        return format_ ? format_(card_.param(index_)) : juce::String(card_.param(index_));
    }
    void updateTooltip()
    {
        setTooltip(index_ >= 0 ? juce::String(card_.info(index_).name) + ": " + text() : juce::String());
    }
    // The value as a position along its range (a log scale for a logarithmic one).
    float normal(float v) const
    {
        const auto& i = card_.info(index_);
        if (i.logarithmic && i.minimum > 0.0f)
            return std::log(v / i.minimum) / std::log(i.maximum / i.minimum);
        return (v - i.minimum) / (i.maximum - i.minimum);
    }
    float denormal(float n) const
    {
        const auto& i = card_.info(index_);
        n = juce::jlimit(0.0f, 1.0f, n);
        if (i.logarithmic && i.minimum > 0.0f)
            return i.minimum * std::pow(i.maximum / i.minimum, n);
        return i.minimum + n * (i.maximum - i.minimum);
    }
    void set(float v)
    {
        const auto& i = card_.info(index_);
        card_.setParam(index_, juce::jlimit(i.minimum, i.maximum, v));
        updateTooltip();
        repaint();
    }
    // One step, as one undo step: a choice's next, else a hundredth of the range.
    void step(float by)
    {
        if (!isEnabled() || index_ < 0)
            return;
        const auto& i = card_.info(index_);
        card_.beginGesture(juce::String(i.name));
        if (!i.choices.empty())
            set(std::round(card_.param(index_)) + by);
        else
            set(denormal(normal(card_.param(index_)) + by * 0.01f));
        card_.endGesture();
    }

    ModuleCard& card_;
    int index_ = -1;
    std::function<juce::String(float)> format_;
    bool secondary_ = false, dragging_ = false;
    float start_ = 0.0f, startValue_ = 0.0f;
};

// An operator's level as a bar: the set level at 40%, the live level solid over
// it, the value at its right. Click or drag across to set it.
class LevelBar final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelBar(ModuleCard& card, int op, juce::Colour hue) : card_(card), op_(op), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTitle("Op " + juce::String(op + 1) + " level");
    }
    float live = 0.0f; // 0..99

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const float level = opValue(card_, op_, cr::level);
        const float fade = level <= 0.0f ? 0.4f : 1.0f;
        const auto track = trackArea();
        g.setColour(c.surface2);
        g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
        const auto upTo = [&](float steps)
        { return track.withWidth(track.getWidth() * juce::jlimit(0.0f, 99.0f, steps) / 99.0f); };
        if (level > 0.0f)
        {
            g.setColour(hue_.withAlpha(0.4f * fade));
            g.fillRoundedRectangle(upTo(level), track.getHeight() * 0.5f);
        }
        if (live > 0.5f)
        {
            g.setColour(hue_.withAlpha(fade));
            g.fillRoundedRectangle(upTo(std::min(live, level)), track.getHeight() * 0.5f);
        }
        g.setFont(theme.value());
        g.setColour(c.textPrimary.withAlpha(fade));
        g.drawText(juce::String(static_cast<int>(std::lround(level))),
                   getLocalBounds().removeFromRight(valueW),
                   juce::Justification::centredRight);
        drawFocus(g, *this, getLocalBounds().toFloat(), theme.radius.sm);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        card_.beginGesture(name());
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto t = trackArea();
        set(std::round((e.position.x - t.getX()) / t.getWidth() * 99.0f));
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
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
            step(1.0f);
        else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
            step(-1.0f);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    static constexpr int valueW = 22;
    juce::String name() const { return "Op " + juce::String(op_ + 1) + " level"; }
    juce::Rectangle<float> trackArea() const
    {
        return getLocalBounds()
            .withTrimmedLeft(4)
            .withTrimmedRight(valueW + 6)
            .toFloat()
            .withSizeKeepingCentre(static_cast<float>(getWidth() - valueW - 10), 6.0f);
    }
    void set(float v)
    {
        card_.setParam(cr::opParam(op_, cr::level), juce::jlimit(0.0f, 99.0f, v));
        setTooltip(name() + ": " +
                   juce::String(static_cast<int>(std::lround(opValue(card_, op_, cr::level)))));
        repaint();
    }
    void step(float by)
    {
        card_.beginGesture(name());
        set(std::round(opValue(card_, op_, cr::level)) + by);
        card_.endGesture();
    }

    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
};

// An operator's wave: a tiny cycle and its name, opening a menu of the eight,
// each drawn.
class WaveCell final : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveCell(ModuleCard& card, int op, juce::Colour hue) : card_(card), op_(op), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle("Op " + juce::String(op + 1) + " wave");
        setTooltip("Op " + juce::String(op + 1) + " wave");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto b = getLocalBounds().toFloat();
        if (isMouseOver(true))
        {
            g.setColour(c.surface2.withAlpha(0.5f));
            g.fillRoundedRectangle(b, theme.radius.sm);
        }
        auto r = getLocalBounds().reduced(4, 0);
        const auto wave = waveOf(card_, op_);
        g.setColour(hue_);
        g.strokePath(waveShape(wave, r.removeFromLeft(12).toFloat().withSizeKeepingCentre(12.0f, 8.0f)),
                     juce::PathStrokeType(1.25f));
        r.removeFromLeft(5);
        g.setFont(theme.caption());
        g.setColour(c.textPrimary);
        const auto& names = card_.info(cr::opParam(op_, cr::wave)).choices;
        g.drawText(juce::String(names[static_cast<size_t>(wave)]), r, juce::Justification::centredLeft);
        drawFocus(g, *this, b, theme.radius.sm);
    }
    void mouseDown(const juce::MouseEvent&) override { showMenu(); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            showMenu();
            return true;
        }
        return false;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    void showMenu()
    {
        juce::PopupMenu menu;
        const auto& names = card_.info(cr::opParam(op_, cr::wave)).choices;
        const int current = static_cast<int>(waveOf(card_, op_));
        for (int w = 0; w < fm::waveCount; ++w)
        {
            juce::PopupMenu::Item item;
            item.text = juce::String(names[static_cast<size_t>(w)]);
            item.itemID = w + 1;
            item.isTicked = w == current;
            auto icon = std::make_unique<juce::DrawablePath>();
            icon->setPath(waveShape(static_cast<fm::Wave>(w), {0.0f, 0.0f, 18.0f, 10.0f}));
            icon->setFill(juce::FillType(juce::Colours::transparentBlack));
            icon->setStrokeFill(juce::FillType(hue_));
            icon->setStrokeType(juce::PathStrokeType(1.25f));
            item.image = std::move(icon);
            menu.addItem(std::move(item));
        }
        juce::Component::SafePointer<WaveCell> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe](int r)
                           {
                               if (safe == nullptr || r <= 0)
                                   return;
                               auto& card = safe->card_;
                               card.beginGesture("Op " + juce::String(safe->op_ + 1) + " wave");
                               card.setParam(cr::opParam(safe->op_, cr::wave), static_cast<float>(r - 1));
                               card.endGesture();
                           });
    }

    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
};

// A small pill in the FIX column: lit while the operator has a fixed frequency.
class FixCell final : public juce::Component, public juce::SettableTooltipClient
{
public:
    FixCell(ModuleCard& card, int op, juce::Colour hue) : card_(card), op_(op), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle("Op " + juce::String(op + 1) + " fixed");
        setTooltip("Fixed: a frequency of its own, whatever the note");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const juce::Rectangle<float> r(4.0f, static_cast<float>(getHeight() - 16) * 0.5f, 28.0f, 16.0f);
        const bool on = isFixed(card_, op_);
        if (on)
        {
            g.setColour(hue_.withAlpha(isMouseOver(true) ? 0.36f : 0.25f));
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            g.setColour(hue_);
        }
        else
        {
            if (isMouseOver(true))
            {
                g.setColour(c.surface2);
                g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            }
            g.setColour(c.lineStrong);
            g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
            g.setColour(c.textTertiary);
        }
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.micro, theme.fonts.weight.semibold));
        g.drawText("Hz", r, juce::Justification::centred);
        drawFocus(g, *this, r, r.getHeight() * 0.5f);
    }
    void mouseDown(const juce::MouseEvent&) override { toggle(); }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            toggle();
            return true;
        }
        return false;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    void toggle()
    {
        card_.beginGesture("Op " + juce::String(op_ + 1) + " fixed");
        card_.setParam(cr::opParam(op_, cr::fixed), isFixed(card_, op_) ? 0.0f : 1.0f);
        card_.endGesture();
    }

    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
};

// The table's columns, left to right, and their widths (the level takes the rest).
struct Columns
{
    static constexpr int op = 30, role = 52, fix = 36, ratio = 52, fine = 42, detune = 52, wave = 72,
                         envelope = 64, velocity = 44, key = 50, gap = 6;
    static constexpr int fixedTotal =
        op + role + fix + ratio + fine + detune + wave + envelope + velocity + key + 10 * gap;
};

// One operator's row: its number and role, then a cell per setting. A click on
// the number, role or envelope selects it.
class OperatorRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    OperatorRow(ModuleCard& card, int op, juce::Colour hue)
        : level(card, op, hue), card_(card), op_(op), hue_(hue), fix_(card, op, hue), ratio_(card),
          fine_(card), detune_(card), wave_(card, op, hue), velocity_(card), key_(card)
    {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &fix_, &ratio_, &fine_, &detune_, &wave_, &level, &velocity_, &key_})
            addAndMakeVisible(c);
        const auto signedInt = [](const char* suffix)
        {
            return [suffix](float v)
            {
                const int n = static_cast<int>(std::lround(v));
                return (n > 0 ? "+" : "") + juce::String(n) + suffix;
            };
        };
        fine_.bind(cr::opParam(op, cr::fine), signedInt("%"), true);
        detune_.bind(cr::opParam(op, cr::detune), signedInt(" ct"), true);
        velocity_.bind(cr::opParam(op, cr::velocity), [](float v) { return percentText(v * 100.0); }, true);
        key_.bind(cr::opParam(op, cr::keyScale), [](float v) { return signedPercentText(v * 100.0); }, true);
        ratio_.setComponentID("op" + juce::String(op + 1) + "-ratio");
        fine_.setComponentID("op" + juce::String(op + 1) + "-fine");
        setWantsKeyboardFocus(true);
        setTitle("Operator " + juce::String(op + 1));
        update();
    }
    LevelBar level;
    bool selected = false;
    std::function<void(int)> onSelect;

    // The ratio cell edits the fixed frequency while Fixed is on, and Fine is
    // then disabled.
    void update()
    {
        const bool fixed = isFixed(card_, op_);
        if (fixed && ratio_.index() != cr::opParam(op_, cr::fixedHz))
            ratio_.bind(cr::opParam(op_, cr::fixedHz),
                        [](float hz)
                        {
                            return hz >= 1000.0f ? fixedText(hz / 1000.0, hz >= 10000.0f ? 1 : 2) + "k"
                                                 : juce::String(static_cast<int>(std::lround(hz)));
                        });
        else if (!fixed && ratio_.index() != cr::opParam(op_, cr::ratio))
            ratio_.bind(cr::opParam(op_, cr::ratio),
                        [](float v)
                        {
                            const double r = cr::ratioOf(v);
                            return juce::String::fromUTF8("\xc3\x97") +
                                   (r < 1.0 ? juce::String("0.5")
                                            : juce::String(static_cast<int>(std::lround(r))));
                        });
        fine_.setEnabled(!fixed);
        const auto& a = algorithmOf(card_);
        setTooltip("Operator " + juce::String(op_ + 1) + ": " +
                   (a.isCarrier(op_) ? juce::String("heard") : "modulates " + targetsOf(a, op_)) +
                   ". Click to select it.");
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto take = [&](int w)
        {
            auto cell = r.removeFromLeft(w).withSizeKeepingCentre(w, 22);
            r.removeFromLeft(Columns::gap);
            return cell;
        };
        take(Columns::op);
        take(Columns::role);
        fix_.setBounds(take(Columns::fix));
        ratio_.setBounds(take(Columns::ratio));
        fine_.setBounds(take(Columns::fine));
        detune_.setBounds(take(Columns::detune));
        wave_.setBounds(take(Columns::wave));
        level.setBounds(take(getWidth() - Columns::fixedTotal));
        envelope_ = take(Columns::envelope);
        velocity_.setBounds(take(Columns::velocity));
        key_.setBounds(r.removeFromLeft(Columns::key).withSizeKeepingCentre(Columns::key, 22));
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        if (selected)
        {
            g.setColour(c.selectionFill);
            g.fillRect(getLocalBounds());
        }
        const float fade = isMuted(card_, op_) ? 0.4f : 1.0f;
        auto r = getLocalBounds();
        drawOpSquare(g,
                     theme,
                     r.removeFromLeft(Columns::op).toFloat().withSizeKeepingCentre(20.0f, 20.0f),
                     hue_,
                     op_,
                     fade);
        r.removeFromLeft(Columns::gap);
        const auto& a = algorithmOf(card_);
        juce::String role = "OUT";
        if (!a.isCarrier(op_))
            role = "MOD" + juce::String::fromUTF8(" \xe2\x86\x92 ") + targetsOf(a, op_).replace(" and ", ",");
        g.setFont(theme.caption());
        g.setColour(c.textSecondary.withAlpha(fade));
        g.drawText(
            role, r.removeFromLeft(Columns::role).withTrimmedLeft(4), juce::Justification::centredLeft);
        g.setColour(hue_.withAlpha(fade));
        g.strokePath(
            envelopeShape(card_, op_, envelope_.toFloat().reduced(4.0f, 5.0f)),
            juce::PathStrokeType(1.25f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        drawFocus(g, *this, getLocalBounds().toFloat().reduced(1.0f), theme.radius.sm);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect(op_);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey && op_ > 0 && onSelect)
            onSelect(op_ - 1);
        else if (key == juce::KeyPress::downKey && op_ < ops - 1 && onSelect)
            onSelect(op_ + 1);
        else if (key == juce::KeyPress::returnKey && onSelect)
            onSelect(op_);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    ModuleCard& card_;
    int op_;
    juce::Colour hue_;
    FixCell fix_;
    TableCell ratio_, fine_, detune_;
    WaveCell wave_;
    TableCell velocity_, key_;
    juce::Rectangle<int> envelope_;
};

// ----- the display: sidebands or the envelope ---------------------------------------------------

// The selected operator's envelope, to drag; the fill brightens with its live level.
class EnvelopeView final : public AdsrDisplay
{
public:
    EnvelopeView(ModuleCard& card, const Hues& hues) : AdsrDisplay(card), hues_(hues)
    {
        setTooltip("Drag the handles: attack, decay and sustain, release");
    }
    int op = 0;
    float live = 0.0f; // 0..1

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto p = plot();
        const auto hue = hues_.op[static_cast<size_t>(op)];
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
            line, juce::PathStrokeType(1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        drawHandles(g, hue);
    }

private:
    int editedAttack() const override { return cr::opParam(op, cr::attack); }
    juce::Rectangle<float> plot() const override { return getLocalBounds().toFloat().reduced(8.0f, 8.0f); }

    const Hues& hues_;
};

// The display: the selected operator's sidebands or its envelope, switched by
// two pills at its top right.
class Display final : public juce::Component
{
public:
    Display(ModuleCard& card, const Hues& hues)
        : envelope(card, hues), attack(card), decay(card), sustain(card), release(card), card_(card),
          hues_(hues)
    {
        setTitle("Sidebands");
        for (auto* c :
             std::initializer_list<juce::Component*>{&envelope, &attack, &decay, &sustain, &release})
            addChildComponent(c);
        attack.setComponentID("envelope-attack");
        for (auto* b : {&sidebandsButton_, &envelopeButton_})
        {
            b->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
            addAndMakeVisible(b);
        }
        sidebandsButton_.setButtonText("Sidebands");
        envelopeButton_.setButtonText("Envelope");
        sidebandsButton_.setTooltip("What FM does to the note: the carrier and its sidebands");
        envelopeButton_.setTooltip("The selected operator's envelope");
        sidebandsButton_.onClick = [this] { setView(0); };
        envelopeButton_.onClick = [this] { setView(1); };
    }

    EnvelopeView envelope;
    TableCell attack, decay, sustain, release;
    std::function<void(int)> onView;
    int note = 60; // the note the sidebands are worked out for

    void setOp(int op)
    {
        op_ = op;
        envelope.op = op;
        const auto ms = [](float v) { return secondsText(v * 0.001); };
        attack.bind(cr::opParam(op, cr::attack), ms);
        decay.bind(cr::opParam(op, cr::decay), ms);
        sustain.bind(cr::opParam(op, cr::sustain), [](float v) { return percentText(v * 100.0); });
        release.bind(cr::opParam(op, cr::release), ms);
        repaint();
    }
    void setView(int view)
    {
        view_ = view;
        const bool env = view == 1;
        for (auto* c :
             std::initializer_list<juce::Component*>{&envelope, &attack, &decay, &sustain, &release})
            c->setVisible(env);
        const auto& c = card_.theme().colours;
        sidebandsButton_.setColour(juce::TextButton::textColourOffId, env ? c.textSecondary : c.textPrimary);
        envelopeButton_.setColour(juce::TextButton::textColourOffId, env ? c.textPrimary : c.textSecondary);
        setTitle(env ? "Envelope" : "Sidebands");
        if (onView)
            onView(view);
        repaint();
    }
    int view() const noexcept { return view_; }

    void resized() override
    {
        auto r = getLocalBounds().reduced(10, 8);
        auto top = r.removeFromTop(18);
        envelopeButton_.setBounds(top.removeFromRight(86));
        top.removeFromRight(4);
        sidebandsButton_.setBounds(top.removeFromRight(96));
        r.removeFromTop(6);
        cellRow_ = r.removeFromBottom(22);
        envelope.setBounds(r.withTrimmedBottom(4));
        auto cells = cellRow_;
        const int w = cells.getWidth() / 4;
        for (auto* c : {&attack, &decay, &sustain, &release})
            c->setBounds(cells.removeFromLeft(w).withTrimmedLeft(52).withWidth(std::min(64, w - 52)));
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        // The two pills: the chosen one filled.
        for (auto* b : {&sidebandsButton_, &envelopeButton_})
        {
            const bool on = (b == &sidebandsButton_) == (view_ == 0);
            const auto r = b->getBounds().toFloat();
            g.setColour(on ? c.surface3 : c.surface1);
            g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        }
        const auto caption = getLocalBounds().reduced(10, 8).removeFromTop(18);
        const auto dot = juce::String::fromUTF8("  \xc2\xb7  ");
        g.setFont(theme.captionCaps());
        g.setColour(c.textTertiary);
        if (view_ == 1)
        {
            g.drawText(
                "ENVELOPE" + dot + "OP " + juce::String(op_ + 1), caption, juce::Justification::centredLeft);
            g.setFont(theme.caption());
            auto labels = cellRow_;
            const int w = labels.getWidth() / 4;
            for (const char* name : {"Attack", "Decay", "Sustain", "Release"})
                g.drawText(name, labels.removeFromLeft(w).withWidth(50), juce::Justification::centredLeft);
            return;
        }
        paintSidebands(g, caption, dot);
    }

private:
    // The selected operator's pair: the carrier and its sidebands at the note.
    void paintSidebands(juce::Graphics& g, juce::Rectangle<int> caption, const juce::String& dot) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto& a = algorithmOf(card_);
        int modulator = -1, carrier = -1;
        if (!a.isCarrier(op_))
        {
            modulator = op_;
            for (int t = 0; t < op_ && carrier < 0; ++t)
                if (((a.modulators[static_cast<size_t>(t)] >> op_) & 1u) != 0)
                    carrier = t;
        }
        else
        {
            carrier = op_;
            for (int m = ops - 1; m > op_ && modulator < 0; --m)
                if (((a.modulators[static_cast<size_t>(op_)] >> m) & 1u) != 0)
                    modulator = m;
        }
        juce::String title = "SIDEBANDS" + dot + juce::String(core::noteName(note));
        if (modulator >= 0)
            title += dot + "OP " + juce::String(modulator + 1) + juce::String::fromUTF8(" \xe2\x86\x92 ") +
                     juce::String(carrier + 1);
        g.drawText(title, caption, juce::Justification::centredLeft);

        const auto plot =
            getLocalBounds().toFloat().reduced(14.0f, 8.0f).withTrimmedTop(38.0f).withTrimmedBottom(14.0f);
        constexpr float bottomDb = -48.0f;
        for (int db = 0; db >= -48; db -= 12)
        {
            g.setColour(db == -48 ? c.lineStrong : c.gridBeat);
            const float y = dbToY(static_cast<float>(db), 0.0f, bottomDb, plot);
            g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
        }
        const double base = 440.0 * std::exp2((note - 69) / 12.0);
        const auto hzOf = [&](int op)
        {
            if (isFixed(card_, op))
                return static_cast<double>(opValue(card_, op, cr::fixedHz));
            return base * cr::ratioOf(opValue(card_, op, cr::ratio)) *
                   (1.0 + opValue(card_, op, cr::fine) * 0.01);
        };
        const double carrierHz = hzOf(carrier);
        double index = 0.0, modulatorHz = 0.0;
        if (modulator >= 0)
        {
            modulatorHz = hzOf(modulator);
            index = 2.0 * juce::MathConstants<double>::pi * fm::maxIndexCycles *
                    fm::levelGain(opValue(card_, modulator, cr::level)) *
                    std::exp2(2.0 * card_.param(cr::tone) * 0.01);
        }
        constexpr int side = 5;
        const float stepX = plot.getWidth() / static_cast<float>(2 * side + 2);
        int partials = 0;
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.regular));
        for (int n = -side; n <= side; ++n)
        {
            const float x = plot.getCentreX() + static_cast<float>(n) * stepX;
            const double amplitude = modulator >= 0 ? std::abs(fm::besselJ(n, index)) : (n == 0 ? 1.0 : 0.0);
            const float db = amplitude > 1.0e-6 ? static_cast<float>(20.0 * std::log10(amplitude)) : -200.0f;
            if (db > -40.0f)
                ++partials;
            g.setColour(c.textTertiary);
            g.drawText(n == 0 ? juce::String("C")
                              : (n > 0 ? juce::String("+") : juce::String::fromUTF8("\xe2\x88\x92")) +
                                    juce::String(std::abs(n)),
                       juce::Rectangle<float>(x - 14.0f, plot.getBottom() + 2.0f, 28.0f, 12.0f),
                       juce::Justification::centred);
            if (db <= bottomDb)
                continue;
            const auto colour =
                n == 0 ? c.textPrimary
                       : (modulator >= 0 ? hues_.op[static_cast<size_t>(modulator)] : c.textTertiary);
            const float top = dbToY(db, 0.0f, bottomDb, plot);
            if (carrierHz + n * modulatorHz < 0.0)
            {
                // Below zero it folds back: dashed, at half strength.
                juce::Path dashed, line;
                line.startNewSubPath(x, plot.getBottom());
                line.lineTo(x, top);
                const float dashes[] = {3.0f, 3.0f};
                juce::PathStrokeType(1.5f).createDashedStroke(dashed, line, dashes, 2);
                g.setColour(colour.withAlpha(0.5f));
                g.fillPath(dashed);
                continue;
            }
            const float w = n == 0 ? 2.0f : 1.5f;
            g.setColour(colour);
            g.fillRect(juce::Rectangle<float>(x - w * 0.5f, top, w, plot.getBottom() - top));
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({x, top}));
        }
        // The reading under the caption.
        const auto reading = getLocalBounds().reduced(10, 8).withTrimmedTop(18).removeFromTop(14);
        g.setColour(c.textSecondary);
        if (modulator >= 0)
            g.drawText("index " + juce::String(index, 1) + " rad" + dot + juce::String(partials) +
                           " partials" + dot + "spacing " + compactHzText(modulatorHz),
                       reading,
                       juce::Justification::centredLeft);
        else
            g.drawText("Nothing modulates op " + juce::String(carrier + 1) + " in this algorithm.",
                       reading,
                       juce::Justification::centredLeft);
    }

    ModuleCard& card_;
    const Hues& hues_;
    int op_ = 0, view_ = 0;
    juce::TextButton sidebandsButton_, envelopeButton_;
    juce::Rectangle<int> cellRow_;
};

// The operator each Cirrus card had selected, and its display's view, kept while
// the app runs.
std::map<core::Id, std::pair<int, int>>& remembered()
{
    static std::map<core::Id, std::pair<int, int>> chosen;
    return chosen;
}

// ----- the editor -------------------------------------------------------------------------------

class CirrusCard final : public ModuleCard
{
public:
    CirrusCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), routing_(*this, hues_),
          algorithms_(*this, hues_.face), rows_{{{*this, 0, hues_.op[0]},
                                                 {*this, 1, hues_.op[1]},
                                                 {*this, 2, hues_.op[2]},
                                                 {*this, 3, hues_.op[3]}}},
          display_(*this, hues_), shape_(theme, hues_.motion), sync_(theme, hues_.motion),
          quality_(theme, hues_.tone)
    {
        routing_.selected = [this] { return selected_; };
        routing_.onSelect = [this](int op) { select(op); };
        for (auto& r : rows_)
        {
            r.onSelect = [this](int op) { select(op); };
            addAndMakeVisible(r);
        }
        for (auto* c : std::initializer_list<juce::Component*>{&routing_, &algorithms_, &display_})
            addAndMakeVisible(c);
        display_.onView = [this](int view) { remembered()[moduleId()].second = view; };
        // Named for the tests and the screen reader's tree.
        routing_.setComponentID("routing");
        algorithms_.setComponentID("algorithms");
        display_.setComponentID("display");
        for (int op = 0; op < ops; ++op)
            rows_[static_cast<size_t>(op)].setComponentID("operator" + juce::String(op + 1));

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
        const auto percent01 = [](double v) { return percentText(v * 100.0); };
        const auto signedPercent = [](double v) { return signedPercentText(v); };
        feedback_ = knob(cr::feedback, "Feedback", hues_.op[ops - 1], percent01);
        rate_ = knob(cr::lfoRate,
                     "Rate",
                     hues_.motion,
                     [this](double v)
                     {
                         if (const int c = plugins::sync::choiceOf(param(cr::lfoSync)); c > 0)
                             return juce::String(plugins::sync::noteValues[static_cast<size_t>(c)].name);
                         return v < 10.0 ? fixedText(v, 2) + " Hz" : fixedText(v, 1) + " Hz";
                     });
        pitchKnobs_ = {rate_,
                       knob(cr::lfoPitch,
                            "Pitch",
                            hues_.motion,
                            [](double v) { return juce::String(static_cast<int>(std::lround(v))) + " ct"; }),
                       knob(cr::lfoTone, "Bright", hues_.motion, percent01),
                       knob(cr::pitchEnvelope,
                            "Bend",
                            hues_.motion,
                            [](double v)
                            {
                                const int n = static_cast<int>(std::lround(v));
                                return (n > 0 ? "+" : "") + juce::String(n) + " st";
                            }),
                       knob(cr::pitchTime, "Bend time", hues_.motion)};
        glide_ = knob(cr::glide, "Glide", hues_.motion);
        pitchKnobs_.push_back(glide_);
        mono_ = &addToggle(cr::voiceMode, "Mono");
        mono_->setTooltip("Mono: one voice, legato");
        tone_ = knob(cr::tone, "Tone", hues_.tone, signedPercent);
        time_ = knob(cr::time, "Time", hues_.tone, signedPercent);
        tone_->setRingSize(46);
        time_->setRingSize(46);
        outputKnobs_ = {knob(cr::highCut,
                             "High cut",
                             hues_.tone,
                             [](double v) { return v >= 19500.0 ? juce::String("Off") : compactHzText(v); }),
                        knob(cr::width, "Width", hues_.tone, [](double v) { return percentText(v); }),
                        knob(cr::outputLevel, "Level", hues_.tone)};

        shape_.setChoices(info(cr::lfoShape));
        shape_.setTooltip("LFO shape");
        shape_.onChoose = [this](int s)
        {
            beginGesture("LFO shape");
            setParam(cr::lfoShape, static_cast<float>(s));
            endGesture();
        };
        for (const auto& c : plugins::sync::choices())
            sync_.choices.emplace_back(c);
        sync_.choices.front() = "Free";
        sync_.setTooltip("Sync the LFO to the project tempo");
        sync_.onChoose = [this](int i)
        {
            beginGesture("LFO sync");
            setParam(cr::lfoSync, static_cast<float>(i));
            endGesture();
        };
        quality_.setChoices(info(cr::quality));
        quality_.setTooltip("High: twice the rate, cleaner high notes. Eco: half the CPU.");
        quality_.onChoose = [this](int q)
        {
            beginGesture("Quality");
            setParam(cr::quality, static_cast<float>(q));
            endGesture();
        };
        for (auto* c : {&shape_, &sync_, &quality_})
            addAndMakeVisible(c);
        shapeLabel_ = &addLabel("Shape");
        syncLabel_ = &addLabel("Sync");

        presets_.onApply = [this](const std::vector<float>& values, const juce::String& name)
        { applyValues(values, "Preset " + name); };
        presets_.setValues(values());
        addAndMakeVisible(presets_);

        const auto known = remembered().find(moduleId());
        const auto choice = known != remembered().end() ? known->second : std::pair<int, int>{0, 0};
        select(choice.first);
        display_.setView(choice.second);
        updateStates();
    }

    int preferredWidth() const override { return 996; }
    int preferredBodyHeight() const override
    {
        return 2 * pad + topH + gap + middleH() + gap + bottomH() + theme_.spacing.sm;
    }

private:
    static constexpr int pad = 10, topH = 28, gap = 8, captionH = 18, panelPad = 8, routingW = 200, rowH = 26,
                         headerRowH = 18, cellW = 52, bigCellW = 62;

    int middleH() const { return captionH + 2 * knobCellHeight() + 2 * panelPad; }
    int bottomH() const { return headerRowH + ops * rowH + 2 * panelPad; }

    struct Panel
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    void select(int op)
    {
        selected_ = juce::jlimit(0, ops - 1, op);
        remembered()[moduleId()].first = selected_;
        for (int o = 0; o < ops; ++o)
        {
            auto& r = rows_[static_cast<size_t>(o)];
            r.selected = o == selected_;
            r.repaint();
        }
        display_.setOp(selected_);
        routing_.repaint();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        face_ = getLocalBounds();
        auto r = body.reduced(pad);
        // The top bar.
        auto top = r.removeFromTop(topH);
        wordmark_ = top.removeFromLeft(178);
        algorithms_.setBounds(
            top.removeFromLeft(AlgorithmButtons::width).withSizeKeepingCentre(AlgorithmButtons::width, 22));
        top.removeFromLeft(12);
        voices_ = top.removeFromRight(8 * 9).withSizeKeepingCentre(8 * 9, 15);
        top.removeFromRight(theme_.spacing.lg);
        presetBar_ = top.removeFromRight(240).withSizeKeepingCentre(240, theme_.sizes.controlCompact);
        presets_.setBounds(presetBar_);
        top.removeFromRight(theme_.spacing.lg);
        routeText_ = top;
        r.removeFromTop(gap);

        panels_.clear();
        const auto captionOf = [&](juce::Rectangle<int> p)
        { return p.reduced(panelPad).removeFromTop(captionH - 2); };
        const int cellH = knobCellHeight();

        // The middle row: routing, the display, pitch and the LFO.
        auto middle = r.removeFromTop(middleH());
        {
            auto p = middle.removeFromLeft(routingW);
            panels_.push_back({"ROUTING", p});
            auto inner = p.reduced(panelPad);
            inner.removeFromTop(captionH);
            placeKnob(*feedback_, inner.removeFromRight(cellW).withSizeKeepingCentre(cellW, cellH));
            routing_.setBounds(inner);
            middle.removeFromLeft(gap);
        }
        {
            auto p = middle.removeFromRight(4 * cellW + 2 * panelPad);
            panels_.push_back({juce::String::fromUTF8("PITCH  \xc2\xb7  LFO"), p});
            auto caption = captionOf(p);
            mono_->setBounds(caption.removeFromRight(52).withSizeKeepingCentre(52, 16));
            auto inner = p.reduced(panelPad);
            inner.removeFromTop(captionH);
            const auto cell = [&](int row, int col)
            {
                return juce::Rectangle<int>(
                    inner.getX() + col * cellW, inner.getY() + row * cellH, cellW, cellH);
            };
            for (int i = 0; i < 6; ++i)
                placeKnob(*pitchKnobs_[static_cast<size_t>(i)], cell(i / 3, i % 3));
            // The chips in knob cells: the chip where a ring sits, the name on the labels' line.
            const auto chipCell = [&](CornerChoice& chip, juce::Label& label, juce::Rectangle<int> c)
            {
                label.setBounds(labelLine(c));
                chip.setBounds(c.removeFromTop(theme_.sizes.knob).withSizeKeepingCentre(cellW - 4, 18));
            };
            chipCell(shape_, *shapeLabel_, cell(0, 3));
            chipCell(sync_, *syncLabel_, cell(1, 3));
            middle.removeFromRight(gap);
        }
        display_.setBounds(middle);
        r.removeFromTop(gap);

        // The bottom row: the operators' table, tone and the output.
        auto bottom = r.removeFromTop(bottomH());
        {
            auto p = bottom.removeFromRight(2 * bigCellW + 3 * cellW + 2 * panelPad);
            panels_.push_back({"TONE", p});
            auto caption = captionOf(p);
            quality_.setBounds(caption.removeFromRight(quality_.preferredWidth())
                                   .withSizeKeepingCentre(quality_.preferredWidth(), 16));
            auto inner = p.reduced(panelPad);
            inner.removeFromTop(captionH);
            // Every label on one line along the panel's foot.
            auto cells = inner.withTop(inner.getBottom() - knobCellHeight(46, theme_.fonts.size.caption));
            placeKnob(*tone_, cells.removeFromLeft(bigCellW), theme_.fonts.size.caption);
            placeKnob(*time_, cells.removeFromLeft(bigCellW), theme_.fonts.size.caption);
            for (auto* k : outputKnobs_)
                placeKnob(*k, cells.removeFromLeft(cellW).withTop(cells.getBottom() - cellH));
            bottom.removeFromRight(gap);
        }
        {
            panels_.push_back({{}, bottom}); // the table's header is its caption
            auto inner = bottom.reduced(panelPad);
            tableHeader_ = inner.removeFromTop(headerRowH);
            for (auto& row : rows_)
                row.setBounds(inner.removeFromTop(rowH));
        }
    }

    // ----- drawing ----------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.setColour(c.surface0);
        g.fillRoundedRectangle(face_.toFloat(), theme_.radius.md);
        // The wordmark and what it is.
        {
            const auto font = theme_.font(Theme::Face::mono, 15.0f, theme_.fonts.weight.semibold);
            juce::GlyphArrangement glyphs;
            float x = static_cast<float>(wordmark_.getX());
            const float baseline = static_cast<float>(wordmark_.getCentreY()) + 5.0f;
            for (const auto ch : juce::String("CIRRUS"))
            {
                juce::GlyphArrangement one;
                one.addLineOfText(font, juce::String::charToString(ch), x, baseline);
                x += one.getBoundingBox(0, -1, true).getWidth() + 5.0f;
                glyphs.addGlyphArrangement(one);
            }
            g.setColour(c.textPrimary);
            glyphs.draw(g);
            g.setFont(theme_.font(Theme::Face::mono, theme_.fonts.size.micro, theme_.fonts.weight.regular));
            g.setColour(c.textTertiary);
            g.drawText(juce::String::fromUTF8("FM \xc2\xb7 4 OP"),
                       juce::Rectangle<float>(x + 8.0f, baseline - 10.0f, 70.0f, 12.0f),
                       juce::Justification::centredLeft);
        }
        g.setFont(theme_.caption());
        g.setColour(c.textSecondary);
        g.drawText(describe(algorithmOf(*this), true), routeText_, juce::Justification::centredLeft);
        paintVoices(g);
        // The panels: canvas wells with a rim, their captions at the top left.
        const auto well = [&](juce::Rectangle<int> b)
        {
            const auto r = b.toFloat();
            g.setColour(c.canvas);
            g.fillRoundedRectangle(r, theme_.radius.sm);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.sm, 1.0f);
        };
        for (const auto& p : panels_)
        {
            well(p.bounds);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textTertiary);
            g.drawText(p.title,
                       p.bounds.reduced(panelPad).removeFromTop(captionH - 2),
                       juce::Justification::centredLeft);
        }
        well(display_.getBounds()); // its caption is its own
        // The table's header and the rules between its rows.
        {
            g.setFont(theme_.captionCaps());
            g.setColour(c.textTertiary);
            auto h = tableHeader_;
            const int levelW = h.getWidth() - Columns::fixedTotal;
            const std::pair<const char*, int> columns[] = {{"OP", Columns::op},
                                                           {"ROLE", Columns::role},
                                                           {"FIX", Columns::fix},
                                                           {"RATIO", Columns::ratio},
                                                           {"FINE", Columns::fine},
                                                           {"DETUNE", Columns::detune},
                                                           {"WAVE", Columns::wave},
                                                           {"LEVEL", levelW},
                                                           {"ENVELOPE", Columns::envelope},
                                                           {"VEL", Columns::velocity},
                                                           {"KEY", Columns::key}};
            for (const auto& [name, w] : columns)
            {
                const bool first = std::string(name) == "OP";
                g.drawText(name,
                           h.removeFromLeft(w).withTrimmedLeft(first ? 0 : 4),
                           juce::Justification::centredLeft);
                h.removeFromLeft(Columns::gap);
            }
            g.setColour(c.lineSubtle);
            for (const auto& row : rows_)
                g.drawHorizontalLine(
                    row.getY(), static_cast<float>(row.getX()), static_cast<float>(row.getRight()));
        }
        // The preset bar's well.
        g.setColour(c.canvas);
        g.fillRoundedRectangle(presetBar_.toFloat(), presetBar_.getHeight() * 0.5f);
        g.setColour(c.lineStrong);
        g.drawRoundedRectangle(presetBar_.toFloat().reduced(0.5f), presetBar_.getHeight() * 0.5f, 1.0f);
    }

    // Sixteen square lights in two rows of eight: how many voices sound.
    void paintVoices(juce::Graphics& g) const
    {
        for (int i = 0; i < cr::voiceCount; ++i)
        {
            const float x = static_cast<float>(voices_.getX()) + 4.0f + static_cast<float>(i % 8) * 9.0f;
            const float y = static_cast<float>(voices_.getCentreY()) + (i < 8 ? -4.5f : 4.5f);
            g.setColour(i < activeVoices_ ? theme_.colours.textPrimary : theme_.colours.surface3);
            g.fillRoundedRectangle(juce::Rectangle<float>(6.0f, 6.0f).withCentre({x, y}), 1.0f);
        }
    }

    // ----- live -------------------------------------------------------------------------------

    // What can be used depends on other values: Glide by Mono, the Rate knob by
    // Sync, Fine and the ratio cell by Fixed.
    void updateStates()
    {
        const bool mono = param(cr::voiceMode) >= 0.5f;
        if (glide_->isEnabled() != mono)
        {
            glide_->setEnabled(mono);
            if (auto* l = labelFor(*glide_))
                l->setColour(juce::Label::textColourId,
                             mono ? theme_.colours.textSecondary : theme_.colours.textDisabled);
        }
        rate_->setAlpha(plugins::sync::choiceOf(param(cr::lfoSync)) > 0 ? 0.5f : 1.0f);
        shape_.selected = juce::jlimit(0, 4, static_cast<int>(std::lround(param(cr::lfoShape))));
        sync_.selected = plugins::sync::choiceOf(param(cr::lfoSync));
        quality_.selected = param(cr::quality) >= 0.5f ? 1 : 0;
        for (auto& r : rows_)
            r.update();
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
        bool routing = false, display = false;
        std::array<bool, ops> row{};
        for (int op = 0; op < ops; ++op)
            for (int f = 0; f < cr::fieldCount; ++f)
                if (changed(cr::opParam(op, static_cast<cr::OpField>(f))))
                {
                    row[static_cast<size_t>(op)] = true;
                    display = true;
                    if (f == cr::level)
                        routing = true;
                }
        if (changed(cr::algorithm))
        {
            routing = display = true;
            row.fill(true);
            algorithms_.repaint();
            repaint(routeText_);
        }
        routing = routing || changed(cr::feedback);
        display = display || changed(cr::tone);
        const bool chips = changed(cr::lfoShape) || changed(cr::lfoSync) || changed(cr::quality);
        last_ = now;
        updateStates();
        if (routing)
            routing_.repaint();
        if (display)
            display_.repaint();
        for (int op = 0; op < ops; ++op)
            if (row[static_cast<size_t>(op)])
                rows_[static_cast<size_t>(op)].repaint();
        if (chips)
            for (auto* c : {&shape_, &sync_, &quality_})
                c->repaint();
        if (changed(cr::lfoSync))
            rate_->repaint();
    }

    void framesArrived(double) override
    {
        // The newest frame carries the voice count.
        if (historySize() > 0)
        {
            const int voices = static_cast<int>(std::lround(historyAt(historySize() - 1).reduction));
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(voices_.expanded(4));
            }
        }
        // The operators' live levels and the note playing.
        const auto* live = liveProcessor();
        const auto* activity = live != nullptr ? cr::activity(*live) : nullptr;
        if (activity == nullptr)
            return;
        const int note = activity->note.load(std::memory_order_relaxed);
        if (note >= 0 && note != display_.note)
        {
            display_.note = note;
            if (display_.view() == 0)
                display_.repaint();
        }
        bool routingMoved = false;
        for (int op = 0; op < ops; ++op)
        {
            const auto o = static_cast<size_t>(op);
            const float steps = levelSteps(activity->level[o].load(std::memory_order_relaxed));
            auto& bar = rows_[o].level;
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
            if (op == selected_ && std::abs(shown - display_.envelope.live) > 0.01f)
            {
                display_.envelope.live = shown;
                if (display_.view() == 1)
                    display_.envelope.repaint();
            }
        }
        if (routingMoved)
            routing_.repaint();
    }

    Hues hues_;
    RoutingDisplay routing_;
    AlgorithmButtons algorithms_;
    std::array<OperatorRow, ops> rows_;
    Display display_;
    CornerChoice shape_, sync_, quality_;
    juce::Label *shapeLabel_ = nullptr, *syncLabel_ = nullptr;
    Knob *feedback_ = nullptr, *rate_ = nullptr, *glide_ = nullptr, *tone_ = nullptr, *time_ = nullptr;
    std::vector<Knob*> pitchKnobs_, outputKnobs_;
    PillToggle* mono_ = nullptr;
    PresetBar presets_{theme_, cr::type, true};
    std::vector<Panel> panels_;
    juce::Rectangle<int> face_, wordmark_, voices_, presetBar_, routeText_, tableHeader_;
    int selected_ = 0, activeVoices_ = 0;
    std::vector<float> last_;
};
} // namespace

std::unique_ptr<ModuleCard>
createCirrusEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<CirrusCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
