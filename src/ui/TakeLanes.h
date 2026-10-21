#pragma once

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northlight::ui
{

// Take lanes and comping in the arrange view (docs/design/screens/comping.md). The
// arrange view owns one, asks it how tall a track's lanes are, and hands it
// paints and presses for a row; everything comp-specific lives here so the
// view itself only routes to it.
//
// On the main lane: each take group's comp clips carry a take badge ("T2"),
// a disclosure chip at the group's lower left ("▸ 3 takes") shows or hides the
// lanes, and the boundaries between comp segments drag (below the clip header).
// In the lanes (newest take on top): a take's audio is dim where the comp does
// not use it and lit where it does; a drag across a lane swipes that take into
// the comp over the dragged range, a click puts it in the comp segment under
// the pointer, a click on a lane's header makes that take the whole comp.
// Right-click a lane: use whole take, rename, move to new track, delete,
// flatten, hide lanes; right-click a comp on the main lane: show/hide lanes,
// use a take, flatten.
class TakeLanes
{
public:
    TakeLanes(const Theme& theme, audio::PeakStore& peaks, juce::Component& owner);
    ~TakeLanes();

    struct Geometry
    {
        std::function<int(core::SamplePosition)> xForSample;
        std::function<core::SamplePosition(int)> sampleForX;
        std::function<core::SamplePosition(core::SamplePosition)> snap; // the view's grid, when on
        std::function<double()> samplesPerPixel;
        std::function<double()> sampleRate;
    } geometry;
    // An edit: commands run as one undo step with this name.
    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string&)> onEdit;
    std::function<void()> onRepaint;
    void setProject(const core::Project* project) noexcept { project_ = project; }

    // How many lanes a track shows (its expanded groups' most takes) and the
    // height they add under its row.
    int laneCount(const core::Track& track) const noexcept;
    int lanesHeight(const core::Track& track) const noexcept;

    // Where a track's row is: the main lane in the lanes column (x from the
    // header column's edge to the lanes' right edge) and the header column's width.
    struct Row
    {
        const core::Track* track = nullptr;
        juce::Rectangle<int> main;
        int headerWidth = 0;
        juce::Colour colour;
    };
    // Over the main lane's clips (the view clips to the lanes column).
    void paintMain(juce::Graphics& g, const Row& row) const;
    // The lanes under the row, in the lanes column (clipped by the view).
    void paintLanes(juce::Graphics& g, const Row& row) const;
    // The lanes' headers, in the header column.
    void paintLaneHeaders(juce::Graphics& g, const Row& row) const;

    // A press in a row: true when it is the take lanes' (then the drag and the
    // release come here too).
    bool mouseDown(const juce::MouseEvent& e, const Row& row);
    void mouseDrag(const juce::MouseEvent& e);
    void mouseUp(const juce::MouseEvent& e);
    bool dragging() const noexcept { return press_.kind != PressKind::none; }
    // The cursor over a point of a row, when the take lanes own it; tracks hover.
    std::optional<juce::MouseCursor> hover(juce::Point<int> p, const Row* row);

    // Alt+Up / Alt+Down: the comp segment at a position plays the next newer
    // (direction +1) or older (-1) take. False when there is no group there.
    bool cycleTake(const core::Track& track, core::SamplePosition at, int direction);
    // Shows the track's take lanes, or hides them when any show (Alt+T, the track menu).
    bool toggleLanes(const core::Track& track);

private:
    enum class PressKind
    {
        none,
        swipe,
        boundary,
    };
    struct Press
    {
        PressKind kind = PressKind::none;
        core::Id track = core::invalidId, group = core::invalidId, take = core::invalidId;
        core::SamplePosition anchor = 0, current = 0; // swipe: the range; boundary: from, to
        int lane = -1;
        juce::Point<int> start;
        bool moved = false;
    };
    // The take a lane shows for a group (null when the group has fewer takes).
    static const core::Take* takeInLane(const core::TakeGroup& group, int lane) noexcept;
    // The lane at a y under the row, or -1.
    int laneAt(const Row& row, int y) const noexcept;
    juce::Rectangle<int> laneBounds(const Row& row, int lane) const noexcept;
    // The group the lane headers speak for: the expanded group with a take in
    // that lane, the one in view first.
    const core::TakeGroup* headerGroup(const Row& row, int lane) const noexcept;
    juce::Rectangle<int> chipBounds(const core::TakeGroup& group, const Row& row) const;
    // The comp boundary within reach of x on the main lane (below the clip header), if any.
    std::optional<core::SamplePosition>
    boundaryAt(const core::TakeGroup& group, const Row& row, juce::Point<int> p) const;
    void drawBadge(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, bool lit) const;
    void drawWave(juce::Graphics& g,
                  const core::AudioClip& clip,
                  juce::Rectangle<int> area,
                  juce::Colour colour,
                  const core::TakeGroup& group) const;
    void emit(std::unique_ptr<core::Command> command, const std::string& name);
    void laneMenu(const core::Track& track,
                  const core::TakeGroup& group,
                  const core::Take& take,
                  juce::Rectangle<int> header);
    void compMenu(const core::Track& track, const core::TakeGroup& group);
    void rename(core::Id track, core::Id group, const core::Take& take, juce::Rectangle<int> area);

    const Theme& theme_;
    audio::PeakStore& peaks_;
    juce::Component& owner_;
    const core::Project* project_ = nullptr;
    Press press_;
    // Hover: the boundary under the pointer (group id and position), for its highlight.
    core::Id hoverGroup_ = core::invalidId;
    core::SamplePosition hoverBoundary_ = -1;
    std::unique_ptr<juce::TextEditor> editor_;
};

} // namespace northlight::ui
