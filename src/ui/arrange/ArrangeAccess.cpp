// What a screen reader finds in the arrange view (design/accessibility-performance.md
// 11.3): the markers, and each track's lane as a list of its clips, with its take
// lanes after it. Only what is on screen; ui::AccessibleItems keeps the stand-ins.
#include "ui/ArrangeView.h"
#include "ui/Db.h"

#include <algorithm>

namespace northlight::ui
{

namespace
{
juce::String nameOr(const std::string& name, const char* otherwise)
{
    return name.empty() ? juce::String(otherwise) : juce::String::fromUTF8(name.c_str());
}
} // namespace

std::vector<AccessibleItem> ArrangeView::accessibleItems()
{
    std::vector<AccessibleItem> items;
    if (project_ == nullptr)
        return items;
    const auto lanes = lanesBounds();
    const SelectionIndex index(*this);
    const auto* current = selection_.empty() ? nullptr : &selection_.back();
    const auto span = [this](core::SamplePosition start, core::SamplePosition end)
    { return spokenPosition(tempoMap_, start) + " to " + spokenPosition(tempoMap_, end); };

    // The markers and regions, in the ruler's markers row.
    AccessibleItem markers;
    markers.key = "markers";
    markers.role = juce::AccessibilityRole::list;
    markers.title = "Markers";
    markers.bounds = {lanes.getX(), 0, lanes.getWidth(), rulerOnly};
    for (const auto& marker : project_->markers)
    {
        AccessibleItem item;
        item.key = juce::String(marker.id);
        item.title =
            juce::String(marker.isRegion() ? "Region" : "Marker") +
            (marker.name.empty() ? juce::String() : " " + juce::String::fromUTF8(marker.name.c_str())) +
            ", " +
            (marker.isRegion() ? span(marker.position, marker.end())
                               : "at " + spokenPosition(tempoMap_, marker.position));
        item.bounds = markers_.flagBounds(marker);
        item.selectable = true;
        item.selected = markers_.selected() == marker.id;
        item.press = [this, id = marker.id]
        {
            if (const auto* m = project_ != nullptr ? project_->findMarker(id) : nullptr)
            {
                markers_.press(*m, m->position);
                markers_.finishDrag(false);
                grabKeyboardFocus();
                repaint();
            }
        };
        markers.children.push_back(std::move(item));
    }
    items.push_back(std::move(markers));

    // Each track's lane: its clips, audio and MIDI, then its take lanes.
    for (int row = 0; row < static_cast<int>(project_->tracks.size()); ++row)
    {
        const auto& track = project_->tracks[static_cast<size_t>(row)];
        const auto bounds = rowBounds(row).withLeft(headerWidth()).getIntersection(lanes);
        if (bounds.isEmpty())
            continue;
        AccessibleItem lane;
        lane.key = "track:" + juce::String(track.id);
        lane.role = juce::AccessibilityRole::list;
        lane.title = nameOr(track.name, "Track") + " lane";
        lane.bounds = bounds;
        const auto addClip = [&](const ClipRef& ref,
                                 const std::string& name,
                                 core::SamplePosition start,
                                 core::SamplePosition length,
                                 juce::String details)
        {
            AccessibleItem item;
            item.key = juce::String(ref.midi ? "midi:" : "audio:") + juce::String(ref.clip);
            item.title = nameOr(name, "Untitled") + ", " + details;
            item.bounds = spanBounds(start, length, row);
            item.selectable = true;
            item.selected = isSelected(ref);
            item.current = current != nullptr && *current == ref;
            item.press = [this, ref] { pressForReader(ref, false); };
            item.showMenu = [this, ref] { pressForReader(ref, true); };
            lane.children.push_back(std::move(item));
        };
        for (const auto& clip : track.clips)
        {
            if (!clipBounds(clip, row).intersects(lanes))
                continue;
            juce::String details = "audio clip, " + span(clip.start, clip.end());
            if (clip.muted)
                details << ", muted";
            if (std::abs(clip.gain - 1.0f) > 1.0e-4f)
                details << ", gain " << db::gainText(clip.gain) << " dB";
            if (missingLabel(clip.mediaPath).isNotEmpty())
                details << ", media missing";
            addClip({track.id, clip.id}, clip.name, clip.start, clip.length, details);
        }
        for (const auto& clip : track.midiClips)
        {
            if (!spanBounds(clip.start, clip.length, row).intersects(lanes))
                continue;
            const auto notes = clip.notes.size();
            juce::String details = "MIDI clip, " + span(clip.start, clip.end()) + ", " + juce::String(notes) +
                                   (notes == 1 ? " note" : " notes");
            if (clip.muted)
                details << ", muted";
            addClip({track.id, clip.id, true}, clip.name, clip.start, clip.length, details);
        }
        std::stable_sort(lane.children.begin(),
                         lane.children.end(),
                         [](const AccessibleItem& a, const AccessibleItem& b)
                         { return a.bounds.getX() < b.bounds.getX(); });
        items.push_back(std::move(lane));
        for (auto& takes : takes_.accessibleItems(takeRow(row)))
        {
            takes.key = "track:" + juce::String(track.id) + ":" + takes.key;
            items.push_back(std::move(takes));
        }
    }
    return items;
}

void ArrangeView::pressForReader(const ClipRef& ref, bool menu)
{
    // As a click on the clip: it alone is selected, on its track, and the lanes
    // take the keyboard focus; the menu opens where a right-click would.
    if (ref.track != selectedTrack())
        selectTrack(ref.track);
    select(ref, false);
    notifySelection();
    grabKeyboardFocus();
    repaint();
    if (!menu)
        return;
    if (const auto s = spanOf(ref))
    {
        auto at =
            spanBounds(s->start, s->length, project_->trackIndex(ref.track)).getIntersection(lanesBounds());
        menus_.clipMenu(ref, localPointToGlobal((at.isEmpty() ? lanesBounds() : at).getCentre()));
    }
}

} // namespace northlight::ui
