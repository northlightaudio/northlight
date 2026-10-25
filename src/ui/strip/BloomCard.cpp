// Bloom's card (screens/plugins.md 7.2a): the tail as a blossom. The display is
// the reverb on its own over the last five seconds drawn as a flower: time runs
// clockwise round to now at the top, pitch rises outward, each petal as bright
// as its band was loud, so a note opens and fades (the highs first), shimmer
// climbs outward and a frozen tail holds, tinted ice. A band round the rim is
// the tail's level, ticks outside it the dry notes. Freeze is the seed at the
// centre. To its right the four controls that make the tail (Decay, Size,
// Shimmer, Swell) as large bud knobs in tiles, the shimmer's interval as pills
// beside the shape of one hit, then the rest in four groups: Space, Tone,
// Dynamics, Output.

#include "engine/Gain.h"
#include "plugins/BeatSync.h"
#include "plugins/Bloom.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::ui::strip
{

namespace pb = plugins::bloom;

namespace
{
constexpr double framesPerSecond = 1.0 / engine::ProcessorFeedback::frameSeconds;
constexpr double fieldSeconds = 5.0;
constexpr double lowestHz = 50.0, highestHz = 16000.0;
// A band's level (dB, a full-scale sine at 0) from nothing to the brightest.
constexpr float fieldFloorDb = -96.0f, fieldTopDb = -24.0f;
constexpr float rimFloorDb = -60.0f;
constexpr float highsHz = 8000.0f; // the frequency the readout gives the highs' decay at

bool frozen(const ModuleCard& card)
{
    return card.param(pb::freeze) >= 0.5f;
}

bool synced(const ModuleCard& card)
{
    return plugins::sync::quarters(card.param(pb::sync)) > 0.0;
}

juce::String noteName(const ModuleCard& card)
{
    return plugins::sync::noteValues[static_cast<size_t>(plugins::sync::choiceOf(card.param(pb::sync)))].name;
}

juce::String infinity()
{
    return juce::String::fromUTF8("\xe2\x88\x9e");
}

float textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text));
}

// The colour of a held tail: ice, the accent lifted toward the text colour.
juce::Colour iceOf(const Theme& theme)
{
    return theme.colours.accent.interpolatedWith(theme.colours.textPrimary, 0.4f);
}

// A snowflake: six arms through the centre, a pair of twigs on each.
juce::Path snowflake(juce::Point<float> centre, float r)
{
    juce::Path flake;
    for (int arm = 0; arm < 6; ++arm)
    {
        const float a = juce::MathConstants<float>::pi * static_cast<float>(arm) / 3.0f;
        flake.startNewSubPath(centre);
        flake.lineTo(centre.getPointOnCircumference(r, a));
        const auto fork = centre.getPointOnCircumference(r * 0.58f, a);
        for (const float side : {-1.0f, 1.0f})
        {
            flake.startNewSubPath(fork);
            flake.lineTo(fork.getPointOnCircumference(r * 0.32f, a + side * 0.75f));
        }
    }
    return flake;
}

// ----- the blossom --------------------------------------------------------------------------

class BlossomField final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int slices = 56; // five seconds round the flower
    static constexpr int bands = 14;  // 50 Hz to 16 kHz outward
    // The field is wider than the flower, so the readouts in its corners clear it.
    static constexpr float widthRatio = 1.32f;
    static constexpr float imageScale = 2.0f;
    // The sweep: 330 degrees, leaving a gap either side of now at the top.
    static constexpr float sweepFrom = 15.0f, sweepDegrees = 330.0f;

    explicit BlossomField(ModuleCard& card) : card_(card)
    {
        setComponentID("blossom");
        window_.resize(fftSize);
        for (int i = 0; i < fftSize; ++i)
            window_[static_cast<size_t>(i)] =
                0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) /
                                       static_cast<float>(fftSize - 1));
        fftData_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
        recent_.assign(static_cast<size_t>(engine::ProcessorFeedback::tapLength / 2), 0.0f);
        levels_.assign(static_cast<size_t>(slices * bands), 0.0f);
        held_.assign(static_cast<size_t>(slices), 0);
        column_.assign(static_cast<size_t>(bands), 0.0f);
        setInterceptsMouseClicks(true, false);
    }

    // The flower's geometry in the component (the seed button sits in its centre).
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radius() const { return 0.5f * static_cast<float>(std::min(getWidth(), getHeight())) - 2.0f; }
    float innerRadius() const { return radius() * 0.25f; }
    float outerRadius() const { return radius() * 0.885f; }

    // New audio arrived: the flower turns on by the slices it covers.
    void advance()
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr)
        {
            repaint();
            return;
        }
        fb->setTapsEnabled(true);
        const auto position = fb->tapPosition(1);
        if (lastTap_ == 0 || position < lastTap_)
        {
            lastTap_ = position;
            repaint();
            return;
        }
        const double perSlice = fieldSeconds * fb->sampleRate() / slices;
        const auto fresh = position - lastTap_;
        carry_ += static_cast<double>(fresh) / perSlice;
        lastTap_ = position;
        const int count = std::min(slices, static_cast<int>(carry_));
        carry_ -= std::floor(carry_);
        if (count > 0)
        {
            addSlices(*fb, count, fresh);
            render();
        }
        repaint();
    }

    void resized() override
    {
        buildLookup();
        render();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const bool held = frozen(card_);
        const auto tint = held ? iceOf(theme) : card_.colour();
        const auto mid = centre();
        const float r = radius(), rIn = innerRadius(), rOut = outerRadius();

        // The ground: the canvas, faintly lit in the tint at the heart.
        juce::ColourGradient ground(
            c.canvas.interpolatedWith(tint, 0.13f), mid.x, mid.y, c.canvas, mid.x + r, mid.y, true);
        ground.addColour(0.7, c.canvas.interpolatedWith(tint, 0.04f));
        g.setGradientFill(ground);
        g.fillEllipse(juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(mid));

        // The frequency rings, dotted; their labels go over the petals.
        const float dots[] = {2.0f, 5.0f};
        const std::array<std::pair<double, const char*>, 3> marks{
            {{100.0, "100"}, {1000.0, "1k"}, {10000.0, "10k"}}};
        const auto ringRadius = [&](double hz)
        {
            return rIn + (rOut - rIn) *
                             static_cast<float>(std::log(hz / lowestHz) / std::log(highestHz / lowestHz));
        };
        for (const auto& [hz, text] : marks)
        {
            const float rr = ringRadius(hz);
            juce::Path ring;
            ring.addEllipse(juce::Rectangle<float>(2.0f * rr, 2.0f * rr).withCentre(mid));
            juce::Path dashed;
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, ring, dots, 2);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.fillPath(dashed);
        }

        // The petals (an image draws at the current colour's alpha: full).
        if (image_.isValid())
        {
            g.setOpacity(1.0f);
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
            g.drawImage(image_, imageBounds(), juce::RectanglePlacement::stretchToFit);
        }

        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        g.setColour(c.textTertiary);
        for (const auto& [hz, text] : marks)
        {
            const auto at = mid.getPointOnCircumference(ringRadius(hz), juce::degreesToRadians(7.0f));
            g.drawText(text,
                       juce::Rectangle<float>(at.x + 3.0f, at.y - 6.0f, 30.0f, 12.0f),
                       juce::Justification::centredLeft);
        }

        drawShimmer(g, tint, rIn, rOut);
        drawRim(g, tint);

        // Now, at the top.
        g.setColour(c.textPrimary);
        g.drawLine(mid.x, mid.y - r + 2.0f, mid.x, mid.y - r * 0.85f, 1.5f);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({mid.x, mid.y - r + 2.0f}));

        drawCorners(g, tint);
    }

private:
    juce::Rectangle<float> imageBounds() const
    {
        const float r = radius();
        return juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(centre());
    }

    float bandRadius(float f) const { return innerRadius() + (outerRadius() - innerRadius()) * f; }

    // Each pixel of the image's slice and band (-1 outside the petals and in
    // the hairline gaps between them), worked out once per size.
    void buildLookup()
    {
        const float r = radius();
        const int size = std::max(1, juce::roundToInt(2.0f * r * imageScale));
        image_ = juce::Image(juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
        lookup_.assign(static_cast<size_t>(size * size), -1);
        const float rIn = innerRadius() * imageScale, rOut = outerRadius() * imageScale;
        const float half = static_cast<float>(size) * 0.5f;
        const float sliceDegrees = sweepDegrees / slices;
        const float gapDegrees = 0.7f, gapPixels = 1.2f * imageScale;
        const float bandPixels = (rOut - rIn) / bands;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - half,
                            dy = static_cast<float>(y) + 0.5f - half;
                const float rr = std::sqrt(dx * dx + dy * dy);
                if (rr < rIn || rr >= rOut)
                    continue;
                float degrees = juce::radiansToDegrees(std::atan2(dx, -dy));
                if (degrees < 0.0f)
                    degrees += 360.0f;
                if (degrees < sweepFrom || degrees >= sweepFrom + sweepDegrees)
                    continue;
                const float sf = (degrees - sweepFrom) / sliceDegrees;
                const int s = static_cast<int>(sf);
                // The gap's width in degrees narrows toward the rim so it stays a hairline.
                if ((sf - static_cast<float>(s)) * sliceDegrees >
                    sliceDegrees - gapDegrees * (rIn / rr + 0.4f))
                    continue;
                const float bf = (rr - rIn) / bandPixels;
                const int b = static_cast<int>(bf);
                if ((bf - static_cast<float>(b)) * bandPixels > bandPixels - gapPixels)
                    continue;
                lookup_[static_cast<size_t>(y * size + x)] =
                    std::clamp(s, 0, slices - 1) * bands + std::clamp(b, 0, bands - 1);
            }
        // The colours from nothing to the brightest: the tint fading in, its top
        // end warming toward the text colour; ice for a held tail.
        const auto& theme = card_.theme();
        const auto fill = [&](std::array<juce::PixelARGB, 64>& palette, juce::Colour tint)
        {
            for (size_t i = 0; i < palette.size(); ++i)
            {
                const float v = static_cast<float>(i) / static_cast<float>(palette.size() - 1);
                auto colour = v > 0.8f
                                  ? tint.interpolatedWith(theme.colours.textPrimary, (v - 0.8f) / 0.2f * 0.5f)
                                  : tint;
                palette[i] = colour.withAlpha(std::min(0.92f, std::pow(v, 1.4f) * 0.95f)).getPixelARGB();
            }
        };
        fill(palette_, card_.colour());
        fill(icePalette_, iceOf(theme));
    }

    void render()
    {
        if (!image_.isValid() || lookup_.empty())
            return;
        juce::Image::BitmapData data(image_, juce::Image::BitmapData::writeOnly);
        const int size = image_.getWidth();
        const juce::PixelARGB clear(0, 0, 0, 0);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                auto* pixel = reinterpret_cast<juce::PixelARGB*>(data.getPixelPointer(x, y));
                const int index = lookup_[static_cast<size_t>(y * size + x)];
                if (index < 0)
                {
                    pixel->set(clear);
                    continue;
                }
                // Slice 0 is the oldest, the ring's slot after the newest.
                const int s = index / bands, b = index % bands;
                const int slot = (head_ + s) % slices;
                const float v = levels_[static_cast<size_t>(slot * bands + b)];
                const auto& palette = held_[static_cast<size_t>(slot)] != 0 ? icePalette_ : palette_;
                pixel->set(palette[static_cast<size_t>(std::clamp(static_cast<int>(v * 63.0f), 0, 63))]);
            }
    }

    // Each band's frequency span as FFT bins.
    void mapBands(double sampleRate)
    {
        const double binHz = sampleRate / fftSize;
        bandBins_.resize(static_cast<size_t>(bands));
        for (int b = 0; b < bands; ++b)
        {
            const auto hzAt = [&](double f) { return lowestHz * std::pow(highestHz / lowestHz, f); };
            bandBins_[static_cast<size_t>(b)] = {hzAt(static_cast<double>(b) / bands) / binHz,
                                                 hzAt(static_cast<double>(b + 1) / bands) / binHz};
        }
        bandsFor_ = sampleRate;
    }

    // The spectrum of fftSize samples into column_ (each band's level, 0..1).
    void spectrum(const float* samples)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData_[static_cast<size_t>(i)] = samples[i] * window_[static_cast<size_t>(i)];
        std::fill(fftData_.begin() + fftSize, fftData_.end(), 0.0f);
        fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
        const double reference = fftSize / 4.0; // a full-scale sine through the Hann window
        for (size_t b = 0; b < bandBins_.size(); ++b)
        {
            const auto [b0, b1] = bandBins_[b];
            // The bins' mean power, so the wide top bands read as level, not their loudest bin.
            double power = 0.0;
            int count = 0;
            for (int i = static_cast<int>(b0); i <= static_cast<int>(b1) && i < fftSize / 2; ++i, ++count)
                power +=
                    static_cast<double>(fftData_[static_cast<size_t>(i)]) * fftData_[static_cast<size_t>(i)];
            const double m = std::sqrt(power / std::max(1, count));
            const float db = static_cast<float>(engine::gainToDb(m / reference, engine::magnitudeFloorDb));
            column_[b] = juce::jlimit(0.0f, 1.0f, (db - fieldFloorDb) / (fieldTopDb - fieldFloorDb));
        }
    }

    // `count` new slices for `fresh` new samples, each the spectrum of a window
    // ending its share of the way into the new audio.
    void addSlices(const engine::ProcessorFeedback& fb, int count, std::uint64_t fresh)
    {
        if (bandsFor_ != fb.sampleRate())
            mapBands(fb.sampleRate());
        const int span = static_cast<int>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(fftSize) + fresh, recent_.size()));
        const bool read = fb.readTap(1, recent_.data(), span);
        const auto held = static_cast<std::uint8_t>(frozen(card_) ? 1 : 0);
        for (int s = 0; s < count; ++s)
        {
            if (read)
                spectrum(recent_.data() + fftSize + (span - fftSize) * (s + 1) / count - fftSize);
            else
                std::fill(column_.begin(), column_.end(), 0.0f);
            std::copy(column_.begin(), column_.end(), levels_.begin() + head_ * bands);
            held_[static_cast<size_t>(head_)] = held;
            head_ = (head_ + 1) % slices;
        }
    }

    // The angle (radians, clockwise from the top) of slice s (0 the oldest), at its middle.
    static float sliceAngle(float s)
    {
        return juce::degreesToRadians(sweepFrom + sweepDegrees * (s + 0.5f) / slices);
    }

    // Shimmer's ladder: dotted spirals climbing outward (falling inward for -12),
    // as strong as the shimmer; gone while frozen (the shimmer leaves the loop).
    void drawShimmer(juce::Graphics& g, juce::Colour tint, float rIn, float rOut) const
    {
        const float amount = card_.param(pb::shimmer) * 0.01f;
        if (amount < 0.02f || frozen(card_))
            return;
        const int pitch = static_cast<int>(std::lround(card_.param(pb::pitch)));
        const bool down = pitch == 0, both = pitch == pb::bothOctaves;
        const auto mid = centre();
        const float dots[] = {1.0f, 4.0f};
        for (int k = 0; k < 3; ++k)
        {
            const bool inward = down || (both && k == 1);
            juce::Path spiral;
            for (int i = 0; i <= 40; ++i)
            {
                const float t = static_cast<float>(i) / 40.0f;
                const float rr = rIn + 20.0f + (rOut - rIn - 30.0f) * (inward ? 1.0f - t : t);
                const auto p =
                    mid.getPointOnCircumference(rr, juce::degreesToRadians(40.0f + 120.0f * k + 88.0f * t));
                if (i == 0)
                    spiral.startNewSubPath(p);
                else
                    spiral.lineTo(p);
            }
            juce::Path dashed;
            juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
                .createDashedStroke(dashed, spiral, dots, 2);
            g.setColour(tint.withAlpha(0.2f + 0.45f * amount));
            g.fillPath(dashed);
        }
    }

    // The tail's level as a band round the rim (-60..0 dB), and each slice's dry
    // input as a tick outside it.
    void drawRim(juce::Graphics& g, juce::Colour tint)
    {
        const auto& c = card_.theme().colours;
        collect();
        const auto mid = centre();
        const float r = radius();
        const float base = r * 0.906f, depth = r * 0.047f;
        const auto level = [](float linear)
        { return juce::jlimit(0.0f, 1.0f, (toDb(linear) - rimFloorDb) / -rimFloorDb); };
        if (!tail_.empty())
        {
            juce::Path band;
            for (int s = 0; s < slices; ++s)
            {
                const auto p = mid.getPointOnCircumference(
                    base + depth * level(tail_[static_cast<size_t>(s)]), sliceAngle(static_cast<float>(s)));
                if (s == 0)
                    band.startNewSubPath(p);
                else
                    band.lineTo(p);
            }
            for (int s = slices - 1; s >= 0; --s)
                band.lineTo(mid.getPointOnCircumference(base, sliceAngle(static_cast<float>(s))));
            band.closeSubPath();
            g.setColour(tint.withAlpha(0.85f));
            g.fillPath(band);
            g.setColour(c.textSecondary);
            for (int s = 0; s < slices; ++s)
            {
                const float d = level(dry_[static_cast<size_t>(s)]);
                if (d < 0.2f)
                    continue;
                const float a = sliceAngle(static_cast<float>(s));
                g.drawLine(juce::Line<float>(mid.getPointOnCircumference(r * 0.968f, a),
                                             mid.getPointOnCircumference(r * (0.968f + 0.025f * d), a)),
                           1.5f);
            }
        }
        // The rim's track, faint.
        g.setColour(c.textPrimary.withAlpha(0.06f));
        g.drawEllipse(juce::Rectangle<float>(2.0f * base, 2.0f * base).withCentre(mid), 1.0f);
    }

    // Per slice: the largest RMS of its frames, counted from the start of the
    // history so the slices keep their frames as the flower turns.
    void collect()
    {
        const auto total = card_.historyTotal();
        if (total == 0)
        {
            tail_.clear();
            dry_.clear();
            return;
        }
        tail_.assign(static_cast<size_t>(slices), 0.0f);
        dry_.assign(static_cast<size_t>(slices), 0.0f);
        const auto perSlice =
            static_cast<std::uint64_t>(std::max(1L, std::lround(fieldSeconds * framesPerSecond / slices)));
        const int size = card_.historySize();
        const auto first = total - static_cast<std::uint64_t>(size);
        const auto newest = (total - 1) / perSlice;
        for (int s = 0; s < slices; ++s)
        {
            const auto fromNow = static_cast<std::uint64_t>(slices - 1 - s);
            if (fromNow > newest)
                continue;
            const auto bucket = newest - fromNow;
            for (auto a = bucket * perSlice; a < (bucket + 1) * perSlice && a < total; ++a)
            {
                if (a < first)
                    continue;
                const auto& f = card_.historyAt(static_cast<int>(a - first));
                tail_[static_cast<size_t>(s)] = std::max(tail_[static_cast<size_t>(s)], f.extra);
                dry_[static_cast<size_t>(s)] = std::max(dry_[static_cast<size_t>(s)], f.extra2);
            }
        }
    }

    // The readouts in the corners: the decay top left, the space top right, the
    // legend and the time bottom left and right.
    void drawCorners(juce::Graphics& g, juce::Colour tint) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        auto area = getLocalBounds().toFloat().reduced(2.0f, 0.0f);
        const auto caps = theme.captionCaps();
        const auto big =
            theme.font(Theme::Face::mono, theme.fonts.size.title + 1.0f, theme.fonts.weight.medium);
        const auto small = theme.value();
        const auto block = [&](bool right,
                               const juce::String& caption,
                               const juce::String& value,
                               const juce::String& detail)
        {
            const auto j = right ? juce::Justification::centredRight : juce::Justification::centredLeft;
            auto r = area;
            g.setFont(caps);
            g.setColour(c.textTertiary);
            g.drawText(caption, r.removeFromTop(13.0f), j);
            g.setFont(big);
            g.setColour(c.textPrimary);
            g.drawText(value, r.removeFromTop(22.0f), j);
            g.setFont(small);
            g.setColour(c.textSecondary);
            g.drawText(detail, r.removeFromTop(15.0f), j);
        };
        const float decay = card_.param(pb::decay);
        if (frozen(card_))
            block(false, "DECAY", infinity(), "held");
        else
            block(false,
                  "DECAY",
                  secondsText(decay),
                  secondsText(pb::decayAt(highsHz,
                                          decay,
                                          card_.param(pb::size),
                                          card_.param(pb::damping),
                                          card_.param(pb::lowCut))) +
                      " at 8 kHz");
        const double pre =
            pb::preDelaySeconds(card_.param(pb::preDelay), card_.param(pb::sync), card_.tempoBpm());
        block(true,
              "SPACE",
              juce::String(static_cast<int>(std::lround(pb::sizeMetres(card_.param(pb::size))))) + " m",
              (synced(card_) ? noteName(card_) : secondsText(pre)) + " before");

        // Bottom left: the legend; bottom right: what the flower spans.
        auto foot = area.removeFromBottom(14.0f);
        g.setFont(caps);
        g.setColour(tint);
        g.fillEllipse(foot.removeFromLeft(9.0f).withSizeKeepingCentre(9.0f, 9.0f));
        foot.removeFromLeft(5.0f);
        g.setColour(c.textTertiary);
        g.drawText("TAIL", foot.removeFromLeft(textWidth(caps, "TAIL")), juce::Justification::centredLeft);
        foot.removeFromLeft(10.0f);
        g.setColour(c.textSecondary);
        g.fillRect(foot.removeFromLeft(9.0f).withSizeKeepingCentre(9.0f, 2.0f));
        foot.removeFromLeft(5.0f);
        g.setColour(c.textTertiary);
        g.drawText("DRY", foot.removeFromLeft(textWidth(caps, "DRY")), juce::Justification::centredLeft);
        g.drawText(
            juce::String::fromUTF8("5 S \xc2\xb7 NOW AT THE TOP"), foot, juce::Justification::centredRight);
    }

    ModuleCard& card_;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_, fftData_, recent_;
    std::vector<std::pair<double, double>> bandBins_;
    double bandsFor_ = 0.0;
    std::vector<float> levels_, column_; // levels_: slices x bands, a ring from head_
    std::vector<std::uint8_t> held_;     // per slice: frozen when it was heard
    int head_ = 0;                       // the oldest slice's slot (the next written)
    std::vector<int> lookup_;
    juce::Image image_;
    std::array<juce::PixelARGB, 64> palette_{}, icePalette_{};
    std::uint64_t lastTap_ = 0;
    double carry_ = 0.0;
    std::vector<float> tail_, dry_; // per slice, reused
};

// ----- the envelope -------------------------------------------------------------------------

// The shape the settings give a single hit: the dry hit at 0, the pre-delay,
// the swell rising (or the tail starting at once), the decay falling 60 dB over
// the RT60 with the highs' faster fall dashed, the gate's cut, or a frozen tail
// holding to the edge.
class EnvelopeGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EnvelopeGraph(ModuleCard& card) : card_(card) {}

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        // Short (a tile beside the pills): no time labels, the curve fills the well.
        const bool compact = area.getHeight() < 56.0f;
        const auto p =
            compact
                ? area.reduced(5.0f, 4.0f)
                : area.reduced(1.0f).withTrimmedTop(10.0f).withTrimmedBottom(14.0f).withTrimmedRight(8.0f);
        const double pre =
            pb::preDelaySeconds(card_.param(pb::preDelay), card_.param(pb::sync), card_.tempoBpm());
        const float swellMs = card_.param(pb::swell);
        const double swell = swellMs > pb::swellOffMs + 0.5f ? swellMs * 0.001 : 0.0;
        const float decay = card_.param(pb::decay);
        const bool held = frozen(card_);
        const float gateMs = card_.param(pb::gate);
        const bool gated = gateMs > pb::gateOffMs + 0.5f && !held;
        const double gate = std::max(pre + 0.01, gateMs * 0.001);
        const float highs = pb::decayAt(
            highsHz, decay, card_.param(pb::size), card_.param(pb::damping), card_.param(pb::lowCut));
        double span = pre + swell + (held ? 2.0 : std::min(static_cast<double>(decay), 12.0)) * 1.1;
        if (gated)
            span = std::min(span, std::max(gate * 1.6, pre + swell + 0.2));
        span = juce::jlimit(0.25, 14.0, span);
        const auto x = [&](double t) { return p.getX() + static_cast<float>(t / span) * p.getWidth(); };
        const auto y = [&](double db) { return dbToY(static_cast<float>(db), 0.0f, floorDb, p); };
        const double peak = pre + swell;
        // The level at time t (dB), and the highs'.
        const auto level = [&](double t, double rt)
        {
            if (t < pre)
                return static_cast<double>(floorDb);
            if (gated && t > gate)
                return static_cast<double>(floorDb);
            if (t < peak)
            {
                const double f = (t - pre) / swell;
                return std::max(static_cast<double>(floorDb), 20.0 * 1.6 * std::log10(std::max(1.0e-4, f)));
            }
            return held ? 0.0 : std::max(static_cast<double>(floorDb), -60.0 * (t - peak) / rt);
        };
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(area.reduced(1.0f).toNearestInt());
            for (const float db : {-20.0f, -40.0f})
            {
                g.setColour(c.gridBeat);
                g.drawHorizontalLine(static_cast<int>(y(db)), p.getX(), p.getRight());
            }
            // The time across, at its end (in a short well, the span at the top right).
            g.setFont(
                theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
            g.setColour(c.textTertiary);
            if (compact && !held)
                g.drawText(secondsText(span),
                           juce::Rectangle<float>(p.getRight() - 60.0f, p.getY() - 1.0f, 60.0f, 11.0f),
                           juce::Justification::centredRight);
            else
            {
                g.drawText("0",
                           juce::Rectangle<float>(p.getX(), p.getBottom() + 2.0f, 20.0f, 11.0f),
                           juce::Justification::centredLeft);
                g.drawText(secondsText(span),
                           juce::Rectangle<float>(p.getRight() - 60.0f, p.getBottom() + 2.0f, 60.0f, 11.0f),
                           juce::Justification::centredRight);
            }
            g.reduceClipRegion(p.withTrimmedTop(-4.0f).toNearestInt());
            // The dry hit.
            g.setColour(c.textTertiary);
            g.fillRect(juce::Rectangle<float>(p.getX(), y(0.0), 2.0f, p.getBottom() - y(0.0)));
            // The tail.
            const int steps = std::max(2, static_cast<int>(p.getWidth()));
            juce::Path curve, fill, dashed;
            fill.startNewSubPath(x(pre), p.getBottom());
            for (int i = 0; i <= steps; ++i)
            {
                const double t = pre + (span - pre) * i / steps;
                const auto pt = juce::Point<float>(x(t), y(level(t, decay)));
                if (i == 0)
                    curve.startNewSubPath(pt);
                else
                    curve.lineTo(pt);
                fill.lineTo(pt);
                const auto ph = juce::Point<float>(x(t), y(level(t, highs)));
                if (t >= peak)
                {
                    if (dashed.isEmpty())
                        dashed.startNewSubPath(ph);
                    else
                        dashed.lineTo(ph);
                }
            }
            fill.lineTo(x(span), p.getBottom());
            fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(
                hue.withAlpha(0.32f), 0.0f, y(0.0), hue.withAlpha(0.04f), 0.0f, p.getBottom(), false));
            g.fillPath(fill);
            g.setColour(hue);
            g.strokePath(
                curve,
                juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (!held)
            {
                juce::Path dashes;
                const float pattern[] = {4.0f, 3.0f};
                juce::PathStrokeType(1.0f).createDashedStroke(dashes, dashed, pattern, 2);
                g.setColour(hue.brighter(0.5f).withAlpha(0.8f));
                g.fillPath(dashes);
            }
        }
        if (held)
        {
            // Held for good: the infinity sign at the end of the line.
            g.setFont(theme.value());
            g.setColour(hue);
            g.drawText(infinity(),
                       juce::Rectangle<float>(p.getRight() - 18.0f, y(0.0) + 2.0f, 16.0f, 13.0f),
                       juce::Justification::centredRight);
        }
        drawWellFrame(g, theme, area);
    }

private:
    static constexpr float floorDb = -60.0f;
    ModuleCard& card_;
};

// ----- the seed: Freeze ---------------------------------------------------------------------

// The blossom's heart: a round button with a snowflake and FREEZE; held, it
// turns to ice with a glow and reads FROZEN.
class SeedButton final : public juce::Button
{
public:
    SeedButton(const Theme& theme, juce::Colour hue) : juce::Button("Freeze"), theme_(theme), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setTitle("Freeze");
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Freeze");
    }

    // Space toggles as Enter does.
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (isEnabled() && key.isKeyCode(juce::KeyPress::spaceKey))
        {
            triggerClick();
            return true;
        }
        return juce::Button::keyPressed(key);
    }

    bool hitTest(int x, int y) override
    {
        return getLocalBounds().toFloat().getCentre().getDistanceFrom(
                   {static_cast<float>(x), static_cast<float>(y)}) <= getWidth() * 0.5f;
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto& c = theme_.colours;
        const bool on = getToggleState();
        const auto ice = iceOf(theme_);
        const auto bounds = getLocalBounds().toFloat();
        const auto seed = bounds.reduced(8.0f);
        const auto mid = seed.getCentre();
        // The glow round it: a dark moat with the hue beyond, or ice while held.
        g.setGradientFill(juce::ColourGradient((on ? ice : hue_).withAlpha(on ? 0.45f : 0.22f),
                                               mid.x,
                                               mid.y,
                                               (on ? ice : hue_).withAlpha(0.0f),
                                               bounds.getRight(),
                                               mid.y,
                                               true));
        g.fillEllipse(bounds);
        if (!on)
        {
            g.setColour(c.canvas.withAlpha(0.9f));
            g.fillEllipse(seed.expanded(5.0f));
        }
        // The body.
        auto top = on ? ice.brighter(0.25f) : c.canvas.interpolatedWith(hue_, 0.14f);
        auto bottom = on ? ice.darker(0.15f) : c.canvas.interpolatedWith(hue_, 0.05f);
        if (over && !down)
            top = top.brighter(0.12f);
        if (down)
            top = top.darker(0.15f);
        g.setGradientFill(juce::ColourGradient(
            top, mid.x, seed.getY() + seed.getHeight() * 0.35f, bottom, mid.x, seed.getBottom(), true));
        g.fillEllipse(seed);
        g.setColour(on ? ice.brighter(0.4f) : hue_.withAlpha(over ? 0.7f : 0.4f));
        g.drawEllipse(seed.reduced(0.5f), 1.0f);

        const auto ink = on ? c.canvas : (over ? c.textPrimary : c.textPrimary.withAlpha(0.85f));
        g.setColour(ink);
        g.strokePath(snowflake({mid.x, mid.y - seed.getHeight() * 0.1f}, seed.getWidth() * 0.15f),
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(theme_.captionCaps());
        g.drawText(
            on ? "FROZEN" : "FREEZE",
            juce::Rectangle<float>(seed.getX(), mid.y + seed.getHeight() * 0.12f, seed.getWidth(), 14.0f),
            juce::Justification::centred);
        if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
            laf->drawFocusRing(g, *this, seed, seed.getWidth() * 0.5f);
    }

private:
    const Theme& theme_;
    juce::Colour hue_;
};

// ----- the card -----------------------------------------------------------------------------

class BloomCard final : public ModuleCard
{
public:
    BloomCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), field_(*this), envelope_(*this), seed_(theme, colour()),
          pitch_(theme, colour(), "Shimmer interval"), sync_(theme, colour())
    {
        addAndMakeVisible(field_);
        addAndMakeVisible(envelope_);

        // The controls that make the tail.
        decay_ = &addKnob(pb::decay);
        decay_->format = [](double s) { return secondsText(s); };
        size_ = &addKnob(pb::size);
        size_->format = [](double v)
        { return juce::String(static_cast<int>(std::lround(pb::sizeMetres(static_cast<float>(v))))) + " m"; };
        shimmer_ = &addKnob(pb::shimmer);
        swell_ = &addKnob(pb::swell);
        swell_->format = [](double ms)
        { return ms <= pb::swellOffMs + 0.5 ? juce::String("Off") : secondsText(ms * 0.001); };
        for (auto* k : {decay_, size_, shimmer_, swell_})
            k->setRingSize(bigRing);

        seed_.onClick = [this]
        {
            beginGesture("Freeze");
            setParam(pb::freeze, frozen(*this) ? 0.0f : 1.0f);
            endGesture();
        };
        addAndMakeVisible(seed_);

        pitch_.labels = {juce::String::fromUTF8("\xe2\x88\x92"
                                                "12"),
                         "+5",
                         "+7",
                         "+12",
                         "+19",
                         juce::String::fromUTF8("\xc2\xb1"
                                                "12")};
        pitch_.setComponentID("interval");
        pitch_.onChange = [this](int value)
        {
            beginGesture("Pitch");
            setParam(pb::pitch, static_cast<float>(value));
            endGesture();
        };
        addAndMakeVisible(pitch_);

        // The rest, in groups.
        const auto knob = [this](int index, const juce::String& label) { return &addKnob(index, label); };
        preDelay_ = knob(pb::preDelay, "Pre-delay");
        preDelay_->format = [this, plain = preDelay_->format](double v)
        { return synced(*this) ? noteName(*this) : plain(v); };
        diffusion_ = knob(pb::diffusion, "Diffusion");
        modulation_ = knob(pb::modulation, "Motion");
        rate_ = knob(pb::modRate, "Rate");
        rate_->format = [](double hz) { return juce::String(hz, hz < 1.0 ? 2 : 1) + " Hz"; };
        lowCut_ = knob(pb::lowCut, "Low cut");
        damping_ = knob(pb::damping, "Damping");
        damping_->format = [](double hz) { return compactHzText(hz); };
        tilt_ = knob(pb::tilt, "Tilt");
        tilt_->format = [](double db) { return dbText(static_cast<float>(db)); };
        duck_ = knob(pb::duck, "Duck");
        duckRelease_ = knob(pb::duckRelease, "Release");
        gate_ = knob(pb::gate, "Gate");
        gate_->format = [](double ms)
        { return ms <= pb::gateOffMs + 0.5 ? juce::String("Off") : secondsText(ms * 0.001); };
        width_ = knob(pb::width, "Width");
        mix_ = knob(pb::mix, "Mix");

        // Every knob is a bud in the hue.
        for (auto* k : {decay_,
                        size_,
                        shimmer_,
                        swell_,
                        preDelay_,
                        diffusion_,
                        modulation_,
                        rate_,
                        lowCut_,
                        damping_,
                        tilt_,
                        duck_,
                        duckRelease_,
                        gate_,
                        width_,
                        mix_})
            k->setStyle(Knob::Style::bud);

        for (const auto& c : plugins::sync::choices())
            sync_.choices.emplace_back(c);
        sync_.choices.front() = "Sync off";
        sync_.setTooltip("Tempo sync");
        sync_.onChoose = [this](int i)
        {
            beginGesture("Pre-delay sync");
            setParam(pb::sync, static_cast<float>(i));
            endGesture();
        };
        addAndMakeVisible(sync_);
        paramsChanged();
    }

    int preferredWidth() const override
    {
        const int side = preferredBodyHeight() - 2 * theme_.spacing.xs;
        return juce::roundToInt(side * BlossomField::widthRatio) + columnGap + controlsWidth +
               2 * (theme_.spacing.sm + theme_.spacing.xs);
    }
    int preferredBodyHeight() const override { return 316; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        const auto& c = theme_.colours;
        // The face: a little deeper than the card, the tiles raised on it.
        g.setColour(c.surface0);
        g.fillRoundedRectangle(face_.toFloat(), theme_.radius.md);
        for (const auto& tile : tiles_)
        {
            g.setColour(c.surface1);
            g.fillRoundedRectangle(tile.toFloat(), theme_.radius.lg);
        }
        g.setFont(theme_.captionCaps());
        for (const auto& caption : captions_)
        {
            g.setColour(c.textTertiary);
            g.drawText(caption.text, caption.area, juce::Justification::centredLeft);
            if (caption.rule)
            {
                const float x = caption.area.getX() + textWidth(theme_.captionCaps(), caption.text) + 8.0f;
                const float right = caption.ruleEnd > 0.0f ? caption.ruleEnd : caption.area.getRight();
                if (right > x)
                {
                    g.setColour(c.textPrimary.withAlpha(0.08f));
                    g.fillRect(juce::Rectangle<float>(x, caption.area.getCentreY(), right - x, 1.0f));
                }
            }
        }
    }

private:
    static constexpr int bigRing = 52;
    static constexpr int columnGap = 20;
    static constexpr int smallCellWidth = 50;
    static constexpr int groupGap = 14;
    static constexpr int controlsWidth = 12 * smallCellWidth + 3 * groupGap;

    void layoutBody(juce::Rectangle<int> body) override
    {
        face_ = body.expanded(theme_.spacing.sm, theme_.spacing.xs);
        tiles_.clear();
        captions_.clear();
        auto area = body.reduced(theme_.spacing.xs, theme_.spacing.xs);

        // The blossom: as tall as the body, its field a little wider for the
        // readouts in its corners; the seed at its heart.
        const int side = std::min(
            area.getHeight(),
            juce::roundToInt((area.getWidth() - controlsWidth - columnGap) / BlossomField::widthRatio));
        const int fieldW = juce::roundToInt(side * BlossomField::widthRatio);
        field_.setBounds(area.removeFromLeft(fieldW).withSizeKeepingCentre(fieldW, side));
        {
            const int d = juce::roundToInt(field_.innerRadius() * 2.0f * 0.84f) + 16;
            seed_.setBounds(juce::Rectangle<int>(d, d).withCentre(field_.getBounds().getCentre()));
        }
        area.removeFromLeft(columnGap);
        auto column = area.removeFromLeft(std::min(controlsWidth, area.getWidth()));

        const int gap = theme_.spacing.md;

        // Four tiles: the big knobs, each with its value and name.
        {
            // The knob's stack ends `spacing.sm` above the tile's foot, as far as
            // the ring is from its top.
            const int tileH = theme_.spacing.sm + knobCellHeight(bigRing, theme_.fonts.size.label);
            auto row = column.removeFromTop(tileH);
            const int tileW = (row.getWidth() - 3 * gap) / 4;
            for (auto* k : {decay_, size_, shimmer_, swell_})
            {
                auto tile = row.removeFromLeft(tileW);
                row.removeFromLeft(gap);
                tiles_.push_back(tile);
                if (auto* label = labelFor(*k))
                    label->setFont(theme_.label());
                placeKnob(*k,
                          tile.reduced(theme_.spacing.xs, 0).withTrimmedTop(theme_.spacing.sm),
                          theme_.fonts.size.label);
            }
        }
        column.removeFromTop(gap);

        // The interval pills beside the shape of one hit.
        {
            const int tileH = 58;
            auto row = column.removeFromTop(tileH);
            auto shape = row.removeFromRight(196);
            row.removeFromRight(gap);
            tiles_.push_back(row);
            tiles_.push_back(shape);
            auto pills = row.reduced(theme_.spacing.md, theme_.spacing.sm);
            captions_.push_back({"SHIMMER INTERVAL", pills.removeFromTop(14).toFloat(), false, 0.0f});
            pitch_.setBounds(pills.withSizeKeepingCentre(pills.getWidth(), 26));
            auto inner = shape.reduced(theme_.spacing.md, theme_.spacing.sm);
            captions_.push_back({"ONE HIT", inner.removeFromTop(14).toFloat(), false, 0.0f});
            envelope_.setBounds(inner.withTrimmedTop(2));
        }
        column.removeFromTop(gap + theme_.spacing.xs);

        // The rest in one row of groups, a caption and hairline over each.
        const int captionH = 16;
        struct Group
        {
            const char* title;
            std::vector<Knob*> knobs;
        };
        const std::array<Group, 4> groups{Group{"SPACE", {preDelay_, diffusion_, modulation_, rate_}},
                                          Group{"TONE", {lowCut_, damping_, tilt_}},
                                          Group{"DYNAMICS", {duck_, duckRelease_, gate_}},
                                          Group{"OUTPUT", {width_, mix_}}};
        auto row = column.removeFromTop(captionH + theme_.spacing.xs + knobCellHeight());
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (i > 0)
                row.removeFromLeft(groupGap);
            const int count = static_cast<int>(groups[i].knobs.size());
            auto cell = row.removeFromLeft(count * smallCellWidth);
            auto caption = cell.removeFromTop(captionH);
            float ruleEnd = 0.0f;
            if (i == 0)
            {
                // The pre-delay's sync chip at the caption's right end.
                const int w = std::min(caption.getWidth() / 2, sync_.preferredWidth());
                sync_.setBounds(caption.removeFromRight(w).withSizeKeepingCentre(w, 16));
                ruleEnd = static_cast<float>(sync_.getX() - 8);
            }
            captions_.push_back({groups[i].title, caption.toFloat(), true, ruleEnd});
            cell.removeFromTop(theme_.spacing.xs);
            layoutKnobs(cell, count, groups[i].knobs);
        }
    }

    void paramsChanged() override
    {
        pitch_.selected = static_cast<int>(std::lround(param(pb::pitch)));
        pitch_.repaint();
        const bool held = frozen(*this);
        seed_.setToggleState(held, juce::dontSendNotification);
        // Frozen, the shimmer leaves the loop: its controls stand aside.
        shimmer_->setAlpha(held ? 0.5f : 1.0f);
        pitch_.setAlpha(held ? 0.5f : 1.0f);
        sync_.selected = plugins::sync::choiceOf(param(pb::sync));
        sync_.repaint();
        preDelay_->setAlpha(synced(*this) ? 0.5f : 1.0f);
        preDelay_->repaint();
        field_.repaint();
        envelope_.repaint();
    }
    void framesArrived(double) override
    {
        field_.advance();
        // A synced pre-delay follows the tempo.
        if (const double bpm = tempoBpm(); bpm != shownTempo_)
        {
            shownTempo_ = bpm;
            envelope_.repaint();
        }
    }
    juce::String headerStatus() const override
    {
        if (isOn() && frozen(*this))
            return "Frozen";
        const float db = reductionReadoutDb();
        return isOn() && db >= 0.1f ? "Duck -" + juce::String(db, 1) + " dB" : juce::String();
    }
    juce::Colour headerStatusColour() const override
    {
        return frozen(*this) ? iceOf(theme_) : theme_.colours.textSecondary;
    }

    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
        bool rule = false;
        float ruleEnd = 0.0f; // where the hairline stops (0: the caption's right edge)
    };

    BlossomField field_;
    EnvelopeGraph envelope_;
    SeedButton seed_;
    PillRow pitch_;
    CornerChoice sync_;
    Knob *decay_ = nullptr, *size_ = nullptr, *shimmer_ = nullptr, *swell_ = nullptr;
    Knob *preDelay_ = nullptr, *diffusion_ = nullptr, *modulation_ = nullptr, *rate_ = nullptr;
    Knob *lowCut_ = nullptr, *damping_ = nullptr, *tilt_ = nullptr;
    Knob *duck_ = nullptr, *duckRelease_ = nullptr, *gate_ = nullptr;
    Knob *width_ = nullptr, *mix_ = nullptr;
    juce::Rectangle<int> face_;
    std::vector<juce::Rectangle<int>> tiles_;
    std::vector<Caption> captions_;
    double shownTempo_ = 0.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createBloomCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<BloomCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
