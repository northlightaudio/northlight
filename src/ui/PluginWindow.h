#pragma once

#include "core/Model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace aural::ui
{

// A floating window for a hosted plugin's own editor (or JUCE's generic one when
// the plugin has none). The window never outlives the plugin instance: the owner
// closes it when the insert leaves the model.
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(core::Id channelId,
                 core::Id insertId,
                 juce::AudioPluginInstance& instance,
                 const juce::String& title);
    ~PluginWindow() override;

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }

    std::function<void()> onClose; // called (asynchronously) after the close button

    void closeButtonPressed() override;

private:
    core::Id channelId_, insertId_;
};

} // namespace aural::ui
