// The opto compressor's card (screens/channel-strip.md 12.5): a VU needle meter,
// the cell (its lamp lit by the reduction, the photocell's memory and the slow
// release it gives), a gain-reduction history, and the controls: Peak
// reduction and Gain as large knobs, then Compress / Limit, Emphasis, Colour
// and Mix.

#include "plugins/ChannelStrip.h"
#include "ui/strip/Graphs.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

#include <cmath>

namespace northlight::ui::strip
{

namespace po = plugins::strip::opto;

namespace
{
// What the VU meter shows, remembered for the session: gain reduction or the output.
bool& meterShowsOutput()
{
    static bool output = false;
    return output;
}

// The VU scale: -20..+3 dB along the arc, linear in amplitude as on a VU
// meter (so -3..+3 takes half the arc).
float vuPosition(float db)
{
    const float lo = std::pow(10.0f, -1.0f), hi = std::pow(10.0f, 3.0f / 20.0f);
    const float a = std::pow(10.0f, juce::jlimit(-24.0f, 4.0f, db) / 20.0f);
    return (a - lo) / (hi - lo);
}

// A needle with a VU meter's mass: a damped spring that reaches 99% in about
// 300 ms with a slight overshoot.
struct Needle
{
    float position = 0.0f, velocity = 0.0f;
    bool started = false;

    void follow(float target, double elapsedMs)
    {
        if (!started)
        {
            position = target;
            started = true;
        }
        constexpr float omega = 18.0f, damping = 0.78f, step = 0.002f;
        for (float t = static_cast<float>(elapsedMs) * 0.001f; t > 0.0f; t -= step)
        {
            const float dt = std::min(step, t);
            const float acceleration =
                omega * omega * (target - position) - 2.0f * damping * omega * velocity;
            velocity += acceleration * dt;
            position += velocity * dt;
        }
        // The pegs at each end.
        if (position < -0.04f || position > 1.05f)
        {
            position = juce::jlimit(-0.04f, 1.05f, position);
            velocity = 0.0f;
        }
    }
};

const ModuleCard::Frame& newestFrame(const ModuleCard& card)
{
    static const ModuleCard::Frame none;
    return card.historySize() > 0 ? card.historyAt(card.historySize() - 1) : none;
}

// ----- VU meter -----------------------------------------------------------------------------

class VuMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit VuMeter(ModuleCard& card) : card_(card)
    {
        setRepaintsOnMouseActivity(true);
        updateTooltip();
    }

    void advance(double elapsedMs)
    {
        const auto& frame = newestFrame(card_);
        // OUT reads 0 VU at -18 dBFS RMS: a sine's peak 3 dB above that.
        const float db = meterShowsOutput() ? toDb(frame.output) + 15.0f : -frame.reduction;
        needle_.follow(vuPosition(db), elapsedMs);
        reading_ = meterShowsOutput() ? card_.outputReadoutDb() : card_.reductionReadoutDb();
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        meterShowsOutput() = !meterShowsOutput();
        updateTooltip();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addRoundedRectangle(area, theme.radius.sm);
            g.reduceClipRegion(clip);
            const auto face = area.reduced(3.0f);
            const float h = face.getHeight(), w = face.getWidth();

            // The lamp behind the face: a warm glow from below.
            g.setGradientFill(juce::ColourGradient(hue.withAlpha(isMouseOver() ? 0.22f : 0.16f),
                                                   face.getCentreX(),
                                                   face.getBottom(),
                                                   hue.withAlpha(0.0f),
                                                   face.getCentreX(),
                                                   face.getY() - h * 0.25f,
                                                   true));
            g.fillRect(face);

            // The scale: an arc around the needle's pivot. The arc is as wide as
            // the face allows; a tall face centres the whole movement (labels,
            // arc, pivot) above the captions, a short one hides the pivot below.
            constexpr float labelRoom = 22.0f, captionRoom = 18.0f;
            const float halfAngle = juce::degreesToRadians(48.0f);
            const float radius =
                std::max(20.0f, std::min((w * 0.5f - 18.0f) / std::sin(halfAngle), (h - labelRoom) * 1.15f));
            float pivotY = face.getY() + labelRoom + radius;
            const float spare = face.getBottom() - captionRoom - pivotY;
            if (spare > 0.0f)
                pivotY += spare * 0.5f;
            const juce::Point<float> pivot(face.getCentreX(), pivotY);
            const auto angleOf = [halfAngle](float position)
            { return -halfAngle + juce::jlimit(-0.06f, 1.06f, position) * 2.0f * halfAngle; };
            const auto at = [&](float position, float r)
            { return pivot.getPointOnCircumference(r, angleOf(position)); };

            const float zero = vuPosition(0.0f);
            juce::Path scale;
            scale.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f, angleOf(0.0f), angleOf(zero), true);
            g.setColour(c.textSecondary.withAlpha(0.7f));
            g.strokePath(scale, juce::PathStrokeType(1.0f));
            juce::Path hot;
            hot.addCentredArc(
                pivot.x, pivot.y, radius + 1.5f, radius + 1.5f, 0.0f, angleOf(zero), angleOf(1.0f), true);
            g.setColour(c.meterHot.withAlpha(0.85f));
            g.strokePath(hot, juce::PathStrokeType(3.0f));

            const auto tick = [&](float db, float length, float thickness)
            {
                const float p = vuPosition(db);
                g.setColour(db > 0.0f ? c.meterHot : c.textSecondary);
                g.drawLine(juce::Line<float>(at(p, radius), at(p, radius + length)), thickness);
            };
            for (float db : {-15.0f, -8.0f, -6.0f, -4.0f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f})
                tick(db, 4.0f, 1.0f);
            g.setFont(theme.caption());
            for (int db : {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3})
            {
                tick(static_cast<float>(db), 7.0f, 1.2f);
                // The numeral's near edge a few px past its tick, whatever its size.
                const auto text = juce::String(std::abs(db));
                const float textW = juce::GlyphArrangement::getStringWidth(theme.caption(), text);
                const float textH = theme.caption().getHeight();
                const auto base = at(vuPosition(static_cast<float>(db)), radius + 10.0f);
                const auto dir = (base - pivot) / std::max(1.0f, base.getDistanceFrom(pivot));
                const float extent = std::abs(dir.x) * textW * 0.5f + std::abs(dir.y) * textH * 0.5f;
                g.setColour(db > 0 ? c.meterHot : c.textSecondary);
                g.drawText(text,
                           juce::Rectangle<float>(textW + 2.0f, textH).withCentre(base + dir * extent),
                           juce::Justification::centred);
            }
            // VU over the pivot, the mode in the lower left, the reading in the lower right.
            g.setFont(theme.label());
            g.setColour(c.textTertiary);
            g.drawText("VU",
                       juce::Rectangle<float>(40.0f, 16.0f).withCentre({pivot.x, pivot.y - radius * 0.5f}),
                       juce::Justification::centred);
            auto bottom = face.withTop(face.getBottom() -
                                       static_cast<float>(theme.lineHeight(theme.fonts.size.caption)) - 6.0f)
                              .reduced(6.0f, 0.0f);
            g.setFont(theme.captionCaps());
            g.drawText(
                meterShowsOutput() ? "OUTPUT" : "GAIN REDUCTION", bottom, juce::Justification::centredLeft);
            g.setFont(theme.value());
            if (meterShowsOutput())
            {
                g.setColour(c.textSecondary);
                g.drawText(formatDb(reading_), bottom, juce::Justification::centredRight);
            }
            else if (reading_ >= 0.1f)
            {
                g.setColour(c.meterHot);
                g.drawText(
                    "-" + juce::String(reading_, 1) + " dB", bottom, juce::Justification::centredRight);
            }

            // The needle: its shadow on the face, then the needle, from the pivot
            // under the face to just past the scale.
            const float angle = angleOf(needle_.position);
            const auto tip = pivot.getPointOnCircumference(radius + 9.0f, angle);
            const auto base = pivot.getPointOnCircumference(radius * 0.18f, angle);
            if (const auto& shadow = theme.material.needleShadow; !shadow.empty())
            {
                g.setColour(shadow.colour);
                g.drawLine(juce::Line<float>(base.translated(shadow.x, shadow.y),
                                             tip.translated(shadow.x, shadow.y)),
                           1.6f);
            }
            g.setColour(c.textPrimary);
            g.drawLine(juce::Line<float>(base, tip), 1.4f);

            // The cover over the pivot, when it is on the face.
            const float cover = std::max(8.0f, radius * 0.12f);
            if (pivot.y - cover < face.getBottom())
            {
                const auto disc = juce::Rectangle<float>(cover * 2.0f, cover * 2.0f).withCentre(pivot);
                g.setGradientFill(juce::ColourGradient::vertical(
                    c.surface2, disc.getY(), c.surface1.darker(0.4f), pivot.y));
                g.fillEllipse(disc);
                juce::Path rim;
                rim.addCentredArc(pivot.x,
                                  pivot.y,
                                  cover,
                                  cover,
                                  0.0f,
                                  -juce::MathConstants<float>::halfPi,
                                  juce::MathConstants<float>::halfPi,
                                  true);
                g.setColour(hue.withAlpha(0.35f));
                g.strokePath(rim, juce::PathStrokeType(1.0f));
            }
        }
        drawWellFrame(g, theme, area);
    }

private:
    void updateTooltip() { setTooltip(meterShowsOutput() ? "Click: show reduction" : "Click: show output"); }

    ModuleCard& card_;
    Needle needle_;
    float reading_ = 0.0f;
};

// ----- the cell -----------------------------------------------------------------------------

// The optical cell: a lamp that the detector lights, facing a photocell. The
// lamp glows with the gain reduction; the photocell warms with its memory
// (sustained compression), and the slow release that memory gives reads
// under it.
class CellView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CellView(ModuleCard& card) : card_(card) {}

    void advance(double elapsedMs)
    {
        const auto& frame = newestFrame(card_);
        // The lamp lights quickly and fades over a few tens of milliseconds.
        const float ms = static_cast<float>(elapsedMs);
        const float target = frame.reduction;
        lamp_ += (target - lamp_) * (1.0f - std::exp(-ms / (target > lamp_ ? 8.0f : 45.0f)));
        memory_ = juce::jlimit(0.0f, 1.0f, frame.extra);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        auto inner = area.reduced(4.0f, 3.0f);
        g.setFont(theme.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText("CELL", inner.removeFromTop(14.0f), juce::Justification::centred);
        auto readout = inner.removeFromBottom(30.0f);
        const float size = std::min(inner.getWidth() * 0.55f, inner.getHeight() * 0.36f);
        const juce::Point<float> lampCentre(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.27f);
        const juce::Point<float> cellCentre(inner.getCentreX(), inner.getY() + inner.getHeight() * 0.76f);

        // The light: brighter as the reduction grows (square-root, so a few dB show).
        const float light = std::sqrt(juce::jlimit(0.0f, 1.0f, lamp_ / 15.0f));
        if (light > 0.01f)
        {
            // The beam from the lamp to the cell.
            const auto beam = juce::Rectangle<float>(
                lampCentre.x - size * 0.32f, lampCentre.y, size * 0.64f, cellCentre.y - lampCentre.y);
            g.setGradientFill(juce::ColourGradient::vertical(
                hue.withAlpha(0.35f * light), beam.getY(), hue.withAlpha(0.08f * light), beam.getBottom()));
            g.fillRect(beam);
            // The halo.
            const float halo = size * 1.25f;
            g.setGradientFill(juce::ColourGradient(hue.withAlpha(0.5f * light),
                                                   lampCentre,
                                                   hue.withAlpha(0.0f),
                                                   lampCentre.translated(halo, 0.0f),
                                                   true));
            g.fillEllipse(juce::Rectangle<float>(halo * 2.0f, halo * 2.0f).withCentre(lampCentre));
        }
        // The lamp: an electroluminescent panel, dark until lit.
        const auto lamp = juce::Rectangle<float>(size, size).withCentre(lampCentre);
        g.setGradientFill(juce::ColourGradient(hue.brighter(0.5f).withAlpha(0.15f + 0.85f * light),
                                               lampCentre,
                                               hue.withAlpha(0.1f + 0.7f * light),
                                               lampCentre.translated(size * 0.5f, 0.0f),
                                               true));
        g.fillEllipse(lamp);
        g.setColour(hue.withAlpha(0.35f + 0.4f * light));
        g.drawEllipse(lamp, 1.0f);

        // The photocell: a dark disc with the cadmium-sulphide track across it,
        // warming with the memory.
        const auto cell = juce::Rectangle<float>(size, size).withCentre(cellCentre);
        g.setColour(c.surface2);
        g.fillEllipse(cell);
        if (memory_ > 0.01f)
        {
            g.setColour(hue.withAlpha(0.3f * memory_));
            g.fillEllipse(cell);
        }
        juce::Path track;
        const int turns = 4;
        const auto r = cell.reduced(size * 0.2f);
        const float pitch = r.getHeight() / static_cast<float>(turns * 2);
        for (int i = 0; i <= turns * 2; ++i)
        {
            const float y = r.getY() + pitch * static_cast<float>(i);
            // The track's width follows the disc.
            const float dy = (y - cellCentre.y) / (size * 0.5f);
            const float half = size * 0.5f * std::sqrt(std::max(0.0f, 1.0f - dy * dy)) - size * 0.14f;
            const float x0 = cellCentre.x - half, x1 = cellCentre.x + half;
            if (i == 0)
                track.startNewSubPath(x0, y);
            const bool right = i % 2 == 0;
            track.lineTo(right ? x1 : x0, y);
            if (i < turns * 2)
                track.lineTo(right ? x1 : x0, y + pitch);
        }
        g.setColour(c.textTertiary.interpolatedWith(hue, memory_).withAlpha(0.55f + 0.45f * memory_));
        g.strokePath(track, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered));
        g.setColour(c.textTertiary.withAlpha(0.6f));
        g.drawEllipse(cell, 1.0f);

        // The memory as a thin bar, and the slow release it gives.
        auto bar = readout.removeFromTop(4.0f).reduced(4.0f, 0.0f);
        g.setColour(c.surface2);
        g.fillRoundedRectangle(bar, 2.0f);
        g.setColour(hue);
        g.fillRoundedRectangle(bar.withWidth(std::max(bar.getHeight(), bar.getWidth() * memory_)), 2.0f);
        g.setFont(theme.value());
        g.setColour(c.textSecondary);
        const float seconds = po::slowReleaseSeconds(memory_);
        g.drawText(juce::String(seconds, 1) + " s",
                   readout.removeFromTop(14.0f).translated(0.0f, 1.0f),
                   juce::Justification::centred);
        g.setFont(theme.caption());
        g.setColour(c.textTertiary);
        g.drawText("release", readout, juce::Justification::centredTop);
        drawWellFrame(g, theme, area);
    }

private:
    ModuleCard& card_;
    float lamp_ = 0.0f, memory_ = 0.0f;
};

// ----- the card -----------------------------------------------------------------------------

class OptoCard final : public ModuleCard
{
public:
    OptoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module), meter_(*this), cell_(*this),
          history_(*this, HistoryGraph::Style{0.0f, -48.0f, false, true, true, -1, false, 3.0f, true})
    {
        addAndMakeVisible(meter_);
        addAndMakeVisible(cell_);
        addAndMakeVisible(history_);
        peak_ = &addKnob(po::peakReduction, "Peak reduction");
        peak_->format = [](double v) { return juce::String(static_cast<int>(std::lround(v))); };
        peak_->setValue(param(po::peakReduction), juce::dontSendNotification);
        gain_ = &addKnob(po::gain);
        for (auto* k : {peak_, gain_})
            k->setRingSize(bigRing);
        mode_ = &addChoice(po::mode);
        mode_->setTooltip("Compress or limit");
        for (int p : {po::emphasis, po::colour, po::mix})
            small_.push_back(&addKnob(p));
    }

    int preferredWidth() const override { return 8 * knobCellWidth() + 2 * theme_.spacing.sm + 10; }
    int preferredBodyHeight() const override { return 240; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        // Scale marks around the large knobs, 0 to 10 as on the hardware.
        const auto& c = theme_.colours;
        for (auto* knob : {peak_, gain_})
        {
            const auto b = knob->getBounds().toFloat();
            const juce::Point<float> centre(b.getCentreX(), b.getY() + bigRing * 0.5f);
            for (int i = 0; i <= 10; ++i)
            {
                const float angle =
                    juce::MathConstants<float>::pi * (1.25f + 1.5f * static_cast<float>(i) / 10.0f);
                const auto p = centre.getPointOnCircumference(bigRing * 0.5f + 4.0f, angle);
                g.setColour(i % 5 == 0 ? c.textSecondary : c.textTertiary);
                g.fillEllipse(
                    juce::Rectangle<float>(i % 5 == 0 ? 3.0f : 2.0f, i % 5 == 0 ? 3.0f : 2.0f).withCentre(p));
            }
        }
        // A faint rule between the main controls and the rest.
        if (divider_ > 0)
        {
            g.setColour(c.lineSubtle);
            g.drawVerticalLine(divider_, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
        }
    }

private:
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int bigCellH = 6 + knobCellHeight(bigRing, theme_.fonts.size.caption);
        const int smallH = theme_.sizes.controlCompact + 6 + knobCellHeight();
        const int controlsH = std::max(bigCellH, smallH);
        auto display = body.removeFromTop(std::max(40, body.getHeight() - controlsH - 6));
        body.removeFromTop(6);
        meter_.setBounds(display.removeFromLeft(
            std::min(display.getWidth() / 2, static_cast<int>(display.getHeight() * 1.8f))));
        display.removeFromLeft(6);
        cell_.setBounds(display.removeFromLeft(64));
        display.removeFromLeft(6);
        history_.setBounds(display);

        auto controls = body.removeFromTop(controlsH);
        // The big knobs centred down the row beside the mode and the small knobs;
        // each ring sits 6 px down in its cell so the scale marks clear the display.
        auto big = controls.removeFromLeft(2 * bigCellWidth);
        big = big.withSizeKeepingCentre(big.getWidth(), bigCellH);
        for (auto* knob : {peak_, gain_})
            placeKnob(*knob, big.removeFromLeft(bigCellWidth).withTrimmedTop(6));
        controls.removeFromLeft(theme_.spacing.sm);
        divider_ = controls.getX();
        dividerTop_ = controls.getY() + 6;
        dividerBottom_ = controls.getBottom() - theme_.spacing.sm;
        controls.removeFromLeft(theme_.spacing.sm);
        auto options = controls.removeFromTop(theme_.sizes.controlCompact);
        mode_->setBounds(options.withSizeKeepingCentre(std::min(options.getWidth(), mode_->preferredWidth()),
                                                       theme_.sizes.controlCompact));
        controls.removeFromTop(6);
        layoutKnobs(controls.removeFromTop(knobCellHeight()), 3, small_);
    }

    void paramsChanged() override { history_.repaint(); }
    void framesArrived(double elapsedMs) override
    {
        meter_.advance(elapsedMs);
        cell_.advance(elapsedMs);
        history_.repaint();
    }
    juce::String headerStatus() const override
    {
        const float db = reductionReadoutDb();
        return isOn() && db >= 0.1f ? "-" + juce::String(db, 1) + " dB" : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    VuMeter meter_;
    CellView cell_;
    HistoryGraph history_;
    Knob* peak_ = nullptr;
    Knob* gain_ = nullptr;
    SegmentedControl* mode_ = nullptr;
    std::vector<Knob*> small_;
    int divider_ = 0, dividerTop_ = 0, dividerBottom_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createOptoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<OptoCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
