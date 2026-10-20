// Pulsar's editor (07-plugins.md 7.4c). Like Aurora, Pulsar reads as its own
// product: deep space with a pulsar in the corner whose beams sweep and flash
// with every hit and send rings out across the sky; the eight pads as glass
// tiles, each with a picture of its sound, lighting as it plays; the selected
// pad's sound drawn large (drag across for the decay, up and down for the
// tune) with its six knobs; the master drive, velocity and level with a scope.
// Colours are the theme's palette.

#include "ui/strip/PulsarEditor.h"

#include "plugins/Pulsar.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/strip/StripStyle.h"

#include <array>
#include <cmath>

namespace aural::ui::strip
{

namespace
{
namespace pu = plugins::pulsar;

juce::Colour padHue(const Theme& theme, int pad)
{
    static const char* names[] = {"Coral", "Amber", "Tangerine", "Lime", "Mint", "Teal", "Sky", "Indigo"};
    return paletteColour(theme, names[juce::jlimit(0, pu::padCount - 1, pad)]);
}

juce::String noteName(int note)
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

// A pad's sound, rendered once per change of its settings and reduced to a
// peak per column for drawing.
struct Preview
{
    std::array<float, 6> key{};
    bool valid = false;
    double seconds = 0.0;
    std::vector<float> peaks; // 0..1, per column of `columns`

    void update(const float* params, int pad, int columns)
    {
        std::array<float, 6> now{};
        for (int f = 0; f < pu::fieldCount; ++f)
            now[static_cast<size_t>(f)] = params[pu::paramIndex(pad, static_cast<pu::Field>(f))];
        if (valid && now == key && static_cast<int>(peaks.size()) == columns)
            return;
        key = now;
        valid = true;
        constexpr double rate = 16000.0;
        seconds = std::clamp(now[pu::decay] * 0.0015, 0.06, 3.0);
        const auto samples = pu::renderPad(params, pad, rate, static_cast<int>(seconds * rate));
        peaks.assign(static_cast<size_t>(std::max(1, columns)), 0.0f);
        float top = 1.0e-4f;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const auto col =
                std::min(peaks.size() - 1, i * peaks.size() / std::max<size_t>(1, samples.size()));
            peaks[col] = std::max(peaks[col], std::abs(samples[i]));
            top = std::max(top, std::abs(samples[i]));
        }
        for (auto& p : peaks)
            p /= top;
    }
};

// The waveform as a mirrored fill with an outline, in `r`.
void drawPeaks(juce::Graphics& g,
               const std::vector<float>& peaks,
               juce::Rectangle<float> r,
               juce::Colour hue,
               float glow)
{
    if (peaks.size() < 2)
        return;
    juce::Path shape;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f;
    const auto x = [&](size_t i)
    { return r.getX() + static_cast<float>(i) / (peaks.size() - 1) * r.getWidth(); };
    shape.startNewSubPath(x(0), mid - peaks[0] * half);
    for (size_t i = 1; i < peaks.size(); ++i)
        shape.lineTo(x(i), mid - peaks[i] * half);
    for (size_t i = peaks.size(); i-- > 0;)
        shape.lineTo(x(i), mid + peaks[i] * half);
    shape.closeSubPath();
    g.setGradientFill(juce::ColourGradient(hue.withAlpha(0.55f + 0.35f * glow),
                                           r.getX(),
                                           0.0f,
                                           hue.withAlpha(0.12f),
                                           r.getRight(),
                                           0.0f,
                                           false));
    g.fillPath(shape);
    g.setColour(hue.withAlpha(0.9f));
    g.strokePath(shape, juce::PathStrokeType(1.0f));
}

// ----- a pad --------------------------------------------------------------------------------------

class PadTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PadTile(ModuleCard& card, int pad) : card_(card), pad_(pad)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip(juce::String(pu::padName(pad)) + " on " + noteName(pu::padNote(pad)) + ": click to edit");
    }
    std::function<void(int)> onSelect;
    bool selected = false;
    float glow = 0.0f; // 0..1: how lit the pad is now

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = padHue(theme, pad_);
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        // Glass, lit from within by the hit.
        g.setColour(c.surface1.withAlpha(0.78f));
        g.fillRoundedRectangle(r, theme.radius.md);
        if (glow > 0.01f)
        {
            g.setGradientFill(juce::ColourGradient(hue.withAlpha(0.45f * glow),
                                                   r.getCentreX(),
                                                   r.getCentreY(),
                                                   hue.withAlpha(0.0f),
                                                   r.getX(),
                                                   r.getY(),
                                                   true));
            g.fillRoundedRectangle(r, theme.radius.md);
        }
        g.setColour(c.textPrimary.withAlpha(0.07f));
        g.drawHorizontalLine(
            static_cast<int>(r.getY()) + 1, r.getX() + theme.radius.md, r.getRight() - theme.radius.md);
        g.setColour(selected ? hue : hue.withAlpha(0.25f + 0.6f * glow));
        g.drawRoundedRectangle(r, theme.radius.md, selected ? 1.6f : 1.0f);
        // The name, the note and the sound.
        auto inner = r.reduced(8.0f, 6.0f);
        auto top = inner.removeFromTop(14.0f);
        g.setFont(theme.captionCaps());
        g.setColour(selected || glow > 0.3f ? c.textPrimary : c.textSecondary);
        g.drawText(pu::padShortName(pad_), top, juce::Justification::centredLeft);
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.medium));
        g.setColour(hue);
        g.drawText(noteName(pu::padNote(pad_)), top, juce::Justification::centredRight);
        preview_.update(card_.params(), pad_, std::max(8, static_cast<int>(inner.getWidth() / 2.0f)));
        drawPeaks(g, preview_.peaks, inner.reduced(0.0f, 4.0f), hue, glow);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect(pad_);
    }

private:
    ModuleCard& card_;
    int pad_;
    Preview preview_;
};

// ----- the selected pad's sound -------------------------------------------------------------------

class PadDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PadDisplay(ModuleCard& card) : card_(card)
    {
        setTooltip("Drag across for the decay, up and down for the tune (Shift: fine)");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    int pad = pu::kick;
    float glow = 0.0f;

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto hue = padHue(theme, pad);
        const auto p = b.reduced(10.0f, 10.0f).withTrimmedTop(10.0f).withTrimmedBottom(12.0f);
        preview_.update(card_.params(), pad, std::max(8, static_cast<int>(p.getWidth())));
        // Time ticks under the sound.
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        const double span = preview_.seconds;
        double step = 0.01;
        for (const double s : {0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0})
            if (span / s <= 6.0)
            {
                step = s;
                break;
            }
        for (double t = step; t < span; t += step)
        {
            const float x = p.getX() + static_cast<float>(t / span) * p.getWidth();
            g.setColour(theme.colours.gridBeat);
            g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
            if (x < p.getRight() - 24.0f)
            {
                g.setColour(theme.colours.textTertiary);
                g.drawText(t < 1.0 ? juce::String(static_cast<int>(std::lround(t * 1000.0))) + " ms"
                                   : juce::String(t, 1) + " s",
                           juce::Rectangle<float>(x - 24.0f, p.getBottom() + 1.0f, 48.0f, 11.0f),
                           juce::Justification::centred);
            }
        }
        g.setColour(theme.colours.lineSubtle);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());
        drawPeaks(g, preview_.peaks, p, hue, glow);
        drawWellFrame(g, theme, b);
        // The readout: tune and decay.
        const float tune = card_.param(pu::paramIndex(pad, pu::tune));
        const float decay = card_.param(pu::paramIndex(pad, pu::decay));
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText((tune > 0.0f ? "+" : "") + juce::String(static_cast<int>(std::lround(tune))) + " st   " +
                       juce::String(static_cast<int>(std::lround(decay))) + " ms",
                   b.reduced(10.0f, 6.0f).removeFromTop(14.0f),
                   juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        startTune_ = card_.param(pu::paramIndex(pad, pu::tune));
        startDecay_ = card_.param(pu::paramIndex(pad, pu::decay));
        card_.beginGesture(juce::String(pu::padName(pad)));
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        card_.setParam(pu::paramIndex(pad, pu::decay),
                       startDecay_ *
                           std::exp2(static_cast<float>(e.getDistanceFromDragStartX()) * fine / 120.0f));
        card_.setParam(
            pu::paramIndex(pad, pu::tune),
            std::round(startTune_ - static_cast<float>(e.getDistanceFromDragStartY()) * fine / 6.0f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    Preview preview_;
    float startTune_ = 0.0f, startDecay_ = 0.0f;
};

// The output, steadied on a rising zero crossing.
class Scope final : public juce::Component
{
public:
    Scope(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue), samples_(4096) {}
    void update()
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr)
            return;
        if (!fb->tapsEnabled())
            fb->setTapsEnabled(true);
        if (const auto position = fb->tapPosition(1); position != lastTap_)
        {
            lastTap_ = position;
            if (fb->readTap(1, samples_.data(), static_cast<int>(samples_.size())))
                repaint();
        }
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto p = b.reduced(6.0f, 6.0f);
        g.setColour(theme.colours.lineSubtle);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());
        juce::Path path;
        const int n = static_cast<int>(samples_.size());
        for (int i = 0; i < n; i += 8)
        {
            const float x = p.getX() + static_cast<float>(i) / static_cast<float>(n) * p.getWidth();
            const float y =
                p.getCentreY() -
                juce::jlimit(-1.0f, 1.0f, samples_[static_cast<size_t>(i)] * 1.6f) * p.getHeight() * 0.48f;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(hue_.withAlpha(0.3f));
        g.strokePath(path, juce::PathStrokeType(3.5f));
        g.setColour(hue_);
        g.strokePath(path, juce::PathStrokeType(1.2f));
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    std::vector<float> samples_;
    std::uint64_t lastTap_ = 0;
};

// ----- the editor ---------------------------------------------------------------------------------

class PulsarCard final : public ModuleCard
{
public:
    PulsarCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), display_(*this),
          scope_(*this, paletteColour(theme, "Rose"))
    {
        for (int pad = 0; pad < pu::padCount; ++pad)
        {
            tiles_[static_cast<size_t>(pad)] = std::make_unique<PadTile>(*this, pad);
            tiles_[static_cast<size_t>(pad)]->onSelect = [this](int p) { select(p); };
            addAndMakeVisible(*tiles_[static_cast<size_t>(pad)]);
        }
        addAndMakeVisible(display_);
        addAndMakeVisible(scope_);
        const auto percent = [](double v)
        { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; };
        for (int pad = 0; pad < pu::padCount; ++pad)
        {
            const auto hue = padHue(theme, pad);
            std::vector<Knob*> knobs;
            const auto add =
                [&](pu::Field field, const juce::String& label, std::function<juce::String(double)> format)
            {
                auto& k = addKnob(pu::paramIndex(pad, field), label);
                k.setArcColour(hue);
                if (format)
                    k.format = std::move(format);
                knobs.push_back(&k);
            };
            add(pu::tune,
                "Tune",
                [](double v)
                {
                    const int n = static_cast<int>(std::lround(v));
                    return (n > 0 ? "+" : "") + juce::String(n) + " st";
                });
            add(pu::decay, "Decay", {});
            add(pu::tone, pu::toneName(pad), percent);
            add(pu::snap, pu::snapName(pad), percent);
            add(pu::level, "Level", {});
            add(pu::pan,
                "Pan",
                [](double v)
                {
                    const int n = static_cast<int>(std::lround(v * 100.0));
                    return n == 0 ? juce::String("C") : (n < 0 ? "L " : "R ") + juce::String(std::abs(n));
                });
            padKnobs_[static_cast<size_t>(pad)] = knobs;
        }
        const auto amber = paletteColour(theme, "Rose");
        for (auto [p, label] : {std::pair<int, const char*>{pu::drive, "Drive"},
                                std::pair<int, const char*>{pu::velocity, "Velocity"},
                                std::pair<int, const char*>{pu::output, "Level"}})
        {
            auto& k = addKnob(p, label);
            k.setArcColour(amber);
            if (p != pu::output)
                k.format = percent;
            masterKnobs_.push_back(&k);
        }
        for (auto* b : {&previous_, &next_, &kitName_})
        {
            b->getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
            addAndMakeVisible(b);
        }
        previous_.setTooltip("Previous kit");
        next_.setTooltip("Next kit");
        kitName_.setTooltip("Kits");
        previous_.onClick = [this] { stepKit(-1); };
        next_.onClick = [this] { stepKit(1); };
        kitName_.onClick = [this] { showKits(); };
        kitName_.setButtonText(currentKitName());
        select(pu::kick);
    }

    // The pads (four columns of 104), the pad editor, padding and gap.
    int preferredWidth() const override
    {
        return 2 * margin + 4 * padW + 3 * gap + gap + 2 * margin + 6 * cellW;
    }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, padW = 104, gap = 8, margin = 8, cellW = 64, captionH = 16;

    void select(int pad)
    {
        selected_ = pad;
        display_.pad = pad;
        for (int p = 0; p < pu::padCount; ++p)
        {
            tiles_[static_cast<size_t>(p)]->selected = p == pad;
            tiles_[static_cast<size_t>(p)]->repaint();
            for (auto* k : padKnobs_[static_cast<size_t>(p)])
            {
                k->setVisible(p == pad);
                if (auto* l = labelFor(*k))
                    l->setVisible(p == pad);
            }
        }
        resized();
        repaint();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        sky_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        star_ = {static_cast<float>(header.getX()) + 22.0f, static_cast<float>(header.getCentreY())};
        wordmark_ = header.removeFromLeft(300).withTrimmedLeft(44);
        auto kit = header.removeFromRight(260).withSizeKeepingCentre(260, theme_.sizes.controlCompact);
        kitBar_ = kit;
        previous_.setBounds(kit.removeFromLeft(theme_.sizes.controlCompact));
        next_.setBounds(kit.removeFromRight(theme_.sizes.controlCompact));
        kitName_.setBounds(kit.reduced(2, 0));
        body.removeFromTop(4);

        // Left: the pads, then the master.
        auto left = body.removeFromLeft(4 * padW + 3 * gap);
        const int padH = std::max(56, (left.getHeight() - knobCellHeight() - captionH - 3 * gap) / 2);
        for (int p = 0; p < pu::padCount; ++p)
        {
            const int row = p / 4, col = p % 4;
            tiles_[static_cast<size_t>(p)]->setBounds(
                left.getX() + col * (padW + gap), left.getY() + row * (padH + gap), padW, padH);
        }
        left.removeFromTop(2 * padH + 2 * gap);
        masterPanel_ = left;
        auto master = left.reduced(margin, margin / 2);
        master.removeFromTop(captionH);
        auto knobs = master.removeFromLeft(3 * 58);
        layoutKnobs(knobs.removeFromTop(knobCellHeight()), 3, masterKnobs_);
        master.removeFromLeft(gap);
        scope_.setBounds(master.withHeight(std::min(master.getHeight(), knobCellHeight())));

        // Right: the selected pad.
        body.removeFromLeft(gap);
        padPanel_ = body;
        auto pad = body.reduced(margin);
        pad.removeFromTop(captionH);
        const auto knobRow = pad.removeFromBottom(knobCellHeight());
        pad.removeFromBottom(gap);
        display_.setBounds(pad);
        layoutKnobs(knobRow, 6, padKnobs_[static_cast<size_t>(selected_)]);
    }

    // ----- drawing ----------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = sky_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(c.canvas.darker(0.6f),
                                               star_.x,
                                               star_.y,
                                               c.surface0,
                                               bounds.getRight(),
                                               bounds.getBottom(),
                                               true));
        g.fillRect(bounds);
        paintStars(g, bounds);
        paintPulsar(g, bounds);
        paintWordmark(g);
        const auto glass = [&](juce::Rectangle<int> r, const juce::String& caption, juce::Colour hue)
        {
            const auto f = r.toFloat();
            g.setColour(c.surface1.withAlpha(0.72f));
            g.fillRoundedRectangle(f, theme_.radius.md);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.drawHorizontalLine(
                static_cast<int>(f.getY()) + 1, f.getX() + theme_.radius.md, f.getRight() - theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(f.reduced(0.5f), theme_.radius.md, 1.0f);
            auto cap = r.reduced(margin, margin / 2).removeFromTop(captionH);
            g.setColour(hue);
            g.fillEllipse(cap.removeFromLeft(8).toFloat().withSizeKeepingCentre(5.0f, 5.0f));
            cap.removeFromLeft(theme_.spacing.xs);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textSecondary);
            g.drawText(caption, cap, juce::Justification::centredLeft);
        };
        glass(masterPanel_, "MASTER", paletteColour(theme_, "Rose"));
        glass(padPanel_,
              juce::String(pu::padName(selected_)).toUpperCase() + juce::String::fromUTF8("  \xc2\xb7  ") +
                  noteName(pu::padNote(selected_)),
              padHue(theme_, selected_));
        g.setColour(c.surface1.withAlpha(0.8f));
        g.fillRoundedRectangle(kitBar_.toFloat(), kitBar_.getHeight() * 0.5f);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(kitBar_.toFloat().reduced(0.5f), kitBar_.getHeight() * 0.5f, 1.0f);
    }

    void paintStars(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        for (int i = 0; i < 56; ++i)
        {
            const float fx = std::fmod(static_cast<float>(i) * 0.7548777f + 0.21f, 1.0f);
            const float fy = std::fmod(static_cast<float>(i) * 0.5698403f + 0.37f, 1.0f);
            const float twinkle = 0.5f + 0.5f * std::sin(time_ * (0.9f + 0.13f * static_cast<float>(i % 5)) +
                                                         static_cast<float>(i) * 1.7f);
            g.setColour(
                theme_.colours.textPrimary.withAlpha(0.1f + 0.28f * twinkle * (i % 4 == 0 ? 1.0f : 0.5f)));
            const float size = i % 11 == 0 ? 1.8f : 1.0f;
            g.fillEllipse(b.getX() + fx * b.getWidth(), b.getY() + fy * b.getHeight(), size, size);
        }
    }

    // The pulsar: a white core with a halo, two beams sweeping round, and rings
    // going out from each hit.
    void paintPulsar(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        const auto& c = theme_.colours;
        const auto beamHue = paletteColour(theme_, "Sky");
        const float flash = juce::jlimit(0.0f, 1.0f, flash_);
        // Rings from the latest hits.
        for (const auto& ring : rings_)
        {
            if (ring.age < 0.0f || ring.age > ringLife)
                continue;
            const float t = ring.age / ringLife;
            const float radius = 14.0f + t * 260.0f;
            g.setColour(ring.hue.withAlpha(0.35f * (1.0f - t)));
            g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(star_), 1.5f);
        }
        // The beams.
        for (int side = 0; side < 2; ++side)
        {
            const float angle = beamAngle_ + static_cast<float>(side) * juce::MathConstants<float>::pi;
            const float length = 170.0f + 120.0f * flash;
            const juce::Point<float> tip =
                star_ + juce::Point<float>(std::cos(angle), std::sin(angle)) * length;
            const juce::Point<float> normal(-std::sin(angle), std::cos(angle));
            juce::Path beam;
            beam.startNewSubPath(star_ + normal * 2.0f);
            beam.lineTo(tip + normal * 16.0f);
            beam.lineTo(tip - normal * 16.0f);
            beam.lineTo(star_ - normal * 2.0f);
            beam.closeSubPath();
            g.setGradientFill(juce::ColourGradient(beamHue.withAlpha(0.35f + 0.45f * flash),
                                                   star_.x,
                                                   star_.y,
                                                   beamHue.withAlpha(0.0f),
                                                   tip.x,
                                                   tip.y,
                                                   false));
            g.fillPath(beam);
        }
        // Halo and core.
        const float halo = 16.0f + 10.0f * flash;
        g.setGradientFill(juce::ColourGradient(beamHue.withAlpha(0.55f + 0.4f * flash),
                                               star_.x,
                                               star_.y,
                                               beamHue.withAlpha(0.0f),
                                               star_.x + halo,
                                               star_.y,
                                               true));
        g.fillEllipse(juce::Rectangle<float>(halo * 2.0f, halo * 2.0f).withCentre(star_));
        g.setColour(c.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(6.0f + 2.0f * flash, 6.0f + 2.0f * flash).withCentre(star_));
        juce::ignoreUnused(b);
    }

    void paintWordmark(juce::Graphics& g) const
    {
        const auto area = wordmark_.toFloat();
        const auto font = theme_.font(Theme::Face::ui, 26.0f, theme_.fonts.weight.semibold);
        juce::GlyphArrangement glyphs;
        float x = area.getX();
        const float baseline = area.getCentreY() + 4.0f;
        for (const auto ch : juce::String("PULSAR"))
        {
            juce::GlyphArrangement one;
            one.addLineOfText(font, juce::String::charToString(ch), x, baseline);
            const float w = one.getBoundingBox(0, -1, true).getWidth();
            glyphs.addGlyphArrangement(one);
            x += w + 6.0f;
        }
        juce::Path text;
        glyphs.createPath(text);
        const auto box = text.getBounds();
        g.setGradientFill(juce::ColourGradient(paletteColour(theme_, "Sky"),
                                               box.getX(),
                                               0.0f,
                                               paletteColour(theme_, "Coral"),
                                               box.getRight(),
                                               0.0f,
                                               false));
        g.fillPath(text);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.medium));
        g.setColour(theme_.colours.textTertiary);
        g.drawText(juce::String::fromUTF8("DRUM SYNTH  \xc2\xb7  AURAL"),
                   juce::Rectangle<float>(box.getRight() + 10.0f, baseline - 10.0f, 150.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // ----- kits --------------------------------------------------------------------------------

    juce::String currentKitName() const
    {
        for (const auto& k : pu::kits())
        {
            bool same = k.values.size() <= params_.size();
            for (size_t i = 0; same && i < k.values.size(); ++i)
                same = std::abs(k.values[i] - params_[i]) < 1.0e-4f;
            if (same)
                return juce::String(k.name);
        }
        return "Custom";
    }
    void applyKit(const pu::Kit& kit)
    {
        beginGesture("Kit " + juce::String(kit.name));
        for (size_t i = 0; i < kit.values.size(); ++i)
            setParam(static_cast<int>(i), kit.values[i]);
        endGesture();
        kitName_.setButtonText(currentKitName());
    }
    void stepKit(int direction)
    {
        const auto& list = pu::kits();
        int at = -1;
        const auto current = currentKitName();
        for (size_t i = 0; i < list.size(); ++i)
            if (juce::String(list[i].name) == current)
                at = static_cast<int>(i);
        const int n = static_cast<int>(list.size());
        applyKit(list[static_cast<size_t>(at < 0 ? (direction > 0 ? 0 : n - 1) : (at + direction + n) % n)]);
    }
    void showKits()
    {
        juce::PopupMenu menu;
        const auto current = currentKitName();
        for (size_t i = 0; i < pu::kits().size(); ++i)
            menu.addItem(static_cast<int>(i) + 1,
                         juce::String(pu::kits()[i].name),
                         true,
                         current == juce::String(pu::kits()[i].name));
        juce::Component::SafePointer<PulsarCard> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(kitName_),
                           [safe](int r)
                           {
                               if (safe != nullptr && r > 0 && static_cast<size_t>(r) <= pu::kits().size())
                                   safe->applyKit(pu::kits()[static_cast<size_t>(r - 1)]);
                           });
    }

    // ----- live --------------------------------------------------------------------------------

    void paramsChanged() override
    {
        kitName_.setButtonText(currentKitName());
        for (auto& t : tiles_)
            t->repaint();
        display_.repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        const float dt = static_cast<float>(elapsedMs * 0.001);
        bool lit = false;
        if (const auto* live = liveProcessor())
            if (const auto* activity = pu::activity(*live))
                for (int p = 0; p < pu::padCount; ++p)
                {
                    const auto hits = activity->hits[static_cast<size_t>(p)].load(std::memory_order_relaxed);
                    auto& tile = *tiles_[static_cast<size_t>(p)];
                    float glow = tile.glow * std::exp(-dt * 5.0f);
                    if (hits != seenHits_[static_cast<size_t>(p)])
                    {
                        if (seenHits_[static_cast<size_t>(p)] != 0 || hits < 64) // not a stale count at open
                        {
                            glow = 1.0f;
                            flash_ = 1.0f;
                            rings_[nextRing_] = {0.0f, padHue(theme_, p)};
                            nextRing_ = (nextRing_ + 1) % rings_.size();
                        }
                        seenHits_[static_cast<size_t>(p)] = hits;
                    }
                    glow = std::max(glow,
                                    activity->level[static_cast<size_t>(p)].load(std::memory_order_relaxed));
                    if (std::abs(glow - tile.glow) > 0.01f)
                    {
                        tile.glow = glow;
                        tile.repaint();
                    }
                    if (p == selected_ && std::abs(glow - display_.glow) > 0.02f)
                    {
                        display_.glow = glow;
                        display_.repaint();
                    }
                    lit = lit || glow > 0.01f;
                }
        scope_.update();
        // The sky: the beams turn and the rings travel while anything plays.
        flash_ *= std::exp(-dt * 4.0f);
        bool rings = false;
        for (auto& r : rings_)
            if (r.age >= 0.0f && r.age <= ringLife)
            {
                r.age += dt;
                rings = true;
            }
        if (theme_.motion.reduceMotion || (!lit && !rings && flash_ < 0.01f))
            return;
        beamAngle_ += dt * (0.6f + 2.0f * flash_);
        time_ += dt;
        sinceSky_ += elapsedMs;
        if (sinceSky_ >= 33.0)
        {
            sinceSky_ = 0.0;
            repaint(sky_.withHeight(headerH + 12));
            repaint(masterPanel_.getUnion(padPanel_).expanded(gap)); // the rings pass behind the glass
        }
    }

    struct Ring
    {
        float age = -1.0f; // seconds since the hit; < 0 = unused
        juce::Colour hue;
    };
    static constexpr float ringLife = 0.9f;

    std::array<std::unique_ptr<PadTile>, pu::padCount> tiles_;
    PadDisplay display_;
    Scope scope_;
    std::array<std::vector<Knob*>, pu::padCount> padKnobs_;
    std::vector<Knob*> masterKnobs_;
    juce::TextButton previous_{juce::String::fromUTF8("\xe2\x80\xb9")},
        next_{juce::String::fromUTF8("\xe2\x80\xba")};
    juce::TextButton kitName_;
    juce::Rectangle<int> sky_, wordmark_, kitBar_, masterPanel_, padPanel_;
    juce::Point<float> star_;
    int selected_ = pu::kick;
    std::array<std::uint32_t, pu::padCount> seenHits_{};
    std::array<Ring, 6> rings_{};
    size_t nextRing_ = 0;
    float flash_ = 0.0f, beamAngle_ = -0.4f, time_ = 0.0f;
    double sinceSky_ = 0.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createPulsarEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<PulsarCard>(theme, host, channelId, insert);
}

} // namespace aural::ui::strip
