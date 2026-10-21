#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace northlight::ui
{

// Automation in the arrange view (04-arrange.md "Automation"): for each track
// whose header's A toggle is on, one lane is drawn over the track's row, the
// clips dimmed under it. Two chips at the row's top left pick the parameter
// (Volume, Pan, or a parameter of an insert or strip module) and the channel's
// mode (Off, Read, Touch, Latch, Write). Without a lane the static value shows
// as a dashed line.
//
// Editing: click adds a point (dragging places it), drag a point to move it
// (snapped in time; Shift keeps its time), double-click a point removes it,
// right-click offers delete point and clear lane. A drag is previewed and
// committed on release as one command. The overlay covers the arrange view and
// only takes the pointer over the rows it is showing.
class AutomationOverlay final : public juce::Component
{
public:
    explicit AutomationOverlay(const Theme& theme);

    // The arrange view's mapping, in this component's coordinates (it covers the view).
    struct Geometry
    {
        std::function<int(core::SamplePosition)> xForSample;
        std::function<core::SamplePosition(int)> sampleForX;
        std::function<juce::Rectangle<int>(int row)> rowBounds; // the whole row
        std::function<juce::Rectangle<int>()> lanesArea;        // right of the headers, under the ruler
        std::function<core::SamplePosition(core::SamplePosition)> snap;
        // The rows: tracks, then FX tracks.
        std::function<int()> rowCount;
        std::function<const core::Channel*(int row)> channelAt;
    };
    Geometry geometry;

    void setProject(const core::Project* project);
    void setShown(core::Id trackId, bool shown);
    bool isShown(core::Id trackId) const noexcept { return shown_.contains(trackId); }
    bool anyShown() const noexcept { return !shown_.empty(); }

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string&)> onEdit;
    std::function<std::string(const std::string& type)> displayName;
    // A hosted plugin's parameters (from its live instance); null when unknown.
    std::function<const std::vector<engine::ParameterInfo>*(core::Id insertId)> hostedParameters;
    // The overlay hid a track's automation itself (its menus): the header follows.
    std::function<void(core::Id trackId, bool shown)> onShownChanged;

    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // The parameter a track shows.
    struct Key
    {
        core::AutomationTarget target = core::AutomationTarget::gain;
        core::Id insert = core::invalidId;
        int parameter = 0;
        bool operator==(const Key&) const = default;
    };
    Key keyFor(core::Id trackId) const;
    void setKey(core::Id trackId, Key key);

private:
    struct Range; // the value mapping for a key
    int rowOfTrack(core::Id trackId) const noexcept;
    int rowAt(int y) const noexcept;
    juce::Rectangle<int> laneArea(int row) const;
    juce::Rectangle<int> parameterChip(int row) const;
    juce::Rectangle<int> modeChip(int row) const;
    juce::String keyName(const core::Channel& track, const Key& key) const;
    float toNorm(const core::Channel& track, const Key& key, float value) const;
    float fromNorm(const core::Channel& track, const Key& key, float norm) const;
    float staticValue(const core::Channel& track, const Key& key) const;
    juce::String valueText(const core::Channel& track, const Key& key, float value) const;
    const core::AutomationLane* laneFor(const core::Channel& track, const Key& key) const;
    const std::vector<engine::ParameterInfo>* parametersOf(const core::Insert& insert) const;
    const engine::ParameterInfo* infoFor(const core::Channel& track, const Key& key) const;
    int pointAt(const core::Channel& track, int row, juce::Point<int> p) const;
    void showParameterMenu(const core::Channel& track, int row);
    void showModeMenu(const core::Channel& track, int row);
    void showPointMenu(const core::Channel& track, int row, int point);
    void emitPoints(const core::Channel& track,
                    const Key& key,
                    std::vector<core::AutomationPoint> points,
                    const std::string& name);

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    std::set<core::Id> shown_;
    std::map<core::Id, Key> keys_;

    // The drag: a preview of the lane's points with one moving.
    core::Id dragTrack_ = core::invalidId;
    std::vector<core::AutomationPoint> dragPoints_;
    int dragIndex_ = -1;
    bool dragNew_ = false;
    core::SamplePosition dragPosition_ = 0; // the moving point's position at mouse down
    int hoverRow_ = -1, hoverPoint_ = -1;
};

} // namespace northlight::ui
