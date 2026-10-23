// Bloom's card (screens/plugins.md 7.2a): the tail as a living thing. The tail
// field is a spectrogram of the reverb on its own over the last five seconds
// (so shimmer climbing, damping darkening and a frozen tail holding are all
// visible), above an energy lane with the dry input in grey under the tail in
// the hue, with the decay, size and pre-delay read out and Freeze marked. Under
// it the defining controls as large knobs (Decay, Size, the Freeze latch,
// Shimmer with its Pitch selector, Swell beside the envelope it shapes), then
// the secondary controls in four captioned groups: Space, Tone, Dynamics, Output.

#include "plugins/BeatSync.h"
#include "plugins/Bloom.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/RotarySelector.h"
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
constexpr double fieldSeconds = 5.0; // the history holds a little more
constexpr float laneTopDb = 0.0f, laneBottomDb = -60.0f;
constexpr double lowestHz = 50.0, highestHz = 16000.0;
// The spectrogram's level range: a bin's level (dB, a full-scale sine at 0) from
// nothing to the brightest.
constexpr float fieldFloorDb = -96.0f, fieldTopDb = -24.0f;
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

juce::String formatSeconds(double s)
{
    if (s < 1.0)
        return juce::String(static_cast<int>(std::lround(s * 1000.0))) + " ms";
    return juce::String(s, s < 10.0 ? 1 : 0) + " s";
}

juce::String infinity()
{
    return juce::String::fromUTF8("\xe2\x88\x9e");
}

float textWidth(const juce::Font& font, const juce::String& text)
{
    return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text));
}

// ----- the tail field -------------------------------------------------------------------------

class TailField final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr float imageScale = 2.0f;
    static constexpr int maxSpectra = 16; // spectra a tick at most (a slow tick blends between them)

    explicit TailField(ModuleCard& card) : card_(card)
    {
        setTooltip("The tail over the last five seconds: its spectrum above (higher is higher in pitch, "
                   "brighter is louder), its level below in colour over the dry input in grey. Shimmer "
                   "climbs, damping darkens the top, a frozen tail holds (marked along the top).");
        window_.resize(fftSize);
        for (int i = 0; i < fftSize; ++i)
            window_[static_cast<size_t>(i)] =
                0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) /
                                       static_cast<float>(fftSize - 1));
        fftData_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
        recent_.assign(static_cast<size_t>(engine::ProcessorFeedback::tapLength / 2), 0.0f);
        setInterceptsMouseClicks(true, false);
    }

    // New audio arrived: the spectrogram moves on by the columns it covers.
    void advance()
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr || !image_.isValid())
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
        const double perColumn = fieldSeconds * fb->sampleRate() / image_.getWidth();
        const auto fresh = position - lastTap_;
        carry_ += static_cast<double>(fresh) / perColumn;
        lastTap_ = position;
        const int columns = std::min(image_.getWidth(), static_cast<int>(carry_));
        carry_ -= std::floor(carry_);
        if (columns > 0)
            addColumns(*fb, columns, fresh);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto field = fieldArea(), lane = laneArea();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(area.reduced(1.0f).toNearestInt());
            g.drawImage(image_, field, juce::RectanglePlacement::stretchToFit);
            drawFrozenMarks(g, field);
            // The lane, under a hairline across the well.
            g.setColour(c.lineSubtle);
            g.drawHorizontalLine(static_cast<int>(lane.getY()) - 1, plot().getX(), plot().getRight());
            drawLane(g, lane);
            drawScales(g, field, lane);
            drawReadouts(g, field);
            if (frozen(card_))
                drawFrozenBadge(g, field, hue);
        }
        drawWellFrame(g, theme, area);
    }

    // The image at twice the field's size, so it stays crisp up to 200%, made
    // here rather than in paint so it fills while nothing is painting it.
    void resized() override
    {
        const auto field = fieldArea();
        const int w = std::max(1, juce::roundToInt(field.getWidth() * imageScale));
        const int h = std::max(1, juce::roundToInt(field.getHeight() * imageScale));
        if (!image_.isValid() || image_.getWidth() != w || image_.getHeight() != h)
            resizeImage(w, h);
    }

private:
    // The well's inside; the field and the lane share its time axis, left of a
    // gutter that holds their scales.
    static constexpr float gutter = 30.0f;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(1.0f); }
    // The lane takes a third, between 24 and 56 px; the field the rest.
    float laneHeight() const { return std::round(juce::jlimit(24.0f, 56.0f, plot().getHeight() * 0.32f)); }
    juce::Rectangle<float> fieldArea() const
    {
        return plot().withTrimmedRight(gutter).withTrimmedBottom(laneHeight() + 1.0f);
    }
    juce::Rectangle<float> laneArea() const
    {
        auto p = plot().withTrimmedRight(gutter);
        return p.removeFromBottom(laneHeight());
    }

    void resizeImage(int w, int h)
    {
        // What has been drawn so far is stretched to the new size, not lost. A
        // software image: it scrolls in place and is written pixel by pixel.
        juce::Image resized(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        std::vector<std::uint8_t> frozen(static_cast<size_t>(w), 0);
        if (image_.isValid())
        {
            juce::Graphics g(resized);
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
            g.drawImage(image_, resized.getBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
            for (int x = 0; x < w; ++x)
                frozen[static_cast<size_t>(x)] =
                    frozenColumns_[static_cast<size_t>(x * image_.getWidth() / w)];
        }
        image_ = resized;
        frozenColumns_ = std::move(frozen);
        column_.assign(static_cast<size_t>(h), 0.0f);
        previous_.assign(static_cast<size_t>(h), 0.0f);
        rowsFor_ = 0.0;
        // The colours from nothing to the brightest: the hue fading in, the top
        // end warming toward the text colour.
        const auto hue = card_.colour();
        const auto top = card_.theme().colours.textPrimary;
        for (int i = 0; i < static_cast<int>(palette_.size()); ++i)
        {
            const float v = static_cast<float>(i) / static_cast<float>(palette_.size() - 1);
            auto colour = hue.withAlpha(std::pow(v, 1.6f) * 0.9f);
            if (v > 0.8f)
                colour =
                    hue.interpolatedWith(top, (v - 0.8f) / 0.2f * 0.5f).withAlpha(std::pow(v, 1.6f) * 0.9f);
            palette_[static_cast<size_t>(i)] = colour;
        }
    }

    // Each image row's frequency span as FFT bins (worked out once per size and rate).
    void mapRows(double sampleRate)
    {
        const int h = image_.getHeight();
        rows_.resize(static_cast<size_t>(h));
        const double binHz = sampleRate / fftSize;
        const auto hzAt = [&](double y) // y 0 = the top edge
        { return highestHz * std::pow(lowestHz / highestHz, y / static_cast<double>(h)); };
        for (int y = 0; y < h; ++y)
            rows_[static_cast<size_t>(y)] = {hzAt(y + 1.0) / binHz, hzAt(static_cast<double>(y)) / binHz};
        rowsFor_ = sampleRate;
    }

    // The spectrum of fftSize samples into column_ (each row's level, 0..1).
    void spectrum(const float* samples)
    {
        for (int i = 0; i < fftSize; ++i)
            fftData_[static_cast<size_t>(i)] = samples[i] * window_[static_cast<size_t>(i)];
        std::fill(fftData_.begin() + fftSize, fftData_.end(), 0.0f);
        fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
        const double reference = fftSize / 4.0; // a full-scale sine through the Hann window
        const auto magnitude = [&](double bin)
        {
            const int i = juce::jlimit(0, fftSize / 2 - 1, static_cast<int>(bin));
            const double t = juce::jlimit(0.0, 1.0, bin - i);
            return fftData_[static_cast<size_t>(i)] * (1.0 - t) + fftData_[static_cast<size_t>(i + 1)] * t;
        };
        for (size_t y = 0; y < rows_.size(); ++y)
        {
            const auto [b0, b1] = rows_[y];
            double m = 0.0;
            if (b1 - b0 < 1.0)
                m = magnitude(0.5 * (b0 + b1)); // narrower than a bin: between its neighbours
            else
            {
                // Wider than a bin: the bins' mean power, so the top rows (many
                // bins each) read as level, not as their loudest bin.
                double power = 0.0;
                int count = 0;
                for (int b = static_cast<int>(b0); b <= static_cast<int>(b1) && b < fftSize / 2; ++b, ++count)
                    power += static_cast<double>(fftData_[static_cast<size_t>(b)]) *
                             fftData_[static_cast<size_t>(b)];
                m = std::sqrt(power / std::max(1, count));
            }
            const float db = static_cast<float>(20.0 * std::log10(std::max(1.0e-9, m / reference)));
            column_[y] = juce::jlimit(0.0f, 1.0f, (db - fieldFloorDb) / (fieldTopDb - fieldFloorDb));
        }
    }

    // `columns` new columns for `fresh` new samples: up to maxSpectra spectra
    // spread through the new audio, each column between the two nearest.
    void addColumns(const engine::ProcessorFeedback& fb, int columns, std::uint64_t fresh)
    {
        if (rowsFor_ != fb.sampleRate())
            mapRows(fb.sampleRate());
        const int h = image_.getHeight(), w = image_.getWidth();
        const int spectra = std::clamp(columns, 1, maxSpectra);
        const int span = static_cast<int>(
            std::min<std::uint64_t>(static_cast<std::uint64_t>(fftSize) + fresh, recent_.size()));
        const bool read = fb.readTap(1, recent_.data(), span);
        // Scroll, then paint the new columns.
        if (columns < w)
            image_.moveImageSection(0, 0, columns, 0, w - columns, h);
        std::rotate(frozenColumns_.begin(), frozenColumns_.begin() + columns, frozenColumns_.end());
        const auto held = static_cast<std::uint8_t>(frozen(card_) ? 1 : 0);
        juce::Image::BitmapData data(image_, juce::Image::BitmapData::writeOnly);
        int column = 0;
        for (int s = 0; s < spectra; ++s)
        {
            if (read)
            {
                // This spectrum's window ends this far into the new audio.
                const int end = fftSize + (span - fftSize) * (s + 1) / spectra;
                spectrum(recent_.data() + end - fftSize);
            }
            else
                std::fill(column_.begin(), column_.end(), 0.0f);
            const int until = columns * (s + 1) / spectra;
            const int count = std::max(1, until - column);
            for (int k = 0; column < until; ++column, ++k)
            {
                const int x = w - columns + column;
                const float t = static_cast<float>(k + 1) / static_cast<float>(count);
                frozenColumns_[static_cast<size_t>(x)] = held;
                for (int y = 0; y < h; ++y)
                {
                    const float v = previous_[static_cast<size_t>(y)] +
                                    (column_[static_cast<size_t>(y)] - previous_[static_cast<size_t>(y)]) * t;
                    const auto index =
                        static_cast<size_t>(juce::jlimit(0,
                                                         static_cast<int>(palette_.size()) - 1,
                                                         static_cast<int>(v * (palette_.size() - 1))));
                    data.setPixelColour(x, y, palette_[index]);
                }
            }
            previous_ = column_;
        }
    }

    // In the gutter: the field's frequencies and the lane's levels, each
    // centred on the height it labels.
    void drawScales(juce::Graphics& g, juce::Rectangle<float> field, juce::Rectangle<float> lane) const
    {
        const auto& theme = card_.theme();
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        g.setColour(theme.colours.textTertiary);
        const auto mark = [&](float y, const juce::String& text)
        {
            g.drawText(text,
                       juce::Rectangle<float>(field.getRight() + 2.0f, y - 6.0f, gutter - 6.0f, 12.0f),
                       juce::Justification::centredRight);
        };
        for (const double hz : {100.0, 1000.0, 10000.0})
        {
            const float f = static_cast<float>(std::log(hz / lowestHz) / std::log(highestHz / lowestHz));
            mark(field.getBottom() - f * field.getHeight(),
                 hz >= 1000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                              : juce::String(static_cast<int>(hz)));
        }
        // The lane's levels when it is tall enough to keep them apart (one otherwise).
        if (lane.getHeight() >= 44.0f)
            for (const float db : {-20.0f, -40.0f})
                mark(dbToY(db, laneTopDb, laneBottomDb, lane), juce::String(static_cast<int>(db)));
        else
            mark(dbToY(-30.0f, laneTopDb, laneBottomDb, lane), "-30");
    }

    // A 2 px line in the hue along the top over the moments the tail was frozen.
    void drawFrozenMarks(juce::Graphics& g, juce::Rectangle<float> field) const
    {
        const int w = static_cast<int>(frozenColumns_.size());
        if (w == 0)
            return;
        g.setColour(card_.colour());
        int start = -1;
        for (int x = 0; x <= w; ++x)
        {
            const bool on = x < w && frozenColumns_[static_cast<size_t>(x)] != 0;
            if (on && start < 0)
                start = x;
            if (!on && start >= 0)
            {
                const float x0 = field.getX() + field.getWidth() * static_cast<float>(start) / w;
                const float x1 = field.getX() + field.getWidth() * static_cast<float>(x) / w;
                g.fillRect(juce::Rectangle<float>(x0, field.getY(), x1 - x0, 2.0f));
                start = -1;
            }
        }
    }

    // The dry input's level (grey) and the tail's (the hue), both RMS.
    void drawLane(juce::Graphics& g, juce::Rectangle<float> lane)
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        // The grid where the gutter labels it.
        g.setColour(c.gridBar);
        for (const float db :
             lane.getHeight() >= 44.0f ? std::vector<float>{-20.0f, -40.0f} : std::vector<float>{-30.0f})
            g.drawHorizontalLine(
                static_cast<int>(dbToY(db, laneTopDb, laneBottomDb, lane)), lane.getX(), lane.getRight());
        collect(lane);
        const auto x = [&](int col) { return lane.getX() + static_cast<float>(col) + 0.5f; };
        const auto yOf = [&](float linear)
        { return std::min(lane.getBottom(), dbToY(toDb(linear), laneTopDb, laneBottomDb, lane)); };
        const auto area = [&](const std::vector<float>& v)
        {
            juce::Path path;
            path.startNewSubPath(x(0), lane.getBottom());
            for (int col = 0; col < static_cast<int>(v.size()); ++col)
                path.lineTo(x(col), yOf(v[static_cast<size_t>(col)]));
            path.lineTo(x(static_cast<int>(v.size()) - 1), lane.getBottom());
            path.closeSubPath();
            return path;
        };
        if (!dry_.empty())
        {
            g.setColour(c.textTertiary.withAlpha(0.34f));
            g.fillPath(area(dry_));
            g.setColour(hue.withAlpha(0.28f));
            g.fillPath(area(tail_));
            juce::Path line;
            for (int col = 0; col < static_cast<int>(tail_.size()); ++col)
            {
                const float y = yOf(tail_[static_cast<size_t>(col)]);
                if (col == 0)
                    line.startNewSubPath(x(col), y);
                else
                    line.lineTo(x(col), y);
            }
            g.setColour(hue.withAlpha(0.95f));
            g.strokePath(
                line,
                juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // The legend, top left of the lane, on a backing so the levels pass under it.
        const juce::String tail = "TAIL", in = "DRY";
        const float legendW =
            textWidth(theme.captionCaps(), tail) + textWidth(theme.captionCaps(), in) + 24.0f;
        g.setColour(theme.material.meterWell.withAlpha(0.8f));
        g.fillRoundedRectangle(juce::Rectangle<float>(lane.getX() + 4.0f, lane.getY() + 3.0f, legendW, 15.0f),
                               theme.radius.sm);
        auto legend = juce::Rectangle<float>(lane.getX() + 12.0f, lane.getY() + 4.0f, legendW, 13.0f);
        g.setFont(theme.captionCaps());
        g.setColour(hue);
        g.drawText(tail,
                   legend.removeFromLeft(textWidth(theme.captionCaps(), tail)),
                   juce::Justification::centredLeft);
        legend.removeFromLeft(8.0f);
        g.setColour(c.textTertiary);
        g.drawText(
            in, legend.removeFromLeft(textWidth(theme.captionCaps(), in)), juce::Justification::centredLeft);
    }

    // Per column: the largest RMS of its frames, columns counted from the start
    // of the history so they keep their frames as the lane scrolls.
    void collect(juce::Rectangle<float> lane)
    {
        const int columns = std::max(1, static_cast<int>(lane.getWidth()));
        dry_.assign(static_cast<size_t>(columns), 0.0f);
        tail_.assign(static_cast<size_t>(columns), 0.0f);
        const auto perColumn =
            static_cast<std::uint64_t>(std::max(1L, std::lround(fieldSeconds * framesPerSecond / columns)));
        const int size = card_.historySize();
        const auto total = card_.historyTotal();
        if (total == 0)
        {
            dry_.clear();
            tail_.clear();
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
                tail_[i] = std::max(tail_[i], f.extra);
                dry_[i] = std::max(dry_[i], f.extra2);
            }
        }
    }

    // Top left: the decay (and the highs'), the size and the pre-delay, on a
    // backing so they read over the spectrogram.
    void drawReadouts(juce::Graphics& g, juce::Rectangle<float> field) const
    {
        const float decay = card_.param(pb::decay);
        const float highs = pb::decayAt(
            highsHz, decay, card_.param(pb::size), card_.param(pb::damping), card_.param(pb::lowCut));
        struct Item
        {
            juce::String caption, value;
            bool primary = true;
        };
        std::vector<Item> items;
        if (frozen(card_))
            items.push_back({"DECAY", infinity() + " held"});
        else
        {
            items.push_back({"DECAY", formatSeconds(decay)});
            items.push_back({"AT 8 KHZ", formatSeconds(highs), false});
        }
        items.push_back(
            {"SIZE",
             juce::String(static_cast<int>(std::lround(pb::sizeMetres(card_.param(pb::size))))) + " m"});
        const double pre =
            pb::preDelaySeconds(card_.param(pb::preDelay), card_.param(pb::sync), card_.tempoBpm());
        items.push_back({"PRE", synced(card_) ? noteName(card_) : formatSeconds(pre)});
        drawPills(g, {field.getX() + 4.0f, field.getY() + 5.0f}, items);
    }

    // Caption and value pairs on a backing that keeps them readable over the displays.
    template <typename Items>
    void drawPills(juce::Graphics& g, juce::Point<float> at, const Items& items) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto caps = theme.captionCaps(), value = theme.value();
        constexpr float gap = 5.0f, between = 14.0f, pad = 8.0f;
        float width = 2.0f * pad - between;
        for (const auto& item : items)
            width += textWidth(caps, item.caption) + gap + textWidth(value, item.value) + between;
        auto row = juce::Rectangle<float>(at.x, at.y, width, 18.0f);
        g.setColour(theme.material.meterWell.withAlpha(0.8f));
        g.fillRoundedRectangle(row, theme.radius.sm);
        row.removeFromLeft(pad);
        for (const auto& item : items)
        {
            g.setFont(caps);
            g.setColour(c.textTertiary);
            g.drawText(item.caption,
                       row.removeFromLeft(textWidth(caps, item.caption)),
                       juce::Justification::centredLeft);
            row.removeFromLeft(gap);
            g.setFont(value);
            g.setColour(item.primary ? c.textPrimary : c.textSecondary);
            g.drawText(item.value,
                       row.removeFromLeft(textWidth(value, item.value)),
                       juce::Justification::centredLeft);
            row.removeFromLeft(between);
        }
    }

    // Top right while frozen: FROZEN on a pill in the hue.
    void drawFrozenBadge(juce::Graphics& g, juce::Rectangle<float> field, juce::Colour hue) const
    {
        const auto& theme = card_.theme();
        const juce::String text = "FROZEN";
        const float w = textWidth(theme.captionCaps(), text) + 16.0f;
        const auto pill = juce::Rectangle<float>(field.getRight() - w - 6.0f, field.getY() + 6.0f, w, 16.0f);
        g.setColour(theme.material.meterWell.withAlpha(0.78f));
        g.fillRoundedRectangle(pill, 8.0f);
        g.setColour(hue.withAlpha(0.25f));
        g.fillRoundedRectangle(pill, 8.0f);
        g.setColour(hue.withAlpha(0.6f));
        g.drawRoundedRectangle(pill.reduced(0.5f), 7.5f, 1.0f);
        g.setFont(theme.captionCaps());
        g.setColour(hue);
        g.drawText(text, pill, juce::Justification::centred);
    }

    ModuleCard& card_;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_, fftData_, recent_; // recent_: the newest of the tail's tap
    juce::Image image_;
    std::array<juce::Colour, 64> palette_;
    std::vector<std::pair<double, double>> rows_;
    double rowsFor_ = 0.0;
    std::vector<float> column_, previous_;
    std::vector<std::uint8_t> frozenColumns_;
    std::uint64_t lastTap_ = 0;
    double carry_ = 0.0;
    std::vector<float> dry_, tail_; // per lane column, reused
};

// ----- the envelope -------------------------------------------------------------------------

// The shape the settings give a single hit: the dry hit at 0, the pre-delay,
// the swell rising (or the tail starting at once), the decay falling 60 dB over
// the RT60 with the highs' faster fall dashed, the gate's cut, or a frozen tail
// holding to the edge.
class EnvelopeGraph final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EnvelopeGraph(ModuleCard& card) : card_(card)
    {
        setTooltip("What a single hit becomes: the pre-delay, the swell building, the decay (the highs' "
                   "dashed), the gate's cut, or a frozen tail holding");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto p =
            area.reduced(1.0f).withTrimmedTop(10.0f).withTrimmedBottom(14.0f).withTrimmedRight(8.0f);
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
            // The time across, at its end.
            g.setFont(
                theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
            g.setColour(c.textTertiary);
            g.drawText("0",
                       juce::Rectangle<float>(p.getX(), p.getBottom() + 2.0f, 20.0f, 11.0f),
                       juce::Justification::centredLeft);
            g.drawText(formatSeconds(span),
                       juce::Rectangle<float>(p.getRight() - 60.0f, p.getBottom() + 2.0f, 60.0f, 11.0f),
                       juce::Justification::centredRight);
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

// ----- the freeze latch ---------------------------------------------------------------------

// A large round latch the size of the big knobs: the knob's body with a
// snowflake, its rim lit in the hue (with a halo) while the tail is held.
class FreezeLatch final : public juce::Button
{
public:
    FreezeLatch(const Theme& theme, juce::Colour hue, int ring)
        : juce::Button("Freeze"), theme_(theme), hue_(hue), ring_(ring)
    {
        setWantsKeyboardFocus(true);
        setTitle("Freeze");
        setTooltip("Freeze: holds the tail for good and keeps it moving; the input stays out of it until "
                   "you let go");
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

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto& c = theme_.colours;
        const auto& m = theme_.material;
        const bool on = getToggleState();
        auto area = getLocalBounds().toFloat();
        const auto ring = area.removeFromTop(static_cast<float>(ring_))
                              .withSizeKeepingCentre(static_cast<float>(ring_), static_cast<float>(ring_));
        const float stroke = 4.0f;
        const auto rim = ring.reduced(stroke * 0.5f + 1.0f);
        const auto body = rim.reduced(stroke * 0.5f + 1.0f);
        const auto centre = ring.getCentre();
        if (on)
        {
            g.setColour(hue_.withAlpha(0.18f));
            g.fillEllipse(ring.expanded(2.0f));
        }
        if (m.enabled)
            g.setGradientFill(juce::ColourGradient(m.knobBodyLight,
                                                   body.getX() + body.getWidth() * 0.3f,
                                                   body.getY() + body.getHeight() * 0.3f,
                                                   m.knobBodyDark,
                                                   body.getRight(),
                                                   body.getBottom(),
                                                   true));
        else
            g.setColour(c.surface2);
        g.fillEllipse(body);
        if (isButtonDown)
        {
            g.setColour(c.surfacePressed.withAlpha(0.45f));
            g.fillEllipse(body);
        }
        else if (isMouseOver && isEnabled())
        {
            g.setColour(c.surface3.withAlpha(0.22f));
            g.fillEllipse(body);
        }
        if (m.enabled)
        {
            g.setColour(m.knobHighlight);
            juce::Path highlight;
            highlight.addCentredArc(centre.x,
                                    centre.y,
                                    body.getWidth() * 0.5f - 1.0f,
                                    body.getHeight() * 0.5f - 1.0f,
                                    0.0f,
                                    -juce::MathConstants<float>::pi * 0.75f,
                                    -juce::MathConstants<float>::pi * 0.25f,
                                    true);
            g.strokePath(highlight, juce::PathStrokeType(1.0f));
        }
        g.setColour(!isEnabled() ? c.textDisabled : on ? hue_ : (m.enabled ? m.knobRim : c.surface2));
        g.drawEllipse(rim, stroke);

        // The snowflake: three strokes through the centre, a pair of branches on each arm.
        const float r = body.getWidth() * 0.3f;
        juce::Path flake;
        for (int arm = 0; arm < 6; ++arm)
        {
            const float a = juce::MathConstants<float>::pi * static_cast<float>(arm) / 3.0f;
            const auto tip = centre.getPointOnCircumference(r, a);
            flake.startNewSubPath(centre);
            flake.lineTo(tip);
            const auto fork = centre.getPointOnCircumference(r * 0.58f, a);
            for (const float side : {-1.0f, 1.0f})
            {
                const auto twig = fork.getPointOnCircumference(r * 0.32f, a + side * 0.75f);
                flake.startNewSubPath(fork);
                flake.lineTo(twig);
            }
        }
        g.setColour(!isEnabled()  ? c.textDisabled
                    : on          ? hue_
                    : isMouseOver ? c.textPrimary
                                  : c.textSecondary);
        g.strokePath(flake,
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The state under the ring, as a knob's value is.
        g.setFont(theme_.value());
        g.setColour(!isEnabled() ? c.textDisabled : on ? hue_ : c.textSecondary);
        g.drawText(on ? "Held" : "Off", area.toNearestInt(), juce::Justification::centredTop);
        if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
            lnf->drawFocusRing(g, *this, ring, ring.getWidth() * 0.5f);
    }

private:
    const Theme& theme_;
    juce::Colour hue_;
    int ring_;
};

// ----- the card -----------------------------------------------------------------------------

class BloomCard final : public ModuleCard
{
public:
    BloomCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), field_(*this), envelope_(*this),
          freeze_(theme, colour(), bigRing), sync_(theme, colour())
    {
        addAndMakeVisible(field_);
        addAndMakeVisible(envelope_);

        // The defining controls.
        decay_ = &addKnob(pb::decay);
        decay_->format = [](double s) { return formatSeconds(s); };
        decay_->setTooltip("How long the tail takes to fall 60 dB, from under a second to a minute");
        size_ = &addKnob(pb::size);
        size_->format = [](double v)
        { return juce::String(static_cast<int>(std::lround(pb::sizeMetres(static_cast<float>(v))))) + " m"; };
        size_->setTooltip("The space: from a large room to an absurd one (the lines' lengths; it glides "
                          "like tape when moved)");
        shimmer_ = &addKnob(pb::shimmer);
        shimmer_->setTooltip(
            "How much of each pass through the tail is pitch-shifted, so the tail climbs (or "
            "falls) by the interval again and again");
        swell_ = &addKnob(pb::swell);
        swell_->format = [](double ms)
        { return ms <= pb::swellOffMs + 0.5 ? juce::String("Off") : formatSeconds(ms * 0.001); };
        swell_->setTooltip("The tail builds over this time instead of starting at once, like a reversed "
                           "reverb");
        for (auto* k : {decay_, size_, shimmer_, swell_})
            k->setRingSize(bigRing);

        freeze_.onClick = [this]
        {
            beginGesture("Freeze");
            setParam(pb::freeze, frozen(*this) ? 0.0f : 1.0f);
            endGesture();
        };
        addAndMakeVisible(freeze_);
        freezeLabel_ = makeLabel("Freeze");

        pitch_ = std::make_unique<RotarySelector>(theme_);
        {
            std::vector<juce::String> full;
            for (const auto& choice : info(pb::pitch).choices)
                full.push_back(juce::String::fromUTF8(choice.c_str()));
            pitch_->setPositions({"-12",
                                  "+5",
                                  "+7",
                                  "+12",
                                  "+19",
                                  juce::String::fromUTF8("\xc2\xb1"
                                                         "12")},
                                 std::move(full),
                                 "Pitch");
        }
        pitch_->setHue(colour());
        pitch_->setDefault(static_cast<int>(std::lround(info(pb::pitch).defaultValue)));
        pitch_->onChange = [this](int value) { setParam(pb::pitch, static_cast<float>(value)); };
        pitch_->onGestureBegin = [this] { beginGesture("Pitch"); };
        pitch_->onGestureEnd = [this] { endGesture(); };
        addAndMakeVisible(*pitch_);
        pitchLabel_ = makeLabel("Pitch (st)");

        // The rest, in groups.
        const auto knob = [this](int index, const juce::String& label, const juce::String& tip)
        {
            auto* k = &addKnob(index, label);
            k->setTooltip(tip);
            return k;
        };
        preDelay_ = knob(pb::preDelay, "Pre-delay", "The gap before the tail starts");
        preDelay_->format = [this, plain = preDelay_->format](double v)
        { return synced(*this) ? noteName(*this) : plain(v); };
        diffusion_ = knob(pb::diffusion,
                          "Diffusion",
                          "Smears the input and the lines: low is grainy and echoing, "
                          "high is a smooth wash");
        modulation_ =
            knob(pb::modulation, "Mod", "Moves the lines: a slow chorus at the bottom, warping at the top");
        rate_ = knob(pb::modRate, "Rate", "How fast the lines move");
        rate_->format = [](double hz) { return juce::String(hz, hz < 1.0 ? 2 : 1) + " Hz"; };
        lowCut_ = knob(
            pb::lowCut, "Low cut", "Below this the tail decays faster, inside the loop: a cleaner low end");
        damping_ = knob(
            pb::damping, "Damping", "Above this the tail decays faster, inside the loop: darker as it fades");
        tilt_ = knob(pb::tilt, "Tilt", "Tilts the tail around 900 Hz: up for brighter, down for darker");
        tilt_->format = [](double db) { return formatDb(static_cast<float>(db)); };
        duck_ = knob(pb::duck,
                     "Duck",
                     "Lowers the tail under the dry input (up to 24 dB), so a huge tail does "
                     "not swamp the source");
        duckRelease_ = knob(pb::duckRelease, "Release", "How quickly the ducked tail comes back");
        gate_ = knob(pb::gate, "Gate", "Cuts the tail this long after the input stops: a gated, abrupt tail");
        gate_->format = [](double ms)
        { return ms <= pb::gateOffMs + 0.5 ? juce::String("Off") : formatSeconds(ms * 0.001); };
        width_ = knob(pb::width, "Width", "The tail's stereo width, from mono to wide");
        mix_ = knob(pb::mix, "Mix", "The tail against the dry signal");

        for (const auto& c : plugins::sync::choices())
            sync_.choices.emplace_back(c);
        sync_.choices.front() = "Sync off";
        sync_.setTooltip("Sync the pre-delay to the project tempo");
        sync_.onChoose = [this](int i)
        {
            beginGesture("Pre-delay sync");
            setParam(pb::sync, static_cast<float>(i));
            endGesture();
        };
        addAndMakeVisible(sync_);
        paramsChanged();
    }

    int preferredWidth() const override { return smallRowWidth() + 2 + 16; }
    int preferredBodyHeight() const override { return 336; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        const auto& c = theme_.colours;
        // Scale marks around the large knobs.
        for (auto* knob : {decay_, size_, shimmer_, swell_})
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
        // The group captions and the rules between groups.
        g.setFont(theme_.captionCaps());
        g.setColour(c.textTertiary);
        for (const auto& caption : captions_)
            g.drawText(caption.text, caption.area, juce::Justification::centredLeft);
        g.setColour(c.lineSubtle);
        for (const auto& rule : rules_)
            g.drawVerticalLine(rule.x, static_cast<float>(rule.top), static_cast<float>(rule.bottom));
    }

private:
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;
    static constexpr int groupCount = 4;

    int ruleWidth() const noexcept { return 2 * theme_.spacing.sm + 1; }
    int smallRowWidth() const noexcept { return 12 * knobCellWidth() + (groupCount - 1) * ruleWidth(); }

    juce::Label* makeLabel(const juce::String& text)
    {
        auto& label = labels_.emplace_back(std::make_unique<juce::Label>());
        label->setText(text, juce::dontSendNotification);
        label->setFont(theme_.caption());
        label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        label->setJustificationType(juce::Justification::centred);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
        return label.get();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int valueH = static_cast<int>(theme_.fonts.size.value) + 4;
        const int labelH = static_cast<int>(theme_.fonts.size.caption) + 4;
        const int bigCellH = bigRing + 6 + valueH + labelH;
        const int captionH = 16;
        const int smallH = captionH + knobCellHeight();
        const int gap = theme_.spacing.sm;
        rules_.clear();
        captions_.clear();

        field_.setBounds(body.removeFromTop(std::max(96, body.getHeight() - bigCellH - smallH - 2 * gap)));
        body.removeFromTop(gap);

        // The defining controls; every label on the row's bottom line.
        auto big = body.removeFromTop(bigCellH);
        const auto rule = [&](juce::Rectangle<int>& row, int top, int bottom)
        {
            row.removeFromLeft(theme_.spacing.sm);
            rules_.push_back({row.getX(), top, bottom});
            row.removeFromLeft(theme_.spacing.sm + 1);
        };
        const auto bigKnob = [&](juce::Component& control, juce::Label* label)
        {
            auto cell = big.removeFromLeft(bigCellWidth);
            if (label != nullptr)
                label->setBounds(cell.removeFromBottom(labelH));
            control.setBounds(cell.removeFromBottom(bigCellH - labelH - 6));
        };
        const int bigTop = big.getY() + 6, bigBottom = big.getBottom() - 4;
        bigKnob(*decay_, labelFor(*decay_));
        bigKnob(*size_, labelFor(*size_));
        bigKnob(freeze_, freezeLabel_);
        rule(big, bigTop, bigBottom);
        bigKnob(*shimmer_, labelFor(*shimmer_));
        {
            auto cell = big.removeFromLeft(pitch_->preferredWidth());
            pitchLabel_->setBounds(cell.removeFromBottom(labelH));
            pitch_->setBounds(cell.removeFromBottom(pitch_->preferredHeight()));
        }
        rule(big, bigTop, bigBottom);
        bigKnob(*swell_, labelFor(*swell_));
        big.removeFromLeft(theme_.spacing.sm);
        envelope_.setBounds(big.withTrimmedTop(2).withTrimmedBottom(2));

        body.removeFromTop(gap);
        // The groups: a caption over each, rules between them.
        auto small = body.removeFromTop(smallH);
        const int smallTop = small.getY() + 2, smallBottom = small.getBottom() - 4;
        const std::array<std::vector<Knob*>, groupCount> groups{
            std::vector<Knob*>{preDelay_, diffusion_, modulation_, rate_},
            std::vector<Knob*>{lowCut_, damping_, tilt_},
            std::vector<Knob*>{duck_, duckRelease_, gate_},
            std::vector<Knob*>{width_, mix_}};
        const std::array<const char*, groupCount> names{"SPACE", "TONE", "DYNAMICS", "OUTPUT"};
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (i > 0)
                rule(small, smallTop, smallBottom);
            const int count = static_cast<int>(groups[i].size());
            auto group = small.removeFromLeft(count * knobCellWidth());
            auto caption = group.removeFromTop(captionH);
            captions_.push_back({names[i], caption.toFloat().withTrimmedLeft(6.0f)});
            if (i == 0)
            {
                // The pre-delay's sync chip on the caption row, at the right.
                const int w = std::min(caption.getWidth() / 2, sync_.preferredWidth());
                sync_.setBounds(caption.removeFromRight(w).withSizeKeepingCentre(w, 16).translated(-4, 0));
            }
            layoutKnobs(group, count, groups[i]);
        }
    }

    void paramsChanged() override
    {
        pitch_->setSelected(static_cast<int>(std::lround(param(pb::pitch))), juce::dontSendNotification);
        const bool held = frozen(*this);
        freeze_.setToggleState(held, juce::dontSendNotification);
        // Frozen, the shimmer leaves the loop: its controls stand aside.
        shimmer_->setAlpha(held ? 0.5f : 1.0f);
        pitch_->setAlpha(held ? 0.5f : 1.0f);
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
        return frozen(*this) ? colour() : theme_.colours.textSecondary;
    }

    struct Rule
    {
        int x = 0, top = 0, bottom = 0;
    };
    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
    };

    TailField field_;
    EnvelopeGraph envelope_;
    FreezeLatch freeze_;
    CornerChoice sync_;
    std::unique_ptr<RotarySelector> pitch_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    juce::Label* freezeLabel_ = nullptr;
    juce::Label* pitchLabel_ = nullptr;
    Knob *decay_ = nullptr, *size_ = nullptr, *shimmer_ = nullptr, *swell_ = nullptr;
    Knob *preDelay_ = nullptr, *diffusion_ = nullptr, *modulation_ = nullptr, *rate_ = nullptr;
    Knob *lowCut_ = nullptr, *damping_ = nullptr, *tilt_ = nullptr;
    Knob *duck_ = nullptr, *duckRelease_ = nullptr, *gate_ = nullptr;
    Knob *width_ = nullptr, *mix_ = nullptr;
    std::vector<Rule> rules_;
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
