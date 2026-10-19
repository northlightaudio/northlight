#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace aural::ui
{

// A floating window for a hosted plugin's own editor (or JUCE's generic one when
// the plugin has none), with a bar above it for bypass, wet/dry mix and "pin"
// (always on top). The window never outlives the plugin instance: the owner closes
// it when the insert leaves the model.
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(core::Id channelId,
                 core::Id insertId,
                 juce::AudioPluginInstance& instance,
                 const juce::String& title,
                 bool pinned);
    ~PluginWindow() override;

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }

    // Refreshes the bar from the model insert (bypass, mix).
    void setInsert(const core::Insert& insert);

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void()> onClose;          // called (asynchronously) after the close button
    std::function<void(bool)> onPinChanged; // the user toggled always-on-top
    std::function<void(juce::Point<int>)> onMoved;

    void closeButtonPressed() override;
    void moved() override;

    static constexpr int barHeight = 30;

private:
    class Holder;
    core::Id channelId_, insertId_;
    Holder* holder_ = nullptr;
};

} // namespace aural::ui
