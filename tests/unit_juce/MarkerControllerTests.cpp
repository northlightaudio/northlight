#include "core/Command.h"
#include "ui/arrange/MarkerController.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
struct Fixture
{
    juce::Component owner;
    ui::TimelineViewport view{100, 0, 100.0}; // 100 samples a pixel from x = 100
    core::Project project;
    ui::MarkerController markers{owner, ui::Theme::dark(), view};
    core::UndoStack stack;
    std::vector<std::string> edits;
    core::SamplePosition located = -1;

    Fixture()
    {
        owner.setSize(800, 200);
        project.markers.push_back({1, "Verse", 48000, 0});
        project.markers.push_back({2, "Chorus", 96000, 0});
        project.nextId = 10;
        markers.setProject(&project);
        markers.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        {
            for (auto& c : commands)
                stack.execute(project, std::move(c));
            markers.setProject(&project);
            edits.push_back(name);
        };
        markers.onLocate = [this](core::SamplePosition at) { located = at; };
    }
};
} // namespace

TEST_CASE("A marker's flag is found at its x in the markers row", "[ui][arrange][markers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    const int x = f.view.xOf(48000); // 580
    REQUIRE(f.markers.markerAt({x + 2, 5}) != nullptr);
    CHECK(f.markers.markerAt({x + 2, 5})->id == 1);
    CHECK(f.markers.markerAt({x + 2, ui::MarkerController::rowHeight + 1}) == nullptr); // below the row
    CHECK(f.markers.markerAt({x - 3, 5}) == nullptr);
}

TEST_CASE("Dragging a marker moves it by one snapped command, and locates on the press",
          "[ui][arrange][markers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.markers.snap = [](core::SamplePosition at) { return at / 1000 * 1000; };
    f.markers.press(f.project.markers[0], 50000);
    CHECK(f.located == 48000);
    CHECK(f.markers.selected() == 1);
    f.markers.dragTo(60500); // 10 500 later: 58 500, snapped to 58 000
    CHECK(f.markers.flagBounds(f.project.markers[0]).getX() == f.view.xOf(58000));
    f.markers.finishDrag(true);
    REQUIRE(f.edits == std::vector<std::string>{"Move marker"});
    CHECK(f.project.findMarker(1)->position == 58000);
    CHECK_FALSE(f.markers.dragging());
}

TEST_CASE("The marker keys add, jump to and remove markers", "[ui][arrange][markers]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    f.markers.addMarker(12000, false, 0, 0);
    f.markers.addMarker(12000, true, 24000, 36000);
    REQUIRE(f.project.markers.size() == 4);
    CHECK(f.edits == std::vector<std::string>{"Add marker", "Add region"});

    f.markers.jump(1, 50000);
    CHECK(f.located == 96000);
    CHECK(f.markers.selected() == 2);
    CHECK(f.markers.removeSelected());
    CHECK(f.project.findMarker(2) == nullptr);
    CHECK_FALSE(f.markers.removeSelected()); // nothing selected now

    f.located = -1;
    f.markers.jump(-1, 0); // nothing before the start: to the start
    CHECK(f.located == 0);
}
