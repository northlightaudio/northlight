#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "ui/Icons.h"
#include "ui/PluginMenu.h"
#include "ui/Theme.h"
#include "ui/components/AddSlot.h"
#include "ui/components/Knob.h"
#include "ui/strip/ChannelStripView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The Editor panel (screens/plugins.md 7.1): the selected channel's insert chain as a
// horizontal strip of device cards, and its channel strip (screens/channel-strip.md)
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
    void setPluginSources(const PluginSources* sources) noexcept { pluginSources_ = sources; }

    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> liveInsert;
    std::function<std::string(const std::string& type)> displayName;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    std::function<void(core::Id channelId, core::Id insertId)> onOpenEditor;
    // Whether an insert's editor window is open (the card shows a dot).
    std::function<bool(core::Id insertId)> isEditorOpen;
    // An insert was added here (the owner remembers the type as recent).
    std::function<void(const std::string& type)> onInserted;
    // The panel's smallest height (its rows and the tallest card's layout), and
    // a call when that changes, so the dock can grow to it.
    int minimumHeight() const;
    std::function<void()> onMinimumHeightChanged;

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int contentMinimumHeight() const;
    int shownMinimum_ = 0;
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
    const PluginSources* pluginSources_ = nullptr;
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
    // Card motion: when the cards change (an insert added, removed or moved, a
    // send added or removed) the layout eases each one from where it was and
    // fades new ones in; switching channels and resizing stay instant.
    std::map<std::string, juce::Rectangle<int>> previousBounds_;
    bool animateLayout_ = false;
    void rememberBounds();
    std::vector<core::Id> builtOrder_;
    core::Id builtChannel_ = core::invalidId; // the channel the cards were built for
    int dropIndex_ = -1;                      // the insertion index while a plugin or card is dragged over
    double lastTickMs_ = 0.0;
    bool wasShowing_ = false;
    int ticks_ = 0;
};

} // namespace northlight::ui
