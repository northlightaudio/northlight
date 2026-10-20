#include "ui/components/Meter.h"

#include <cmath>

namespace aural::ui
{

namespace
{
float toDb(float linear) noexcept
{
    return linear > 0.0f ? 20.0f * std::log10(linear) : -100.0f;
}
} // namespace

Meter::Meter(const Theme& theme, int channels, bool vertical)
    : theme_(theme), channels_(static_cast<size_t>(std::max(1, channels))), vertical_(vertical)
{
    setInterceptsMouseClicks(true, false);
}

void Meter::push(const float* peaks, int numChannels, double elapsedMs)
{
    const float fall = theme_.motion.meterFallDbPerSec * static_cast<float>(elapsedMs / 1000.0);
    for (size_t ch = 0; ch < channels_.size(); ++ch)
    {
        auto& c = channels_[ch];
        const float peak = ch < static_cast<size_t>(numChannels) ? peaks[ch] : 0.0f;
        const float db = toDb(peak);
        // Rise instantly, fall at the token rate.
        c.levelDb = db >= c.levelDb ? db : std::max(db, c.levelDb - fall);
        c.peakAgeMs += elapsedMs;
        if (db >= c.peakDb || c.peakAgeMs > theme_.motion.peakHoldMs)
        {
            c.peakDb = db >= c.peakDb ? db : c.levelDb;
            c.peakAgeMs = 0.0;
        }
        if (peak >= 1.0f)
        {
            c.clipped = true;
            c.clipAgeMs = 0.0;
        }
        else if (c.clipped && autoClearClip_)
        {
            c.clipAgeMs += elapsedMs;
            if (c.clipAgeMs > theme_.meter.clipHoldMs)
                c.clipped = false;
        }
    }
    // Silence stays silent: no repaint while every channel sits on the floor.
    bool changed = false;
    for (const auto& c : channels_)
        changed = changed || c.levelDb > theme_.meter.floorDb || c.peakDb > theme_.meter.floorDb || c.clipped;
    if (changed || !wasSilent_)
        repaint();
    wasSilent_ = !changed;
}

void Meter::clearClip()
{
    for (auto& c : channels_)
        c.clipped = false;
    repaint();
}

bool Meter::clipped() const noexcept
{
    for (const auto& c : channels_)
        if (c.clipped)
            return true;
    return false;
}

int Meter::barsWidth() const noexcept
{
    const int n = static_cast<int>(channels_.size());
    return n * theme_.sizes.meterWidth + (n - 1) * theme_.sizes.meterGap;
}

float Meter::yForDb(float db, juce::Rectangle<float> bar) const noexcept
{
    const float floorDb = theme_.meter.floorDb;
    const float fraction = juce::jlimit(0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
    return vertical_ ? bar.getBottom() - fraction * bar.getHeight() : bar.getX() + fraction * bar.getWidth();
}

void Meter::mouseDown(const juce::MouseEvent&)
{
    clearClip();
}

void Meter::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const float w = static_cast<float>(theme_.sizes.meterWidth);
    const float gap = static_cast<float>(theme_.sizes.meterGap);
    const float clipSize = static_cast<float>(theme_.sizes.meterWidth);
    auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> scale;
    if (vertical_ && showScale_)
        scale = area.removeFromLeft(area.getWidth() - static_cast<float>(barsWidth()));
    auto clipRow = vertical_ ? area.removeFromTop(clipSize + gap) : area.removeFromRight(clipSize + gap);

    for (size_t ch = 0; ch < channels_.size(); ++ch)
    {
        const auto& channel = channels_[ch];
        juce::Rectangle<float> bar;
        if (vertical_)
            bar = {area.getX() + static_cast<float>(ch) * (w + gap), area.getY(), w, area.getHeight()};
        else
            bar = {area.getX(), area.getY() + static_cast<float>(ch) * (w + gap), area.getWidth(), w};
        const auto& m = theme_.material;
        g.setColour(m.enabled ? m.meterWell : c.meterTrack);
        g.fillRect(bar);
        // Solid zone segments up to the level.
        for (const auto& zone : theme_.meter.zones)
        {
            const float from = std::max(zone.fromDb, theme_.meter.floorDb);
            const float to = std::min(zone.toDb, channel.levelDb);
            if (to <= from)
                continue;
            g.setColour(zone.colour);
            if (vertical_)
                g.fillRect(bar.getX(), yForDb(to, bar), w, yForDb(from, bar) - yForDb(to, bar));
            else
                g.fillRect(yForDb(from, bar), bar.getY(), yForDb(to, bar) - yForDb(from, bar), w);
        }
        if (m.enabled && m.ledGapPx > 0)
        {
            // LED segments: one dark gap every pitch, a single repeating fill.
            const float pitch = static_cast<float>(std::max(1, m.ledPitchPx + m.ledGapPx));
            g.setColour(m.meterWell);
            if (vertical_)
                for (float y = bar.getBottom() - pitch; y > bar.getY(); y -= pitch)
                    g.fillRect(bar.getX(), y, w, static_cast<float>(m.ledGapPx));
            else
                for (float x = bar.getX() + pitch; x < bar.getRight(); x += pitch)
                    g.fillRect(x, bar.getY(), static_cast<float>(m.ledGapPx), w);
        }
        if (channel.peakDb > theme_.meter.floorDb)
        {
            g.setColour(c.meterPeakHold);
            if (vertical_)
                g.fillRect(bar.getX(), yForDb(channel.peakDb, bar), w, 1.0f);
            else
                g.fillRect(yForDb(channel.peakDb, bar) - 1.0f, bar.getY(), 1.0f, w);
        }
        // The bar's rim, so it reads as cut into the strip rather than lying on it.
        if (m.enabled && !m.meterWellInset.empty())
        {
            g.setColour(m.meterWellInset.colour.withMultipliedAlpha(0.9f));
            g.drawRect(bar, 1.0f);
        }
        // The clip square.
        juce::Rectangle<float> clip =
            vertical_ ? juce::Rectangle<float>(bar.getX(), clipRow.getY(), w, clipSize)
                      : juce::Rectangle<float>(clipRow.getX() + gap, bar.getY(), clipSize, w);
        g.setColour(channel.clipped ? c.meterClip : c.meterTrack);
        g.fillRect(clip);
    }
    if (!scale.isEmpty())
    {
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        const auto bar = juce::Rectangle<float>(area.getX(), area.getY(), w, area.getHeight());
        for (const float db : theme_.meter.scaleTicksDb)
        {
            const float y = yForDb(db, bar);
            g.drawText(juce::String(static_cast<int>(db)),
                       scale.withY(y - theme_.fonts.size.caption * 0.6f)
                           .withHeight(theme_.fonts.size.caption * 1.2f)
                           .withTrimmedRight(gap)
                           .toNearestInt(),
                       juce::Justification::centredRight);
        }
    }
}

namespace
{
class MeterValue final : public juce::AccessibilityValueInterface
{
public:
    explicit MeterValue(const Meter& meter) : meter_(meter) {}
    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override { return meter_.accessibleValue(); }
    juce::String getCurrentValueAsString() const override
    {
        return juce::String(meter_.accessibleValue(), 1) + " dB";
    }
    void setValue(double) override {}
    void setValueAsString(const juce::String&) override {}
    AccessibleValueRange getRange() const override { return {{-100.0, 6.0}, 0.1}; }

private:
    const Meter& meter_;
};
} // namespace

double Meter::accessibleValue() const noexcept
{
    double loudest = -100.0;
    for (const auto& channel : channels_)
        loudest = std::max(loudest, static_cast<double>(channel.levelDb));
    return loudest;
}

std::unique_ptr<juce::AccessibilityHandler> Meter::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::slider,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<MeterValue>(*this)});
}

} // namespace aural::ui
