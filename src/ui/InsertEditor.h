#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/NumericField.h"
#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The generic parameter editor (07-plugins.md 7.3) for built-ins and GUI-less
// hosted plugins: a scrollable list of 28 px rows, each the name in body (160
// px), a knob in the centre and the value as an editable numeric field (80 px)
// on the right; a footer line with the latency and the key input. Standalone
// (a built-in's window) it has a title and the bypass and mix controls above
// the rows; embedded in a plugin window the bar has those. A knob drag is one
// gesture (one undo step); the field commits on Enter.
class InsertEditor final : public juce::Component, private juce::Timer
{
public:
    // `title` overrides the built-in display name (a hosted plugin's name);
    // `embedded` hides the title, bypass and mix row (the plugin window's bar has them).
    InsertEditor(core::Id channelId,
                 core::Id insertId,
                 const engine::InsertProcessor& processor,
                 const std::string& title = {},
                 bool embedded = false);

    // A built-in's own editor (ui/strip/BuiltInCards.h) in place of the rows:
    // the same display and controls as its card in the Editor panel, live from
    // the processor `live` finds. Sizes the editor to it.
    void showBuiltInBody(const core::Insert& insert,
                         std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> live);

    // Refreshes the controls from the model insert (after undo, for instance).
    void setInsert(const core::Insert& insert);
    // The live latency, for the footer.
    void setLatency(int samples);

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    // The project, for a built-in body that names other channels (the
    // compressor's key source).
    std::function<const core::Project*()> project;
    // A parameter drag began/ended (by index): a hosted plugin gets its gesture
    // events through these.
    std::function<void(int)> onParameterGestureBegin;
    std::function<void(int)> onParameterGestureEnd;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int rowHeight = 28;
    static int preferredHeight(int parameterCount, bool embedded = false) noexcept
    {
        return (embedded ? 0 : 2 * 28 + 12) + std::min(parameterCount, 14) * rowHeight + 24 + 24;
    }
    static constexpr int preferredWidth = 420;

private:
    struct Row
    {
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<Knob> knob;
        std::unique_ptr<NumericField> value;
    };
    void setParameter(int index, float value);

    const Theme& theme_ = Theme::dark();
    core::Id channelId_, insertId_;
    std::vector<engine::ParameterInfo> infos_;
    bool updating_ = false;
    bool embedded_ = false;
    int latency_ = 0;
    core::Id sidechain_ = core::invalidId;
    juce::Label title_;
    juce::TextButton bypass_{"Bypass"};
    juce::Slider mix_;
    juce::Viewport viewport_;
    juce::Component rows_;
    std::vector<Row> parameters_;
    void timerCallback() override;
    strip::CardHost host_;
    std::unique_ptr<strip::ModuleCard> body_;
    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> live_;
    double lastTickMs_ = 0.0;
    bool instrument_ = false; // a built-in instrument: no mix
};

} // namespace aural::ui
