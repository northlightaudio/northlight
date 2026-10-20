#include "ui/strip/Graphs.h"

#include "plugins/ChannelStrip.h"
#include "ui/strip/StripStyle.h"

#include <cmath>

namespace aural::ui::strip
{

namespace
{
constexpr double framesPerSecond = 1.0 / engine::ProcessorFeedback::frameSeconds;

void drawDbGrid(
    juce::Graphics& g, const Theme& theme, juce::Rectangle<float> plot, float topDb, float bottomDb)
{
    g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
    for (float db = topDb - 12.0f; db > bottomDb; db -= 12.0f)
    {
        const float y = dbToY(db, topDb, bottomDb, plot);
        g.setColour(theme.colours.gridBar);
        g.drawHorizontalLine(static_cast<int>(y), plot.getX(), plot.getRight());
        g.setColour(theme.colours.textTertiary);
        g.drawText(juce::String(static_cast<int>(db)),
                   juce::Rectangle<float>(plot.getRight() - 26.0f, y - 12.0f, 24.0f, 11.0f),
                   juce::Justification::centredRight);
    }
}

juce::Colour reductionColour(const Theme& theme)
{
    return theme.colours.meterHot;
}
} // namespace

// ----- history -------------------------------------------------------------------------------------

HistoryGraph::HistoryGraph(ModuleCard& card, Style style) : card_(card), style_(style)
{
    setOpaque(false);
}

juce::Rectangle<float> HistoryGraph::plot() const
{
    return getLocalBounds().toFloat().reduced(1.0f);
}

void HistoryGraph::paint(juce::Graphics& g)
{
    paintContent(g);
    drawWellFrame(g, card_.theme(), getLocalBounds().toFloat());
}

void HistoryGraph::paintContent(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    const auto area = getLocalBounds().toFloat();
    drawWell(g, theme, area);
    auto p = plot();
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(p.toNearestInt());
    const float top = style_.topDb, bottom = style_.bottomDb;
    drawDbGrid(g, theme, p, top, bottom);

    // Columns, newest on the right. Each column is a fixed run of `perColumn`
    // frames counted from the start of the history (not from the newest frame),
    // so a column keeps its frames as the graph scrolls and the picture moves a
    // whole column at a time instead of shimmering as frames regroup.
    const int columns = std::max(1, static_cast<int>(p.getWidth()));
    level_.assign(static_cast<size_t>(columns), 0.0f);
    output_.assign(static_cast<size_t>(columns), 0.0f);
    reduction_.assign(static_cast<size_t>(columns), 0.0f);
    const auto perColumn =
        static_cast<std::uint64_t>(std::max(1L, std::lround(style_.seconds * framesPerSecond / columns)));
    const int size = card_.historySize();
    const auto total = card_.historyTotal();
    const auto first = total - static_cast<std::uint64_t>(size); // absolute number of historyAt(0)
    const auto newestBucket = total > 0 ? (total - 1) / perColumn : 0;
    for (int col = 0; col < columns && total > 0; ++col)
    {
        const auto fromRight = static_cast<std::uint64_t>(columns - 1 - col);
        if (fromRight > newestBucket)
            continue;
        const auto bucket = newestBucket - fromRight;
        float l = 0.0f, o = 0.0f, r = 0.0f;
        for (auto a = bucket * perColumn; a < (bucket + 1) * perColumn && a < total; ++a)
        {
            if (a < first)
                continue;
            const auto& f = card_.historyAt(static_cast<int>(a - first));
            l = std::max(l, style_.levelFromExtra ? f.extra : f.input);
            o = std::max(o, f.output);
            r = std::max(r, f.reduction);
        }
        level_[static_cast<size_t>(col)] = l;
        output_[static_cast<size_t>(col)] = o;
        reduction_[static_cast<size_t>(col)] = r;
    }
    // Meter ballistics across the columns: a level rises at once and falls at
    // 30 dB a second, so the curves read as envelopes rather than as the raw
    // 5 ms peaks (which are jagged at this scale).
    const auto fall =
        static_cast<float>(std::pow(10.0, -30.0 * static_cast<double>(perColumn) / framesPerSecond / 20.0));
    for (int col = 1; col < columns; ++col)
    {
        auto& l = level_[static_cast<size_t>(col)];
        auto& o = output_[static_cast<size_t>(col)];
        l = std::max(l, level_[static_cast<size_t>(col - 1)] * fall);
        o = std::max(o, output_[static_cast<size_t>(col - 1)] * fall);
    }
    const auto x = [&](int col) { return p.getX() + static_cast<float>(col) + 0.5f; };
    const float bottomY = p.getBottom() - (style_.gateLane ? 5.0f : 0.0f);
    const auto yOf = [&](float linear) { return std::min(bottomY, dbToY(toDb(linear), top, bottom, p)); };

    const juce::PathStrokeType lineStroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    // The level as an area.
    juce::Path levelArea;
    levelArea.startNewSubPath(p.getX(), bottomY);
    for (int col = 0; col < columns; ++col)
        levelArea.lineTo(x(col), yOf(level_[static_cast<size_t>(col)]));
    levelArea.lineTo(p.getRight(), bottomY);
    levelArea.closeSubPath();
    g.setColour(c.textTertiary.withAlpha(0.35f));
    g.fillPath(levelArea);

    // The output as a line with a faint fill in the module's hue.
    if (style_.outputLine)
    {
        juce::Path line;
        for (int col = 0; col < columns; ++col)
        {
            const float y = yOf(output_[static_cast<size_t>(col)]);
            if (col == 0)
                line.startNewSubPath(x(col), y);
            else
                line.lineTo(x(col), y);
        }
        if (style_.outputFill)
        {
            juce::Path fill(line);
            fill.lineTo(p.getRight(), bottomY);
            fill.lineTo(p.getX(), bottomY);
            fill.closeSubPath();
            g.setColour(card_.colour().withAlpha(0.18f));
            g.fillPath(fill);
        }
        g.setColour(card_.colour().withAlpha(0.9f));
        g.strokePath(line, lineStroke);
    }

    // Gain reduction hanging from the top (0 dB of reduction is the top edge).
    if (style_.reductionFromTop)
    {
        juce::Path area2;
        area2.startNewSubPath(p.getX(), p.getY());
        bool any = false;
        for (int col = 0; col < columns; ++col)
        {
            const float r = reduction_[static_cast<size_t>(col)];
            any = any || r > 0.05f;
            area2.lineTo(x(col), dbToY(top - r, top, bottom, p));
        }
        area2.lineTo(p.getRight(), p.getY());
        area2.closeSubPath();
        if (any)
        {
            g.setColour(reductionColour(theme).withAlpha(0.35f));
            g.fillPath(area2);
            juce::Path edge;
            for (int col = 0; col < columns; ++col)
            {
                const float y = dbToY(top - reduction_[static_cast<size_t>(col)], top, bottom, p);
                if (col == 0)
                    edge.startNewSubPath(x(col), y);
                else
                    edge.lineTo(x(col), y);
            }
            g.setColour(reductionColour(theme).withAlpha(0.9f));
            g.strokePath(edge, juce::PathStrokeType(1.0f));
        }
    }

    // The gate lane: open in the hue, closed dim, drawn as runs (not a
    // rectangle per pixel).
    if (style_.gateLane)
    {
        int start = 0;
        for (int col = 1; col <= columns; ++col)
        {
            const bool open = reduction_[static_cast<size_t>(start)] < 1.0f && size > 0;
            if (col < columns && (reduction_[static_cast<size_t>(col)] < 1.0f && size > 0) == open)
                continue;
            g.setColour(open ? card_.colour().withAlpha(0.85f) : c.surface3);
            g.fillRect(juce::Rectangle<float>(p.getX() + static_cast<float>(start),
                                              p.getBottom() - 4.0f,
                                              static_cast<float>(col - start),
                                              4.0f));
            start = col;
        }
    }

    // The threshold.
    if (style_.thresholdParam >= 0)
    {
        const float db = card_.param(style_.thresholdParam);
        const float y = dbToY(db, top, bottom, p);
        const bool hot = dragging_ || isMouseOver();
        g.setColour(c.textPrimary.withAlpha(hot ? 0.9f : 0.6f));
        const float dashes[] = {4.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(p.getX(), y, p.getRight(), y), dashes, 2, hot ? 1.5f : 1.0f);
        g.setFont(theme.value());
        g.setColour(c.textPrimary.withAlpha(hot ? 1.0f : 0.75f));
        const auto label = juce::String(db, 1) + " dB";
        const float ly = y > p.getY() + 16.0f ? y - 15.0f : y + 2.0f;
        g.drawText(label,
                   juce::Rectangle<float>(p.getX() + 4.0f, ly, 80.0f, 13.0f),
                   juce::Justification::centredLeft);
    }
}

bool HistoryGraph::nearThreshold(int y) const
{
    if (style_.thresholdParam < 0)
        return false;
    const float ty = dbToY(card_.param(style_.thresholdParam), style_.topDb, style_.bottomDb, plot());
    return std::abs(static_cast<float>(y) - ty) <= 6.0f;
}

void HistoryGraph::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(style_.thresholdParam >= 0 ? (nearThreshold(e.y) ? juce::MouseCursor::UpDownResizeCursor
                                                                    : juce::MouseCursor::NormalCursor)
                                              : juce::MouseCursor::NormalCursor);
    repaint();
}

void HistoryGraph::mouseDown(const juce::MouseEvent& e)
{
    if (style_.thresholdParam < 0)
        return;
    // Anywhere in the graph grabs the threshold (it jumps to the pointer).
    dragging_ = true;
    card_.beginGesture(card_.info(style_.thresholdParam).name);
    card_.setParam(style_.thresholdParam,
                   yToDb(static_cast<float>(e.y), style_.topDb, style_.bottomDb, plot()));
    repaint();
}

void HistoryGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    card_.setParam(style_.thresholdParam,
                   yToDb(static_cast<float>(e.y), style_.topDb, style_.bottomDb, plot()));
    repaint();
}

void HistoryGraph::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    card_.endGesture();
    repaint();
}

void HistoryGraph::mouseDoubleClick(const juce::MouseEvent&)
{
    if (style_.thresholdParam < 0)
        return;
    card_.beginGesture(card_.info(style_.thresholdParam).name);
    card_.setParam(style_.thresholdParam, card_.info(style_.thresholdParam).defaultValue);
    card_.endGesture();
}

// ----- transfer curve -------------------------------------------------------------------------------

namespace comp = plugins::strip::compressor;

TransferCurve::TransferCurve(ModuleCard& card) : card_(card)
{
    setOpaque(false);
}

juce::Rectangle<float> TransferCurve::plot() const
{
    auto r = getLocalBounds().toFloat();
    const float side = std::min(r.getWidth(), r.getHeight());
    return r.withSizeKeepingCentre(side, side).reduced(1.0f);
}

float TransferCurve::outputDb(float inputDb) const
{
    float makeup = card_.param(comp::makeup);
    if (card_.param(comp::autoMakeup) >= 0.5f)
        makeup += comp::autoMakeupDb(card_.param(comp::threshold), card_.param(comp::ratio));
    const float wet = card_.param(comp::mix) * 0.01f;
    const float reduction = comp::gainReductionDb(
        inputDb, card_.param(comp::threshold), card_.param(comp::ratio), card_.param(comp::knee));
    // With the dry mix the curve is the blend of the two paths' gains.
    const float gain = (1.0f - wet) + wet * std::pow(10.0f, (makeup - reduction) / 20.0f);
    return inputDb + 20.0f * std::log10(std::max(1.0e-6f, gain));
}

void TransferCurve::paint(juce::Graphics& g)
{
    paintContent(g);
    drawWellFrame(g, card_.theme(), plot().expanded(1.0f));
}

void TransferCurve::paintContent(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    const auto p = plot();
    drawWell(g, theme, p.expanded(1.0f));
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(p.toNearestInt());
    // Grid every 12 dB both ways and the unity diagonal.
    for (float db = -12.0f; db > floorDb; db -= 12.0f)
    {
        g.setColour(c.gridBar);
        const float y = dbToY(db, 0.0f, floorDb, p);
        const float x = p.getX() + (db - floorDb) / -floorDb * p.getWidth();
        g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
        g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
    }
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(c.textTertiary.withAlpha(0.6f));
    g.drawDashedLine(juce::Line<float>(p.getBottomLeft(), p.getTopRight()), dashes, 2, 1.0f);

    const auto xOf = [&](float db) { return p.getX() + (db - floorDb) / -floorDb * p.getWidth(); };
    const auto inAt = [&](float x) { return floorDb + (x - p.getX()) / p.getWidth() * -floorDb; };

    // The knee region, shaded.
    const float threshold = card_.param(comp::threshold);
    const float knee = card_.param(comp::knee);
    if (knee > 0.0f)
    {
        g.setColour(card_.colour().withAlpha(0.08f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(
            xOf(threshold - knee * 0.5f), p.getY(), xOf(threshold + knee * 0.5f), p.getBottom()));
    }
    // The curve.
    juce::Path curve;
    for (float x = p.getX(); x <= p.getRight(); x += 1.0f)
    {
        const float y = dbToY(outputDb(inAt(x)), 0.0f, floorDb, p);
        if (x == p.getX())
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    juce::Path under(curve);
    under.lineTo(p.getRight(), p.getBottom());
    under.lineTo(p.getX(), p.getBottom());
    under.closeSubPath();
    g.setColour(card_.colour().withAlpha(0.12f));
    g.fillPath(under);
    g.setColour(card_.colour());
    g.strokePath(curve, juce::PathStrokeType(2.0f));

    // The threshold handle.
    const float tx = xOf(threshold);
    const float ty = dbToY(outputDb(threshold), 0.0f, floorDb, p);
    g.setColour(c.textPrimary.withAlpha(mode_ == Mode::threshold ? 1.0f : 0.7f));
    g.drawVerticalLine(static_cast<int>(tx), ty + 6.0f, p.getBottom());
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({tx, ty}));

    // The live detector level on the curve, with a trail line from the floor.
    const float level = card_.extraDb();
    if (level > floorDb)
    {
        const float lx = xOf(level);
        const float ly = dbToY(outputDb(level), 0.0f, floorDb, p);
        g.setColour(card_.colour().withAlpha(0.25f));
        g.drawVerticalLine(static_cast<int>(lx), ly, p.getBottom());
        g.setColour(card_.colour().withAlpha(0.35f));
        g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre({lx, ly}));
        g.setColour(c.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({lx, ly}));
    }
    // Ratio caption.
    g.setFont(theme.value());
    g.setColour(c.textSecondary);
    g.drawText(juce::String(card_.param(comp::ratio), 1) + ":1",
               p.reduced(4.0f).removeFromTop(14.0f),
               juce::Justification::centredLeft);
}

TransferCurve::Mode TransferCurve::modeAt(juce::Point<float> pt) const
{
    const auto p = plot();
    const float tx = p.getX() + (card_.param(comp::threshold) - floorDb) / -floorDb * p.getWidth();
    if (std::abs(pt.x - tx) <= 8.0f || pt.x < tx)
        return Mode::threshold;
    return Mode::ratio;
}

void TransferCurve::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(modeAt(e.position) == Mode::threshold ? juce::MouseCursor::LeftRightResizeCursor
                                                         : juce::MouseCursor::UpDownResizeCursor);
}

void TransferCurve::mouseDown(const juce::MouseEvent& e)
{
    mode_ = modeAt(e.position);
    card_.beginGesture(mode_ == Mode::threshold ? "Threshold" : "Ratio");
    mouseDrag(e);
}

void TransferCurve::mouseDrag(const juce::MouseEvent& e)
{
    const auto p = plot();
    const float inDb = floorDb + (e.position.x - p.getX()) / p.getWidth() * -floorDb;
    if (mode_ == Mode::threshold)
        card_.setParam(comp::threshold, juce::jlimit(floorDb, 0.0f, inDb));
    else if (mode_ == Mode::ratio)
    {
        // The pointer sets the slope above the threshold: out = t + (in - t) / ratio.
        const float threshold = card_.param(comp::threshold);
        const float outDb = yToDb(e.position.y, 0.0f, floorDb, p);
        const float over = std::max(1.0f, inDb - threshold);
        const float rise = juce::jlimit(over / 20.0f, over, outDb - threshold);
        card_.setParam(comp::ratio, juce::jlimit(1.0f, 20.0f, over / std::max(0.05f, rise)));
    }
    repaint();
}

void TransferCurve::mouseUp(const juce::MouseEvent&)
{
    if (mode_ != Mode::none)
        card_.endGesture();
    mode_ = Mode::none;
    repaint();
}

// ----- saturation curve -----------------------------------------------------------------------------

namespace sat = plugins::strip::saturation;

SaturationCurve::SaturationCurve(ModuleCard& card) : card_(card)
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

void SaturationCurve::paint(juce::Graphics& g)
{
    paintContent(g);
    drawWellFrame(g, card_.theme(), getLocalBounds().toFloat());
}

void SaturationCurve::paintContent(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    auto r = getLocalBounds().toFloat();
    drawWell(g, theme, r);
    const auto p = r.reduced(6.0f);
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(r.reduced(1.0f).toNearestInt());
    const float drive = std::pow(10.0f, card_.param(sat::drive) / 20.0f);
    const float trim = std::pow(
        10.0f, (sat::driveCompensationDb(card_.param(sat::drive)) + card_.param(sat::output)) / 20.0f);
    const float wet = card_.param(sat::mix) * 0.01f;
    const int type = static_cast<int>(std::lround(card_.param(sat::type)));
    const auto xOf = [&](float v) { return p.getCentreX() + v * p.getWidth() * 0.5f; };
    const auto yOf = [&](float v)
    { return p.getCentreY() - juce::jlimit(-1.1f, 1.1f, v) * p.getHeight() * 0.5f; };
    // Axes and the unity line.
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(p.getCentreY()), r.getX(), r.getRight());
    g.drawVerticalLine(static_cast<int>(p.getCentreX()), r.getY(), r.getBottom());
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(c.textTertiary.withAlpha(0.5f));
    g.drawDashedLine(juce::Line<float>(xOf(-1.0f), yOf(-1.0f), xOf(1.0f), yOf(1.0f)), dashes, 2, 1.0f);

    // The part of the curve the signal is using right now.
    const float peak = std::min(1.0f, std::pow(10.0f, card_.inputDb() / 20.0f));
    if (peak > 0.001f)
    {
        g.setColour(card_.colour().withAlpha(0.12f));
        g.fillRect(
            juce::Rectangle<float>::leftTopRightBottom(xOf(-peak), r.getY(), xOf(peak), r.getBottom()));
    }
    const auto curveAt = [&](float x) { return (1.0f - wet) * x + wet * sat::shape(type, x * drive) * trim; };
    juce::Path curve;
    for (float px = p.getX(); px <= p.getRight(); px += 1.0f)
    {
        const float x = (px - p.getCentreX()) / (p.getWidth() * 0.5f);
        const float y = yOf(curveAt(x));
        if (px == p.getX())
            curve.startNewSubPath(px, y);
        else
            curve.lineTo(px, y);
    }
    g.setColour(card_.colour());
    g.strokePath(curve, juce::PathStrokeType(2.0f));
    if (peak > 0.001f)
    {
        for (const float x : {-peak, peak})
        {
            const juce::Point<float> at{xOf(x), yOf(curveAt(x))};
            g.setColour(card_.colour().withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre(at));
            g.setColour(c.textPrimary);
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(at));
        }
    }
}

// ----- threshold meter -----------------------------------------------------------------------------

ThresholdMeter::ThresholdMeter(ModuleCard& card,
                               int parameter,
                               Level level,
                               float bottomDb,
                               juce::String caption,
                               juce::Colour overColour)
    : card_(card), parameter_(parameter), level_(level), bottomDb_(bottomDb), caption_(std::move(caption)),
      over_(overColour)
{
    setOpaque(false);
    setRepaintsOnMouseActivity(true);
    setTitle(juce::String(card.info(parameter).name));
    setTooltip(juce::String(card.info(parameter).name) +
               ": drag on the meter (Shift for fine), wheel to step, double-click to reset");
}

juce::Rectangle<float> ThresholdMeter::bar() const
{
    const auto& theme = card_.theme();
    const float captionH = theme.fonts.size.caption + 4.0f;
    auto r = getLocalBounds().toFloat();
    r.removeFromTop(captionH);
    r.removeFromBottom(captionH);
    return r.withSizeKeepingCentre(14.0f, r.getHeight()).reduced(0.0f, 2.0f);
}

float ThresholdMeter::dbAt(float y) const
{
    const auto& info = card_.info(parameter_);
    return juce::jlimit(info.minimum, info.maximum, yToDb(y, 0.0f, bottomDb_, bar()));
}

void ThresholdMeter::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    const auto b = bar();
    const float captionH = theme.fonts.size.caption + 4.0f;
    const float threshold = card_.param(parameter_);
    const float level = level_ == Level::input ? card_.inputDb() : card_.extraDb();
    const float ty = dbToY(threshold, 0.0f, bottomDb_, b);
    const float ly = dbToY(level, 0.0f, bottomDb_, b);
    drawWell(g, theme, b);
    // The level: below the threshold in the meter's zone colours, over it in `over_`.
    if (level > bottomDb_)
    {
        const auto inner = b.reduced(2.0f, 1.0f);
        g.setColour(
            theme.meter.colourAt(std::min(level, threshold)).withMultipliedAlpha(card_.isOn() ? 1.0f : 0.6f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(
            inner.getX(), std::max(ly, ty), inner.getRight(), b.getBottom() - 1.0f));
        if (ly < ty)
        {
            g.setColour(over_);
            g.fillRect(juce::Rectangle<float>::leftTopRightBottom(inner.getX(), ly, inner.getRight(), ty));
        }
    }
    // Scale ticks every 12 dB, on the left.
    g.setColour(c.textTertiary);
    for (float db = -12.0f; db > bottomDb_; db -= 12.0f)
    {
        const float y = dbToY(db, 0.0f, bottomDb_, b);
        g.fillRect(juce::Rectangle<float>(b.getX() - 5.0f, y, 3.0f, 1.0f));
    }
    drawWellFrame(g, theme, b);
    // The threshold: a line across the well and a handle pointing at it.
    const bool hot = dragging_ || isMouseOver();
    g.setColour(c.textPrimary.withAlpha(hot ? 1.0f : 0.85f));
    g.fillRect(juce::Rectangle<float>(b.getX() - 2.0f, ty - 0.75f, b.getWidth() + 4.0f, 1.5f));
    juce::Path handle;
    const float hx = b.getRight() + 2.0f, size = hot ? 7.0f : 6.0f;
    handle.addTriangle(hx, ty, hx + size, ty - size * 0.75f, hx + size, ty + size * 0.75f);
    g.fillPath(handle);
    // The caption above and the value below.
    g.setFont(theme.caption());
    g.setColour(c.textTertiary);
    g.drawText(caption_, getLocalBounds().toFloat().withHeight(captionH), juce::Justification::centred);
    g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption, theme.fonts.weight.medium));
    g.setColour(hot ? c.textPrimary : c.textSecondary);
    g.drawText(juce::String(threshold, 1),
               getLocalBounds().toFloat().withTop(getHeight() - captionH),
               juce::Justification::centred);
}

void ThresholdMeter::mouseDown(const juce::MouseEvent& e)
{
    dragging_ = true;
    dragStartDb_ = card_.param(parameter_);
    dragStartY_ = e.y;
    card_.beginGesture(card_.info(parameter_).name);
    // A click away from the handle moves the threshold there; on the handle it only grabs it.
    const float ty = dbToY(dragStartDb_, 0.0f, bottomDb_, bar());
    if (std::abs(static_cast<float>(e.y) - ty) > 5.0f && !e.mods.isShiftDown())
    {
        dragStartDb_ = dbAt(static_cast<float>(e.y));
        card_.setParam(parameter_, dragStartDb_);
    }
    repaint();
}

void ThresholdMeter::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    const float perPixel = (0.0f - bottomDb_) / std::max(1.0f, bar().getHeight());
    const float scale = e.mods.isShiftDown() ? 0.2f : 1.0f;
    card_.setParam(parameter_, dragStartDb_ - static_cast<float>(e.y - dragStartY_) * perPixel * scale);
    repaint();
}

void ThresholdMeter::mouseUp(const juce::MouseEvent&)
{
    if (dragging_)
        card_.endGesture();
    dragging_ = false;
    repaint();
}

void ThresholdMeter::mouseDoubleClick(const juce::MouseEvent&)
{
    card_.beginGesture(card_.info(parameter_).name);
    card_.setParam(parameter_, card_.info(parameter_).defaultValue);
    card_.endGesture();
}

void ThresholdMeter::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY == 0.0f)
        return;
    card_.beginGesture(card_.info(parameter_).name);
    card_.setParam(parameter_, card_.param(parameter_) + (wheel.deltaY > 0.0f ? 0.5f : -0.5f));
    card_.endGesture();
}

// ----- level bars -----------------------------------------------------------------------------------

LevelBars::LevelBars(ModuleCard& card, std::vector<Kind> kinds) : card_(card), kinds_(std::move(kinds))
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

void LevelBars::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    auto area = getLocalBounds();
    const int columnWidth = area.getWidth() / std::max<int>(1, static_cast<int>(kinds_.size()));
    const int captionH = static_cast<int>(theme.fonts.size.caption) + 4;
    for (const auto kind : kinds_)
    {
        auto col = area.removeFromLeft(columnWidth);
        const auto caption = col.removeFromBottom(captionH);
        const auto value = col.removeFromTop(captionH);
        auto bar = col.withSizeKeepingCentre(14, col.getHeight()).toFloat().reduced(0.0f, 2.0f);
        drawWell(g, theme, bar);
        const auto inner = bar.reduced(2.0f, 1.0f);
        juce::String text, name;
        if (kind == Kind::reduction)
        {
            const float gr = card_.reductionDb();
            const float h = juce::jlimit(0.0f, 1.0f, gr / 24.0f) * inner.getHeight();
            g.setColour(c.meterHot);
            g.fillRect(inner.withHeight(h));
            text = gr >= 0.05f ? "-" + juce::String(gr, gr >= 10.0f ? 0 : 1) : juce::String("0.0");
            name = "GR";
        }
        else
        {
            const float db = kind == Kind::input ? card_.inputDb() : card_.outputDb();
            const float f = juce::jlimit(0.0f, 1.0f, (db - theme.meter.floorDb) / -theme.meter.floorDb);
            const auto fill = inner.withTop(inner.getBottom() - f * inner.getHeight());
            g.setColour(theme.meter.colourAt(db));
            g.fillRect(fill);
            text = db > -99.0f ? juce::String(db, db <= -10.0f ? 0 : 1)
                               : juce::String::fromUTF8("-\xe2\x88\x9e");
            name = kind == Kind::input ? "IN" : "OUT";
        }
        drawWellFrame(g, theme, bar);
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.medium));
        g.setColour(c.textSecondary);
        g.drawText(text, value, juce::Justification::centred);
        g.setFont(theme.caption());
        g.setColour(c.textTertiary);
        g.drawText(name, caption, juce::Justification::centred);
    }
}

} // namespace aural::ui::strip
