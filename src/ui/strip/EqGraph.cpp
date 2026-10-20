#include "ui/strip/EqGraph.h"

#include "ui/strip/StripStyle.h"

#include <cmath>

namespace aural::ui::strip
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

constexpr float analyserTopDb = 0.0f, analyserBottomDb = -96.0f;
// Tilt so a typical mix (falling about 3 dB an octave) reads flat-ish.
constexpr double tiltDbPerOctave = 3.0;
} // namespace

EqGraph::EqGraph(ModuleCard& card)
    : card_(card), analyser_(analyserPreference()), analyserToggle_(card.theme(), "Analyser", card.colour())
{
    setOpaque(false);
    window_.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi *
                                                                 static_cast<float>(i) / (fftSize - 1));
    fftData_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
    samples_.assign(static_cast<size_t>(fftSize), 0.0f);
    analyserToggle_.setToggleState(analyser_, juce::dontSendNotification);
    analyserToggle_.setTooltip("Spectrum analyser: the signal after the EQ (filled) and before it (line)");
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
    analyserToggle_.setToggleState(on, juce::dontSendNotification);
    if (auto* fb = card_.liveFeedback())
        fb->setTapsEnabled(on);
    for (auto& s : spectrum_)
        s.clear();
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
    for (auto& s : spectrum_)
        s.clear();
}

bool EqGraph::bandOn(int band) const
{
    return card_.param(eq::paramIndex(band, eq::on)) >= 0.5f;
}

juce::Point<float> EqGraph::nodePosition(int band) const
{
    const auto p = plot();
    const float x = hzToX(card_.param(eq::paramIndex(band, eq::frequency)), p);
    float db = 0.0f;
    if (!eq::isCut(band))
    {
        const int shape = static_cast<int>(std::lround(card_.param(eq::paramIndex(band, eq::type))));
        if (shape != eq::notch)
            db = card_.param(eq::paramIndex(band, eq::gain));
    }
    return {x, dbToY(db, rangeDb, -rangeDb, p)};
}

int EqGraph::bandAt(juce::Point<float> pt) const
{
    // The selected node wins a tie, then the nearest within reach.
    int best = -1;
    float bestDistance = 11.0f;
    for (int b = 0; b < eq::bandCount; ++b)
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
    const auto p = plot();
    const int columns = std::max(1, static_cast<int>(p.getWidth()));
    const double rate = card_.sampleRate();
    std::vector<float> params(card_.params(), card_.params() + eq::count);
    if (curveValid_ && params == lastParams_ && rate == lastRate_ &&
        static_cast<int>(curveDb_.size()) == columns)
        return;
    lastParams_ = params;
    lastRate_ = rate;
    curveValid_ = true;
    plugins::Biquad all[eq::maxSections];
    eq::design(params.data(), rate, all);
    curveDb_.assign(static_cast<size_t>(columns), 0.0f);
    for (int b = 0; b < eq::bandCount; ++b)
        bandDb_[static_cast<size_t>(b)].assign(static_cast<size_t>(columns), 0.0f);
    std::array<std::array<plugins::Biquad, eq::maxSections>, eq::bandCount> perBand{};
    for (int b = 0; b < eq::bandCount; ++b)
        eq::design(params.data(), rate, perBand[static_cast<size_t>(b)].data(), b);
    for (int col = 0; col < columns; ++col)
    {
        const double hz = xToHz(p.getX() + static_cast<float>(col) + 0.5f, p);
        if (hz >= rate * 0.5)
        {
            curveDb_[static_cast<size_t>(col)] = curveDb_[static_cast<size_t>(std::max(0, col - 1))];
            continue;
        }
        double magnitude = 1.0;
        for (const auto& s : all)
            if (!s.isIdentity())
                magnitude *= s.magnitude(hz, rate);
        curveDb_[static_cast<size_t>(col)] =
            static_cast<float>(20.0 * std::log10(std::max(1.0e-9, magnitude))) + params[eq::output];
        for (int b = 0; b < eq::bandCount; ++b)
        {
            double m = 1.0;
            for (const auto& s : perBand[static_cast<size_t>(b)])
                if (!s.isIdentity())
                    m *= s.magnitude(hz, rate);
            bandDb_[static_cast<size_t>(b)][static_cast<size_t>(col)] =
                static_cast<float>(20.0 * std::log10(std::max(1.0e-9, m)));
        }
    }
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
            analyse(1, spectrum_[1], sinceAnalysisMs_);
            analyse(0, spectrum_[0], sinceAnalysisMs_);
            sinceAnalysisMs_ = 0.0;
            repaint();
        }
    }
    if (std::abs(card_.sampleRate() - lastRate_) > 0.5)
        invalidateCurve();
}

void EqGraph::analyse(int tap, std::vector<float>& smoothed, double elapsedMs)
{
    auto* fb = card_.liveFeedback();
    const auto p = plot();
    const int columns = std::max(1, static_cast<int>(p.getWidth()));
    if (static_cast<int>(smoothed.size()) != columns)
        smoothed.assign(static_cast<size_t>(columns), analyserBottomDb);
    const float fall = static_cast<float>(elapsedMs * 0.001 * 40.0); // dB per second
    const auto position = fb->tapPosition(tap);
    const bool fresh = position != lastTap_[static_cast<size_t>(tap)];
    lastTap_[static_cast<size_t>(tap)] = position;
    if (!fresh || !fb->readTap(tap, samples_.data(), fftSize))
    {
        // Nothing new (the device stopped): let it fall away.
        for (auto& v : smoothed)
            v = std::max(analyserBottomDb, v - fall);
        return;
    }
    std::fill(fftData_.begin(), fftData_.end(), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        fftData_[static_cast<size_t>(i)] = samples_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)];
    fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
    const double rate = fb->sampleRate();
    const double binHz = rate / fftSize;
    const double reference = fftSize / 4.0; // a full-scale sine through the Hann window
    const auto binDb = [&](int bin)
    {
        const double m = fftData_[static_cast<size_t>(juce::jlimit(0, fftSize / 2, bin))] / reference;
        return 20.0 * std::log10(std::max(1.0e-9, m));
    };
    for (int col = 0; col < columns; ++col)
    {
        const double f0 = xToHz(p.getX() + static_cast<float>(col), p);
        const double f1 = xToHz(p.getX() + static_cast<float>(col) + 1.0f, p);
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        double db;
        if (b1 - b0 < 1.0)
        {
            // Narrower than a bin: interpolate between the neighbours.
            const double centre = 0.5 * (b0 + b1);
            const int lo = static_cast<int>(std::floor(centre));
            const double t = centre - lo;
            db = binDb(lo) * (1.0 - t) + binDb(lo + 1) * t;
        }
        else
        {
            db = -200.0;
            for (int bin = static_cast<int>(std::ceil(b0)); bin <= static_cast<int>(b1); ++bin)
                db = std::max(db, binDb(bin));
        }
        const double hz = 0.5 * (f0 + f1);
        db += tiltDbPerOctave * std::log2(hz / 1000.0);
        const float v = static_cast<float>(juce::jlimit<double>(analyserBottomDb, analyserTopDb + 12.0, db));
        auto& s = smoothed[static_cast<size_t>(col)];
        s = v > s ? s + (v - s) * 0.6f : std::max(v, s - fall);
    }
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
        // Frequency grid and labels.
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        for (const double hz :
             {30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0, 20000.0})
        {
            const float x = hzToX(hz, p);
            const bool major = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
            g.setColour(major ? c.gridBar : c.gridBeat);
            g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
            if (hz == 30.0 || hz == 20000.0)
                continue;
            g.setColour(c.textTertiary);
            const auto label = hz >= 1000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                                            : juce::String(static_cast<int>(hz));
            g.drawText(label,
                       juce::Rectangle<float>(x - 16.0f, p.getBottom() + 1.0f, 32.0f, 12.0f),
                       juce::Justification::centred);
        }
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
            const auto& after = spectrum_[1];
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
            const auto& before = spectrum_[0];
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
            g.setColour(bandColour(theme, focus).withAlpha(0.16f));
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

    // The nodes, the selected one on top.
    for (int pass = 0; pass < 2; ++pass)
        for (int b = 0; b < eq::bandCount; ++b)
        {
            if ((b == selected_) != (pass == 1))
                continue;
            const auto at = nodePosition(b);
            const bool on = bandOn(b);
            const auto colour = bandColour(theme, b);
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
            g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.caption, theme.fonts.weight.semibold));
            const juce::String name = b == eq::lowCut ? "L" : b == eq::highCut ? "H" : juce::String(b);
            g.drawText(name, r, juce::Justification::centred);
        }

    // A readout for the band under the pointer (or being dragged).
    const int readout = dragging_ >= 0 ? dragging_ : hovered_;
    if (readout >= 0)
    {
        juce::String text = formatHz(card_.param(eq::paramIndex(readout, eq::frequency)));
        if (eq::isCut(readout))
            text << "  "
                 << eq::slopes[static_cast<size_t>(juce::jlimit(
                        0, 5, static_cast<int>(std::lround(card_.param(eq::paramIndex(readout, eq::type))))))]
                 << " dB/oct";
        else
        {
            const int shape = static_cast<int>(std::lround(card_.param(eq::paramIndex(readout, eq::type))));
            if (shape != eq::notch)
                text << "  " << formatDb(card_.param(eq::paramIndex(readout, eq::gain)));
            text << "  Q " << juce::String(card_.param(eq::paramIndex(readout, eq::q)), 2);
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
        g.drawText(formatHz(xToHz(hoverPoint_.x, p)) + "  " +
                       formatDb(yToDb(hoverPoint_.y, rangeDb, -rangeDb, p)),
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
    card_.beginGesture(band == eq::lowCut    ? "Low cut"
                       : band == eq::highCut ? "High cut"
                                             : "Band " + juce::String(band));
    // Dragging a band that is off switches it on.
    if (!bandOn(band))
        card_.setParam(eq::paramIndex(band, eq::on), 1.0f);
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
    const int fIndex = eq::paramIndex(dragging_, eq::frequency);
    if (!e.mods.isAltDown()) // Alt: gain only
        card_.setParam(fIndex, static_cast<float>(xToHz(pos.x, p)));
    const int gIndex = eq::paramIndex(dragging_, eq::gain);
    if (gIndex >= 0)
    {
        const int shape = static_cast<int>(std::lround(card_.param(eq::paramIndex(dragging_, eq::type))));
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
    card_.setParam(eq::paramIndex(band, eq::on), bandOn(band) ? 0.0f : 1.0f);
    card_.endGesture();
}

void EqGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int band = hovered_ >= 0 ? hovered_ : bandAt(e.position) >= 0 ? bandAt(e.position) : selected_;
    if (band < 0 || wheel.deltaY == 0.0f)
        return;
    const float direction = wheel.deltaY > 0.0f ? 1.0f : -1.0f;
    card_.beginGesture("Q");
    if (eq::isCut(band))
    {
        const int index = eq::paramIndex(band, eq::type);
        card_.setParam(index, juce::jlimit(0.0f, 5.0f, std::round(card_.param(index)) + direction));
    }
    else
    {
        const int index = eq::paramIndex(band, eq::q);
        const float step = e.mods.isShiftDown() ? 1.02f : 1.12f;
        card_.setParam(index, card_.param(index) * (direction > 0.0f ? step : 1.0f / step));
    }
    card_.endGesture();
    repaint();
}

void EqGraph::showBandMenu(int band)
{
    juce::PopupMenu menu;
    const auto& typeInfo = card_.info(eq::paramIndex(band, eq::type));
    const int current = static_cast<int>(std::lround(card_.param(eq::paramIndex(band, eq::type))));
    menu.addSectionHeader(eq::isCut(band) ? "Slope" : "Shape");
    for (size_t i = 0; i < typeInfo.choices.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1,
                     eq::isCut(band) ? juce::String(typeInfo.choices[i]) + "/oct"
                                     : juce::String(typeInfo.choices[i]),
                     true,
                     static_cast<int>(i) == current);
    menu.addSeparator();
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
                               card.setParam(eq::paramIndex(band, eq::on), safe->bandOn(band) ? 0.0f : 1.0f);
                           else if (choice == 101)
                           {
                               for (const auto field : {eq::type, eq::frequency, eq::gain, eq::q})
                                   if (const int index = eq::paramIndex(band, field); index >= 0)
                                       card.setParam(index, card.info(index).defaultValue);
                           }
                           else
                               card.setParam(eq::paramIndex(band, eq::type), static_cast<float>(choice - 1));
                           card.endGesture();
                           safe->repaint();
                       });
}

} // namespace aural::ui::strip
