#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The Editor panel (07-plugins.md 7.1): the selected channel's insert chain as a
// horizontal strip of device cards. Each card has a header (power, name, format
// badge, menu), a body (knobs for a built-in or GUI-less plugin, an Open button
// for a plugin with its own GUI) and a footer with the wet/dry mix; a ghost "+"
// card adds an insert. The owner gives the model, the live instances and the
// registry, and takes commands back.
class EditorPanel final : public juce::Component, private juce::Timer
{
public:
    explicit EditorPanel(const Theme& theme);
    ~EditorPanel() override;

    void setProject(const core::Project* project);
    void setChannel(core::Id channelId);
    core::Id channelId() const noexcept { return channelId_; }
    void setRegistry(const hosting::PluginRegistry* registry) { registry_ = registry; }

    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> liveInsert;
    std::function<std::string(const std::string& type)> displayName;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    std::function<void(core::Id channelId, core::Id insertId)> onOpenEditor;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Card;
    void rebuild();
    void timerCallback() override;
    void showAddMenu();
    const core::Channel* channel() const;

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    const hosting::PluginRegistry* registry_ = nullptr;
    core::Id channelId_ = core::invalidId;
    juce::Viewport viewport_;
    juce::Component strip_;
    std::vector<std::unique_ptr<Card>> cards_;
    juce::TextButton add_{"+"};
    std::vector<core::Id> builtOrder_;
};

} // namespace aural::ui
