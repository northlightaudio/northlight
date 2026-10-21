#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/PluginPresets.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace northlight::ui
{

// A floating window for a hosted plugin's editor (VST3 or CLAP, or a generic one
// when the plugin has none) with the 32 px bar of 07-plugins.md 7.2 above it:
// power (bypass), "Plugin · Track", the format badge, the wet/dry slider with
// its value, the presets menu and pin (always on top). The window never
// outlives the plugin instance: it shares ownership of the processor, and the
// owner closes it when the insert leaves the model.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // `keepAlive` is the live processor: the window shares its ownership so the
    // plugin cannot be freed under its open editor. `editor` is the plugin's own
    // editor component (any format); `presets` serves the Presets menu.
    PluginWindow(core::Id channelId,
                 core::Id insertId,
                 std::shared_ptr<engine::InsertProcessor> keepAlive,
                 hosting::PluginPresets& presets,
                 std::unique_ptr<juce::Component> editor,
                 bool resizable,
                 const juce::String& pluginName,
                 const juce::String& trackName,
                 const juce::String& format,
                 bool pinned);
    ~PluginWindow() override;

    core::Id channelId() const noexcept { return channelId_; }
    core::Id insertId() const noexcept { return insertId_; }
    // The instance the editor is on; a replaced instance closes the window.
    const engine::InsertProcessor* instance() const noexcept { return keepAlive_.get(); }

    // Refreshes the bar from the model insert (bypass, mix).
    void setInsert(const core::Insert& insert);
    // The editor changed size (a plugin resized itself): follow it.
    void editorSizeChanged(int width, int height);
    // The editor's size the user left (resizable windows), to restore next time.
    void setEditorSize(int width, int height);

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void()> onClose;          // called (asynchronously) after the close button
    std::function<void(bool)> onPinChanged; // the user toggled always-on-top
    std::function<void(juce::Rectangle<int>)> onBoundsChanged; // moved or resized by the user

    void closeButtonPressed() override;
    void moved() override;
    void resized() override;

    static constexpr int barHeight = 32;
    static constexpr int minWidth = 420;

private:
    class Holder;
    std::shared_ptr<engine::InsertProcessor> keepAlive_; // first member: destroyed last, after the editor
    core::Id channelId_, insertId_;
    Holder* holder_ = nullptr;
};

} // namespace northlight::ui
