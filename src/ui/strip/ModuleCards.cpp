// The six channel strip cards (screens/channel-strip.md 12.3-12.8): each lays out
// its display and controls on the shared ModuleCard.

#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/SidechainMenu.h"
#include "ui/components/Accessibility.h"
#include "ui/strip/CardParts.h"
#include "ui/strip/EqGraph.h"
#include "ui/strip/Graphs.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

namespace northlight::ui::strip
{

namespace ps = plugins::strip;

namespace
{
// ----- gate -------------------------------------------------------------------------------------

class GateCard final : public ModuleCard
{
public:
    GateCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module),
          graph_(*this,
                 HistoryGraph::Style{.bottomDb = -80.0f,
                                     .levelFromExtra = true,
                                     .reductionFromTop = false,
                                     .thresholdParam = ps::gate::threshold,
                                     .gateLane = true,
                                     .outputFill = false}),
          threshold_(*this, ps::gate::threshold, ThresholdMeter::Level::extra, -80.0f, "KEY", colour_)
    {
        graph_.setTooltip("Drag: threshold");
        addAndMakeVisible(graph_);
        addAndMakeVisible(threshold_);
        for (int p : {ps::gate::range,
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
        auto display = takeDisplay(body, 2 * knobCellHeight());
        threshold_.setBounds(display.removeFromLeft(ThresholdMeter::preferredWidth));
        display.removeFromLeft(4);
        graph_.setBounds(display);
        layoutKnobs(body, 3, knobs_);
    }
    void paramsChanged() override
    {
        graph_.repaint();
        threshold_.repaint();
    }
    void framesArrived(double) override
    {
        graph_.repaint();
        threshold_.repaint();
    }
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
    ThresholdMeter threshold_;
    std::vector<Knob*> knobs_;
};

// ----- compressor ---------------------------------------------------------------------------------

class CompressorCard final : public ModuleCard
{
public:
    CompressorCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
        : ModuleCard(theme, host, channelId, module), curve_(*this),
          history_(*this, HistoryGraph::Style{.thresholdParam = ps::compressor::threshold}),
          bars_(*this, {LevelBars::Kind::reduction, LevelBars::Kind::output}),
          threshold_(*this,
                     ps::compressor::threshold,
                     ThresholdMeter::Level::input,
                     -60.0f,
                     "IN",
                     theme.colours.meterHot)
    {
        addAndMakeVisible(threshold_);
        curve_.setTooltip("Drag: threshold, ratio");
        history_.setTooltip("Drag: threshold");
        addAndMakeVisible(curve_);
        addAndMakeVisible(history_);
        addAndMakeVisible(bars_);
        for (int p : {ps::compressor::ratio,
                      ps::compressor::attack,
                      ps::compressor::release,
                      ps::compressor::knee,
                      ps::compressor::makeup,
                      ps::compressor::mix})
            knobs_.push_back(&addKnob(p));
        auto_ = &addToggle(ps::compressor::autoMakeup, "Auto make-up");
        detector_ = &addChoice(ps::compressor::detector);
        // The insert (not the strip module) takes a key: its Sidechain button
        // picks the source, and lights with its name while one is set.
        if (module.type == plugins::types::compressor)
        {
            sidechain_ = std::make_unique<PillToggle>(theme, "Sidechain", colour());
            sidechain_->setClickingTogglesState(false);
            sidechain_->onClick = [this] { showSidechainMenu(); };
            addAndMakeVisible(*sidechain_);
            insertChanged(module);
        }
    }

    int preferredWidth() const override { return 7 * knobCellWidth() + 2 * theme_.spacing.sm + 10; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        const int optionsH = theme_.sizes.controlCompact;
        auto display = takeDisplay(body, knobCellHeight() + optionsH + 6);
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        threshold_.setBounds(display.removeFromRight(ThresholdMeter::preferredWidth));
        display.removeFromRight(4);
        curve_.setBounds(display.removeFromLeft(std::min(display.getHeight(), display.getWidth() / 2)));
        display.removeFromLeft(6);
        history_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 6, knobs_);
        body.removeFromTop(6);
        auto options = body.removeFromTop(optionsH);
        auto_->setBounds(options.removeFromLeft(110).reduced(0, 2));
        options.removeFromLeft(theme_.spacing.sm);
        detector_->setBounds(options.removeFromLeft(std::min(options.getWidth(), 120)));
        if (sidechain_ != nullptr)
        {
            options.removeFromLeft(theme_.spacing.sm);
            sidechain_->setBounds(options.removeFromRight(std::min(options.getWidth(), 170)).reduced(0, 2));
        }
    }
    void insertChanged(const core::Insert& insert) override
    {
        if (sidechain_ == nullptr)
            return;
        const auto* project = host_.project ? host_.project() : nullptr;
        const bool keyed = insert.sidechain != core::invalidId;
        const auto name = project != nullptr ? sidechainLabel(*project, insert) : juce::String("?");
        sidechain_->setButtonText(keyed ? "Key: " + name : juce::String("Sidechain"));
        showToggle(*sidechain_, keyed);
        sidechain_->setTooltip(keyed ? "Key: " + name : juce::String("Key input"));
        // Keyed, the input meter and the history's grey trace show the key, and
        // the threshold acts on it.
        threshold_.setCaption(keyed ? "KEY" : "IN");
        threshold_.setTooltip("Drag: threshold");
        history_.setTooltip("Drag: threshold");
    }
    void showSidechainMenu()
    {
        const auto* project = host_.project ? host_.project() : nullptr;
        const auto* channel = project != nullptr ? project->findChannel(channelId_) : nullptr;
        const auto* insert = channel != nullptr ? channel->findInsert(moduleId_) : nullptr;
        if (insert == nullptr)
            return;
        juce::PopupMenu menu;
        auto actions = std::make_shared<std::vector<std::function<void()>>>();
        addSidechainItems(menu, *actions, *project, channelId_, *insert, host_.onCommand);
        juce::Component::SafePointer<juce::Component> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(sidechain_.get()),
                           [safe, actions](int result)
                           {
                               if (safe != nullptr && result >= 1 &&
                                   static_cast<size_t>(result) <= actions->size())
                                   (*actions)[static_cast<size_t>(result) - 1]();
                           });
    }
    void paramsChanged() override
    {
        curve_.repaint();
        history_.repaint();
        threshold_.repaint();
    }
    void framesArrived(double) override
    {
        curve_.repaint();
        history_.repaint();
        bars_.repaint();
        threshold_.repaint();
    }
    juce::String headerStatus() const override
    {
        return isOn() ? reductionText(reductionReadoutDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    TransferCurve curve_;
    HistoryGraph history_;
    LevelBars bars_;
    ThresholdMeter threshold_;
    std::vector<Knob*> knobs_;
    PillToggle* auto_ = nullptr;
    SegmentedControl* detector_ = nullptr;
    std::unique_ptr<PillToggle> sidechain_; // the insert only
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
            band.on->setTooltip("Band on/off");
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
        graph_.setBounds(takeDisplay(body, controlsH));
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
                 HistoryGraph::Style{
                     .levelFromExtra = true, .outputLine = false, .thresholdParam = ps::deesser::threshold}),
          threshold_(*this,
                     ps::deesser::threshold,
                     ThresholdMeter::Level::extra,
                     -60.0f,
                     "BAND",
                     theme.colours.meterHot)
    {
        addAndMakeVisible(threshold_);
        graph_.setTooltip("Drag: threshold");
        addAndMakeVisible(graph_);
        for (int p : {ps::deesser::frequency, ps::deesser::range})
            knobs_.push_back(&addKnob(p));
        mode_ = &addChoice(ps::deesser::mode);
        listen_ = &addToggle(ps::deesser::listen, "Listen");
        listen_->setTooltip("Listen");
    }

    int preferredWidth() const override { return 4 * knobCellWidth() + 2 * theme_.spacing.sm; }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        const int optionsH = theme_.sizes.controlCompact;
        auto display = takeDisplay(body, knobCellHeight() + optionsH + 6);
        threshold_.setBounds(display.removeFromRight(ThresholdMeter::preferredWidth));
        display.removeFromRight(4);
        graph_.setBounds(display);
        layoutKnobs(body.removeFromTop(knobCellHeight()), 2, knobs_);
        body.removeFromTop(6);
        auto options = body.removeFromTop(optionsH);
        listen_->setBounds(options.removeFromRight(64).reduced(0, 2));
        options.removeFromRight(theme_.spacing.sm);
        mode_->setBounds(options.removeFromLeft(std::min(options.getWidth(), 120)));
    }
    void paramsChanged() override
    {
        graph_.repaint();
        threshold_.repaint();
    }
    void framesArrived(double) override
    {
        graph_.repaint();
        threshold_.repaint();
    }
    juce::String headerStatus() const override
    {
        return isOn() ? reductionText(reductionReadoutDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    HistoryGraph graph_;
    ThresholdMeter threshold_;
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

// The strip's limiter and the built-in one: the reduction history with the
// ceiling to drag, the input against the ceiling, the reduction and output
// bars, and two knobs under them.
class LimiterCard final : public ModuleCard
{
public:
    LimiterCard(const Theme& theme,
                CardHost& host,
                core::Id channelId,
                const core::Insert& module,
                LimiterLayout layout)
        : ModuleCard(theme, host, channelId, module), layout_(layout),
          graph_(*this, HistoryGraph::Style{.bottomDb = -36.0f, .thresholdParam = layout.ceiling}),
          bars_(*this, {LevelBars::Kind::reduction, LevelBars::Kind::output}),
          ceiling_(*this, layout.ceiling, ThresholdMeter::Level::input, -36.0f, "IN", theme.colours.meterHot)
    {
        addAndMakeVisible(ceiling_);
        graph_.setTooltip("Drag: ceiling");
        addAndMakeVisible(graph_);
        addAndMakeVisible(bars_);
        for (int p : layout.knobs)
            knobs_.push_back(&addKnob(p));
    }

    int preferredWidth() const override
    {
        return layout_.width > 0 ? layout_.width : 3 * knobCellWidth() + 78 + 2 * theme_.spacing.sm + 10;
    }
    int preferredBodyHeight() const override
    {
        return layout_.displayHeight > 0 ? layout_.displayHeight + knobCellHeight()
                                         : ModuleCard::preferredBodyHeight();
    }

private:
    void layoutBody(juce::Rectangle<int> body) override
    {
        auto display = takeDisplay(body, knobCellHeight());
        bars_.setBounds(display.removeFromRight(bars_.preferredWidth()));
        ceiling_.setBounds(display.removeFromRight(ThresholdMeter::preferredWidth));
        display.removeFromRight(4);
        graph_.setBounds(display);
        auto row = body.removeFromTop(knobCellHeight());
        layoutKnobs(layout_.knobColumns ? row.removeFromLeft(2 * knobCellWidth()) : row, 2, knobs_);
    }
    void paramsChanged() override
    {
        graph_.repaint();
        ceiling_.repaint();
    }
    void framesArrived(double) override
    {
        graph_.repaint();
        bars_.repaint();
        ceiling_.repaint();
    }
    juce::String headerStatus() const override
    {
        return isOn() ? reductionText(reductionReadoutDb()) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    LimiterLayout layout_;
    HistoryGraph graph_;
    LevelBars bars_;
    ThresholdMeter ceiling_;
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
    if (module.type == t::opto)
        return createOptoCard(theme, host, channelId, module);
    if (module.type == t::deesser)
        return std::make_unique<DeEsserCard>(theme, host, channelId, module);
    if (module.type == t::saturation)
        return std::make_unique<SaturationCard>(theme, host, channelId, module);
    if (module.type == t::limiter)
        return std::make_unique<LimiterCard>(
            theme,
            host,
            channelId,
            module,
            LimiterLayout{ps::limiter::ceiling, {ps::limiter::input, ps::limiter::release}});
    return nullptr;
}

std::unique_ptr<ModuleCard> createLimiterCard(
    const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert, LimiterLayout layout)
{
    return std::make_unique<LimiterCard>(theme, host, channelId, insert, layout);
}

std::unique_ptr<ModuleCard>
createCompressorCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<CompressorCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
