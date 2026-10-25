#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Icons.h"
#include "ui/MeterBallistics.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui::strip
{

// What the module cards need from their owner (the channel strip view).
struct CardHost
{
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> liveInsert;
    // The project (for a card that names other channels: the compressor's key).
    std::function<const core::Project*()> project;
    // A drag on a card's header reorders the strip: the screen position as it
    // moves, `released` at the end.
    std::function<void(core::Id moduleId, juce::Point<int> screen, bool released)> onReorder;
    // The card asked to collapse (or, clicked while collapsed, to expand).
    std::function<void(core::Id moduleId, bool collapse)> onCollapse;
    // The header's model switch asked for another model of the module's slot
    // (plugins::strip::alternatives).
    std::function<void(core::Id moduleId, const std::string& type)> onSwapModel;
};

// A small on/off pill for a two-state parameter ("Auto", "Listen"): the
// module's hue at 25% when on.
class PillToggle final : public juce::Button
{
public:
    PillToggle(const Theme& theme, const juce::String& text, juce::Colour onColour);
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    const Theme& theme_;
    juce::Colour onColour_;
};

// One channel strip module (screens/channel-strip.md 12.2): a card with a 28 px
// header (power in the module's hue, name, a live status readout), a display
// with real-time feedback, and the module's controls. The card keeps the
// model's values (the truth for built-ins) and polls the live processor's
// feedback on tick(): the level history, and the input, output and
// gain-reduction meters with their ballistics. Switched off, the body dims but
// stays live: the display keeps showing the input so a threshold can be set
// before the module is switched on.
class ModuleCard : public juce::Component
{
public:
    using Frame = engine::ProcessorFeedback::Frame;
    static constexpr int historyCapacity = 1024; // frames, about five seconds

    ModuleCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module);
    ~ModuleCard() override;

    core::Id moduleId() const noexcept { return moduleId_; }
    core::Id channelId() const noexcept { return channelId_; }
    const std::string& type() const noexcept { return type_; }
    juce::Colour colour() const noexcept { return colour_; }
    bool isOn() const noexcept { return !bypass_; }
    const Theme& theme() const noexcept { return theme_; }

    // The model changed (values, bypass).
    void refresh(const core::Insert& module);
    // About 60 times a second while visible: reads new feedback.
    void tick(double elapsedMs);
    // The card was hidden (another panel tab): the processor stops measuring
    // and its taps stop until the next tick.
    void stopWatching();
    // The card's width at the strip's height, expanded.
    virtual int preferredWidth() const = 0;
    // Collapsed, a card is a narrow tab: the power button and the name running
    // down it. A click on it expands it.
    void setCollapsed(bool collapsed);
    bool isCollapsed() const noexcept { return collapsed_; }
    int layoutWidth() const { return collapsed_ ? collapsedWidth : preferredWidth(); }
    static constexpr int collapsedWidth = 34;
    // Embedded: the card is the body of another frame (an insert's card in the
    // Editor panel), which draws the surface, the header and the bypass dimming;
    // the card shows only its display and controls.
    void setEmbedded(bool embedded);
    // The height the card's body wants at its preferred width (for an embedded
    // card, whose frame sizes to it).
    virtual int preferredBodyHeight() const { return 220; }
    // The smallest body its layout still works at: the preferred height for a
    // card laid out in pixels; a face that scales (RenderedCard) goes lower.
    virtual int minimumBodyHeight() const { return preferredBodyHeight(); }
    // The whole card's smallest height: its header (unless embedded) and body,
    // or nothing while it is collapsed to a tab.
    int minimumHeight() const;
    // The card's width when its body is `bodyHeight` tall: the preferred width,
    // unless the card scales its body to fit (a hardware face drawn at one size).
    virtual int preferredWidthAt(int bodyHeight) const
    {
        juce::ignoreUnused(bodyHeight);
        return preferredWidth();
    }

    // ----- for the displays ------------------------------------------------------
    float param(int index) const noexcept;
    bool hasParam(int index) const noexcept
    {
        return index >= 0 && static_cast<size_t>(index) < params_.size();
    }
    const float* params() const noexcept { return params_.data(); }
    const engine::ParameterInfo& info(int index) const;
    // A user edit: updates the card at once and sends the command.
    void setParam(int index, float value);
    // A whole set of values chosen at once (a preset): every parameter, each
    // held to its range, as one command named `name`, so one undo step.
    void applyValues(const std::vector<float>& values, const juce::String& name);
    void beginGesture(const juce::String& name);
    void endGesture();
    // The level history, oldest first.
    int historySize() const noexcept { return count_; }
    // Frames received since the card was made (the newest is number total - 1):
    // displays group frames by this absolute count so their columns stay put.
    std::uint64_t historyTotal() const noexcept { return total_; }
    const Frame& historyAt(int i) const noexcept
    {
        return ring_[static_cast<size_t>((head_ - count_ + i + historyCapacity) % historyCapacity)];
    }
    // Meter readings in dB, with ballistics.
    float inputDb() const noexcept { return inDb_; }
    float outputDb() const noexcept { return outDb_; }
    float reductionDb() const noexcept { return grDb_; }
    float extraDb() const noexcept { return extraDb_; }
    // The same as numbers for a readout: the loudest of each quarter second, so
    // a printed value is readable rather than flickering every frame.
    float inputReadoutDb() const noexcept { return inReadout_.value(); }
    float outputReadoutDb() const noexcept { return outReadout_.value(); }
    float reductionReadoutDb() const noexcept { return grReadout_.value(); }
    // The live processor's feedback (null before the engine has an instance).
    engine::ProcessorFeedback* liveFeedback() const noexcept;
    // The live processor itself (null before the engine has an instance).
    const engine::InsertProcessor* liveProcessor() const noexcept { return live_.get(); }
    double sampleRate() const noexcept;
    // The project tempo the live processor plays at (120 before it has played).
    double tempoBpm() const noexcept;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    static constexpr int headerHeight = 34;
    static constexpr int hueLine = 2;

protected:
    // Controls bound to parameters (refreshed from the model, sending commands).
    Knob& addKnob(int index, const juce::String& label = {});
    SegmentedControl& addChoice(int index);
    PillToggle& addToggle(int index, const juce::String& text);
    // The knobs added so far, in a grid of `perRow` columns from the top of `area`.
    void layoutKnobs(juce::Rectangle<int> area, int perRow, const std::vector<Knob*>& knobs);
    // A knob row's height: the ring, value and label, and the gap under them.
    int knobCellHeight() const noexcept;
    int knobCellHeight(int ring, float labelSize) const noexcept;
    // A knob (any ring size), its value and label at the bottom of `cell`, the
    // label ending `spacing.sm` above the cell's bottom: the line every label in
    // a row of knobs shares, whatever the ring.
    void placeKnob(Knob& knob, juce::Rectangle<int> cell, float labelSize);
    void placeKnob(Knob& knob, juce::Rectangle<int> cell)
    {
        placeKnob(knob, cell, theme_.fonts.size.caption);
    }
    // For a control that is not a knob in such a row: its label's box on that
    // line, and the room above it.
    juce::Rectangle<int> labelLine(juce::Rectangle<int> cell) const;
    juce::Rectangle<int> aboveLabelLine(juce::Rectangle<int> cell) const;
    int knobCellWidth() const noexcept { return 60; }
    juce::Label* labelFor(const Knob& knob) const;

    virtual void layoutBody(juce::Rectangle<int> body) = 0;
    virtual void paramsChanged() {}
    // A card that repaints exactly what a model change moved (RenderedCard's
    // repaintChanged, called from paramsChanged) is not repainted whole by
    // refresh(); any other card is.
    virtual bool repaintsChangedParameters() const noexcept { return false; }
    // Every model refresh, before the parameters (for state beyond them, such
    // as the key source).
    virtual void insertChanged(const core::Insert& insert) { juce::ignoreUnused(insert); }
    // New frames arrived (the displays repaint).
    virtual void framesArrived(double elapsedMs) { juce::ignoreUnused(elapsedMs); }
    // The header's live readout (a gain reduction, "open").
    virtual juce::String headerStatus() const { return {}; }
    virtual juce::Colour headerStatusColour() const { return theme_.colours.textSecondary; }

    const Theme& theme_;
    CardHost& host_;
    core::Id channelId_, moduleId_;
    std::string type_;
    juce::Colour colour_;
    std::vector<engine::ParameterInfo> infos_;
    std::vector<float> params_;
    std::vector<float> model_; // the model's values at the last refresh

private:
    int labelTop(int ring) const noexcept;
    juce::Rectangle<int> headerArea() const;
    // The header's name: the slot's ("Compressor") when the card has a model switch.
    juce::String headerName() const;
    struct Binding
    {
        int index = -1;
        Knob* knob = nullptr;
        SegmentedControl* choice = nullptr;
        PillToggle* toggle = nullptr;
        juce::Label* label = nullptr;
    };
    void updateBinding(const Binding& b);

    std::vector<Binding> bindings_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
    IconButton power_;
    std::unique_ptr<juce::Button> collapse_;
    std::unique_ptr<SegmentedControl> modelSwitch_; // a slot with several models (Clean / Opto)
    bool bypass_ = true, updating_ = false, dragging_ = false, collapsed_ = false, embedded_ = false;
    std::vector<juce::Component::SafePointer<juce::Component>> hiddenByCollapse_;
    juce::String shownStatus_;
    std::shared_ptr<engine::InsertProcessor> live_;
    std::uint64_t cursor_ = 0;
    std::vector<Frame> ring_;
    int head_ = 0, count_ = 0;
    std::uint64_t total_ = 0;
    float inDb_ = -120.0f, outDb_ = -120.0f, grDb_ = 0.0f, extraDb_ = -120.0f;
    MeterReadout inReadout_, outReadout_, grReadout_{0.0f};
};

// The card for a module type (null for an unknown type).
std::unique_ptr<ModuleCard>
createModuleCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module);
// The compressor's card for any insert that runs the strip compressor (the
// "northlight.compressor" insert as well as the strip module).
std::unique_ptr<ModuleCard>
createCompressorCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// The opto compressor's card, for the strip module and the "northlight.opto" insert.
std::unique_ptr<ModuleCard>
createOptoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// The glue compressor's card (the "northlight.glue" insert).
std::unique_ptr<ModuleCard>
createGlueCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// The stereo widener's card (the "northlight.widener" insert).
std::unique_ptr<ModuleCard>
createWidenerCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// The tape echo's card (the "northlight.tapeecho" insert).
std::unique_ptr<ModuleCard>
createTapeEchoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// Orbit's card (the "northlight.orbit" insert).
std::unique_ptr<ModuleCard>
createOrbitCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// Patina's card (the "northlight.patina" insert).
std::unique_ptr<ModuleCard>
createPatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// The exciter's card (the "northlight.exciter" insert).
std::unique_ptr<ModuleCard>
createExciterCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// Bloom's card (the "northlight.bloom" insert).
std::unique_ptr<ModuleCard>
createBloomCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
// Vesper's card (the "northlight.vesper" insert).
std::unique_ptr<ModuleCard>
createVesperCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

// A guitar pedal's card (the Gravity, Flare, Comet, Tide and Polaris inserts).
std::unique_ptr<ModuleCard>
createPedalCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

// Corona's card (the "northlight.amp" insert), and its page: the amp or the cabinet.
std::unique_ptr<ModuleCard>
createEmberCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);
void showEmberPage(ModuleCard& card, bool cabinet);
// The page a new Corona card opens on (for the --corona-page launch flag).
void setEmberStartPage(bool cabinet);

} // namespace northlight::ui::strip
