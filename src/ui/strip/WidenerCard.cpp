// The stereo widener's card (screens/plugins.md 7.2a): a vectorscope of the
// output with the correlation meter under it, and the width bands on a
// frequency axis: each band's set width as a block to drag, the crossovers and
// Mono below as lines to drag, and over them the width the output measures at
// each frequency, so how the settings reshape the image is visible. Then the
// controls: the three widths and Mono below; the spread's mode, amount and Haas
// time; rotation and balance; the guard and the output.

#include "plugins/Widener.h"
#include "ui/strip/CardParts.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/TapSpectrum.h"
#include "ui/strip/WidthScale.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace northlight::ui::strip
{

namespace pw = plugins::widener;

namespace
{
constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder; // the width spectrum's window (85 ms at 48 kHz)
constexpr int scopeSamples = 2048;     // the vectorscope's (the newest of the same read)
constexpr int spectrumBands = 56;      // log-spaced, 20 Hz to 20 kHz

// The correlation's zone colour: out of phase, borderline, safe.
juce::Colour zoneColour(const Theme& theme, float correlation)
{
    if (correlation < 0.0f)
        return theme.colours.danger;
    if (correlation < 0.3f)
        return theme.colours.warning;
    return theme.colours.success;
}

juce::String formatCorrelation(float r)
{
    const auto sign = r >= 0.005f    ? juce::String("+")
                      : r <= -0.005f ? juce::String::fromUTF8("\xe2\x88\x92")
                                     : juce::String();
    return sign + juce::String(std::abs(r), 2);
}

// ----- the output's two channels, read from the processor's taps ------------------------------

// Tap 0 carries the output's left channel and tap 1 its right; both are written
// per block, so a read is good when neither moved while it was taken.
class StereoTaps
{
public:
    StereoTaps() : left_(fftSize, 0.0f), right_(fftSize, 0.0f) {}

    // True when new samples were read.
    bool update(engine::ProcessorFeedback* fb)
    {
        if (fb == nullptr || !fb->hasTaps())
            return false;
        fb->setTapsEnabled(true);
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto p0 = fb->tapPosition(0), p1 = fb->tapPosition(1);
            if (p0 != p1)
                continue;
            if (p0 == position_)
                return false;
            if (!fb->readTap(0, left_.data(), fftSize) || !fb->readTap(1, right_.data(), fftSize))
                return false;
            if (fb->tapPosition(0) == p0 && fb->tapPosition(1) == p1)
            {
                position_ = p0;
                return true;
            }
        }
        return false;
    }

    const std::vector<float>& left() const noexcept { return left_; }
    const std::vector<float>& right() const noexcept { return right_; }

private:
    std::vector<float> left_, right_;
    std::uint64_t position_ = 0;
};

// ----- the vectorscope ---------------------------------------------------------------------------

// The output as a goniometer: the mid up, the side across, left up the left
// diagonal and right up the right one. A mono source is a vertical line, a wide
// one a cloud, an out-of-phase one lies on its side. The newest samples are the
// brightest; the scale follows the level so a quiet source still fills it.
class Vectorscope final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Vectorscope(ModuleCard& card) : card_(card) {}

    void update(const StereoTaps& taps, bool fresh, double elapsedMs)
    {
        if (fresh)
        {
            const auto& l = taps.left();
            const auto& r = taps.right();
            const int from = fftSize - scopeSamples;
            float peak = 0.0f;
            points_.resize(scopeSamples);
            for (int i = 0; i < scopeSamples; ++i)
            {
                const float a = l[static_cast<size_t>(from + i)], b = r[static_cast<size_t>(from + i)];
                const juce::Point<float> p{0.5f * (b - a), 0.5f * (a + b)};
                points_[static_cast<size_t>(i)] = p;
                peak = std::max(peak, std::hypot(p.x, p.y));
            }
            peak_ = peak;
            quietMs_ = peak > 1.0e-4f ? 0.0 : quietMs_ + elapsedMs;
        }
        // The scale: quick to shrink for a louder sound, slow to grow for a quieter.
        const float before = gain_;
        if (peak_ > 1.0e-4f)
        {
            const float target = 0.92f / std::max(peak_, 0.003f);
            const float k = target < gain_ ? 1.0f : static_cast<float>(std::min(1.0, elapsedMs / 600.0));
            gain_ += (target - gain_) * k;
        }
        // No new samples and a settled scale: the dots are where they were.
        if (fresh || std::abs(gain_ - before) > 1.0e-4f * std::max(1.0f, gain_))
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto face = area.reduced(static_cast<float>(theme.spacing.sm));
        const float radius = std::min(face.getWidth(), face.getHeight()) * 0.5f;
        const auto centre = face.getCentre();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(area.reduced(1.0f).toNearestInt());
            // The axes: L and R on the diagonals, M upright, S across, and the edge.
            g.setColour(c.gridBar);
            g.drawEllipse(juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre), 1.0f);
            const float d = radius * std::numbers::sqrt2_v<float> * 0.5f;
            g.drawLine(centre.x - d, centre.y - d, centre.x + d, centre.y + d, 1.0f);
            g.drawLine(centre.x + d, centre.y - d, centre.x - d, centre.y + d, 1.0f);
            g.setColour(c.gridBeat);
            g.drawLine(centre.x, centre.y - radius, centre.x, centre.y + radius, 1.0f);
            g.drawLine(centre.x - radius, centre.y, centre.x + radius, centre.y, 1.0f);
            g.setFont(theme.captionCaps());
            g.setColour(c.textTertiary);
            const auto label = [&](const juce::String& text, juce::Point<float> at)
            {
                g.drawText(
                    text, juce::Rectangle<float>(16.0f, 12.0f).withCentre(at), juce::Justification::centred);
            };
            label("L", {centre.x - d - 6.0f, centre.y - d - 6.0f});
            label("R", {centre.x + d + 6.0f, centre.y - d - 6.0f});
            label("M", {centre.x + 9.0f, face.getY() + 5.0f});

            // The rotation: where a centred sound now sits.
            if (const float degrees = card_.param(pw::rotation); std::abs(degrees) >= 0.5f)
            {
                const float a = juce::degreesToRadians(degrees);
                const juce::Point<float> tip{centre.x + radius * std::sin(a),
                                             centre.y - radius * std::cos(a)};
                const float dashes[] = {3.0f, 3.0f};
                g.setColour(c.textSecondary.withAlpha(0.8f));
                g.drawDashedLine({centre, tip}, dashes, 2, 1.2f);
            }

            if (quietMs_ > 250.0 || points_.empty())
            {
                // Silence: the dot at the centre, and why it is empty.
                g.setColour(c.textTertiary.withAlpha(0.6f));
                g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre(centre));
                g.setFont(theme.caption());
                g.drawText("No signal",
                           juce::Rectangle<float>(face.getWidth(), 14.0f)
                               .withCentre({centre.x, centre.y + radius * 0.45f}),
                           juce::Justification::centred);
            }
            else
            {
                // The samples in four ages, the newest brightest.
                const float scale = gain_ * radius;
                const float dot = 1.6f;
                const int groups = 4;
                const int per = static_cast<int>(points_.size()) / groups;
                for (int group = 0; group < groups; ++group)
                {
                    auto& dots = dots_[static_cast<size_t>(group)]; // kept: no allocation per frame
                    dots.clear();
                    dots.ensureStorageAllocated(per);
                    for (int i = group * per; i < (group + 1) * per; ++i)
                    {
                        const auto p = points_[static_cast<size_t>(i)];
                        dots.addWithoutMerging(juce::Rectangle<float>(dot, dot).withCentre(
                            {centre.x + p.x * scale, centre.y - p.y * scale}));
                    }
                    g.setColour(hue.withAlpha(0.18f + 0.22f * static_cast<float>(group)));
                    g.fillRectList(dots);
                }
            }
        }
        drawWellFrame(g, theme, area);
    }

private:
    ModuleCard& card_;
    std::vector<juce::Point<float>> points_;
    std::array<juce::RectangleList<float>, 4> dots_; // the four ages' dots, reused each paint
    float peak_ = 0.0f, gain_ = 4.0f;
    double quietMs_ = 1.0e9;
};

// ----- the correlation meter ---------------------------------------------------------------------

// The output's correlation from -1 (out of phase) to +1 (mono), on a track with
// its zones: out of phase in danger, borderline in warning, safe in success. The
// reading is a bar from 0, the input's a small mark below it, the lowest of the
// last moment holds as a tick; the guard's threshold is a line in the hue to drag.
class CorrelationMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CorrelationMeter(ModuleCard& card) : card_(card) { setTooltip("Drag: guard"); }

    void update(double elapsedMs)
    {
        // The frames carry the newest output and input correlation.
        std::optional<float> out, in;
        const int size = card_.historySize();
        for (int i = std::max(0, size - 4); i < size; ++i)
        {
            const auto& f = card_.historyAt(i);
            out = pw::decodeCorrelation(f.extra);
            in = pw::decodeCorrelation(f.extra2);
        }
        output_ = out;
        input_ = in;
        if (output_)
        {
            if (*output_ <= lowest_ || heldMs_ > card_.theme().motion.peakHoldMs)
            {
                lowest_ = *output_;
                heldMs_ = 0.0;
            }
            else
                heldMs_ += elapsedMs;
        }
        else
        {
            lowest_ = 1.0f;
            heldMs_ = 0.0;
        }
        repaint();
    }

    std::optional<float> output() const noexcept { return output_; }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto t = track();
        const auto x = [&](float r)
        { return t.getX() + (juce::jlimit(-1.0f, 1.0f, r) + 1.0f) * 0.5f * t.getWidth(); };
        // The zones.
        const auto zone = [&](float from, float to, juce::Colour colour)
        {
            g.setColour(colour.withAlpha(0.2f));
            g.fillRect(juce::Rectangle<float>::leftTopRightBottom(x(from), t.getY(), x(to), t.getBottom()));
        };
        zone(-1.0f, 0.0f, c.danger);
        zone(0.0f, 0.3f, c.warning);
        zone(0.3f, 1.0f, c.success);
        // The scale under it.
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        g.setColour(c.textTertiary);
        const float labelY = t.getBottom() + 3.0f;
        g.drawText(juce::String::fromUTF8("\xe2\x88\x92") + "1",
                   juce::Rectangle<float>(t.getX() - 2.0f, labelY, 20.0f, 11.0f),
                   juce::Justification::centredLeft);
        g.drawText("0",
                   juce::Rectangle<float>(20.0f, 11.0f).withCentre({x(0.0f), labelY + 5.5f}),
                   juce::Justification::centred);
        g.drawText("+1",
                   juce::Rectangle<float>(t.getRight() - 18.0f, labelY, 20.0f, 11.0f),
                   juce::Justification::centredRight);
        g.setColour(c.lineStrong);
        g.drawVerticalLine(static_cast<int>(x(0.0f)), t.getY() - 2.0f, t.getBottom() + 2.0f);

        // The guard's threshold: solid in the hue while on, dashed grey while off.
        const bool guardOn = card_.param(pw::guard) >= 0.5f;
        const float gx = x(card_.param(pw::guardAt));
        const bool hot = dragging_ || hover_;
        if (guardOn)
        {
            g.setColour(hue.withAlpha(hot ? 1.0f : 0.85f));
            g.fillRect(juce::Rectangle<float>(gx - 0.75f, t.getY() - 3.0f, 1.5f, t.getHeight() + 6.0f));
        }
        else
        {
            const float dashes[] = {2.0f, 2.0f};
            g.setColour(c.textTertiary.withAlpha(hot ? 1.0f : 0.7f));
            g.drawDashedLine({gx, t.getY() - 3.0f, gx, t.getBottom() + 3.0f}, dashes, 2, 1.0f);
        }
        juce::Path notch;
        notch.addTriangle(gx - 3.5f, t.getY() - 7.0f, gx + 3.5f, t.getY() - 7.0f, gx, t.getY() - 3.0f);
        g.setColour(guardOn ? hue : c.textTertiary);
        g.fillPath(notch);

        if (output_)
        {
            const float r = *output_;
            const auto colour = zoneColour(theme, r);
            // The bar from 0 to the reading, the reading itself bright.
            const float x0 = x(0.0f), xr = x(r);
            g.setColour(colour.withAlpha(0.75f));
            g.fillRect(juce::Rectangle<float>::leftTopRightBottom(
                std::min(x0, xr), t.getY() + 2.0f, std::max(x0, xr), t.getBottom() - 2.0f));
            g.setColour(c.textPrimary);
            g.fillRect(juce::Rectangle<float>(xr - 1.0f, t.getY() - 1.0f, 2.0f, t.getHeight() + 2.0f));
            // The lowest of the last moment.
            if (lowest_ < r - 0.02f)
            {
                g.setColour(zoneColour(theme, lowest_));
                g.fillRect(juce::Rectangle<float>(x(lowest_) - 0.75f, t.getY(), 1.5f, t.getHeight()));
            }
            // The input's, a mark under the track.
            if (input_)
            {
                const float xi = x(*input_);
                juce::Path mark;
                mark.addTriangle(xi - 3.0f,
                                 t.getBottom() + 5.0f,
                                 xi + 3.0f,
                                 t.getBottom() + 5.0f,
                                 xi,
                                 t.getBottom() + 1.0f);
                g.setColour(c.textSecondary);
                g.fillPath(mark);
            }
        }
        // The reading at the right.
        g.setFont(theme.value());
        g.setColour(output_ ? zoneColour(theme, *output_) : c.textTertiary);
        g.drawText(output_ ? formatCorrelation(*output_) : juce::String::fromUTF8("\xe2\x80\x93"),
                   readoutArea(),
                   juce::Justification::centredRight);
        drawWellFrame(g, theme, area);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool near = std::abs(e.position.x - guardX()) <= 6.0f;
        if (near != hover_)
        {
            hover_ = near;
            repaint();
        }
        setMouseCursor(near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = false;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!track().expanded(8.0f, 10.0f).contains(e.position))
            return;
        dragging_ = true;
        card_.beginGesture(card_.info(pw::guardAt).name);
        set(e.position.x);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_)
            set(e.position.x);
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
        card_.beginGesture(card_.info(pw::guardAt).name);
        card_.setParam(pw::guardAt, card_.info(pw::guardAt).defaultValue);
        card_.endGesture();
    }

private:
    juce::Rectangle<float> readoutArea() const
    {
        const auto inner =
            getLocalBounds().toFloat().reduced(static_cast<float>(card_.theme().spacing.sm), 0.0f);
        return inner.withLeft(inner.getRight() - 36.0f);
    }
    juce::Rectangle<float> track() const
    {
        const auto inner =
            getLocalBounds().toFloat().reduced(static_cast<float>(card_.theme().spacing.sm), 0.0f);
        return {inner.getX() + 4.0f,
                inner.getY() + 11.0f,
                inner.getWidth() - 36.0f - card_.theme().spacing.sm - 8.0f,
                8.0f};
    }
    float guardX() const
    {
        const auto t = track();
        return t.getX() + (card_.param(pw::guardAt) + 1.0f) * 0.5f * t.getWidth();
    }
    void set(float px)
    {
        const auto t = track();
        const float r = (px - t.getX()) / t.getWidth() * 2.0f - 1.0f;
        card_.setParam(pw::guardAt, std::round(r * 20.0f) / 20.0f); // steps of 0.05
        repaint();
    }

    ModuleCard& card_;
    std::optional<float> output_, input_;
    float lowest_ = 1.0f;
    double heldMs_ = 0.0;
    bool dragging_ = false, hover_ = false;
};

// ----- the width bands ---------------------------------------------------------------------------

// The three bands on a frequency axis, width up (0 mono, 100% as it came, 200%
// twice the side): each band's set width as a block in the hue, the crossovers
// and Mono below as lines to drag, and the width the output measures at each
// frequency (its side over its mid) as a line over them. A guard that narrows
// shows the width it leaves as a dashed line in warning.
class WidthBands final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WidthBands(ModuleCard& card)
        : card_(card), window_(hannWindow(fftSize)), in_(fftSize), out_(fftSize)
    {
        setTooltip("Drag: widths, crossovers");
        mid_.fill(0.0f);
        side_.fill(0.0f);
    }

    void update(const StereoTaps& taps, bool fresh)
    {
        // Drawn from the last analysis: nothing new, nothing to repaint.
        if (!fresh)
            return;
        analyse(taps);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const auto bounds = getLocalBounds().toFloat();
        drawWell(g, theme, bounds);
        const auto p = plot();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(bounds.reduced(1.0f).toNearestInt());
            drawGrid(g, p);

            // Mono below: the region under it shaded, its edge a line.
            const float monoHz = card_.param(pw::monoBelow);
            const bool monoOn = monoHz > pw::monoOffHz + 0.5f;
            const float monoX = monoOn ? hzToX(monoHz, p) : p.getX();

            // The bands.
            const float x1 = hzToX(card_.param(pw::lowSplit), p);
            const float x2 = hzToX(card_.param(pw::highSplit), p);
            const std::array<float, 4> edges{p.getX(), x1, x2, p.getRight()};
            const float guard = 1.0f - card_.reductionDb() * 0.01f; // the frames' reduction is the guard's %
            for (int b = 0; b < 3; ++b)
            {
                const float left = edges[static_cast<size_t>(b)], right = edges[static_cast<size_t>(b) + 1];
                const float w = card_.param(pw::lowWidth + b);
                const float y = widthToY(w, p);
                const bool active = drag_ == static_cast<Drag>(b) || (drag_ == Drag::none && hover_ == b);
                const auto block = juce::Rectangle<float>::leftTopRightBottom(left, y, right, p.getBottom());
                g.setColour(hue.withAlpha(active ? 0.22f : 0.14f));
                g.fillRect(block);
                g.setColour(hue.withAlpha(active ? 1.0f : 0.85f));
                g.fillRect(juce::Rectangle<float>(left, y - 1.0f, right - left, 2.0f));
                if (card_.param(pw::guard) >= 0.5f && guard < 0.99f && w > 0.5f)
                {
                    const float dashes[] = {4.0f, 3.0f};
                    const float gy = widthToY(w * guard, p);
                    g.setColour(c.warning.withAlpha(0.9f));
                    g.drawDashedLine({left + 2.0f, gy, right - 2.0f, gy}, dashes, 2, 1.2f);
                }
            }
            if (monoOn)
            {
                g.setColour(c.canvas.withAlpha(0.8f)); // no side here: the bands' fill goes
                g.fillRect(
                    juce::Rectangle<float>::leftTopRightBottom(p.getX(), p.getY(), monoX, p.getBottom()));
                if (monoX - p.getX() > 44.0f)
                {
                    g.setFont(theme.captionCaps());
                    g.setColour(c.textSecondary);
                    g.drawText("MONO",
                               juce::Rectangle<float>(
                                   p.getX() + 6.0f, p.getBottom() - 18.0f, monoX - p.getX() - 8.0f, 12.0f),
                               juce::Justification::centredLeft);
                }
            }

            // The measured width.
            drawMeasured(g, p);

            // The crossovers and Mono below's edge.
            for (const auto& [x, which] : {std::pair{x1, Drag::lowSplit}, std::pair{x2, Drag::highSplit}})
            {
                const bool active = drag_ == which || (drag_ == Drag::none && hoverLine_ == which);
                g.setColour(active ? c.textPrimary : c.lineStrong);
                g.fillRect(juce::Rectangle<float>::leftTopRightBottom(x - (active ? 0.75f : 0.5f),
                                                                      bounds.getY() + 1.0f,
                                                                      x + (active ? 0.75f : 0.5f),
                                                                      p.getBottom()));
            }
            {
                const bool active =
                    drag_ == Drag::monoBelow || (drag_ == Drag::none && hoverLine_ == Drag::monoBelow);
                const float dashes[] = {3.0f, 3.0f};
                g.setColour(active ? c.textPrimary : c.textSecondary);
                if (monoOn)
                    g.drawDashedLine({monoX + 0.5f, p.getY(), monoX + 0.5f, p.getBottom()},
                                     dashes,
                                     2,
                                     active ? 1.5f : 1.0f);
            }
        }
        drawWellFrame(g, theme, bounds);

        // The handles: each crossover's at the foot of its line, Mono below's
        // at the top, each band's on its width at the band's middle.
        const auto& names = bandNames();
        const float x1 = hzToX(card_.param(pw::lowSplit), p);
        const float x2 = hzToX(card_.param(pw::highSplit), p);
        const std::array<float, 4> edges{p.getX(), x1, x2, p.getRight()};
        for (int b = 0; b < 3; ++b)
        {
            const float left = edges[static_cast<size_t>(b)], right = edges[static_cast<size_t>(b) + 1];
            const float w = card_.param(pw::lowWidth + b);
            const float y = widthToY(w, p);
            const bool active = drag_ == static_cast<Drag>(b) || (drag_ == Drag::none && hover_ == b);
            drawHandle(g, theme, hue, {(left + right) * 0.5f, y}, active);
            // The band's name and width at its top, where there is room.
            const auto label = juce::Rectangle<float>(
                left + 6.0f, p.getY() - static_cast<float>(captionRow) + 3.0f, right - left - 12.0f, 13.0f);
            const auto value = w < 0.5f ? juce::String("Mono") : juce::String(juce::roundToInt(w)) + "%";
            const float nameW = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(
                theme.captionCaps(), names[static_cast<size_t>(b)]));
            const float valueW =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), value));
            if (label.getWidth() >= nameW + 6.0f + valueW)
            {
                auto row = label;
                g.setFont(theme.captionCaps());
                g.setColour(c.textTertiary);
                g.drawText(names[static_cast<size_t>(b)],
                           row.removeFromLeft(nameW),
                           juce::Justification::centredLeft);
                row.removeFromLeft(6.0f);
                g.setFont(theme.value());
                g.setColour(active ? c.textPrimary : c.textSecondary);
                g.drawText(value, row, juce::Justification::centredLeft);
            }
            else if (label.getWidth() >= valueW)
            {
                g.setFont(theme.value());
                g.setColour(active ? c.textPrimary : c.textSecondary);
                g.drawText(value, label, juce::Justification::centredLeft);
            }
        }
        for (const auto& [x, which] : {std::pair{x1, Drag::lowSplit}, std::pair{x2, Drag::highSplit}})
        {
            const bool active = drag_ == which || (drag_ == Drag::none && hoverLine_ == which);
            drawGrip(g, {x, p.getBottom() - 10.0f}, active);
            if (active)
                drawFrequency(
                    g, x, which == Drag::lowSplit ? card_.param(pw::lowSplit) : card_.param(pw::highSplit));
        }
        {
            const float monoHz = card_.param(pw::monoBelow);
            const bool monoOn = monoHz > pw::monoOffHz + 0.5f;
            const float monoX = monoOn ? hzToX(monoHz, p) : p.getX() + 5.0f;
            const bool active =
                drag_ == Drag::monoBelow || (drag_ == Drag::none && hoverLine_ == Drag::monoBelow);
            drawGrip(g, {monoX, monoHandleY(p)}, active, !monoOn); // off: parked at the left
            if (active)
                drawFrequency(g, monoX, monoHz);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto line = lineAt(e.position);
        const int band = line == Drag::none ? bandAt(e.position.x) : -1;
        if (line != hoverLine_ || band != hover_)
        {
            hoverLine_ = line;
            hover_ = band;
            repaint();
        }
        setMouseCursor(line != Drag::none ? juce::MouseCursor::LeftRightResizeCursor
                                          : juce::MouseCursor::UpDownResizeCursor);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = -1;
        hoverLine_ = Drag::none;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        drag_ = lineAt(e.position);
        if (drag_ == Drag::none)
            drag_ = static_cast<Drag>(bandAt(e.position.x));
        card_.beginGesture(card_.info(paramOf(drag_)).name);
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto p = plot();
        switch (drag_)
        {
            case Drag::low:
            case Drag::mid:
            case Drag::high:
            {
                float w = widthscale::settableAt(e.position.y, p);
                // Settle on 100% (as it came) and on mono near them.
                if (std::abs(w - 100.0f) < 3.0f)
                    w = 100.0f;
                else if (w < 3.0f)
                    w = 0.0f;
                card_.setParam(paramOf(drag_), std::round(w));
                break;
            }
            case Drag::lowSplit:
            case Drag::highSplit:
                card_.setParam(paramOf(drag_), static_cast<float>(xToHz(e.position.x, p)));
                break;
            case Drag::monoBelow:
            {
                const double hz = xToHz(e.position.x, p);
                card_.setParam(pw::monoBelow, hz < 22.0 ? pw::monoOffHz : static_cast<float>(hz));
                break;
            }
            case Drag::none:
                break;
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ != Drag::none)
            card_.endGesture();
        drag_ = Drag::none;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        auto which = lineAt(e.position);
        if (which == Drag::none)
            which = static_cast<Drag>(bandAt(e.position.x));
        const int index = paramOf(which);
        card_.beginGesture(card_.info(index).name);
        card_.setParam(index, card_.info(index).defaultValue);
        card_.endGesture();
    }

    // For tests: the measured width (%) at a frequency, when there is signal there.
    std::optional<float> measuredWidthAt(double hz) const
    {
        const int band = juce::jlimit(0, spectrumBands - 1, static_cast<int>(std::floor(bandOf(hz))));
        return widthOf(band);
    }

private:
    enum class Drag
    {
        low = 0,
        mid = 1,
        high = 2,
        lowSplit,
        highSplit,
        monoBelow,
        none
    };

    static const std::array<juce::String, 3>& bandNames()
    {
        static const std::array<juce::String, 3> names{"LOW", "MID", "HIGH"};
        return names;
    }

    static int paramOf(Drag d)
    {
        switch (d)
        {
            case Drag::low:
                return pw::lowWidth;
            case Drag::mid:
                return pw::midWidth;
            case Drag::high:
                return pw::highWidth;
            case Drag::lowSplit:
                return pw::lowSplit;
            case Drag::highSplit:
                return pw::highSplit;
            case Drag::monoBelow:
                return pw::monoBelow;
            case Drag::none:
                break;
        }
        return pw::midWidth;
    }

    juce::Rectangle<float> plot() const
    {
        // Room at the left for the width scale and below for the frequencies.
        return getLocalBounds()
            .toFloat()
            .reduced(1.0f)
            .withTrimmedLeft(34.0f)
            .withTrimmedBottom(15.0f)
            .withTrimmedTop(static_cast<float>(captionRow));
    }
    static constexpr int captionRow = 20; // the bands' names above the plot
    static float monoHandleY(juce::Rectangle<float> p) { return p.getY() + 12.0f; }
    static float widthToY(float w, juce::Rectangle<float> p) { return widthscale::toY(w, p); }

    int bandAt(float x) const
    {
        const auto p = plot();
        if (x < hzToX(card_.param(pw::lowSplit), p))
            return 0;
        return x < hzToX(card_.param(pw::highSplit), p) ? 1 : 2;
    }

    Drag lineAt(juce::Point<float> at) const
    {
        const auto p = plot();
        const float monoHz = card_.param(pw::monoBelow);
        const float monoX = monoHz > pw::monoOffHz + 0.5f ? hzToX(monoHz, p) : p.getX() + 5.0f;
        const float x1 = hzToX(card_.param(pw::lowSplit), p), x2 = hzToX(card_.param(pw::highSplit), p);
        // The nearest line within reach; Mono below's only near its handle when off.
        Drag best = Drag::none;
        float bestDistance = 6.0f;
        const auto consider = [&](float x, Drag which)
        {
            const float d = std::abs(at.x - x);
            if (d <= bestDistance)
            {
                bestDistance = d;
                best = which;
            }
        };
        consider(x1, Drag::lowSplit);
        consider(x2, Drag::highSplit);
        if (monoHz > pw::monoOffHz + 0.5f || std::abs(at.y - monoHandleY(p)) < 10.0f)
            consider(monoX, Drag::monoBelow);
        return best;
    }

    void drawGrid(juce::Graphics& g, juce::Rectangle<float> p) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        static constexpr std::array lines{50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
        drawFrequencyGrid(g, theme, p, lines, 2.0f); // the width labels share its font
        for (const float w : {50.0f, 100.0f, 150.0f})
        {
            const float y = widthToY(w, p);
            g.setColour(w == 100.0f ? c.lineStrong : c.gridBeat);
            g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
        }
        // The headroom strip over 200%: what the output measures past what a band
        // can be set to, compressed.
        const float edge = widthscale::settableTop(p);
        g.setColour(c.textPrimary.withAlpha(0.035f));
        g.fillRect(juce::Rectangle<float>::leftTopRightBottom(p.getX(), p.getY(), p.getRight(), edge));
        const float dashes[] = {3.0f, 3.0f};
        g.setColour(c.gridBar);
        g.drawDashedLine({p.getX(), edge, p.getRight(), edge}, dashes, 2, 1.0f);
        g.setColour(c.textTertiary);
        for (const float w : {0.0f, 100.0f, 200.0f})
        {
            const float y = juce::jlimit(p.getY() + 6.0f, p.getBottom() - 6.0f, widthToY(w, p));
            g.drawText(juce::String(static_cast<int>(w)) + "%",
                       juce::Rectangle<float>(p.getX() - 34.0f, y - 6.0f, 30.0f, 12.0f),
                       juce::Justification::centredRight);
        }
        g.setFont(theme.captionCaps());
        const float overW = juce::GlyphArrangement::getStringWidth(theme.captionCaps(), "OVER");
        const float lineH = static_cast<float>(theme.lineHeight(theme.fonts.size.caption));
        g.drawText(
            "OVER",
            juce::Rectangle<float>(p.getX() - 4.0f - overW - 2.0f, p.getY() - 3.0f, overW + 2.0f, lineH),
            juce::Justification::centredRight);
    }

    // A line's grip: a small upright pill, so it reads as "drag sideways" beside
    // the bands' round handles, which drag up and down.
    void drawGrip(juce::Graphics& g, juce::Point<float> at, bool active, bool dim = false) const
    {
        const auto& c = card_.theme().colours;
        const auto r = juce::Rectangle<float>(7.0f, 16.0f).withCentre(at);
        if (active)
        {
            g.setColour(c.textPrimary.withAlpha(0.18f));
            g.fillRoundedRectangle(r.expanded(3.0f), 5.0f);
        }
        g.setColour(active ? c.textPrimary : dim ? c.textTertiary : c.textSecondary);
        g.fillRoundedRectangle(r, 3.5f);
        g.setColour(c.surface0);
        g.drawHorizontalLine(static_cast<int>(at.y) - 2, r.getX() + 2.0f, r.getRight() - 2.0f);
        g.drawHorizontalLine(static_cast<int>(at.y) + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
    }

    void drawFrequency(juce::Graphics& g, float x, float hz) const
    {
        const auto& theme = card_.theme();
        const auto p = plot();
        const auto text = hz <= pw::monoOffHz + 0.5f ? juce::String("Off") : hzText(hz);
        const float w =
            static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), text)) + 10.0f;
        auto box = juce::Rectangle<float>(w, 16.0f).withCentre({x, monoHandleY(p) + 20.0f});
        box = box.withX(juce::jlimit(p.getX() + 2.0f, p.getRight() - w - 2.0f, box.getX()));
        g.setColour(theme.colours.surface3);
        g.fillRoundedRectangle(box, theme.radius.sm);
        g.setColour(theme.colours.textPrimary);
        g.setFont(theme.value());
        g.drawText(text, box, juce::Justification::centred);
    }

    void drawMeasured(juce::Graphics& g, juce::Rectangle<float> p) const
    {
        juce::Path line;
        bool drawing = false;
        for (int b = 0; b < spectrumBands; ++b)
        {
            const auto w = widthOf(b);
            if (!w)
            {
                drawing = false;
                continue;
            }
            const float x = hzToX(centreHz(b), p);
            const float y = widthToY(*w, p);
            if (!drawing)
                line.startNewSubPath(x, y);
            else
                line.lineTo(x, y);
            drawing = true;
        }
        g.setColour(card_.theme().colours.textPrimary.withAlpha(0.6f));
        g.strokePath(
            line, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawOverMarkers(g, p);
    }

    // Over the top: above each run of bands measuring past 210%, a small
    // triangle in the warning colour at the strip's top, with the run's widest
    // ("340%", or ">800%" off the strip) beside it where there is room.
    void drawOverMarkers(juce::Graphics& g, juce::Rectangle<float> p) const
    {
        const auto& theme = card_.theme();
        const auto font =
            theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.medium);
        float lastRight = p.getX() - 1.0f;
        int b = 0;
        while (b < spectrumBands)
        {
            const auto w = widthOf(b);
            if (!w || *w <= widthscale::settable)
            {
                ++b;
                continue;
            }
            int peak = b;
            float most = *w;
            for (; b < spectrumBands; ++b)
            {
                const auto v = widthOf(b);
                if (!v || *v <= widthscale::settable)
                    break;
                if (*v > most)
                {
                    most = *v;
                    peak = b;
                }
            }
            // A run that only grazes the edge gets no marker (it would flicker).
            if (most < widthscale::settable * 1.05f)
                continue;
            const float x = hzToX(centreHz(peak), p);
            const float top = p.getY() + 2.0f;
            juce::Path arrow;
            arrow.addTriangle(x - 4.0f, top + 6.0f, x + 4.0f, top + 6.0f, x, top);
            g.setColour(theme.colours.warning);
            g.fillPath(arrow);
            const auto text = most >= widthscale::ceiling
                                  ? ">" + juce::String(juce::roundToInt(widthscale::ceiling)) + "%"
                                  : juce::String(juce::roundToInt(most)) + "%";
            const float textW =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text)) + 2.0f;
            const auto box = juce::Rectangle<float>(x + 6.0f, top - 2.0f, textW, 11.0f);
            if (box.getX() > lastRight && box.getRight() < p.getRight() - 2.0f)
            {
                g.setFont(font);
                g.drawText(text, box, juce::Justification::centredLeft);
                lastRight = box.getRight() + 4.0f;
            }
        }
    }

    static double bandOf(double hz) { return std::log(hz / minHz) / std::log(maxHz / minHz) * spectrumBands; }
    static double centreHz(int band)
    {
        return minHz * std::pow(maxHz / minHz, (static_cast<double>(band) + 0.5) / spectrumBands);
    }

    // A band's width with its neighbours' (a third of an octave or so each side),
    // none where it is too quiet to say: below about -80 dBFS, or 60 dB under
    // the loudest band.
    std::optional<float> widthOf(int band) const
    {
        float m = 0.0f, s = 0.0f;
        for (int b = std::max(0, band - 1); b <= std::min(spectrumBands - 1, band + 1); ++b)
        {
            const float weight = b == band ? 2.0f : 1.0f;
            m += weight * mid_[static_cast<size_t>(b)];
            s += weight * side_[static_cast<size_t>(b)];
        }
        const float own = mid_[static_cast<size_t>(band)] + side_[static_cast<size_t>(band)];
        if (own < quietPower_ || own < loudest_ * 1.0e-6f)
            return std::nullopt;
        return 100.0f * std::sqrt(s / std::max(m, 1.0e-20f));
    }

    // The output's mid and side power in each band, from one window of both taps.
    void analyse(const StereoTaps& taps)
    {
        const auto& l = taps.left();
        const auto& r = taps.right();
        const double rate = card_.sampleRate();
        // Left and right go through one complex FFT: left real, right imaginary.
        for (int i = 0; i < fftSize; ++i)
            in_[static_cast<size_t>(i)] = {l[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)],
                                           r[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)]};
        fft_.perform(in_.data(), out_.data(), false);
        std::array<float, spectrumBands> mid{}, side{};
        std::array<int, spectrumBands> bins{};
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double hz = k * rate / fftSize;
            if (hz < minHz || hz >= maxHz)
                continue;
            // Untangle the two real spectra from the one complex one.
            const auto z = out_[static_cast<size_t>(k)];
            const auto zc = std::conj(out_[static_cast<size_t>(fftSize - k)]);
            const auto left = (z + zc) * 0.5f;
            const auto right = (z - zc) * juce::dsp::Complex<float>(0.0f, -0.5f);
            const auto m = (left + right) * 0.5f, s = (left - right) * 0.5f;
            const auto band =
                static_cast<size_t>(juce::jlimit(0, spectrumBands - 1, static_cast<int>(bandOf(hz))));
            mid[band] += std::norm(m);
            side[band] += std::norm(s);
            ++bins[band];
        }
        // A band too narrow for a bin takes its neighbour's.
        for (int b = 0; b < spectrumBands; ++b)
            if (bins[static_cast<size_t>(b)] == 0)
                for (int d = 1; d < spectrumBands; ++d)
                {
                    const int n = b + d < spectrumBands && bins[static_cast<size_t>(b + d)] > 0 ? b + d : -1;
                    if (n >= 0)
                    {
                        mid[static_cast<size_t>(b)] =
                            mid[static_cast<size_t>(n)] / static_cast<float>(bins[static_cast<size_t>(n)]);
                        side[static_cast<size_t>(b)] =
                            side[static_cast<size_t>(n)] / static_cast<float>(bins[static_cast<size_t>(n)]);
                        bins[static_cast<size_t>(b)] = -1; // borrowed, already per bin
                        break;
                    }
                }
        loudest_ = 0.0f;
        for (size_t b = 0; b < static_cast<size_t>(spectrumBands); ++b)
        {
            // About 150 ms of averaging at the display's rate: steady enough to read.
            const float n = bins[b] > 0 ? static_cast<float>(bins[b]) : 1.0f;
            mid_[b] += (mid[b] / n - mid_[b]) * 0.12f;
            side_[b] += (side[b] / n - side_[b]) * 0.12f;
            loudest_ = std::max(loudest_, mid_[b] + side_[b]);
        }
    }

    ModuleCard& card_;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_;
    std::vector<juce::dsp::Complex<float>> in_, out_; // reused every analysis
    std::array<float, spectrumBands> mid_{}, side_{};
    float loudest_ = 0.0f;
    // Below about -80 dB per bin (a full-scale sine through the Hann window is
    // about (fftSize / 4)^2): too quiet to say how wide.
    const float quietPower_ = 1.0e-8f * static_cast<float>(fftSize) * static_cast<float>(fftSize) / 16.0f;
    Drag drag_ = Drag::none, hoverLine_ = Drag::none;
    int hover_ = -1;
};

// ----- the card ------------------------------------------------------------------------------------

class WidenerCard final : public ModuleCard
{
public:
    WidenerCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), scope_(*this), meter_(*this), bands_(*this)
    {
        addAndMakeVisible(scope_);
        addAndMakeVisible(meter_);
        addAndMakeVisible(bands_);

        const auto width = [this](int index, const juce::String& label)
        {
            auto& k = addKnob(index, label);
            k.format = [](double v)
            { return v < 0.5 ? juce::String("Mono") : juce::String(juce::roundToInt(v)) + "%"; };
            return &k;
        };
        widthKnobs_ = {width(pw::lowWidth, "Low"), width(pw::midWidth, "Mid"), width(pw::highWidth, "High")};
        monoBelow_ = &addKnob(pw::monoBelow, "Mono below");
        monoBelow_->format = [](double hz)
        { return hz <= pw::monoOffHz + 0.5 ? juce::String("Off") : hzText(hz); };

        mode_ = &addChoice(pw::spreadMode);
        mode_->setTooltip("Spread mode");
        modeCaption_.setText("Mode", juce::dontSendNotification);
        modeCaption_.setFont(theme_.caption());
        modeCaption_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        modeCaption_.setJustificationType(juce::Justification::centred);
        modeCaption_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(modeCaption_);
        spread_ = &addKnob(pw::spread);
        haasTime_ = &addKnob(pw::haasTime, "Time");

        rotation_ = &addKnob(pw::rotation);
        rotation_->format = [](double v)
        {
            const int d = juce::roundToInt(v);
            return (d > 0 ? "+" : "") + juce::String(d) + juce::String::fromUTF8("\xc2\xb0");
        };
        balance_ = &addKnob(pw::balance);
        balance_->format = [](double v)
        {
            const int b = juce::roundToInt(v);
            return b == 0 ? juce::String("C") : (b < 0 ? "L " : "R ") + juce::String(std::abs(b));
        };

        guard_ = &addToggle(pw::guard, "Guard");
        guard_->setTooltip("Mono guard");
        guardCaption_.setText("Mono safe", juce::dontSendNotification);
        guardCaption_.setFont(theme_.caption());
        guardCaption_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        guardCaption_.setJustificationType(juce::Justification::centred);
        guardCaption_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(guardCaption_);
        output_ = &addKnob(pw::output);
        paramsChanged();
    }

    int preferredWidth() const override
    {
        return 4 * knobCellWidth() + modeWidth() + 2 * knobCellWidth() + 2 * knobCellWidth() + guardWidth +
               knobCellWidth() + 3 * ruleWidth() + 2 * theme_.spacing.sm;
    }
    int preferredBodyHeight() const override { return 272; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        g.setColour(theme_.colours.lineSubtle);
        for (const int x : dividers_)
            g.drawVerticalLine(x, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
    }

private:
    static constexpr int guardWidth = 84;
    static constexpr int corrHeight = 38;
    int modeWidth() const { return std::max(112, mode_->preferredWidth() + 2 * theme_.spacing.sm); }
    int ruleWidth() const noexcept { return 2 * theme_.spacing.sm + 1; }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int labelH = theme_.lineHeight(theme_.fonts.size.caption);
        const int controlsH = knobCellHeight();
        auto display = body.removeFromTop(std::max(96, body.getHeight() - controlsH - theme_.spacing.sm));
        body.removeFromTop(theme_.spacing.sm);
        // The scope square on the left, the meter under it; the bands the rest.
        const int side = std::max(64, display.getHeight() - corrHeight - theme_.spacing.sm);
        auto left = display.removeFromLeft(side);
        scope_.setBounds(left.removeFromTop(side));
        left.removeFromTop(theme_.spacing.sm);
        meter_.setBounds(left);
        display.removeFromLeft(theme_.spacing.sm);
        bands_.setBounds(display);

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
        // A non-knob control centred on the knobs' rings, its caption on their labels' line.
        const int ringCentre = theme_.sizes.knob / 2;
        const auto place =
            [&](juce::Component& c, juce::Label& caption, juce::Rectangle<int> cell, int w, int h)
        {
            caption.setBounds(cell.withTop(cell.getBottom() - labelH));
            c.setBounds(juce::Rectangle<int>(w, h).withCentre({cell.getCentreX(), cell.getY() + ringCentre}));
        };
        layoutKnobs(controls.removeFromLeft(4 * knobCellWidth()),
                    4,
                    {widthKnobs_[0], widthKnobs_[1], widthKnobs_[2], monoBelow_});
        rule();
        place(*mode_,
              modeCaption_,
              controls.removeFromLeft(modeWidth()),
              mode_->preferredWidth(),
              theme_.sizes.controlCompact);
        layoutKnobs(controls.removeFromLeft(2 * knobCellWidth()), 2, {spread_, haasTime_});
        rule();
        layoutKnobs(controls.removeFromLeft(2 * knobCellWidth()), 2, {rotation_, balance_});
        rule();
        place(*guard_,
              guardCaption_,
              controls.removeFromLeft(guardWidth),
              guardWidth - 2 * theme_.spacing.sm,
              theme_.sizes.controlCompact);
        layoutKnobs(controls.removeFromLeft(knobCellWidth()), 1, {output_});
    }

    void paramsChanged() override
    {
        // Haas time only counts in Haas mode.
        const bool haasMode = std::lround(param(pw::spreadMode)) == pw::haas;
        haasTime_->setEnabled(haasMode);
        if (auto* label = labelFor(*haasTime_))
            label->setColour(juce::Label::textColourId,
                             haasMode ? theme_.colours.textSecondary : theme_.colours.textDisabled);
        bands_.repaint();
        meter_.repaint();
        scope_.repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        const bool fresh = taps_.update(liveFeedback());
        scope_.update(taps_, fresh, elapsedMs);
        bands_.update(taps_, fresh);
        meter_.update(elapsedMs);
    }

    juce::String headerStatus() const override
    {
        const float narrowed = reductionReadoutDb(); // the guard's narrowing, in %
        if (isOn() && param(pw::guard) >= 0.5f && narrowed >= 1.0f)
            return "Guard " + juce::String::fromUTF8("\xe2\x88\x92") +
                   juce::String(juce::roundToInt(narrowed)) + "%";
        return {};
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.warning; }

    StereoTaps taps_;
    Vectorscope scope_;
    CorrelationMeter meter_;
    WidthBands bands_;
    std::array<Knob*, 3> widthKnobs_{};
    Knob* monoBelow_ = nullptr;
    SegmentedControl* mode_ = nullptr;
    juce::Label modeCaption_, guardCaption_;
    Knob* spread_ = nullptr;
    Knob* haasTime_ = nullptr;
    Knob* rotation_ = nullptr;
    Knob* balance_ = nullptr;
    PillToggle* guard_ = nullptr;
    Knob* output_ = nullptr;
    std::vector<int> dividers_;
    int dividerTop_ = 0, dividerBottom_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createWidenerCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<WidenerCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
