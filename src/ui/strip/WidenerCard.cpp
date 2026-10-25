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
#include "ui/strip/WidthBands.h"
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
using namespace widthbands;
constexpr int scopeSamples = 2048; // the vectorscope's (the newest of the same read)

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
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.regular));
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
