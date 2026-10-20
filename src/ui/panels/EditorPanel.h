#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/AddSlot.h"
#include "ui/components/Knob.h"
#include "ui/strip/ChannelStripView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The Editor panel (07-plugins.md 7.1): the selected channel's insert chain as a
// horizontal strip of device cards, and its channel strip (12-channel-strip.md)
// before or after them in signal order. Each card has a header (power, name, format
// badge, menu), a body (knobs for a built-in or GUI-less plugin, an Open button
// for a plugin with its own GUI) and a footer with the wet/dry mix; a ghost "+"
// card adds an insert. The owner gives the model, the live instances and the
// registry, and takes commands back.
class EditorPanel final : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    explicit EditorPanel(const Theme& theme);
    ~EditorPanel() override;

    void setProject(const core::Project* project);
    void setChannel(core::Id channelId);
    // The channel shown: the one selected, or for a MIDI track the instrument
    // channel it plays.
    core::Id channelId() const noexcept { return channelId_; }
    // A MIDI track that plays nothing chose an instrument: make an instrument
    // channel with it and aim the track there.
    std::function<void(core::Id midiTrack, const std::string& type)> onPlayNewInstrument;
    void setRegistry(const hosting::PluginRegistry* registry) { registry_ = registry; }

    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> liveInsert;
    std::function<std::string(const std::string& type)> displayName;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    std::function<void(core::Id channelId, core::Id insertId)> onOpenEditor;
    // Whether an insert's editor window is open (the card shows a dot).
    std::function<bool(core::Id insertId)> isEditorOpen;
    // Recent and favourite plugin types for the "+" menu; may be null.
    std::function<std::vector<std::string>(const std::string& key)> listSetting;
    // An insert was added here (the owner remembers the type as recent).
    std::function<void(const std::string& type)> onInserted;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Card;
    class SendCard;
    void rebuild();
    void timerCallback() override;
    void showAddMenu();
    // The instrument menu (built-in and scanned instruments) for the slot.
    void showInstrumentMenu(juce::Component& target);
    void setInstrument(const std::string& type);
    const core::Channel* channel() const;
    int insertIndexForX(int stripX) const;
    void reorderCard(core::Id insertId, juce::Point<int> screen, bool released);
    void addInsert(const std::string& type, int index);
    // The sends: one card per send after the inserts and the strip.
    void rebuildSends(const core::Channel* ch, bool& shape);
    void showAddSendMenu();

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    const hosting::PluginRegistry* registry_ = nullptr;
    void resolveChannel();
    core::Id channelId_ = core::invalidId;
    core::Id selectedId_ = core::invalidId;  // what the selection asked for
    core::Id midiTrackId_ = core::invalidId; // a MIDI track selected: shown through its target
    // The scrolled content: the section captions are painted on it.
    struct Content final : juce::Component
    {
        std::function<void(juce::Graphics&)> onPaint;
        void paint(juce::Graphics& g) override
        {
            if (onPaint)
                onPaint(g);
        }
    };
    juce::Viewport viewport_;
    Content strip_;
    strip::ChannelStripView stripView_;
    int insertsX_ = 0; // where the inserts section starts in the content
    bool laidOutPreInserts_ = false;
    std::vector<std::unique_ptr<Card>> cards_;
    AddSlot add_{theme_, "Insert"};
    std::unique_ptr<Card> instrumentCard_; // the instrument slot, filled
    AddSlot addInstrument_{theme_, "Instrument"};
    int instrumentX_ = 0;
    std::vector<std::unique_ptr<SendCard>> sendCards_;
    AddSlot addSend_{theme_, "Send"};
    int sendsX_ = 0;
    std::vector<core::Id> builtOrder_;
    core::Id builtChannel_ = core::invalidId; // the channel the cards were built for
    int dropIndex_ = -1;                      // the insertion index while a plugin or card is dragged over
    double lastTickMs_ = 0.0;
    bool wasShowing_ = false;
    int ticks_ = 0;
};

} // namespace aural::ui
