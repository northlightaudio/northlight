#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northlight::ui
{

// Automation in the arrange view (screens/arrange.md "Automation"): a channel's
// lanes are rows of their own under its row (and its take lanes), one per
// automated parameter, when the channel's lanes are open (the header's A).
// Each lane has a header in the header column (collapse, the parameter menu,
// the value at the playhead, hide) and its curve to the right. A lane is
// resized at its bottom edge, collapsed to a strip or hidden, none of which
// changes what plays. A footer row adds a lane and lists the hidden ones.
//
// Editing a curve: click adds a point (dragging places it), drag a point to
// move it (snapped in time; Shift keeps its time), double-click a point
// removes it. A drag is previewed and committed on release as one command.
//
// The arrange view lays the rows out (blockHeight gives each channel's share)
// and paints the lanes' backgrounds under its grid (paintBackgrounds); this
// component covers the view, paints the rest and takes the pointer only over
// the lane rows.
class AutomationLanes final : public juce::Component
{
public:
    explicit AutomationLanes(const Theme& theme);

    // The arrange view's mapping, in this component's coordinates (it covers the view).
    struct Geometry
    {
        std::function<int(core::SamplePosition)> xForSample;
        std::function<core::SamplePosition(int)> sampleForX;
        std::function<core::SamplePosition(core::SamplePosition)> snap;
        std::function<int()> headerWidth;
        std::function<juce::Rectangle<int>()> lanesArea; // right of the headers, under the ruler
        // The rows: tracks, then FX tracks.
        std::function<int()> rowCount;
        std::function<const core::Channel*(int row)> channelAt;
        // A row's lanes, full width (header column included); empty when it has none.
        std::function<juce::Rectangle<int>(int row)> block;
        std::function<juce::Colour(int row)> colour; // the track's colour
        std::function<core::SamplePosition()> playhead;
    };
    Geometry geometry;

    void setProject(const core::Project* project);

    // The height a channel's lanes take under its row: 0 when they are closed.
    int blockHeight(const core::Channel& channel) const noexcept;
    // The lanes' backgrounds, under the arrange view's grid.
    void paintBackgrounds(juce::Graphics& g) const;
    // The playhead moved: repaints the lane headers whose value changed.
    void playheadMoved();
    // The A toggle's menu for a channel: its mode and its lanes.
    void showChannelMenu(core::Id channelId, juce::Rectangle<int> screenArea);

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string&)> onEdit;
    std::function<std::string(const std::string& type)> displayName;
    // A hosted plugin's parameters (from its live instance); null when unknown.
    std::function<const std::vector<engine::ParameterInfo>*(core::Id insertId)> hostedParameters;
    // A lane's height changed while it is dragged: the arrange view lays out again.
    std::function<void()> onLayoutChanged;

    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // A lane's parameter.
    struct Key
    {
        core::AutomationTarget target = core::AutomationTarget::gain;
        core::Id insert = core::invalidId;
        int parameter = 0;
        bool operator==(const Key&) const = default;
    };

    // One row of a channel's block: a lane (index into its automation) or the footer (-1).
    struct Slot
    {
        int row = -1;
        int lane = -1;
        juce::Rectangle<int> bounds; // full width
    };
    std::vector<Slot> slotsOf(int row) const;

private:
    int laneHeight(const core::AutomationLane& lane) const noexcept;
    std::optional<Slot> slotAt(juce::Point<int> p) const;
    const core::AutomationLane* laneOf(const Slot& slot) const;
    const core::Channel* channelOf(const Slot& slot) const;
    static Key keyOf(const core::AutomationLane& lane) noexcept;
    juce::Rectangle<int> curveArea(const Slot& slot) const;
    // The header's parts.
    juce::Rectangle<int> headerOf(const Slot& slot) const;
    juce::Rectangle<int> chevronOf(const Slot& slot) const;
    juce::Rectangle<int> eyeOf(const Slot& slot) const;
    juce::Rectangle<int> valueOf(const Slot& slot) const;
    juce::Rectangle<int> nameOf(const Slot& slot) const;
    juce::Rectangle<int> addOf(const Slot& slot) const;    // the footer's "+ Add lane"
    juce::Rectangle<int> hiddenOf(const Slot& slot) const; // the footer's "n hidden"
    bool onResizeEdge(const Slot& slot, juce::Point<int> p) const;

    juce::String keyName(const core::Channel& track, const Key& key) const;
    juce::String rangeText(const core::Channel& track, const Key& key) const;
    float toNorm(const core::Channel& track, const Key& key, float value) const;
    float fromNorm(const core::Channel& track, const Key& key, float norm) const;
    float staticValue(const core::Channel& track, const Key& key) const;
    juce::String valueText(const core::Channel& track, const Key& key, float value) const;
    juce::String valueAtPlayhead(const core::Channel& track, const core::AutomationLane& lane) const;
    const std::vector<engine::ParameterInfo>* parametersOf(const core::Insert& insert) const;
    const engine::ParameterInfo* infoFor(const core::Channel& track, const Key& key) const;
    int pointAt(const Slot& slot, juce::Point<int> p) const;
    float pointX(const Slot& slot, core::SamplePosition position) const;

    void paintLane(juce::Graphics& g, const Slot& slot) const;
    void paintHeader(juce::Graphics& g, const Slot& slot) const;
    void paintFooter(juce::Graphics& g, const Slot& slot) const;

    // Menus. `menuArea` is in this component's coordinates.
    void addParameters(juce::PopupMenu& menu,
                       const core::Channel& track,
                       const Key* current,
                       std::vector<Key>& keys) const;
    void showLaneMenu(const Slot& slot, juce::Rectangle<int> menuArea, int point);
    void showAddMenu(core::Id channelId, juce::Rectangle<int> screenArea);
    void showHiddenMenu(core::Id channelId, juce::Rectangle<int> screenArea);
    // Shows the parameter's lane (opening, expanding or unhiding it) or adds one.
    void revealOrAdd(core::Id channelId, const Key& key);
    void setView(core::Id channelId, core::Id laneId, core::AutomationLaneView view, const std::string& name);
    void emit(std::unique_ptr<core::Command> command, const std::string& name);
    void emitPoints(const core::Channel& track,
                    const core::AutomationLane& lane,
                    std::vector<core::AutomationPoint> points,
                    const std::string& name);

    const Theme& theme_;
    const core::Project* project_ = nullptr;

    // The drag: a preview of a lane's points with one moving.
    core::Id dragChannel_ = core::invalidId, dragLane_ = core::invalidId;
    std::vector<core::AutomationPoint> dragPoints_;
    int dragIndex_ = -1;
    bool dragNew_ = false;
    core::SamplePosition dragPosition_ = 0; // the moving point's position at mouse down
    // A lane being resized, at its previewed height.
    core::Id resizeLane_ = core::invalidId, resizeChannel_ = core::invalidId;
    int resizeHeight_ = 0, resizeStartHeight_ = 0;
    // The point under the pointer, and the header part (a Part) under it.
    core::Id hoverLane_ = core::invalidId;
    int hoverPoint_ = -1;
    enum class Part
    {
        none,
        chevron,
        name,
        eye,
        add,
        hidden,
    };
    Part hoverPart_ = Part::none;
    int hoverRow_ = -1, hoverSlotLane_ = -1; // the slot whose part is hovered
    Part partAt(const Slot& slot, juce::Point<int> p) const;
    bool hovered(const Slot& slot, Part part) const noexcept
    {
        return hoverPart_ == part && hoverRow_ == slot.row && hoverSlotLane_ == slot.lane;
    }
    // The values the headers show, to repaint only those that change as the playhead moves.
    mutable std::map<core::Id, juce::String> shownValues_;
};

} // namespace northlight::ui
