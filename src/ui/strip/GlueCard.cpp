// The glue compressor's card (screens/plugins.md 7.2a): the dynamics ribbon
// (each moment of the last three seconds as a band from its loudness to its
// peak, input in grey under the output in the hue, so how the compressor
// reshapes the distance between body and peaks is visible), the crest factor going in
// and coming out, an LED gain-reduction arc, and the controls: Threshold and
// Makeup as large knobs, Ratio, Attack and Release as stepped rotary selectors,
// then Key HP and Mix.

#include "plugins/Glue.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::ui::strip
{

namespace pg = plugins::glue;

namespace
{
constexpr double framesPerSecond = 1.0 / engine::ProcessorFeedback::frameSeconds;
constexpr float ribbonSeconds = 3.0f;
constexpr float crestSeconds = 1.0f;
constexpr float topDb = 0.0f, bottomDb = -48.0f;

// Peak over RMS (dB) for frames [from, to) of the card's history: how far the
// peaks stand above the body of the sound. Zero with too little signal to say.
float crestDb(const ModuleCard& card, int from, int to, bool output)
{
    float peak = 0.0f;
    double sum = 0.0;
    int n = 0;
    for (int i = std::max(0, from); i < to; ++i)
    {
        const auto& f = card.historyAt(i);
        const float p = output ? f.output : f.input;
        const float r = output ? f.extra2 : f.extra;
        peak = std::max(peak, p);
        sum += static_cast<double>(r) * r;
        ++n;
    }
    const double rms = n > 0 ? std::sqrt(sum / n) : 0.0;
    if (rms < 1.0e-4 || peak <= 0.0f) // below -80 dB: silence has no crest
        return 0.0f;
    return std::max(0.0f, 20.0f * static_cast<float>(std::log10(static_cast<double>(peak) / rms)));
}

// ----- the dynamics ribbon ----------------------------------------------------------------

class DynamicsRibbon final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DynamicsRibbon(ModuleCard& card) : card_(card) { setTooltip("Drag: threshold"); }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto p = area.reduced(1.0f);
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(p.toNearestInt());
            // The grid every 12 dB, its labels at the right.
            g.setFont(
                theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
            for (float db = topDb - 12.0f; db > bottomDb; db -= 12.0f)
            {
                const float y = dbToY(db, topDb, bottomDb, p);
                g.setColour(c.gridBar);
                g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
                g.setColour(c.textTertiary);
                g.drawText(juce::String(static_cast<int>(db)),
                           juce::Rectangle<float>(p.getRight() - 26.0f, y - 12.0f, 24.0f, 11.0f),
                           juce::Justification::centredRight);
            }
            collect(p);
            const auto x = [&](int col) { return p.getX() + static_cast<float>(col) + 0.5f; };
            const auto yOf = [&](float linear)
            { return std::min(p.getBottom(), dbToY(toDb(linear), topDb, bottomDb, p)); };
            // A band: the peak edge left to right, the RMS edge back.
            const auto band = [&](const std::vector<float>& peaks, const std::vector<float>& rms)
            {
                juce::Path path;
                const int columns = static_cast<int>(peaks.size());
                for (int col = 0; col < columns; ++col)
                {
                    const float y = yOf(peaks[static_cast<size_t>(col)]);
                    if (col == 0)
                        path.startNewSubPath(x(col), y);
                    else
                        path.lineTo(x(col), y);
                }
                for (int col = columns - 1; col >= 0; --col)
                    path.lineTo(x(col), yOf(rms[static_cast<size_t>(col)]));
                path.closeSubPath();
                return path;
            };
            if (!inPeak_.empty())
            {
                g.setColour(c.textTertiary.withAlpha(0.32f));
                g.fillPath(band(inPeak_, inRms_));
                const auto hue = card_.colour();
                g.setColour(hue.withAlpha(0.42f));
                g.fillPath(band(outPeak_, outRms_));
                // The output's body: its RMS as a line, the edge the ear follows.
                juce::Path body;
                for (int col = 0; col < static_cast<int>(outRms_.size()); ++col)
                {
                    const float y = yOf(outRms_[static_cast<size_t>(col)]);
                    if (col == 0)
                        body.startNewSubPath(x(col), y);
                    else
                        body.lineTo(x(col), y);
                }
                g.setColour(hue.withAlpha(0.95f));
                g.strokePath(
                    body,
                    juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // The threshold, dashed, with its value.
            const float db = card_.param(pg::threshold);
            const float y = dbToY(db, topDb, bottomDb, p);
            const bool hot = dragging_ || isMouseOver();
            g.setColour(c.textPrimary.withAlpha(hot ? 0.9f : 0.6f));
            const float dashes[] = {4.0f, 3.0f};
            g.drawDashedLine(juce::Line<float>(p.getX(), y, p.getRight(), y), dashes, 2, hot ? 1.5f : 1.0f);
            g.setFont(theme.value());
            g.setColour(c.textPrimary.withAlpha(hot ? 1.0f : 0.75f));
            const float ly = y > p.getY() + 30.0f ? y - 15.0f : y + 2.0f;
            g.drawText(juce::String(db, 1) + " dB",
                       juce::Rectangle<float>(p.getX() + 6.0f, ly, 80.0f, 13.0f),
                       juce::Justification::centredLeft);

            // The crest factor in and out, top left.
            auto readout =
                juce::Rectangle<float>(p.getX() + 6.0f, p.getY() + 4.0f, p.getWidth() - 40.0f, 14.0f);
            g.setFont(theme.captionCaps());
            g.setColour(c.textTertiary);
            const juce::String caption = "CREST";
            const float captionW =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.captionCaps(), caption));
            g.drawText(caption, readout.removeFromLeft(captionW), juce::Justification::centredLeft);
            readout.removeFromLeft(6.0f);
            g.setFont(theme.value());
            if (crestIn_ > 0.0f)
            {
                const auto in = juce::String(crestIn_, 1);
                const auto out = juce::String(crestOut_, 1) + " dB";
                const auto arrow = juce::String::fromUTF8(" \xe2\x86\x92 ");
                const auto w = [&](const juce::String& s)
                { return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), s)); };
                g.setColour(c.textSecondary);
                g.drawText(in, readout.removeFromLeft(w(in)), juce::Justification::centredLeft);
                g.setColour(c.textTertiary);
                g.drawText(arrow, readout.removeFromLeft(w(arrow)), juce::Justification::centredLeft);
                g.setColour(card_.colour());
                g.drawText(out, readout, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour(c.textTertiary);
                g.drawText(juce::String::fromUTF8("\xe2\x80\x93"), readout, juce::Justification::centredLeft);
            }
        }
        drawWellFrame(g, theme, area);
    }

    void refresh()
    {
        const int size = card_.historySize();
        const int recent = static_cast<int>(crestSeconds * framesPerSecond);
        crestIn_ = crestDb(card_, size - recent, size, false);
        crestOut_ = crestDb(card_, size - recent, size, true);
        repaint();
    }

    float crestIn() const noexcept { return crestIn_; }
    float crestOut() const noexcept { return crestOut_; }

    void mouseMove(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = true;
        card_.beginGesture(card_.info(pg::threshold).name);
        setThreshold(e.y);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_)
            setThreshold(e.y);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (!dragging_)
            return;
        dragging_ = false;
        card_.endGesture();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        card_.beginGesture(card_.info(pg::threshold).name);
        card_.setParam(pg::threshold, card_.info(pg::threshold).defaultValue);
        card_.endGesture();
    }

private:
    void setThreshold(int y)
    {
        const auto p = getLocalBounds().toFloat().reduced(1.0f);
        card_.setParam(pg::threshold, yToDb(static_cast<float>(y), topDb, bottomDb, p));
        repaint();
    }

    // Per column: the largest peak and RMS of its frames, columns counted from
    // the start of the history so they keep their frames as the ribbon scrolls.
    void collect(juce::Rectangle<float> p)
    {
        const int columns = std::max(1, static_cast<int>(p.getWidth()));
        for (auto* v : {&inPeak_, &inRms_, &outPeak_, &outRms_})
            v->assign(static_cast<size_t>(columns), 0.0f);
        const auto perColumn =
            static_cast<std::uint64_t>(std::max(1L, std::lround(ribbonSeconds * framesPerSecond / columns)));
        const int size = card_.historySize();
        const auto total = card_.historyTotal();
        if (total == 0)
        {
            inPeak_.clear();
            return;
        }
        const auto first = total - static_cast<std::uint64_t>(size);
        const auto newestBucket = (total - 1) / perColumn;
        for (int col = 0; col < columns; ++col)
        {
            const auto fromRight = static_cast<std::uint64_t>(columns - 1 - col);
            if (fromRight > newestBucket)
                continue;
            const auto bucket = newestBucket - fromRight;
            for (auto a = bucket * perColumn; a < (bucket + 1) * perColumn && a < total; ++a)
            {
                if (a < first)
                    continue;
                const auto& f = card_.historyAt(static_cast<int>(a - first));
                const auto i = static_cast<size_t>(col);
                inPeak_[i] = std::max(inPeak_[i], f.input);
                inRms_[i] = std::max(inRms_[i], f.extra);
                outPeak_[i] = std::max(outPeak_[i], f.output);
                outRms_[i] = std::max(outRms_[i], f.extra2);
            }
        }
        // Peaks fall at 30 dB a second across the columns, as the other
        // histories do, so the band reads as an envelope, not as jagged 5 ms peaks.
        const auto fall = static_cast<float>(
            std::pow(10.0, -30.0 * static_cast<double>(perColumn) / framesPerSecond / 20.0));
        for (int col = 1; col < columns; ++col)
        {
            const auto i = static_cast<size_t>(col);
            inPeak_[i] = std::max(inPeak_[i], inPeak_[i - 1] * fall);
            outPeak_[i] = std::max(outPeak_[i], outPeak_[i - 1] * fall);
        }
    }

    ModuleCard& card_;
    bool dragging_ = false;
    float crestIn_ = 0.0f, crestOut_ = 0.0f;
    std::vector<float> inPeak_, inRms_, outPeak_, outRms_; // per column, reused
};

// ----- the LED gain-reduction arc ---------------------------------------------------------

class ReductionArc final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr std::array<float, 10> steps{
        0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 14.0f, 20.0f};
    static constexpr float hotFromDb = 10.0f;

    explicit ReductionArc(ModuleCard& card) : card_(card) {}

    void advance(double elapsedMs)
    {
        reduction_ = card_.reductionDb();
        if (reduction_ >= held_)
        {
            held_ = reduction_;
            heldMs_ = 0.0;
        }
        else if ((heldMs_ += elapsedMs) > card_.theme().motion.peakHoldMs)
            held_ = std::max(reduction_, held_ - static_cast<float>(elapsedMs) * 0.02f); // 20 dB a second
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto face = area.reduced(6.0f);
        const float sweep = juce::degreesToRadians(240.0f);
        const float start = -sweep * 0.5f;
        // Room outside the arc for its labels; the 240 degree arc reaches half its
        // radius below the centre, so it is 1.5 radii tall.
        constexpr float labelRoom = 20.0f;
        const float thickness = 7.0f;
        const float outside = thickness * 0.5f + labelRoom;
        const float radius = std::max(16.0f,
                                      std::min((face.getWidth() * 0.5f - outside) / std::sin(sweep * 0.5f),
                                               (face.getHeight() - 2.0f * outside) / 1.5f));
        const float height = 1.5f * radius + 2.0f * outside;
        const juce::Point<float> centre(face.getCentreX(),
                                        face.getY() + (face.getHeight() - height) * 0.5f + outside + radius);
        const auto n = static_cast<int>(steps.size());
        const float gap = juce::degreesToRadians(3.0f);
        const float segment = (sweep - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i)
        {
            const float a0 = start + static_cast<float>(i) * (segment + gap);
            const float db = steps[static_cast<size_t>(i)];
            const bool lit = reduction_ >= db;
            const bool held =
                !lit && held_ >= db && (i + 1 == n || held_ < steps[static_cast<size_t>(i + 1)]);
            const auto colour = db > hotFromDb ? c.meterHot : card_.colour();
            juce::Path seg;
            seg.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, a0, a0 + segment, true);
            const juce::PathStrokeType stroke(
                thickness, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
            g.setColour(lit ? colour : c.surface2);
            g.strokePath(seg, stroke);
            if (held)
            {
                g.setColour(colour.withAlpha(0.8f));
                juce::Path outline;
                stroke.createStrokedPath(outline, seg);
                g.strokePath(outline, juce::PathStrokeType(1.0f));
            }
            // A few of the steps labelled, outside the arc.
            if (db == 1.0f || db == 4.0f || db == 10.0f || db == 20.0f)
            {
                // The near edge a few px off the arc, whatever the numeral's size.
                const auto text = juce::String(static_cast<int>(db));
                const float w = juce::GlyphArrangement::getStringWidth(theme.caption(), text);
                const float h = theme.caption().getHeight();
                const float angle = a0 + segment * 0.5f;
                const float extent =
                    std::abs(std::sin(angle)) * w * 0.5f + std::abs(std::cos(angle)) * h * 0.5f;
                const auto at =
                    centre.getPointOnCircumference(radius + thickness * 0.5f + 3.0f + extent, angle);
                g.setFont(theme.caption());
                g.setColour(db > hotFromDb ? c.meterHot : c.textTertiary);
                g.drawText(
                    text, juce::Rectangle<float>(w + 2.0f, h).withCentre(at), juce::Justification::centred);
            }
        }
        // The reading in the middle, GR under it.
        g.setFont(theme.value());
        const float shown = card_.reductionReadoutDb();
        g.setColour(shown >= 0.1f ? c.meterHot : c.textTertiary);
        g.drawText(shown >= 0.1f ? minusSign() + juce::String(shown, 1) : juce::String("0.0"),
                   juce::Rectangle<float>(radius * 1.4f, 14.0f).withCentre(centre.translated(0.0f, -2.0f)),
                   juce::Justification::centred);
        g.setFont(theme.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText("GR",
                   juce::Rectangle<float>(radius, 12.0f).withCentre(centre.translated(0.0f, 12.0f)),
                   juce::Justification::centred);
        drawWellFrame(g, theme, area);
    }

private:
    ModuleCard& card_;
    float reduction_ = 0.0f, held_ = 0.0f;
    double heldMs_ = 0.0;
};

// ----- the card -----------------------------------------------------------------------------

class GlueCard final : public ModuleCard
{
public:
    GlueCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), ribbon_(*this), arc_(*this)
    {
        addAndMakeVisible(ribbon_);
        addAndMakeVisible(arc_);
        threshold_ = &addKnob(pg::threshold);
        makeup_ = &addKnob(pg::makeup);
        for (auto* k : {threshold_, makeup_})
            k->setRingSize(bigRing);

        const auto selector = [this](int index,
                                     std::vector<juce::String> labels,
                                     const juce::String& caption,
                                     const juce::String& tooltipName)
        {
            auto& s = selectors_.emplace_back(std::make_unique<RotarySelector>(theme_));
            std::vector<juce::String> full;
            for (const auto& choice : info(index).choices)
                full.push_back(juce::String::fromUTF8(choice.c_str()));
            s->setPositions(std::move(labels), std::move(full), tooltipName);
            s->setHue(colour());
            s->setDefault(static_cast<int>(std::lround(info(index).defaultValue)));
            s->setSelected(static_cast<int>(std::lround(param(index))), juce::dontSendNotification);
            s->onChange = [this, index](int value) { setParam(index, static_cast<float>(value)); };
            s->onGestureBegin = [this, tooltipName] { beginGesture(tooltipName); };
            s->onGestureEnd = [this] { endGesture(); };
            addAndMakeVisible(*s);
            // Named as a knob is, so the labels of the row read as one.
            captions_.push_back(&addLabel(caption));
            selectorParams_.push_back(index);
        };
        selector(pg::ratio, {"1.5", "2", "4", "10"}, "Ratio", "Ratio");
        selector(pg::attack, {"0.1", ".3", "1", "3", "10", "30"}, "Attack (ms)", "Attack");
        // Auto: quick back from a hit, slow back from held compression.
        selector(pg::release, {".1", ".3", ".6", "1.2", "Auto"}, "Release (s)", "Release");

        keyHp_ = &addKnob(pg::keyHighPass, "Key HP");
        keyHp_->format = [](double hz)
        { return hz <= pg::keyHighPassOffHz + 0.5 ? juce::String("Off") : hzText(hz); };
        mix_ = &addKnob(pg::mix);
        paramsChanged();
    }

    int preferredWidth() const override
    {
        return 2 * bigCellWidth + 3 * selectorWidth() + 2 * knobCellWidth() + 4 * theme_.spacing.sm + 2 + 16;
    }
    int preferredBodyHeight() const override { return 252; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        const auto& c = theme_.colours;
        // Scale marks around the large knobs.
        for (auto* knob : {threshold_, makeup_})
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
        // Faint rules between the three groups of controls.
        g.setColour(c.lineSubtle);
        for (const int x : dividers_)
            g.drawVerticalLine(x, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
    }

private:
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;
    int selectorWidth() const noexcept
    {
        return selectors_.empty() ? 104 : selectors_.front()->preferredWidth();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int labelH = theme_.lineHeight(theme_.fonts.size.caption);
        // The ring sits 6 px down so its scale marks clear the display.
        const int bigCellH = 6 + knobCellHeight(bigRing, theme_.fonts.size.caption);
        const int selectorH = selectors_.front()->preferredHeight() + labelH + theme_.spacing.sm;
        const int controlsH = std::max({bigCellH, selectorH, knobCellHeight()});
        auto display = body.removeFromTop(std::max(48, body.getHeight() - controlsH - 6));
        body.removeFromTop(6);
        arc_.setBounds(display.removeFromRight(std::min(120, display.getHeight() + 24)));
        display.removeFromRight(6);
        ribbon_.setBounds(display);

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
        for (auto* knob : {threshold_, makeup_})
            placeKnob(*knob, controls.removeFromLeft(bigCellWidth));
        rule();
        for (size_t i = 0; i < selectors_.size(); ++i)
        {
            auto cell = controls.removeFromLeft(selectorWidth());
            captions_[i]->setBounds(labelLine(cell));
            selectors_[i]->setBounds(aboveLabelLine(cell).removeFromBottom(selectors_[i]->preferredHeight()));
        }
        rule();
        layoutKnobs(controls.removeFromBottom(knobCellHeight()), 2, {keyHp_, mix_});
    }

    void paramsChanged() override
    {
        for (size_t i = 0; i < selectors_.size(); ++i)
            selectors_[i]->setSelected(static_cast<int>(std::lround(param(selectorParams_[i]))),
                                       juce::dontSendNotification);
        ribbon_.repaint();
    }
    void framesArrived(double elapsedMs) override
    {
        ribbon_.refresh();
        arc_.advance(elapsedMs);
    }
    juce::String headerStatus() const override
    {
        const float db = reductionReadoutDb();
        return isOn() ? reductionText(db) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    DynamicsRibbon ribbon_;
    ReductionArc arc_;
    Knob* threshold_ = nullptr;
    Knob* makeup_ = nullptr;
    Knob* keyHp_ = nullptr;
    Knob* mix_ = nullptr;
    std::vector<std::unique_ptr<RotarySelector>> selectors_;
    std::vector<juce::Label*> captions_;
    std::vector<int> selectorParams_;
    std::vector<int> dividers_;
    int dividerTop_ = 0, dividerBottom_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createGlueCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<GlueCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
