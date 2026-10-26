// The piano roll's controller lane on its own: its value scale, drawing, selecting
// and deleting points, driven through its mouse handlers (no window).

#include "core/Command.h"
#include "ui/pianoroll/ControllerLane.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace northlight;

namespace
{
struct Fixture
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::ControllerLane lane{ui::Theme::dark()};
    core::Project project;
    core::UndoStack stack;
    std::vector<std::string> edits;
    bool snap = true;

    Fixture()
    {
        core::Track t;
        t.id = 1;
        t.kind = core::TrackKind::midi;
        core::MidiClip c;
        c.id = 2;
        c.length = 48000;
        c.controllers = {{10, core::MidiControllerType::controller, 1, 20, 1000},
                         {11, core::MidiControllerType::controller, 1, 100, 30000},
                         {12, core::MidiControllerType::controller, 7, 64, 2000}}; // another CC
        t.midiClips.push_back(c);
        project.tracks.push_back(t);
        project.nextId = 100;
        lane.setSize(480, 120);
        lane.setLane(ui::MidiLane::controller(core::MidiControllerType::controller, 1));
        lane.setClip(&project, 1, 2);
        lane.view = [] { return ui::TimelineViewport{0, 0, 100.0}; }; // 100 samples a pixel
        lane.snap = [this](core::SamplePosition s) { return snap ? s / 1000 * 1000 : s; };
        lane.gridSamples = [] { return core::SamplePosition{1000}; };
        lane.snapEnabled = [this] { return snap; };
        lane.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        {
            for (auto& c : commands)
                stack.execute(project, std::move(c));
            edits.push_back(name);
            lane.setClip(&project, 1, 2);
        };
    }

    const std::vector<core::MidiControllerEvent>& events() const
    {
        return project.tracks[0].midiClips[0].controllers;
    }
    int countIn(int number) const
    {
        int n = 0;
        for (const auto& e : events())
            n += e.number == number ? 1 : 0;
        return n;
    }

    juce::MouseEvent event(juce::Point<int> at, juce::Point<int> down, juce::ModifierKeys mods, bool dragged)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                at.toFloat(),
                                mods,
                                1.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &lane,
                                &lane,
                                juce::Time::getCurrentTime(),
                                down.toFloat(),
                                juce::Time::getCurrentTime(),
                                1,
                                dragged);
    }
    void drag(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = {})
    {
        const auto held = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
        lane.mouseDown(event(from, from, held, false));
        lane.mouseDrag(event(to, from, held, true));
        lane.mouseUp(event(to, from, mods, true));
    }
};
} // namespace

TEST_CASE("The controller lane maps values to heights and back", "[ui][midi][cc]")
{
    Fixture f;
    CHECK(f.lane.yForValue(0) == 120 - 4);
    CHECK(f.lane.yForValue(127) == 6);
    // 128 values on 110 px: back within a value.
    for (int v = 0; v <= 127; ++v)
        CHECK(std::abs(f.lane.valueForY(f.lane.yForValue(v)) - v) <= 1);
    CHECK(f.lane.valueForY(-50) == 127); // clamped
    CHECK(f.lane.valueForY(500) == 0);

    // Pitch bend: its whole range over the same height.
    f.lane.setLane(ui::MidiLane::controller(core::MidiControllerType::pitchBend));
    CHECK(f.lane.yForValue(core::midiControllerMinimum(core::MidiControllerType::pitchBend)) == 120 - 4);
    CHECK(f.lane.yForValue(core::midiControllerMaximum(core::MidiControllerType::pitchBend)) == 6);
}

TEST_CASE("A Shift+drag with the pencil draws a line of grid points over what was there", "[ui][midi][cc]")
{
    Fixture f;
    f.lane.setTool(ui::ControllerLane::Tool::draw);
    // From 0 to 40 000 (x 0 to 400), low to high: a point every 1 000 samples,
    // replacing the two CC 1 points in the span and leaving CC 7 alone.
    f.drag({0, f.lane.yForValue(0)}, {400, f.lane.yForValue(127)}, juce::ModifierKeys::shiftModifier);
    REQUIRE(f.edits == std::vector<std::string>{"Draw controller line"});
    CHECK(f.countIn(7) == 1);
    CHECK(f.countIn(1) > 30);
    CHECK(f.lane.selection().size() == static_cast<size_t>(f.countIn(1)));
    // Rising: no point lower than the one before it.
    int last = -1;
    for (const auto& e : f.events())
        if (e.number == 1)
        {
            CHECK(e.time % 1000 == 0);
            CHECK(e.value >= last);
            last = e.value;
        }
}

TEST_CASE("Points are selected by a click or a lasso and deleted together", "[ui][midi][cc]")
{
    Fixture f;
    // A lasso round both CC 1 points (x 10 and 300).
    f.drag({0, 0}, {320, 120});
    CHECK(f.lane.selection().size() == 2);
    f.lane.deleteSelection();
    CHECK(f.edits == std::vector<std::string>{"Delete controller points"});
    CHECK(f.countIn(1) == 0);
    CHECK(f.countIn(7) == 1);
    CHECK(f.lane.selection().empty());

    f.stack.undo(f.project);
    f.lane.setClip(&f.project, 1, 2);
    f.lane.selectAll();
    CHECK(f.lane.selection().size() == 2);
}

TEST_CASE("Dragging a point moves it in time on the grid and in value", "[ui][midi][cc]")
{
    Fixture f;
    const juce::Point<int> point{10, f.lane.yForValue(20)};
    f.drag(point, point + juce::Point<int>{52, -10}); // 5 200 later, snapped to 6 000
    REQUIRE(f.edits == std::vector<std::string>{"Move controller points"});
    const auto* moved = f.project.tracks[0].midiClips[0].findController(10);
    REQUIRE(moved != nullptr);
    CHECK(moved->time == 6000);
    CHECK(moved->value > 20);
}
