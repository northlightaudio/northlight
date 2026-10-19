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
    // `title` overrides the built-in display name (a hosted plugin's name);
    // `embedded` hides the bypass and mix row (the plugin window's bar has them).
    InsertEditor(core::Id channelId,
                 core::Id insertId,
                 const engine::InsertProcessor& processor,
                 const std::string& title = {},
                 bool embedded = false);

    // Refreshes the controls from the model insert (after undo, for instance).
    void setInsert(const core::Insert& insert);

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static int preferredHeight(int parameterCount, bool embedded = false) noexcept
    {
        return (embedded ? 24 : 56 + 30) + parameterCount * 30 + 16;
    }
    static constexpr int preferredWidth = 420;

private:
    const Theme& theme_ = Theme::dark();
    core::Id channelId_, insertId_;
    std::vector<engine::ParameterInfo> infos_;
    bool updating_ = false;
    bool embedded_ = false;
    juce::Label title_;
    juce::ToggleButton bypass_{"Bypass"};
    juce::Label mixLabel_;
    juce::Slider mix_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    std::vector<std::unique_ptr<juce::Slider>> sliders_;
};

} // namespace aural::ui
