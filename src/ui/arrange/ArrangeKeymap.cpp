#include "ui/arrange/ArrangeKeymap.h"

#include "ui/ArrangeView.h"

namespace northlight::ui
{

namespace
{
using Key = juce::KeyPress;
using Mods = juce::ModifierKeys;

std::function<bool(const Key&)> plain(juce::juce_wchar ch)
{
    return [ch](const Key& k) { return k == Key(ch, Mods(), 0); };
}
std::function<bool(const Key&)> command(juce::juce_wchar ch)
{
    return [ch](const Key& k) { return k == Key(ch, Mods::commandModifier, 0); };
}
// Shift and a letter, or the capital letter typed alone.
std::function<bool(const Key&)> shifted(juce::juce_wchar lower, juce::juce_wchar upper)
{
    return [lower, upper](const Key& k)
    { return k == Key(lower, Mods::shiftModifier, 0) || k == Key(upper); };
}
// An action that always takes the key.
std::function<bool(ArrangeView&, const Key&)> always(std::function<void(ArrangeView&)> f)
{
    return [f](ArrangeView& v, const Key&)
    {
        f(v);
        return true;
    };
}
std::function<bool(ArrangeView&, const Key&)> alwaysWithKey(std::function<void(ArrangeView&, const Key&)> f)
{
    return [f](ArrangeView& v, const Key& k)
    {
        f(v, k);
        return true;
    };
}
bool isAlt(const Key& k) noexcept
{
    return k.getModifiers() == Mods::altModifier;
}
} // namespace

const std::vector<ArrangeKey>& ArrangeKeymap::bindings()
{
    static const std::vector<ArrangeKey> keys = []
    {
        std::vector<ArrangeKey> b;
        b.push_back({"Delete the selected marker, else the selected clips",
                     [](const Key& k) { return k == Key::deleteKey || k == Key::backspaceKey; },
                     always(
                         [](ArrangeView& v)
                         {
                             if (!v.markers_.removeSelected())
                                 v.deleteSelection();
                         })});
        b.push_back({"Add a marker at the playhead",
                     plain('m'),
                     always([](ArrangeView& v) { v.addMarkerAtPlayhead(false); })});
        b.push_back({"Add a region from the loop",
                     shifted('m', 'M'),
                     always([](ArrangeView& v) { v.addMarkerAtPlayhead(true); })});
        b.push_back(
            {"Jump to the previous marker", plain(','), always([](ArrangeView& v) { v.jumpToMarker(-1); })});
        b.push_back(
            {"Jump to the next marker", plain('.'), always([](ArrangeView& v) { v.jumpToMarker(1); })});
        b.push_back({"Duplicate", command('d'), always([](ArrangeView& v) { v.duplicateSelection(); })});
        b.push_back({"Copy", command('c'), always([](ArrangeView& v) { v.copySelection(); })});
        b.push_back({"Cut", command('x'), always([](ArrangeView& v) { v.cutSelection(); })});
        b.push_back({"Paste at the playhead", command('v'), always([](ArrangeView& v) { v.paste(); })});
        for (int i = 0; i < ArrangeView::toolCount; ++i)
            b.push_back({"Pick a tool (1 to 7)",
                         plain(static_cast<juce::juce_wchar>('1' + i)),
                         always([i](ArrangeView& v) { v.setTool(static_cast<ArrangeView::Tool>(i)); })});
        b.push_back({"Mute or unmute the selected clips",
                     command('m'),
                     always([](ArrangeView& v) { v.toggleMuteSelection(); })});
        // Take lanes: Alt+Up / Alt+Down cycle the take at the playhead, Alt+T shows or hides them.
        b.push_back({"Play the next newer or older take",
                     [](const Key& k)
                     { return isAlt(k) && (k.getKeyCode() == Key::upKey || k.getKeyCode() == Key::downKey); },
                     [](ArrangeView& v, const Key& k)
                     {
                         const auto* track =
                             v.project_ != nullptr ? v.project_->findTrack(v.selectedTrack()) : nullptr;
                         return track != nullptr &&
                                v.takes_.cycleTake(*track,
                                                   v.playheadForEdits ? v.playheadForEdits() : v.playhead_,
                                                   k.getKeyCode() == Key::upKey ? 1 : -1);
                     }});
        b.push_back({"Show or hide the take lanes",
                     [](const Key& k)
                     { return isAlt(k) && (k.getKeyCode() == 'T' || k.getKeyCode() == 't'); },
                     [](ArrangeView& v, const Key&)
                     {
                         const auto* track =
                             v.project_ != nullptr ? v.project_->findTrack(v.selectedTrack()) : nullptr;
                         return track != nullptr && v.takes_.toggleLanes(*track);
                     }});
        b.push_back({"Nudge by a grid step (Shift: a quarter of one)",
                     [](const Key& k)
                     {
                         return k.getModifiers().isAltDown() &&
                                (k.getKeyCode() == Key::leftKey || k.getKeyCode() == Key::rightKey);
                     },
                     alwaysWithKey(
                         [](ArrangeView& v, const Key& k)
                         {
                             v.nudgeSelection(k.getKeyCode() == Key::leftKey ? -1 : 1,
                                              k.getModifiers().isShiftDown());
                         })});
        b.push_back({"Select every clip", command('a'), always([](ArrangeView& v) { v.selectAll(); })});
        b.push_back({"Open or close the selected track's automation lanes",
                     plain('a'),
                     [](ArrangeView& v, const Key&)
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
                     }});
        b.push_back({"Normalise",
                     [](const Key& k)
                     { return k == Key('n', Mods::commandModifier | Mods::shiftModifier, 0); },
                     always([](ArrangeView& v) { v.normaliseSelection(); })});
        b.push_back({"Clear the selection",
                     [](const Key& k) { return k == Key::escapeKey; },
                     always([](ArrangeView& v) { v.clearSelection(); })});
        b.push_back(
            {"Split at the playhead", plain('s'), always([](ArrangeView& v) { v.splitAtPlayhead(); })});
        b.push_back({"Split at the time selection",
                     shifted('s', 'S'),
                     always([](ArrangeView& v) { v.splitAtSelection(); })});
        b.push_back({"Join", plain('j'), always([](ArrangeView& v) { v.joinSelection(); })});
        b.push_back({"Crossfade", plain('x'), always([](ArrangeView& v) { v.crossfadeSelection(); })});
        b.push_back({"Snap on or off",
                     plain('g'),
                     always([](ArrangeView& v) { v.setSnapEnabled(!v.snapEnabled()); })});
        b.push_back({"Ripple on or off",
                     plain('e'),
                     always([](ArrangeView& v) { v.setRippleEnabled(!v.rippleEnabled()); })});
        b.push_back({"Nudge the selected clips by a grid step",
                     [](const Key& k) { return k == Key::leftKey || k == Key::rightKey; },
                     [](ArrangeView& v, const Key& k)
                     {
                         if (v.selection_.empty())
                             return false;
                         v.nudgeSelection(k == Key::leftKey ? -1 : 1);
                         return true;
                     }});
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
