#pragma once

#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

// A channel's strip in the Editor panel (screens/channel-strip.md 12.1): a caption
// row ("CHANNEL STRIP", the position switch, all-off) over the module cards in
// processing order. Cards are switched on in place; dragging a card's header
// reorders the strip. While visible it polls every card's feedback at 60 Hz.
class ChannelStripView final : public juce::Component, private juce::Timer
{
public:
    explicit ChannelStripView(const Theme& theme);
    ~ChannelStripView() override;

    // The owner fills these (the cards call through them).
    CardHost host;
    // Where the user's presets live (tests point it elsewhere).
    juce::File presetFolder;
    // The strip's width changed (a card collapsed or expanded): the owner re-lays out.
    std::function<void()> onWidthChanged;

    // The channel to show (null clears). Rebuilds when the module set or order
    // changed, otherwise refreshes the values.
    void setChannel(const core::Channel* channel);
    core::Id channelId() const noexcept { return channelId_; }
    int preferredWidth() const;
    int moduleCount() const noexcept { return static_cast<int>(cards_.size()); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int captionHeight = 22;

    // For the drag benchmark: the intervals between display ticks while the
    // strip is showing, since measuring began.
    static void startMeasuring();
    static std::vector<double> takeTickIntervals();
    // Which module types the user opened or closed is remembered for the session;
    // tests start from none.
    static void forgetExpandedForTesting();

private:
    void timerCallback() override;
    void reorder(core::Id moduleId, juce::Point<int> screen, bool released);
    void collapse(core::Id moduleId, bool collapsed);
    void showPresetMenu();
    // Sets the strip to a preset (one undo step) and opens the cards of the
    // modules it switches on, folding the rest.
    void applyPreset(const std::vector<core::Insert>& modules, const std::string& name);
    void savePreset();
    // Whether a module type's card is expanded: what the user chose this
    // session, otherwise whether the module is on.
    static bool expanded(const std::string& type, bool on);
    int indexForX(int x) const;

    const Theme& theme_;
    CardHost cardHost_; // what the cards see: the owner's host plus reordering
    core::Id channelId_ = core::invalidId;
    std::vector<std::unique_ptr<ModuleCard>> cards_;
    // Card motion: a card that collapses, expands or switches on (and its
    // neighbours) glides to its new place. Each card's last target is kept, so a
    // relayout that does not move it (the panel gliding the strip's width, frame
    // by frame) leaves its glide alone.
    bool animateCards_ = false;
    std::map<const ModuleCard*, juce::Rectangle<int>> cardTargets_;
    // Lays the cards out again, gliding them.
    void relayoutAnimated();
    std::vector<core::Id> order_;
    bool preInserts_ = false;
    int dropIndex_ = -1;
    double lastTickMs_ = 0.0;
    bool wasShowing_ = false;
    juce::TextButton position_, allOff_, presets_;
    std::vector<core::Insert> modules_; // the channel's strip as last set (for saving a preset)
    std::vector<int> laidOutWidths_;    // the cards' widths at the last layout
};

} // namespace northlight::ui::strip
