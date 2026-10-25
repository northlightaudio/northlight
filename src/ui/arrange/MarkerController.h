#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/timeline/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The arrange view's markers and regions (screens/arrange.md "Ruler"): their flags
// in the ruler's markers row and their lines through the lanes, the selected and
// hovered one, a flag dragged along the timeline, the inline rename, and the
// marker keys' edits (add at the playhead or from the loop, jump, remove). Edits
// go out through onEdit as one command each. Coordinates are the owner's.
class MarkerController
{
public:
    static constexpr int rowHeight = 14; // the ruler's markers row

    MarkerController(juce::Component& owner, const Theme& theme, const TimelineViewport& view);

    void setProject(const core::Project* project); // a marker that went is no longer selected

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onEdit;
    std::function<void(core::SamplePosition)> onLocate;
    std::function<core::SamplePosition(core::SamplePosition)> snap; // the view's grid

    // The flag at a point in the markers row (later markers win), and a flag's bounds.
    const core::Marker* markerAt(juce::Point<int> p) const noexcept;
    juce::Rectangle<int> flagBounds(const core::Marker& marker) const noexcept;
    // The flags in `row`, clipped to [left, right); the lines through `lanes`.
    void paintFlags(juce::Graphics& g, juce::Rectangle<int> row, int left, int right) const;
    void paintLines(juce::Graphics& g, juce::Rectangle<int> lanes) const;

    core::Id selected() const noexcept { return selected_; }
    void clearSelection() noexcept { selected_ = core::invalidId; }
    // The hovered flag; true when it changed (the ruler repaints).
    bool setHover(core::Id marker) noexcept;
    core::Id hovered() const noexcept { return hover_; }

    // A press on a flag selects it, locates there and starts a drag.
    void press(const core::Marker& marker, core::SamplePosition at);
    void dragTo(core::SamplePosition at); // the pointer's timeline position
    void finishDrag(bool moved);          // moves it (one command) if it moved
    bool dragging() const noexcept { return dragging_; }

    // M: a marker at `at`; Shift+M: a region over [regionStart, regionEnd).
    void addMarker(core::SamplePosition at,
                   bool region,
                   core::SamplePosition regionStart,
                   core::SamplePosition regionEnd);
    // , and .: the previous or next marker from `at` (the start before the first).
    void jump(int direction, core::SamplePosition at);
    // Delete: removes the selected marker; false when none is selected.
    bool removeSelected();
    // An inline field over the name: Enter renames, Escape or a click away cancels.
    void rename(core::Id marker);
    void closeEditor() { editor_.reset(); }

private:
    void emit(std::unique_ptr<core::Command> command, const std::string& name);
    core::SamplePosition shownPosition(const core::Marker& marker) const noexcept;

    juce::Component& owner_;
    const Theme& theme_;
    const TimelineViewport& view_;
    const core::Project* project_ = nullptr;
    core::Id selected_ = core::invalidId, hover_ = core::invalidId;
    bool dragging_ = false;
    core::Marker dragged_;                 // the marker being dragged, as it was
    core::SamplePosition dragStartAt_ = 0; // where the press was
    core::SamplePosition preview_ = 0;     // its position during the drag
    std::unique_ptr<juce::TextEditor> editor_;
};

} // namespace northlight::ui
