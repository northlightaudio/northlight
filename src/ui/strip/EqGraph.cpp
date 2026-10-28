#include "ui/strip/EqGraph.h"

#include "engine/Gain.h"
#include "ui/components/Accessibility.h"
#include "ui/strip/StripStyle.h"

#include <cmath>

namespace northlight::ui::strip
{

namespace eq = plugins::strip::eq;

namespace
{
// The analyser's switch is remembered for every EQ (and every rebuild of the
// card) for the session.
bool& analyserPreference()
{
    static bool on = true;
    return on;
}

constexpr float analyserTopDb = TapSpectrum::topDb, analyserBottomDb = TapSpectrum::bottomDb;
} // namespace

EqBands stripEqBands()
{
    EqBands b;
    b.count = eq::bandCount;
    b.selected = eq::band2;
    b.output = eq::output;
    b.index = [](int band, EqBands::Field field) { return eq::paramIndex(band, field); };
    b.isCut = [](int band) { return eq::isCut(band); };
    b.colour = [](const Theme& theme, int band) { return bandColour(theme, band); };
    b.name = [](int band)
    {
        return band == eq::lowCut    ? juce::String("Low cut")
               : band == eq::highCut ? juce::String("High cut")
                                     : "Band " + juce::String(band);
    };
    b.label = [](int band)
    {
        return band == eq::lowCut    ? juce::String("L")
               : band == eq::highCut ? juce::String("H")
                                     : juce::String(band);
    };
    b.design = [](const float* params, double rate, plugins::Biquad* out, int band)
    { eq::design(params, rate, out, band); };
    return b;
}

EqGraph::EqGraph(ModuleCard& card, EqBands bands)
    : card_(card), bands_(std::move(bands)), selected_(bands_.selected), analyser_(analyserPreference()),
      analyserToggle_(card.theme(), "Analyser", card.colour())
{
    setOpaque(false);
    if (!bands_.isOn)
        bands_.isOn = [this](const ModuleCard& c, int band) { return c.param(index(band, eq::on)) >= 0.5f; };
    if (!bands_.setOn)
        bands_.setOn = [this](ModuleCard& c, int band, bool on)
        { c.setParam(index(band, eq::on), on ? 1.0f : 0.0f); };
    bandDb_.resize(static_cast<size_t>(bands_.count));
    bandKeys_.resize(static_cast<size_t>(bands_.count));
    showToggle(analyserToggle_, analyser_);
    analyserToggle_.setTooltip("Analyser");
    analyserToggle_.onClick = [this] { setAnalyser(analyserToggle_.getToggleState()); };
    addAndMakeVisible(analyserToggle_);
}

EqGraph::~EqGraph()
{
    if (auto* fb = card_.liveFeedback())
        fb->setTapsEnabled(false);
}

void EqGraph::setSelectedBand(int band)
{
    if (band == selected_)
        return;
    selected_ = band;
    repaint();
}

void EqGraph::setAnalyser(bool on)
{
    analyser_ = on;
    analyserPreference() = on;
    showToggle(analyserToggle_, on);
    if (auto* fb = card_.liveFeedback())
        fb->setTapsEnabled(on);
    spectrum_.clear();
    repaint();
}

juce::Rectangle<float> EqGraph::plot() const
{
    // Room at the bottom for the frequency labels.
    return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(14.0f);
}

void EqGraph::resized()
{
    analyserToggle_.setBounds(getLocalBounds().removeFromTop(22).removeFromRight(72).reduced(3));
    curveValid_ = false;
    spectrum_.clear();
}

bool EqGraph::bandOn(int band) const
{
    return bands_.isOn(card_, band);
}

juce::Point<float> EqGraph::nodePosition(int band) const
{
    const auto p = plot();
    const float x = hzToX(std::max<double>(bands_.lowestNodeHz, card_.param(index(band, eq::frequency))), p);
    float db = 0.0f;
    if (!bands_.isCut(band) && index(band, eq::gain) >= 0)
    {
        const int typeIndex = index(band, eq::type);
        const int shape = typeIndex >= 0 ? static_cast<int>(std::lround(card_.param(typeIndex))) : eq::peak;
        if (shape != eq::notch)
            db = card_.param(index(band, eq::gain));
    }
    return {x, dbToY(db, rangeDb, -rangeDb, p)};
}

int EqGraph::bandAt(juce::Point<float> pt) const
{
    // The selected node wins a tie, then the nearest within reach.
    int best = -1;
    float bestDistance = 11.0f;
    for (int b = 0; b < bands_.count; ++b)
    {
        const float d = nodePosition(b).getDistanceFrom(pt) - (b == selected_ ? 2.0f : 0.0f);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = b;
        }
    }
    return best;
}

void EqGraph::rebuildCurve()
{
    // Each band's curve is recomputed only when that band's settings change (a
    // drag moves one band), and the overall response is their sum in dB: the
    // bands' sections multiply.
    const auto p = plot();
    const int columns = std::max(1, static_cast<int>(p.getWidth()));
    const double rate = card_.sampleRate();
    const float* params = card_.params();
    const bool layout = !curveValid_ || rate != lastRate_ || static_cast<int>(curveDb_.size()) != columns;
    const float output = bands_.output >= 0 ? params[bands_.output] : 0.0f;
    bool changed = layout || output != lastOutput_;
    for (int band = 0; band < bands_.count; ++band)
    {
        std::array<float, 5> key{};
        for (int f = 0; f < 5; ++f)
            if (const int i = index(band, static_cast<eq::Field>(f)); i >= 0)
                key[static_cast<size_t>(f)] = params[i];
        auto& curve = bandDb_[static_cast<size_t>(band)];
        if (!layout && key == bandKeys_[static_cast<size_t>(band)])
            continue;
        bandKeys_[static_cast<size_t>(band)] = key;
        changed = true;
        plugins::Biquad sections[EqBands::maxSections];
        bands_.design(params, rate, sections, band);
        curve.assign(static_cast<size_t>(columns), 0.0f);
        for (int col = 0; col < columns; ++col)
        {
            const double hz = std::min(xToHz(p.getX() + static_cast<float>(col) + 0.5f, p), rate * 0.499);
            double m = 1.0;
            for (const auto& section : sections)
                if (!section.isIdentity())
                    m *= section.magnitude(hz, rate);
            curve[static_cast<size_t>(col)] =
                static_cast<float>(engine::gainToDb(m, engine::magnitudeFloorDb));
        }
    }
    curveValid_ = true;
    lastRate_ = rate;
    lastOutput_ = output;
    if (!changed)
        return;
    curveDb_.assign(static_cast<size_t>(columns), output);
    for (const auto& curve : bandDb_)
        for (int col = 0; col < columns; ++col)
            curveDb_[static_cast<size_t>(col)] += curve[static_cast<size_t>(col)];
}

void EqGraph::update(double elapsedMs)
{
    auto* fb = card_.liveFeedback();
    if (fb != nullptr && fb->tapsEnabled() != analyser_)
        fb->setTapsEnabled(analyser_);
    if (analyser_ && fb != nullptr)
    {
        sinceAnalysisMs_ += elapsedMs;
        if (sinceAnalysisMs_ >= 30.0)
        {
            spectrum_.analyse(*fb, 1, plot(), sinceAnalysisMs_);
            spectrum_.analyse(*fb, 0, plot(), sinceAnalysisMs_);
            sinceAnalysisMs_ = 0.0;
            repaint();
        }
    }
    if (std::abs(card_.sampleRate() - lastRate_) > 0.5)
        invalidateCurve();
}

void EqGraph::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    drawWell(g, theme, getLocalBounds().toFloat());
    const auto p = plot();
    rebuildCurve();
    const int columns = static_cast<int>(curveDb_.size());
    const auto colX = [&](int col) { return p.getX() + static_cast<float>(col) + 0.5f; };
    const auto yDb = [&](float db) { return dbToY(db, rangeDb, -rangeDb, p); };

    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(getLocalBounds().reduced(1));
        // Frequency grid and labels (the dB labels share their font).
        drawFrequencyGrid(g, theme, p);
        for (float db = -18.0f; db <= 18.0f; db += 6.0f)
        {
            const float y = yDb(db);
            g.setColour(db == 0.0f ? c.lineStrong : c.gridBeat);
            g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
            if (db != 0.0f && std::fmod(std::abs(db), 12.0f) == 0.0f)
            {
                g.setColour(c.textTertiary);
                g.drawText((db > 0 ? "+" : "") + juce::String(static_cast<int>(db)),
                           juce::Rectangle<float>(p.getX() + 3.0f, y - 6.0f, 24.0f, 12.0f),
                           juce::Justification::centredLeft);
            }
        }

        g.reduceClipRegion(p.toNearestInt());
        // The analyser behind everything.
        if (analyser_)
        {
            const auto yA = [&](float db) { return dbToY(db, analyserTopDb, analyserBottomDb, p); };
            const auto& after = spectrum_.spectrum(1);
            if (static_cast<int>(after.size()) == columns && columns > 1)
            {
                juce::Path fill;
                fill.startNewSubPath(p.getX(), p.getBottom());
                for (int col = 0; col < columns; ++col)
                {
                    // A light three-column average smooths the outline.
                    const float v = (after[static_cast<size_t>(std::max(0, col - 1))] +
                                     after[static_cast<size_t>(col)] * 2.0f +
                                     after[static_cast<size_t>(std::min(columns - 1, col + 1))]) *
                                    0.25f;
                    fill.lineTo(colX(col), yA(v));
                }
                fill.lineTo(p.getRight(), p.getBottom());
                fill.closeSubPath();
                g.setGradientFill(juce::ColourGradient(card_.colour().withAlpha(0.32f),
                                                       0.0f,
                                                       p.getY(),
                                                       card_.colour().withAlpha(0.06f),
                                                       0.0f,
                                                       p.getBottom(),
                                                       false));
                g.fillPath(fill);
            }
            const auto& before = spectrum_.spectrum(0);
            if (static_cast<int>(before.size()) == columns && columns > 1)
            {
                juce::Path line;
                for (int col = 0; col < columns; ++col)
                {
                    const float v = (before[static_cast<size_t>(std::max(0, col - 1))] +
                                     before[static_cast<size_t>(col)] * 2.0f +
                                     before[static_cast<size_t>(std::min(columns - 1, col + 1))]) *
                                    0.25f;
                    if (col == 0)
                        line.startNewSubPath(colX(col), yA(v));
                    else
                        line.lineTo(colX(col), yA(v));
                }
                g.setColour(c.textSecondary.withAlpha(0.45f));
                g.strokePath(line, juce::PathStrokeType(1.0f));
            }
        }

        // The selected (or hovered) band's own curve, filled to 0 dB.
        const int focus = dragging_ >= 0 ? dragging_ : hovered_ >= 0 ? hovered_ : selected_;
        if (focus >= 0 && bandOn(focus) && columns > 1)
        {
            const auto& band = bandDb_[static_cast<size_t>(focus)];
            juce::Path area;
            area.startNewSubPath(colX(0), yDb(0.0f));
            for (int col = 0; col < columns; ++col)
                area.lineTo(colX(col), yDb(band[static_cast<size_t>(col)]));
            area.lineTo(colX(columns - 1), yDb(0.0f));
            area.closeSubPath();
            g.setColour(bands_.colour(theme, focus).withAlpha(0.16f));
            g.fillPath(area);
        }

        // The overall response.
        if (columns > 1)
        {
            juce::Path curve;
            for (int col = 0; col < columns; ++col)
            {
                const float y = yDb(curveDb_[static_cast<size_t>(col)]);
                if (col == 0)
                    curve.startNewSubPath(colX(col), y);
                else
                    curve.lineTo(colX(col), y);
            }
            g.setColour(card_.isOn() ? c.textPrimary : c.textSecondary);
            g.strokePath(
                curve,
                juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    drawWellFrame(g, theme, getLocalBounds().toFloat());

    // The nodes, the selected one on top.
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < bands_.count; ++b)
        {
            if ((b == selected_) != (pass == 1))
                continue;
            const auto at = nodePosition(b);
            const bool on = bandOn(b);
            const auto colour = bands_.colour(theme, b);
            const float radius = b == selected_ ? 8.0f : 7.0f;
            const auto r = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(at);
            if (b == hovered_ || b == dragging_)
            {
                g.setColour(colour.withAlpha(0.25f));
                g.fillEllipse(r.expanded(5.0f));
            }
            g.setColour(on ? colour : c.surface2);
            g.fillEllipse(r);
            g.setColour(on ? colour.brighter(0.3f) : colour.withAlpha(0.7f));
            g.drawEllipse(r, b == selected_ ? 2.0f : 1.0f);
            if (b == selected_)
            {
                g.setColour(c.textPrimary);
                g.drawEllipse(r.expanded(2.5f), 1.0f);
            }
            g.setColour(on ? c.textOnColour : colour);
            // The band's letters, fitted to the node (two letters, HP, fill it).
            g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.micro, theme.fonts.weight.semibold));
            g.drawFittedText(
                bands_.label(b), r.expanded(2.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

    // A readout for the band under the pointer (or being dragged).
    const int readout = dragging_ >= 0 ? dragging_ : hovered_;
    if (readout >= 0)
    {
        juce::String text = hzText(card_.param(index(readout, eq::frequency)));
        const int typeIndex = index(readout, eq::type);
        if (bands_.isCut(readout))
        {
            if (typeIndex >= 0)
                text << "  "
                     << eq::slopes[static_cast<size_t>(
                            juce::jlimit(0, 5, static_cast<int>(std::lround(card_.param(typeIndex)))))]
                     << " dB/oct";
        }
        else
        {
            const int shape =
                typeIndex >= 0 ? static_cast<int>(std::lround(card_.param(typeIndex))) : eq::peak;
            if (shape != eq::notch && index(readout, eq::gain) >= 0)
                text << "  " << dbText(card_.param(index(readout, eq::gain)));
            if (index(readout, eq::q) >= 0)
                text << "  Q " << juce::String(card_.param(index(readout, eq::q)), 2);
        }
        g.setFont(theme.value());
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 12;
        const auto at = nodePosition(readout);
        auto box = juce::Rectangle<float>(static_cast<float>(w), 18.0f)
                       .withCentre({at.x, at.y - 22.0f})
                       .constrainedWithin(p.reduced(2.0f));
        if (box.contains(at))
            box.setY(at.y + 14.0f);
        g.setColour(c.surface3.withAlpha(0.95f));
        g.fillRoundedRectangle(box, theme.radius.sm);
        g.setColour(c.textPrimary);
        g.drawText(text, box, juce::Justification::centred);
    }
    else if (hoverPoint_.x >= 0.0f && p.contains(hoverPoint_))
    {
        // The frequency and level under the pointer.
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption, theme.fonts.weight.regular));
        g.setColour(c.textTertiary);
        g.drawText(hzText(xToHz(hoverPoint_.x, p)) + "  " +
                       dbText(yToDb(hoverPoint_.y, rangeDb, -rangeDb, p)),
                   p.reduced(6.0f, 4.0f).removeFromTop(14.0f),
                   juce::Justification::centredLeft);
    }
}

void EqGraph::mouseMove(const juce::MouseEvent& e)
{
    const int band = bandAt(e.position);
    hoverPoint_ = e.position;
    if (band != hovered_)
        hovered_ = band;
    setMouseCursor(band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void EqGraph::mouseExit(const juce::MouseEvent&)
{
    hovered_ = -1;
    hoverPoint_ = {-1.0f, -1.0f};
    repaint();
}

void EqGraph::mouseDown(const juce::MouseEvent& e)
{
    const int band = bandAt(e.position);
    if (band < 0)
        return;
    if (band != selected_)
    {
        selected_ = band;
        if (onSelect)
            onSelect(band);
    }
    if (e.mods.isPopupMenu())
    {
        showBandMenu(band);
        return;
    }
    dragging_ = band;
    card_.beginGesture(bands_.name(band));
    // Dragging a band that is off switches it on.
    if (!bandOn(band))
        bands_.setOn(card_, band, true);
    repaint();
}

void EqGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ < 0)
        return;
    const auto p = plot();
    // Shift is fine control: a quarter of the pointer's movement.
    auto pos = e.position;
    if (e.mods.isShiftDown())
    {
        const auto start = e.mouseDownPosition;
        pos = start + (pos - start) * 0.25f;
    }
    const int fIndex = index(dragging_, eq::frequency);
    if (!e.mods.isAltDown()) // Alt: gain only
        card_.setParam(fIndex, static_cast<float>(xToHz(pos.x, p)));
    const int gIndex = index(dragging_, eq::gain);
    if (gIndex >= 0)
    {
        const int typeIndex = index(dragging_, eq::type);
        const int shape = typeIndex >= 0 ? static_cast<int>(std::lround(card_.param(typeIndex))) : eq::peak;
        if (shape != eq::notch)
            card_.setParam(gIndex, yToDb(pos.y, rangeDb, -rangeDb, p));
    }
    hoverPoint_ = e.position;
    repaint();
}

void EqGraph::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ >= 0)
        card_.endGesture();
    dragging_ = -1;
    repaint();
}

void EqGraph::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int band = bandAt(e.position);
    if (band < 0)
        return;
    card_.beginGesture("Switch band");
    bands_.setOn(card_, band, !bandOn(band));
    card_.endGesture();
}

void EqGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = hovered_ >= 0 ? hovered_ : bandAt(e.position) >= 0 ? bandAt(e.position) : selected_;
    if (band < 0 || wheel.deltaY == 0.0f)
        return;
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
    const int typeIndex = index(band, eq::type), qIndex = index(band, eq::q);
    if (bands_.isCut(band) ? typeIndex < 0 : qIndex < 0)
        return; // nothing for the wheel on this band
    card_.beginGesture("Q");
    if (bands_.isCut(band))
        card_.setParam(typeIndex, juce::jlimit(0.0f, 5.0f, std::round(card_.param(typeIndex)) + direction));
    else
    {
        const float step = e.mods.isShiftDown() ? 1.02f : 1.12f;
        card_.setParam(qIndex, card_.param(qIndex) * (direction > 0.0f ? step : 1.0f / step));
    }
    card_.endGesture();
    repaint();
}

void EqGraph::showBandMenu(int band)
{
    juce::PopupMenu menu;
    const int typeIndex = index(band, eq::type);
    if (typeIndex >= 0)
    {
        const auto& typeInfo = card_.info(typeIndex);
        const int current = static_cast<int>(std::lround(card_.param(typeIndex)));
        menu.addSectionHeader(bands_.isCut(band) ? "Slope" : "Shape");
        for (size_t i = 0; i < typeInfo.choices.size(); ++i)
            menu.addItem(static_cast<int>(i) + 1,
                         bands_.isCut(band) ? juce::String(typeInfo.choices[i]) + "/oct"
                                            : juce::String(typeInfo.choices[i]),
                         true,
                         static_cast<int>(i) == current);
        menu.addSeparator();
    }
    menu.addItem(100, bandOn(band) ? "Switch band off" : "Switch band on");
    menu.addItem(101, "Reset band");
    juce::Component::SafePointer<EqGraph> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(
                           juce::Rectangle<int>(1, 1).withPosition(nodePosition(band).toInt()))),
                       [safe, band](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           auto& card = safe->card_;
                           card.beginGesture("Band");
                           if (choice == 100)
                               safe->bands_.setOn(card, band, !safe->bandOn(band));
                           else if (choice == 101)
                           {
                               for (const auto field : {eq::type, eq::frequency, eq::gain, eq::q})
                                   if (const int i = safe->index(band, field); i >= 0)
                                       card.setParam(i, card.info(i).defaultValue);
                           }
                           else
                               card.setParam(safe->index(band, eq::type), static_cast<float>(choice - 1));
                           card.endGesture();
                           safe->repaint();
                       });
}

} // namespace northlight::ui::strip
