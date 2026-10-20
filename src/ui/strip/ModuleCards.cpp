// The six channel strip cards (12-channel-strip.md 12.3-12.8): each lays out
// its display and controls on the shared ModuleCard.

#include "plugins/ChannelStrip.h"
#include "ui/strip/EqGraph.h"
#include "ui/strip/Graphs.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

namespace aural::ui::strip
{

namespace ps = plugins::strip;

namespace
{
// The display's share of the body above `controlsHeight` of controls.
juce::Rectangle<int> takeDisplay(juce::Rectangle<int>& body, int controlsHeight, int minimum = 96)
{
    const int h = std::max(minimum, body.getHeight() - controlsHeight);
    auto display = body.removeFromTop(h);
    body.removeFromTop(6);
    return display;
}

juce::String gainReductionText(float db)
{
    return db >= 0.1f ? "-" + juce::String(db, 1) + " dB" : juce::String();
}

// ----- gate -------------------------------------------------------------------------------------

class GateCard final : public ModuleCard
{
public:
    GateCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module),
          graph_(*this,
                 HistoryGraph::Style{0.0f, -80.0f, true, true, false, ps::gate::threshold, true, 3.0f, false})
    {
        graph_.setTooltip(
            "Key level (grey) and output; drag to set the threshold. The lane shows the gate open.");
        addAndMakeVisible(graph_);
        for (int p : {ps::gate::threshold,
                      ps::gate::range,
                      ps::gate::attack,
                      ps::gate::hold,
                      ps::gate::release,
                      ps::gate::keyHighPass,
                      ps::gate::keyLowPass})
            knobs_.push_back(&addKnob(p));
    }

    int preferredWidth() const override { return 4 * knobCellWidth() + 2 * theme_.spacing.sm; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        graph_.setBounds(takeDisplay(body, 2 * knobCellHeight()));
        layoutKnobs(body, 4, knobs_);
    }
    void paramsChanged() override { graph_.repaint(); }
    void framesArrived(double) override { graph_.repaint(); }
    juce::String headerStatus() const override
    {
        if (!isOn() || historySize() == 0 || inputDb() < -100.0f)
            return {};
        return reductionDb() < 1.0f ? "open" : "closed";
    }
    juce::Colour headerStatusColour() const override
    {
        return reductionDb() < 1.0f ? colour_ : theme_.colours.textTertiary;
    }

    HistoryGraph graph_;
    std::vector<Knob*> knobs_;
};

// ----- compressor ---------------------------------------------------------------------------------

class CompressorCard final : public ModuleCard
{
public:
    CompressorCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module), curve_(*this),
          history_(
              *this,
              HistoryGraph::Style{0.0f, -60.0f, false, true, true, ps::compressor::threshold, false, 3.0f}),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::reduction, LevelBars::Kind::output})
    {
        curve_.setTooltip(
            "The curve with make-up; drag left of the knee for the threshold, right of it for the ratio");
        history_.setTooltip(
            "Input (grey), output and gain reduction (from the top); drag to set the threshold");
        addAndMakeVisible(curve_);
        addAndMakeVisible(history_);
        addAndMakeVisible(bars_);
        for (int p : {ps::compressor::threshold,
                      ps::compressor::ratio,
                      ps::compressor::attack,
                      ps::compressor::release,
                      ps::compressor::knee,
                      ps::compressor::makeup,
                      ps::compressor::mix})
            knobs_.push_back(&addKnob(p));
        auto_ = &addToggle(ps::compressor::autoMakeup, "Auto make-up");
        detector_ = &addChoice(ps::compressor::detector);
    }

    int preferredWidth() const override { return 7 * knobCellWidth() + 2 * theme_.spacing.sm; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        const int optionsH = theme_.sizes.controlCompact;
        auto display = takeDisplay(body, knobCellHeight() + optionsH + 6);
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        curve_.setBounds(display.removeFromLeft(std::min(display.getHeight(), display.getWidth() / 2)));
        display.removeFromLeft(6);
        history_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 7, knobs_);
        body.removeFromTop(6);
        auto options = body.removeFromTop(optionsH);
        auto_->setBounds(options.removeFromLeft(110).reduced(0, 2));
        options.removeFromLeft(theme_.spacing.sm);
        detector_->setBounds(options.removeFromLeft(std::min(options.getWidth(), 120)));
    }
    void paramsChanged() override
    {
        curve_.repaint();
        history_.repaint();
    }
    void framesArrived(double) override
    {
        curve_.repaint();
        history_.repaint();
        bars_.repaint();
    }
    juce::String headerStatus() const override
    {
        return isOn() ? gainReductionText(reductionDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    TransferCurve curve_;
    HistoryGraph history_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
    PillToggle* auto_ = nullptr;
    SegmentedControl* detector_ = nullptr;
};

// ----- EQ -----------------------------------------------------------------------------------------

// The band selector under the graph: a chip per band in its hue, filled when
// the band is on; click selects, double-click switches.
class BandChips final : public juce::Component
{
public:
    explicit BandChips(ModuleCard& card) : card_(card) {}

    int selected = ps::eq::band2;
    std::function<void(int)> onSelect;

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        for (int b = 0; b < ps::eq::bandCount; ++b)
        {
            const auto r = chip(b).toFloat().reduced(2.0f, 1.0f);
            const bool on = card_.param(ps::eq::paramIndex(b, ps::eq::on)) >= 0.5f;
            const auto colour = bandColour(theme, b);
            g.setColour(on ? colour.withAlpha(b == selected ? 0.45f : 0.22f) : theme.colours.surface2);
            g.fillRoundedRectangle(r, theme.radius.sm);
            if (b == selected)
            {
                g.setColour(on ? colour : theme.colours.textSecondary);
                g.drawRoundedRectangle(r, theme.radius.sm, 1.5f);
            }
            g.setFont(theme.caption());
            g.setColour(on ? theme.colours.textPrimary : theme.colours.textTertiary);
            g.drawText(name(b), r, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int b = 0; b < ps::eq::bandCount; ++b)
            if (chip(b).contains(e.getPosition()))
            {
                selected = b;
                if (onSelect)
                    onSelect(b);
                repaint();
            }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        for (int b = 0; b < ps::eq::bandCount; ++b)
            if (chip(b).contains(e.getPosition()))
            {
                const int index = ps::eq::paramIndex(b, ps::eq::on);
                card_.beginGesture("Switch band");
                card_.setParam(index, card_.param(index) >= 0.5f ? 0.0f : 1.0f);
                card_.endGesture();
            }
    }

private:
    static juce::String name(int band)
    {
        return band == ps::eq::lowCut ? "Low cut" : band == ps::eq::highCut ? "High cut" : juce::String(band);
    }
    juce::Rectangle<int> chip(int band) const
    {
        // The cuts are wider than the numbered bands.
        const int w = getWidth();
        const int cut = w * 2 / 9, mid = (w - 2 * cut) / 4;
        if (band == ps::eq::lowCut)
            return {0, 0, cut, getHeight()};
        if (band == ps::eq::highCut)
            return {w - cut, 0, cut, getHeight()};
        return {cut + (band - ps::eq::band1) * mid, 0, mid, getHeight()};
    }

    ModuleCard& card_;
};

class EqCard final : public ModuleCard
{
public:
    EqCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module), graph_(*this), chips_(*this)
    {
        addAndMakeVisible(graph_);
        addAndMakeVisible(chips_);
        graph_.onSelect = [this](int band) { select(band); };
        chips_.onSelect = [this](int band) { select(band); };
        // Every band's controls exist; the selected band's are shown.
        for (int b = 0; b < ps::eq::bandCount; ++b)
        {
            auto& band = bands_[static_cast<size_t>(b)];
            band.on = &addToggle(ps::eq::paramIndex(b, ps::eq::on), "On");
            band.on->setTooltip("Switch the band on or off");
            band.type = &addChoice(ps::eq::paramIndex(b, ps::eq::type));
            band.frequency = &addKnob(ps::eq::paramIndex(b, ps::eq::frequency), "Frequency");
            if (!ps::eq::isCut(b))
            {
                band.gain = &addKnob(ps::eq::paramIndex(b, ps::eq::gain), "Gain");
                band.q = &addKnob(ps::eq::paramIndex(b, ps::eq::q), "Q");
            }
            for (auto* k : {band.frequency, band.gain, band.q})
                if (k != nullptr)
                    k->setArcColour(bandColour(theme, b));
        }
        output_ = &addKnob(ps::eq::output, "Output");
        select(ps::eq::band2);
    }

    int preferredWidth() const override { return 480; }

private:
    struct Band
    {
        PillToggle* on = nullptr;
        SegmentedControl* type = nullptr;
        Knob* frequency = nullptr;
        Knob* gain = nullptr;
        Knob* q = nullptr;
    };

    void select(int band)
    {
        selected_ = band;
        graph_.setSelectedBand(band);
        chips_.selected = band;
        chips_.repaint();
        for (int b = 0; b < ps::eq::bandCount; ++b)
        {
            const bool show = b == band;
            const auto& controls = bands_[static_cast<size_t>(b)];
            controls.on->setVisible(show);
            controls.type->setVisible(show);
            for (auto* k : {controls.frequency, controls.gain, controls.q})
                if (k != nullptr)
                {
                    k->setVisible(show);
                    if (auto* l = labelFor(*k))
                        l->setVisible(show);
                }
        }
        resized();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int chipsH = 22;
        const int controlsH = chipsH + 6 + theme_.sizes.controlCompact + 4 + knobCellHeight();
        graph_.setBounds(takeDisplay(body, controlsH, 140));
        chips_.setBounds(body.removeFromTop(chipsH));
        body.removeFromTop(6);
        const auto& band = bands_[static_cast<size_t>(selected_)];
        auto options = body.removeFromTop(theme_.sizes.controlCompact);
        band.on->setBounds(options.removeFromLeft(48).reduced(0, 2));
        options.removeFromLeft(theme_.spacing.sm);
        band.type->setBounds(
            options.removeFromLeft(std::min(options.getWidth(), band.type->preferredWidth())));
        body.removeFromTop(4);
        auto knobs = body.removeFromTop(knobCellHeight());
        layoutKnobs(knobs.removeFromRight(knobCellWidth()), 1, {output_});
        std::vector<Knob*> shown;
        for (auto* k : {band.frequency, band.gain, band.q})
            if (k != nullptr)
                shown.push_back(k);
        layoutKnobs(knobs.removeFromLeft(3 * knobCellWidth()), 3, shown);
    }

    void paramsChanged() override
    {
        graph_.invalidateCurve();
        chips_.repaint();
    }
    void framesArrived(double elapsedMs) override { graph_.update(elapsedMs); }

    EqGraph graph_;
    BandChips chips_;
    std::array<Band, ps::eq::bandCount> bands_;
    Knob* output_ = nullptr;
    int selected_ = ps::eq::band2;
};

// ----- de-esser -----------------------------------------------------------------------------------

class DeEsserCard final : public ModuleCard
{
public:
    DeEsserCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module),
          graph_(*this,
                 HistoryGraph::Style{0.0f, -60.0f, true, false, true, ps::deesser::threshold, false, 3.0f})
    {
        graph_.setTooltip("The sibilance band's level (grey) and the reduction; drag to set the threshold");
        addAndMakeVisible(graph_);
        for (int p : {ps::deesser::frequency, ps::deesser::threshold, ps::deesser::range})
            knobs_.push_back(&addKnob(p));
        mode_ = &addChoice(ps::deesser::mode);
        listen_ = &addToggle(ps::deesser::listen, "Listen");
        listen_->setTooltip("Hear the sibilance band alone");
    }

    int preferredWidth() const override { return 4 * knobCellWidth() + 2 * theme_.spacing.sm; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        const int optionsH = theme_.sizes.controlCompact;
        graph_.setBounds(takeDisplay(body, knobCellHeight() + optionsH + 6));
        layoutKnobs(body.removeFromTop(knobCellHeight()), 3, knobs_);
        body.removeFromTop(6);
        auto options = body.removeFromTop(optionsH);
        listen_->setBounds(options.removeFromRight(64).reduced(0, 2));
        options.removeFromRight(theme_.spacing.sm);
        mode_->setBounds(options.removeFromLeft(std::min(options.getWidth(), 120)));
    }
    void paramsChanged() override { graph_.repaint(); }
    void framesArrived(double) override { graph_.repaint(); }
    juce::String headerStatus() const override
    {
        return isOn() ? gainReductionText(reductionDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    HistoryGraph graph_;
    std::vector<Knob*> knobs_;
    SegmentedControl* mode_ = nullptr;
    PillToggle* listen_ = nullptr;
};

// ----- saturation ---------------------------------------------------------------------------------

class SaturationCard final : public ModuleCard
{
public:
    SaturationCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module), curve_(*this),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::output})
    {
        curve_.setTooltip("The curve at this drive; the shaded part is what the signal is using");
        addAndMakeVisible(curve_);
        addAndMakeVisible(bars_);
        for (int p :
             {ps::saturation::drive, ps::saturation::tone, ps::saturation::output, ps::saturation::mix})
            knobs_.push_back(&addKnob(p));
        knobs_[1]->format = [](double v)
        {
            if (std::abs(v) < 0.02)
                return juce::String("Neutral");
            return juce::String(v < 0 ? "Dark " : "Bright ") +
                   juce::String(static_cast<int>(std::lround(std::abs(v) * 100.0))) + "%";
        };
        knobs_[1]->setValue(param(ps::saturation::tone), juce::dontSendNotification);
        type_ = &addChoice(ps::saturation::type);
    }

    int preferredWidth() const override { return 4 * knobCellWidth() + 2 * theme_.spacing.sm + 20; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        const int optionsH = theme_.sizes.controlCompact;
        auto display = takeDisplay(body, knobCellHeight() + optionsH + 6);
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        curve_.setBounds(display);
        auto options = body.removeFromTop(optionsH);
        type_->setBounds(
            options.withSizeKeepingCentre(std::min(options.getWidth(), type_->preferredWidth()), optionsH));
        body.removeFromTop(6);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 4, knobs_);
    }
    void paramsChanged() override { curve_.repaint(); }
    void framesArrived(double) override
    {
        curve_.repaint();
        bars_.repaint();
    }

    SaturationCurve curve_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
    SegmentedControl* type_ = nullptr;
};

// ----- limiter ------------------------------------------------------------------------------------

class LimiterCard final : public ModuleCard
{
public:
    LimiterCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module),
          graph_(*this,
                 HistoryGraph::Style{0.0f, -36.0f, false, true, true, ps::limiter::ceiling, false, 3.0f}),
          bars_(*this, {LevelBars::Kind::input, LevelBars::Kind::reduction, LevelBars::Kind::output})
    {
        graph_.setTooltip("Input (grey), output and gain reduction; drag to set the ceiling");
        addAndMakeVisible(graph_);
        addAndMakeVisible(bars_);
        for (int p : {ps::limiter::input, ps::limiter::ceiling, ps::limiter::release})
            knobs_.push_back(&addKnob(p));
    }

    int preferredWidth() const override { return 3 * knobCellWidth() + 78 + 2 * theme_.spacing.sm + 10; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        display.removeFromRight(4);
        graph_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 3, knobs_);
    }
    void paramsChanged() override { graph_.repaint(); }
    void framesArrived(double) override
    {
        graph_.repaint();
        bars_.repaint();
    }
    juce::String headerStatus() const override
    {
        return isOn() ? gainReductionText(reductionDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    HistoryGraph graph_;
    LevelBars bars_;
    std::vector<Knob*> knobs_;
};
} // namespace

std::unique_ptr<ModuleCard>
createModuleCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
{
    namespace t = ps::types;
    if (module.type == t::gate)
        return std::make_unique<GateCard>(theme, host, channelId, module);
    if (module.type == t::eq)
        return std::make_unique<EqCard>(theme, host, channelId, module);
    if (module.type == t::compressor)
        return std::make_unique<CompressorCard>(theme, host, channelId, module);
    if (module.type == t::deesser)
        return std::make_unique<DeEsserCard>(theme, host, channelId, module);
    if (module.type == t::saturation)
        return std::make_unique<SaturationCard>(theme, host, channelId, module);
    if (module.type == t::limiter)
        return std::make_unique<LimiterCard>(theme, host, channelId, module);
    return nullptr;
}

} // namespace aural::ui::strip
