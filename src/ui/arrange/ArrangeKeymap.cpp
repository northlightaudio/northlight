#include "ui/arrange/ArrangeKeymap.h"

#include "ui/ArrangeView.h"
#include "ui/Shortcuts.h"

namespace northlight::ui
{

namespace
{
using Press = juce::KeyPress;

// An action that always takes the key.
std::function<bool(ArrangeView&, const Press&)> always(std::function<void(ArrangeView&)> f)
{
    return [f](ArrangeView& v, const Press&)
    {
        f(v);
        return true;
    };
}
std::function<bool(ArrangeView&, const Press&)>
alwaysWithKey(std::function<void(ArrangeView&, const Press&)> f)
{
    return [f](ArrangeView& v, const Press& k)
    {
        f(v, k);
        return true;
    };
}
// The binding for a command: its name and keys from the shortcut table.
ArrangeKey bind(Command command, std::function<bool(ArrangeView&, const Press&)> act)
{
    return {Shortcuts::of(command).name,
            [command](const Press& k) { return Shortcuts::matches(command, k); },
            std::move(act)};
}
} // namespace

const std::vector<ArrangeKey>& ArrangeKeymap::bindings()
{
    static const std::vector<ArrangeKey> keys = []
    {
        std::vector<ArrangeKey> b;
        b.push_back(bind(Command::deleteSelection,
                         always(
                             [](ArrangeView& v)
                             {
                                 if (!v.markers_.removeSelected())
                                     v.deleteSelection();
                             })));
        b.push_back(bind(Command::addMarker, always([](ArrangeView& v) { v.addMarkerAtPlayhead(false); })));
        b.push_back(bind(Command::addRegion, always([](ArrangeView& v) { v.addMarkerAtPlayhead(true); })));
        b.push_back(bind(Command::previousMarker, always([](ArrangeView& v) { v.jumpToMarker(-1); })));
        b.push_back(bind(Command::nextMarker, always([](ArrangeView& v) { v.jumpToMarker(1); })));
        b.push_back(bind(Command::duplicate, always([](ArrangeView& v) { v.duplicateSelection(); })));
        b.push_back(bind(Command::copy, always([](ArrangeView& v) { v.copySelection(); })));
        b.push_back(bind(Command::cut, always([](ArrangeView& v) { v.cutSelection(); })));
        b.push_back(bind(Command::paste, always([](ArrangeView& v) { v.paste(); })));
        b.push_back(bind(Command::pickTool,
                         [](ArrangeView& v, const Press& k)
                         {
                             const int tool = Shortcuts::matchIndex(Command::pickTool, k);
                             if (tool < 0 || tool >= ArrangeView::toolCount)
                                 return false;
                             v.setTool(static_cast<ArrangeView::Tool>(tool));
                             return true;
                         }));
        b.push_back(bind(Command::muteClips, always([](ArrangeView& v) { v.toggleMuteSelection(); })));
        // Take lanes: Alt+Up / Alt+Down cycle the take at the playhead, Alt+T shows or hides them.
        b.push_back(bind(Command::cycleTake,
                         [](ArrangeView& v, const Press& k)
                         {
                             const auto* track =
                                 v.project_ != nullptr ? v.project_->findTrack(v.selectedTrack()) : nullptr;
                             return track != nullptr &&
                                    v.takes_.cycleTake(*track,
                                                       v.playheadForEdits ? v.playheadForEdits()
                                                                          : v.playhead_,
                                                       k.getKeyCode() == Press::upKey ? 1 : -1);
                         }));
        b.push_back(bind(Command::selectTrack,
                         [](ArrangeView& v, const Press& k)
                         {
                             v.moveTrackSelection(k.getKeyCode() == Press::upKey ? -1 : 1,
                                                  k.getModifiers().isShiftDown());
                             return true;
                         }));
        b.push_back(bind(Command::takeLanes,
                         [](ArrangeView& v, const Press&)
                         {
                             const auto* track =
                                 v.project_ != nullptr ? v.project_->findTrack(v.selectedTrack()) : nullptr;
                             return track != nullptr && v.takes_.toggleLanes(*track);
                         }));
        b.push_back(bind(Command::nudgeFine,
                         alwaysWithKey(
                             [](ArrangeView& v, const Press& k)
                             {
                                 v.nudgeSelection(k.getKeyCode() == Press::leftKey ? -1 : 1,
                                                  k.getModifiers().isShiftDown());
                             })));
        b.push_back(bind(Command::selectAll, always([](ArrangeView& v) { v.selectAll(); })));
        b.push_back(bind(Command::automationLanes,
                         [](ArrangeView& v, const Press&)
                         {
                             const auto* channel =
                                 v.project_ != nullptr ? v.project_->findChannel(v.selectedTrack()) : nullptr;
                             if (channel == nullptr)
                                 return false;
                             const auto* track = v.project_->findTrack(v.selectedTrack());
                             if (track != nullptr && !track->hasAudio())
                                 return false;
                             v.showAutomation(v.selectedTrack(), !channel->automationOpen);
                             return true;
                         }));
        b.push_back(bind(Command::normalise, always([](ArrangeView& v) { v.normaliseSelection(); })));
        b.push_back(bind(Command::clearSelection, always([](ArrangeView& v) { v.clearSelection(); })));
        b.push_back(bind(Command::splitAtPlayhead, always([](ArrangeView& v) { v.splitAtPlayhead(); })));
        b.push_back(bind(Command::splitAtSelection, always([](ArrangeView& v) { v.splitAtSelection(); })));
        b.push_back(bind(Command::join, always([](ArrangeView& v) { v.joinSelection(); })));
        b.push_back(bind(Command::crossfade, always([](ArrangeView& v) { v.crossfadeSelection(); })));
        b.push_back(bind(Command::selectClip,
                         [](ArrangeView& v, const Press& k)
                         {
                             return v.stepClipSelection(k.getKeyCode() == Press::leftKey ? -1 : 1,
                                                        k.getModifiers().isShiftDown());
                         }));
        b.push_back(bind(Command::nudge,
                         [](ArrangeView& v, const Press& k)
                         {
                             if (v.selection_.empty())
                                 return false;
                             v.nudgeSelection(k == Press::leftKey ? -1 : 1);
                             return true;
                         }));
        return b;
    }();
    return keys;
}

bool ArrangeKeymap::handle(ArrangeView& view, const juce::KeyPress& key)
{
    for (const auto& binding : bindings())
        if (binding.matches(key))
            return binding.act(view, key);
    return false;
}

} // namespace northlight::ui
