// The timeline views' smaller parts that had no tests: the pitch tools' rules,
// the automation lanes' scale, the take lanes and the tempo track.

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Comping.h"
#include "ui/AutomationScale.h"
#include "ui/TakeLanes.h"
#include "ui/TempoLane.h"
#include "ui/audioeditor/PitchEditing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using Catch::Approx;

namespace
{
juce::MouseEvent
mouse(juce::Component& c, juce::Point<int> at, juce::Point<int> down, bool dragged, int clicks = 1)
{
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            at.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            &c,
                            &c,
                            juce::Time::getCurrentTime(),
                            down.toFloat(),
                            juce::Time::getCurrentTime(),
                            clicks,
                            dragged);
}
} // namespace

// ----- the audio editor's pitch tools -----------------------------------------------------

TEST_CASE("A pitch drag lands a note on the nearest semitone, or in cents with Alt", "[ui][audioeditor]")
{
    // A note at 6 020 cents (C3 +20, middle C being C3) dragged up 120 cents lands on 6 100.
    CHECK(ui::pitchedit::dragDelta(6020.0f, 120.0f, false) == Approx(80.0f));
    CHECK(ui::pitchedit::dragDelta(6020.0f, 120.0f, true) == Approx(120.0f));
    CHECK(ui::pitchedit::dragDelta(6020.0f, 20.0f, false) == Approx(-20.0f)); // back to 6 000
}

TEST_CASE("A pitch reads as its nearest note and how far from it", "[ui][audioeditor]")
{
    CHECK(ui::pitchedit::label(6000.0f) == "C3");
    CHECK(ui::pitchedit::label(6002.4f) == "C3 +2 ct");
    CHECK(ui::pitchedit::label(4586.0f) == juce::String::fromUTF8("A#1 \xe2\x88\x92"
                                                                  "14 ct"));
    CHECK(ui::pitchedit::label(6000.4f) == "C3"); // under a cent
    CHECK(ui::pitchedit::signedCents(0.2f) == "0 ct");
}

TEST_CASE("The pitch tools find the note under the pointer and the join beside it", "[ui][audioeditor]")
{
    core::VocalEdit edit;
    core::VocalNote a, b, c;
    a.id = 1;
    a.start = 0;
    a.end = 1000;
    a.target = 6000.0f;
    b.id = 2;
    b.start = 1000;
    b.end = 2000;
    b.target = 6400.0f;
    c.id = 3;
    c.start = 2500;
    c.end = 3000;
    c.target = 6400.0f;
    edit.notes = {a, b, c};
    // 14 px a semitone, 6 400 cents at y 0; 10 samples a pixel from x 0.
    const auto yForCents = [](float cents) { return (6400.0f - cents) / 100.0f * 14.0f; };
    const auto xForSource = [](double s) { return static_cast<float>(s / 10.0); };

    REQUIRE(ui::pitchedit::noteAt(edit, 500, 56.0f, yForCents, 14.0f) != nullptr);
    CHECK(ui::pitchedit::noteAt(edit, 500, 56.0f, yForCents, 14.0f)->id == 1);
    CHECK(ui::pitchedit::noteAt(edit, 500, 0.0f, yForCents, 14.0f) == nullptr); // a row too far from its note
    CHECK(ui::pitchedit::noteAt(edit, 2200, 0.0f, yForCents, 14.0f) == nullptr); // between notes

    // The join at 1 000 (x 100), anywhere between the two rows.
    REQUIRE(ui::pitchedit::joinAt(edit.notes, 102.0f, 30.0f, xForSource, yForCents, 14.0f) != nullptr);
    CHECK(ui::pitchedit::joinAt(edit.notes, 102.0f, 30.0f, xForSource, yForCents, 14.0f)->id == 1);
    CHECK(ui::pitchedit::joinAt(edit.notes, 106.0f, 30.0f, xForSource, yForCents, 14.0f) == nullptr);
    // b and c do not touch: no join between them.
    CHECK(ui::pitchedit::joinAt(edit.notes, 200.0f, 0.0f, xForSource, yForCents, 14.0f) == nullptr);
}

// ----- the automation lanes' scale --------------------------------------------------------

TEST_CASE("An automation lane maps volume in dB, pan and a parameter's range to its height",
          "[ui][automation]")
{
    const ui::AutomationScale gain{core::AutomationTarget::gain};
    CHECK(gain.toNorm(0.0f) == 0.0f);
    CHECK(gain.toNorm(ui::db::toGain(6.0)) == Approx(1.0f));
    CHECK(gain.toNorm(1.0f) == Approx(60.0f / 66.0f));
    CHECK(gain.fromNorm(0.0f) == 0.0f); // silence at the bottom
    CHECK(gain.fromNorm(gain.toNorm(0.5f)) == Approx(0.5f).margin(1.0e-4));

    const ui::AutomationScale pan{core::AutomationTarget::pan};
    CHECK(pan.toNorm(-1.0f) == 0.0f);
    CHECK(pan.toNorm(0.0f) == Approx(0.5f));
    CHECK(pan.fromNorm(1.0f) == Approx(1.0f));

    engine::ParameterInfo cutoff;
    cutoff.minimum = 20.0f;
    cutoff.maximum = 20000.0f;
    cutoff.logarithmic = true;
    const ui::AutomationScale log{core::AutomationTarget::insertParameter, &cutoff};
    CHECK(log.toNorm(std::sqrt(20.0f * 20000.0f)) == Approx(0.5f));
    CHECK(log.fromNorm(0.5f) == Approx(std::sqrt(20.0f * 20000.0f)));

    engine::ParameterInfo mode;
    mode.minimum = 0.0f;
    mode.maximum = 3.0f;
    mode.choices = {"a", "b", "c", "d"};
    const ui::AutomationScale stepped{core::AutomationTarget::insertParameter, &mode};
    CHECK(stepped.fromNorm(0.45f) == 1.0f); // whole steps

    // One mapping between height and y, both ways.
    CHECK(ui::AutomationScale::yFor(0.0f, 10, 40) == Approx(50.0f));
    CHECK(ui::AutomationScale::yFor(1.0f, 10, 40) == Approx(10.0f));
    CHECK(ui::AutomationScale::normAt(ui::AutomationScale::yFor(0.3f, 10, 40), 10, 40) == Approx(0.3f));
}

// ----- the take lanes ----------------------------------------------------------------------

namespace
{
// A track with one take group of two takes over [0, 48 000), the comp playing the first.
core::Project takesProject()
{
    core::Project p;
    core::Track t;
    t.id = 1;
    core::TakeGroup group;
    group.id = 5;
    for (const core::Id id : {50, 51})
    {
        core::Take take;
        take.clip.id = id;
        take.clip.mediaPath = "missing.wav";
        take.clip.length = 48000;
        take.number = static_cast<int>(id) - 49;
        group.takes.push_back(take);
    }
    group.comp = {{0, 48000, 50}};
    group.clips = {60};
    core::AudioClip comp;
    comp.id = 60;
    comp.mediaPath = "missing.wav";
    comp.length = 48000;
    t.clips.push_back(comp);
    t.takeGroups.push_back(group);
    p.tracks.push_back(t);
    p.nextId = 100;
    return p;
}
} // namespace

TEST_CASE("Take lanes count a track's takes when shown, and toggle and cycle them", "[ui][takes]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    juce::Component owner;
    const ui::TimelineViewport view{0, 0, 100.0};
    ui::TakeLanes lanes(ui::Theme::dark(), peaks, owner, view);
    auto project = takesProject();
    lanes.setProject(&project);
    std::vector<std::string> edits;
    core::UndoStack stack;
    lanes.onEdit = [&](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    {
        for (auto& c : commands)
            stack.execute(project, std::move(c));
        edits.push_back(name);
    };

    CHECK(lanes.laneCount(project.tracks[0]) == 0); // collapsed
    CHECK(lanes.toggleLanes(project.tracks[0]));
    CHECK(edits.back() == "Show take lanes");
    CHECK(lanes.laneCount(project.tracks[0]) == 2);
    CHECK(lanes.lanesHeight(project.tracks[0]) == 2 * ui::Theme::dark().sizes.takeLane);

    // Alt+Up at the playhead: the comp segment there plays the next take.
    CHECK(lanes.cycleTake(project.tracks[0], 1000, 1));
    CHECK(edits.back() == "Next take");
    CHECK(core::segmentAt(project.tracks[0].takeGroups[0], 1000)->take == 51);
    // No group there: not the take lanes' key.
    CHECK_FALSE(lanes.cycleTake(project.tracks[0], 96000, 1));
}

// ----- the tempo track ---------------------------------------------------------------------

TEST_CASE("The tempo track adds a change where double-clicked, on the view's grid", "[ui][tempo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ui::TimelineViewport view{260, 0, 1200.0}; // 120 BPM: a beat every 20 px
    ui::TempoLane lane(ui::Theme::dark(), view);
    lane.setSize(1000, 52);
    core::Project project;
    project.sampleRate = 48000.0;
    lane.setProject(&project);
    lane.snap = [](const core::TempoMap& map, core::SamplePosition at) { return map.snap(at, {0, 1.0}); };
    std::vector<core::TempoState> edits;
    lane.onEdit = [&](core::TempoState state, const std::string&) { edits.push_back(std::move(state)); };

    // x 507: 296 400 samples, snapped to the beat at 288 000 (quarter 12).
    lane.mouseDoubleClick(mouse(lane, {507, 30}, {507, 30}, false, 2));
    REQUIRE(edits.size() == 1);
    REQUIRE(edits[0].tempoChanges.size() == 1);
    CHECK(edits[0].tempoChanges[0].quarter == Approx(12.0));
    CHECK(edits[0].tempoChanges[0].bpm == Approx(120.0));

    // Without a snap rule the change goes where the pointer is.
    lane.snap = nullptr;
    lane.mouseDoubleClick(mouse(lane, {507, 30}, {507, 30}, false, 2));
    REQUIRE(edits.size() == 2);
    CHECK(edits[1].tempoChanges[0].quarter == Approx(296400.0 / 24000.0));
}

TEST_CASE("Dragging the tempo track's first handle up raises the song's tempo", "[ui][tempo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ui::TimelineViewport view{260, 0, 1200.0};
    ui::TempoLane lane(ui::Theme::dark(), view);
    lane.setSize(1000, 52);
    core::Project project;
    project.sampleRate = 48000.0;
    lane.setProject(&project);
    std::vector<core::TempoState> edits;
    lane.onEdit = [&](core::TempoState state, const std::string&) { edits.push_back(std::move(state)); };
    // The start's handle sits at x 260, half way up the lane's range (108 to 132 BPM).
    const juce::Point<int> handle{260, 33};
    lane.mouseDown(mouse(lane, handle, handle, false));
    lane.mouseDrag(mouse(lane, handle.translated(0, -10), handle, true));
    lane.mouseUp(mouse(lane, handle.translated(0, -10), handle, true));
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].bpm > 120.0);
    CHECK(edits[0].tempoChanges.empty());
}
