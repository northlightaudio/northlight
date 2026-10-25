#pragma once

#include "core/Model.h"
#include "ui/edit/ClipOps.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace northlight::ui
{

class ArrangeView;

// The arrange view's menus (screens/arrange.md): a clip's, a track's, what a MIDI
// track plays, the ruler's, the "+" corner's, a bus's, the master's and the tools
// menu. Every item carries its own action (no number ranges to map back); the
// actions hold a safe pointer to the view, so a menu outliving it does nothing.
class ArrangeMenus
{
public:
    explicit ArrangeMenus(ArrangeView& view) noexcept : view_(view) {}

    void clipMenu(const edit::ClipRef& ref, juce::Point<int> screen);
    // The track's menu; on an audio clip (clipId) it leads with the clip's items.
    void trackMenu(core::Id trackId, juce::Point<int> screen, core::Id clipId = core::invalidId);
    // What a MIDI track plays: an instrument channel (or none), the MIDI channel it
    // sends on, or a new instrument (a channel for it, just under the track).
    void targetMenu(core::Id trackId, juce::Rectangle<int> screenArea);
    void rulerMenu(juce::Point<int> screen);
    void addTrackMenu();
    void busMenu(core::Id busId, juce::Point<int> screen); // a group bus's or an FX track's row
    void masterMenu(juce::Point<int> screen);
    void toolsMenu();

private:
    // The FX track choices as items: each adds one, with sends from `sendFrom`.
    void addFxItems(juce::PopupMenu& menu, int& nextId, std::vector<core::Id> sendFrom) const;

    ArrangeView& view_;
};

} // namespace northlight::ui
