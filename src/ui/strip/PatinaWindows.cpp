#include "ui/strip/PatinaWindows.h"

#include "ui/Number.h"
#include "ui/components/HardwareParts.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace northlight::ui::strip::patinaface
{

namespace pa = plugins::patina;
using hardware::drawFocusRing;
using hardware::legendFont;
using hardware::polar;

// ----- the window, drawn --------------------------------------------------------------------------

StageWindow::StageWindow(WindowHost& card, int stage) : card_(card), stage_(stage)
{
    setWantsKeyboardFocus(true);
    setComponentID(juce::String("stage:") + stageNames[static_cast<size_t>(stage)]);
    setTooltip(stageNames[static_cast<size_t>(stage)]);
}

juce::Rectangle<float> StageWindow::well() const
{
    const auto glass = glassOf(stage_);
    return {0.0f, 0.0f, glass.getWidth(), glass.getHeight()};
}

bool StageWindow::hitTest(int x, int y)
{
    return well().contains(static_cast<float>(x), static_cast<float>(y));
}

bool StageWindow::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        card_.focusWindow(stage_ + (key == juce::KeyPress::leftKey ? -1 : 1));
    else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        card_.toggleStage(stage_);
    else
        return false;
    return true;
}

void StageWindow::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& hw = theme.hardware;
    const bool on = card_.stageOn(stage_);

    // The glass, lit from behind while the stage is on, cut to the bezel's opening.
    const auto glass = well();
    g.saveState();
    {
        juce::Path opening;
        opening.addRoundedRectangle(glass, openingRadius);
        g.reduceClipRegion(opening);
    }
    {
        juce::ColourGradient light(on ? hw.glassCentre : hw.glassOffCentre,
                                   glass.getCentreX(),
                                   glass.getY() + glass.getHeight() * 0.6f,
                                   on ? hw.glassEdge : hw.glassOffEdge,
                                   glass.getX(),
                                   glass.getY(),
                                   true);
        if (on)
            light.addColour(0.55, hw.glassMid);
        g.setGradientFill(light);
        g.fillRect(glass);
    }
    const auto ink = on ? hw.glassInk : hw.glassMid;
    inks_ = {ink,
             ink,
             ink.withAlpha(0.55f),
             ink.withAlpha(0.5f),
             ink.withAlpha(0.16f),
             on ? hw.glassCentre : hw.glassOffCentre};
    // Switched off, the window goes dark: no readout, no grid, no figure.
    if (on)
    {
        juce::Graphics::ScopedSaveState save(g);
        const auto p = glass.reduced(1.0f);
        g.reduceClipRegion(p.toNearestInt());
        switch (stage_)
        {
            case pa::heat:
                paintHeat(g, p);
                break;
            case pa::chamber:
                paintChamber(g, p);
                break;
            case pa::drift:
                paintDrift(g, p);
                break;
            case pa::grain:
                paintGrain(g, p);
                break;
            case pa::wear:
                paintWear(g, p);
                break;
            default:
                paintCrush(g, p);
                break;
        }
    }
    // The shade under the bezel's lip and the glass's reflection.
    g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.45f),
                                           0.0f,
                                           glass.getY(),
                                           hw.shadow.withAlpha(0.0f),
                                           0.0f,
                                           glass.getY() + 6.0f,
                                           false));
    g.fillRect(glass.withHeight(6.0f));
    g.setGradientFill(juce::ColourGradient(hw.highlight.withAlpha(on ? 0.22f : 0.1f),
                                           glass.getX(),
                                           glass.getY(),
                                           hw.highlight.withAlpha(0.0f),
                                           glass.getX() + glass.getWidth() * 0.12f,
                                           glass.getY() + glass.getHeight() * 0.38f,
                                           false));
    g.fillRect(glass);
    g.restoreState();
    paintLamps(g);
    drawFocusRing(g, *this, glass.expanded(3.0f), openingRadius + 3.0f);
}

void StageWindow::paintLamps(juce::Graphics& g)
{
    // The amount Age and Breath leave, live; dim up to where Age alone puts it.
    const auto& hw = card_.theme().hardware;
    const bool on = card_.stageOn(stage_);
    const float live =
        on ? std::clamp(card_.newest()[static_cast<size_t>(Telemetry::amountHeat + stage_)], 0.0f, 1.0f)
           : 0.0f;
    const float set = on ? std::clamp(card_.setAmount(stage_), 0.0f, 1.0f) : 0.0f;
    const int lit = juce::roundToInt(live * static_cast<float>(ledCount));
    const int dim = juce::roundToInt(set * static_cast<float>(ledCount));
    const float rowW = ledCount * ledWidth + (ledCount - 1) * ledGap;
    float x = static_cast<float>(getWidth()) * 0.5f - rowW * 0.5f;
    const float y = static_cast<float>(getHeight()) - ledHeight - 1.0f;
    for (int i = 0; i < ledCount; ++i, x += ledWidth + ledGap)
    {
        const auto led = juce::Rectangle<float>(x, y, ledWidth, ledHeight);
        const auto colour = i >= ledCount - 2 ? hw.ledHot : hw.led;
        if (i < lit)
        {
            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(led.expanded(1.5f), 2.5f);
            g.setColour(colour);
            g.fillRoundedRectangle(led, 1.0f);
        }
        else
        {
            g.setColour(hw.ledOff);
            g.fillRoundedRectangle(led, 1.0f);
            if (i < dim)
            {
                g.setColour(colour.withAlpha(0.4f));
                g.fillRoundedRectangle(led.reduced(1.0f), 0.5f);
            }
        }
    }
}

void StageWindow::readout(juce::Graphics& g,
                          juce::Rectangle<float> p,
                          const juce::String& text,
                          juce::Colour colour)
{
    g.setFont(card_.theme().value());
    g.setColour(colour);
    g.drawText(text,
               juce::Rectangle<float>(p.getX() + 4.0f, p.getY() + 3.0f, p.getWidth() - 8.0f, 14.0f),
               juce::Justification::centredRight);
}

void StageWindow::scaleLabel(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text)
{
    g.setFont(card_.theme().caption());
    g.setColour(inks_.textTertiary);
    g.drawText(text,
               juce::Rectangle<float>(p.getX() + 5.0f, p.getY() + 3.0f, 60.0f, 14.0f),
               juce::Justification::centredLeft);
}

namespace
{
// Per column of `columns`, the largest (or with `mean`, the average) of one
// value over the frames that fall in it: columns counted from the start of the
// history so they keep their frames as the display scrolls.
void collect(const WindowHost& card, int value, int columns, bool mean, std::vector<float>& out)
{
    out.assign(static_cast<size_t>(std::max(1, columns)), std::numeric_limits<float>::quiet_NaN());
    const auto total = card.historyTotal();
    if (total == 0)
        return;
    const auto perColumn = static_cast<std::uint64_t>(std::max(1, historyFrames / std::max(1, columns)));
    const int size = card.historyCount();
    const auto first = total - static_cast<std::uint64_t>(size);
    const auto newestBucket = (total - 1) / perColumn;
    for (int col = 0; col < columns; ++col)
    {
        const auto fromRight = static_cast<std::uint64_t>(columns - 1 - col);
        if (fromRight > newestBucket)
            continue;
        const auto bucket = newestBucket - fromRight;
        float acc = mean ? 0.0f : -1.0e30f;
        int n = 0;
        for (auto a = bucket * perColumn; a < (bucket + 1) * perColumn && a < total; ++a)
        {
            if (a < first)
                continue;
            const float v = card.frameAt(static_cast<int>(a - first))[static_cast<size_t>(value)];
            acc = mean ? acc + v : std::max(acc, v);
            ++n;
        }
        if (n > 0)
            out[static_cast<size_t>(col)] = mean ? acc / static_cast<float>(n) : acc;
    }
}

// A line through the columns that have values.
juce::Path
traceLine(const std::vector<float>& values, juce::Rectangle<float> p, const std::function<float(float)>& y)
{
    juce::Path path;
    bool started = false;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (std::isnan(values[i]))
        {
            started = false;
            continue;
        }
        const float x = p.getX() + static_cast<float>(i) + 0.5f;
        if (!started)
            path.startNewSubPath(x, y(values[i]));
        else
            path.lineTo(x, y(values[i]));
        started = true;
    }
    return path;
}

// The same, closed down to the bottom edge as an area.
juce::Path
traceArea(const std::vector<float>& values, juce::Rectangle<float> p, const std::function<float(float)>& y)
{
    juce::Path path;
    int start = -1;
    const auto close = [&](int end)
    {
        if (start < 0)
            return;
        path.lineTo(p.getX() + static_cast<float>(end) - 0.5f, p.getBottom());
        path.lineTo(p.getX() + static_cast<float>(start) + 0.5f, p.getBottom());
        path.closeSubPath();
        start = -1;
    };
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (std::isnan(values[i]))
        {
            close(static_cast<int>(i));
            continue;
        }
        const float x = p.getX() + static_cast<float>(i) + 0.5f;
        if (start < 0)
        {
            start = static_cast<int>(i);
            path.startNewSubPath(x, p.getBottom());
            path.lineTo(x, y(values[i]));
        }
        else
            path.lineTo(x, y(values[i]));
    }
    close(static_cast<int>(values.size()));
    return path;
}

const juce::PathStrokeType traceStroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
} // namespace

void StageWindow::paintHeat(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    const auto& f = card_.newest();
    const int character = static_cast<int>(std::lround(card_.param(pa::heatCharacter)));
    const float amount = card_.stageOn(pa::heat) ? f[Telemetry::amountHeat] : card_.setAmount(pa::heat);
    // Input across (-1..1), output up, in a square centred in the well.
    const auto plot = p.withTrimmedTop(readoutRoom);
    const float side = std::min(plot.getWidth(), plot.getHeight()) - 6.0f;
    const auto sq = juce::Rectangle<float>(side, side).withCentre(plot.getCentre());
    const auto toX = [&](float x) { return sq.getCentreX() + x * sq.getWidth() * 0.5f; };
    const auto toY = [&](float y)
    { return sq.getCentreY() - std::clamp(y, -1.1f, 1.1f) * sq.getHeight() * 0.5f; };
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(sq.getCentreY()), p.getX(), p.getRight());
    g.drawVerticalLine(static_cast<int>(sq.getCentreX()), p.getY(), p.getBottom());
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(c.textTertiary.withAlpha(0.5f));
    g.drawDashedLine(juce::Line<float>(toX(-1.0f), toY(-1.0f), toX(1.0f), toY(1.0f)), dashes, 2, 1.0f);
    juce::Path curve;
    constexpr int steps = 96;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = -1.0f + 2.0f * static_cast<float>(i) / steps;
        const float y = pa::heatTransfer(character, amount, x);
        if (i == 0)
            curve.startNewSubPath(toX(x), toY(y));
        else
            curve.lineTo(toX(x), toY(y));
    }
    g.setColour(hue);
    g.strokePath(curve, traceStroke);
    // The level riding the curve: the loudest of the last 50 ms.
    float level = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 10); i < card_.historyCount(); ++i)
        level = std::max(level, card_.frameAt(i)[Telemetry::heatLevel]);
    level = std::min(level, 1.0f);
    if (level > 0.001f && card_.stageOn(pa::heat))
    {
        const juce::Point<float> dot(toX(level), toY(pa::heatTransfer(character, amount, level)));
        g.setColour(c.dotRing);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(dot));
        g.setColour(hue);
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(dot));
    }
    float squash = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 40); i < card_.historyCount(); ++i)
        squash = std::max(squash, card_.frameAt(i)[Telemetry::heatSquash]);
    readout(g, p, reductionText(squash, "0.0 dB"), squash >= 0.1f ? hue : c.textTertiary);
}

void StageWindow::paintChamber(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    constexpr float top = 0.0f, bottom = -60.0f;
    for (float db = -20.0f; db > bottom; db -= 20.0f)
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(
            static_cast<int>(dbToY(db, top, bottom, p.withTrimmedTop(readoutRoom))), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::chamberWet, static_cast<int>(p.getWidth()), false, a_);
    const auto y = [&](float v) { return dbToY(toDb(v), top, bottom, p.withTrimmedTop(readoutRoom)); };
    g.setColour(hue.withAlpha(0.32f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    const int space = static_cast<int>(std::lround(card_.param(pa::chamberType)));
    const float rt = pa::chamberDecaySeconds(space, card_.param(pa::chamberDecay) * 0.01f);
    readout(g, p, juce::String(rt, 1) + " s", c.textSecondary);
}

void StageWindow::paintDrift(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    collect(card_, Telemetry::driftLeft, static_cast<int>(p.getWidth()), true, a_);
    collect(card_, Telemetry::driftRight, static_cast<int>(p.getWidth()), true, b_);
    float peak = 0.0f;
    for (size_t i = 0; i < a_.size(); ++i)
        for (const float v : {a_[i], b_[i]})
            if (!std::isnan(v))
                peak = std::max(peak, std::abs(v));
    // The scale grows to fit what plays: +-5, 10, 20 or 40 cents.
    float range = 5.0f;
    while (range < 40.0f && peak > range * 0.95f)
        range *= 2.0f;
    const auto mid = p.getCentreY() + 6.0f;
    const float half = std::min(mid - p.getY() - 18.0f, p.getBottom() - mid - 6.0f);
    const auto y = [&](float cents) { return mid - std::clamp(cents / range, -1.0f, 1.0f) * half; };
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(mid), p.getX(), p.getRight());
    const float dashes[] = {2.0f, 3.0f};
    for (const float edge : {mid - half, mid + half})
        g.drawDashedLine(juce::Line<float>(p.getX(), edge, p.getRight(), edge), dashes, 2, 1.0f);
    if (card_.param(pa::driftStereo) > 0.5f)
    {
        g.setColour(c.textSecondary.withAlpha(0.7f));
        g.strokePath(traceLine(b_, p, y), juce::PathStrokeType(1.0f));
    }
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    scaleLabel(g, p, juce::String::fromUTF8("\xc2\xb1") + juce::String(static_cast<int>(range)) + " c");
    // The widest swing of the last second.
    float recent = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 200); i < card_.historyCount(); ++i)
    {
        const auto& f = card_.frameAt(i);
        recent = std::max({recent, std::abs(f[Telemetry::driftLeft]), std::abs(f[Telemetry::driftRight])});
    }
    readout(g,
            p,
            juce::String::fromUTF8("\xc2\xb1") + juce::String(recent, 1) + " c",
            recent >= 0.1f ? hue : c.textTertiary);
}

void StageWindow::paintGrain(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    constexpr float top = 0.0f, bottom = -84.0f;
    for (float db = -24.0f; db > bottom; db -= 24.0f)
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(
            static_cast<int>(dbToY(db, top, bottom, p.withTrimmedTop(readoutRoom))), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::grainNoise, static_cast<int>(p.getWidth()), false, a_);
    collect(card_, Telemetry::grainSignal, static_cast<int>(p.getWidth()), false, b_);
    const auto y = [&](float v) { return dbToY(toDb(v), top, bottom, p.withTrimmedTop(readoutRoom)); };
    g.setColour(hue.withAlpha(0.36f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), juce::PathStrokeType(1.0f));
    g.setColour(c.textSecondary.withAlpha(0.8f));
    g.strokePath(traceLine(b_, p, y), traceStroke);
    float noise = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 50); i < card_.historyCount(); ++i)
        noise = std::max(noise, card_.frameAt(i)[Telemetry::grainNoise]);
    readout(g,
            p,
            noise > 1.0e-6f ? dbText(toDb(noise), 0) : juce::String::fromUTF8("\xe2\x80\x93"),
            noise > 1.0e-6f ? hue : c.textTertiary);
}

void StageWindow::paintWear(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    // Frequency up a log axis, 1 kHz at the bottom to 20 kHz at the top.
    const auto y = [&](float hz)
    {
        const float f = std::log(std::clamp(hz, 1000.0f, 20000.0f) / 1000.0f) / std::log(20.0f);
        return p.getBottom() - f * (p.getHeight() - 18.0f);
    };
    for (const float hz : {2000.0f, 5000.0f, 10000.0f})
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(static_cast<int>(y(hz)), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::wearCutoff, static_cast<int>(p.getWidth()), false, a_);
    // Dropouts are brief: take each column's lowest, not its highest.
    {
        std::vector<float> lowest;
        collect(card_, Telemetry::wearCutoff, static_cast<int>(p.getWidth()), true, lowest);
        for (size_t i = 0; i < a_.size() && i < lowest.size(); ++i)
            if (!std::isnan(lowest[i]))
                a_[i] = std::min(a_[i], lowest[i]);
    }
    g.setColour(hue.withAlpha(0.26f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    scaleLabel(g, p, "20k");
    const auto& f = card_.newest();
    const float cutoff =
        card_.historyCount() > 0 ? f[Telemetry::wearCutoff] : pa::wearCutoffHz(card_.setAmount(pa::wear));
    float dip = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 60); i < card_.historyCount(); ++i)
        dip = std::max(dip, card_.frameAt(i)[Telemetry::wearDipDb]);
    readout(g,
            p,
            dip >= 1.0f ? reductionText(dip, "0.0 dB") : compactHzText(cutoff),
            dip >= 1.0f ? hue : c.textSecondary);
}

void StageWindow::paintCrush(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    const auto& f = card_.newest();
    const bool live = card_.historyCount() > 0 && card_.stageOn(pa::crush);
    const float bits = live ? f[Telemetry::crushBits] : pa::crushBitsFor(card_.setAmount(pa::crush));
    const float hz = live ? f[Telemetry::crushRate] : card_.param(pa::crushRate);
    const auto mid = p.getCentreY() + 7.0f;
    const float half = std::min(mid - p.getY() - 18.0f, p.getBottom() - mid - 4.0f);
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(mid), p.getX(), p.getRight());
    // A window of about six of the converter's samples, from a rising zero
    // crossing so the trace holds still.
    const auto* t = card_.telemetry();
    if (t != nullptr && live)
    {
        const double fs = t->sampleRate();
        const int window = std::clamp(static_cast<int>(6.0 * fs / std::max(1.0f, hz)), 96, 512);
        const int span = window * 3;
        scopeIn_.resize(static_cast<size_t>(span));
        scopeOut_.resize(static_cast<size_t>(span));
        if (t->readScope(scopeIn_.data(), scopeOut_.data(), span))
        {
            int start = span - window;
            for (int i = span - window - 1; i > 0; --i)
                if (scopeIn_[static_cast<size_t>(i - 1)] < 0.0f && scopeIn_[static_cast<size_t>(i)] >= 0.0f)
                {
                    start = i;
                    break;
                }
            float peak = 1.0e-3f;
            for (int i = start; i < start + window; ++i)
                peak = std::max(peak, std::abs(scopeIn_[static_cast<size_t>(i)]));
            const float scale = 1.0f / std::max(0.05f, peak);
            const auto trace = [&](const std::vector<float>& v, bool steps)
            {
                juce::Path path;
                for (int i = 0; i < window; ++i)
                {
                    const float x =
                        p.getX() + p.getWidth() * static_cast<float>(i) / static_cast<float>(window - 1);
                    const float yy =
                        mid - std::clamp(v[static_cast<size_t>(start + i)] * scale, -1.0f, 1.0f) * half;
                    if (i == 0)
                        path.startNewSubPath(x, yy);
                    else
                    {
                        if (steps)
                            path.lineTo(x, path.getCurrentPosition().y);
                        path.lineTo(x, yy);
                    }
                }
                return path;
            };
            g.setColour(c.textSecondary.withAlpha(0.6f));
            g.strokePath(trace(scopeIn_, false), juce::PathStrokeType(1.0f));
            g.setColour(hue);
            g.strokePath(trace(scopeOut_, true), juce::PathStrokeType(1.5f));
        }
    }
    const auto text = juce::String(static_cast<int>(std::lround(bits))) + " bit " +
                      juce::String::fromUTF8("\xc2\xb7 ") + compactHzText(hz);
    readout(g, p, text, live ? c.textSecondary : c.textTertiary);
}

} // namespace northlight::ui::strip::patinaface
