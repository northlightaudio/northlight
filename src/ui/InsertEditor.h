#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// A generic parameter editor for one insert: a labelled slider per parameter, a
// bypass toggle, issuing commands through onCommand. Hosted plugin editors (Phase
// 5) replace this for their own windows; the built-ins use it.
class InsertEditor final : public juce::Component
{
public:
    InsertEditor(core::Id channelId, core::Id insertId, const engine::InsertProcessor& processor);

    // Refreshes the controls from the model insert (after undo, for instance).
    void setInsert(const core::Insert& insert);

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }

    std::function<void(std::unique_ptr<core::Command>)> onCommand;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static int preferredHeight(int parameterCount) noexcept { return 56 + 30 + parameterCount * 30 + 16; }
    static constexpr int preferredWidth = 420;

private:
    const Theme& theme_ = Theme::dark();
    core::Id channelId_, insertId_;
    std::vector<engine::ParameterInfo> infos_;
    bool updating_ = false;
    juce::Label title_;
    juce::ToggleButton bypass_{"Bypass"};
    juce::Label mixLabel_;
    juce::Slider mix_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    std::vector<std::unique_ptr<juce::Slider>> sliders_;
};

} // namespace aural::ui
