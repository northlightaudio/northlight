#include "core/Comping.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace northlight::core;

namespace
{
AudioClip pass(const char* media, SamplePosition start, SamplePosition length)
{
    AudioClip c;
    c.name = media;
    c.mediaPath = media;
    c.start = start;
    c.length = length;
    return c;
}

// A project at 48 kHz with one audio track (id 1); `passes` recorded on it as takes.
Project projectWithTakes(UndoStack& stack, std::vector<AudioClip> passes)
{
    Project p;
    p.sampleRate = 48000.0;
    p.nextId = 2;
    Track t;
    t.id = 1;
    t.name = "Vox";
    p.tracks.push_back(t);
    if (!passes.empty())
        stack.execute(p, std::make_unique<RecordTakes>(1, std::move(passes)));
    return p;
}

const TakeGroup& onlyGroup(const Project& p)
{
    REQUIRE(p.tracks[0].takeGroups.size() == 1);
    return p.tracks[0].takeGroups[0];
}

// Two project states alike but for the next id (undo never hands ids back).
bool same(const Project& a, Project b)
{
    b.nextId = a.nextId;
    return a == b;
}

// The comp's clips on the track, in time order.
std::vector<AudioClip> compOf(const Project& p)
{
    std::vector<AudioClip> out;
    const auto& g = onlyGroup(p);
    for (const auto& c : p.tracks[0].clips)
        if (g.ownsClip(c.id))
            out.push_back(c);
    return out;
}
} // namespace

TEST_CASE("The default comp crossfade is 10 ms", "[core][comping]")
{
    CHECK(defaultCompCrossfade(48000.0) == 480);
    CHECK(defaultCompCrossfade(44100.0) == 441);
}

TEST_CASE("A comp plays one clip per segment, crossfaded around each boundary", "[core][comping]")
{
    TakeGroup g;
    g.crossfade = 100;
    g.takes.push_back(Take{pass("a.wav", 1000, 4000), 1});
    g.takes.push_back(Take{pass("b.wav", 1000, 4000), 2});
    g.takes[0].clip.id = 10;
    g.takes[1].clip.id = 11;
    g.takes[1].clip.sourceOffset = 7;
    g.comp = {{1000, 2000, 10}, {2000, 3001, 11}, {3001, 5000, 10}};
    const auto clips = compClips(g);
    REQUIRE(clips.size() == 3);
    // [1000, 2050): take 1, fading out over the 100 samples centred on 2000.
    CHECK(clips[0].mediaPath == "a.wav");
    CHECK(clips[0].start == 1000);
    CHECK(clips[0].end() == 2050);
    CHECK(clips[0].sourceOffset == 0);
    CHECK(clips[0].fadeIn == 0);
    CHECK(clips[0].fadeOut == 100);
    // [1950, 3051): take 2, the source offset following the trimmed start.
    CHECK(clips[1].mediaPath == "b.wav");
    CHECK(clips[1].start == 1950);
    CHECK(clips[1].end() == 3051);
    CHECK(clips[1].sourceOffset == 7 + 950);
    CHECK(clips[1].fadeIn == 100);
    CHECK(clips[1].fadeOut == 100);
    CHECK(clips[2].start == 2951);
    CHECK(clips[2].end() == 5000);
    CHECK(clips[2].fadeIn == 100);
    CHECK(clips[2].fadeOut == 0);
    for (const auto& c : clips)
        CHECK(c.id == invalidId);
}

TEST_CASE("A comp splices without a crossfade where a take does not reach across", "[core][comping]")
{
    TakeGroup g;
    g.crossfade = 100;
    g.takes.push_back(Take{pass("a.wav", 0, 2000), 1});
    g.takes.push_back(Take{pass("b.wav", 1000, 3000), 2}); // starts right at the boundary
    g.takes[0].clip.id = 10;
    g.takes[1].clip.id = 11;
    g.comp = {{0, 1000, 10}, {1000, 4000, 11}};
    auto clips = compClips(g);
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].end() == 1000);
    CHECK(clips[0].fadeOut == 0);
    CHECK(clips[1].start == 1000);
    CHECK(clips[1].fadeIn == 0);

    // A short segment limits the crossfade to half its length.
    g.takes[1].clip.start = 0;
    g.takes[1].clip.length = 4000;
    g.comp = {{0, 1000, 10}, {1000, 1040, 11}, {1040, 2000, 10}};
    clips = compClips(g);
    REQUIRE(clips.size() == 3);
    CHECK(clips[1].start == 990);
    CHECK(clips[1].end() == 1050);
    CHECK(clips[1].fadeIn == 20);
    CHECK(clips[1].fadeOut == 20);
}

TEST_CASE("Swiping a range selects a take there and keeps the comp contiguous", "[core][comping]")
{
    TakeGroup g;
    g.takes.push_back(Take{pass("a.wav", 0, 1000), 1});
    g.takes.push_back(Take{pass("b.wav", 0, 1000), 2});
    g.takes.push_back(Take{pass("c.wav", 0, 600), 3});
    g.takes[0].clip.id = 10;
    g.takes[1].clip.id = 11;
    g.takes[2].clip.id = 12;
    normaliseComp(g);
    REQUIRE(g.comp.size() == 1);
    CHECK(g.comp[0] == CompSegment{0, 1000, 12}); // the newest take, over the whole group

    CHECK(selectTakeForRange(g, 10, 200, 400));
    REQUIRE(g.comp.size() == 3);
    CHECK(g.comp[0] == CompSegment{0, 200, 12});
    CHECK(g.comp[1] == CompSegment{200, 400, 10});
    CHECK(g.comp[2] == CompSegment{400, 1000, 12});
    CHECK_FALSE(selectTakeForRange(g, 10, 250, 350)); // already so
    CHECK_FALSE(selectTakeForRange(g, 12, 700, 900)); // take 3 has no audio there

    // Joining neighbours of the same take merges them.
    CHECK(selectTakeForRange(g, 12, 200, 400));
    REQUIRE(g.comp.size() == 1);

    // Boundaries move within their neighbours; one dragged over a segment removes it.
    CHECK(selectTakeForRange(g, 11, 300, 500));
    CHECK(moveCompBoundary(g, 300, 250));
    CHECK(g.comp[0].end == 250);
    CHECK(g.comp[1].start == 250);
    CHECK_FALSE(moveCompBoundary(g, 123, 200)); // not a boundary
    CHECK(moveCompBoundary(g, 250, 5000));      // clamped at the next segment's end
    CHECK(g.comp.size() == 1);
    REQUIRE(segmentAt(g, 999) != nullptr);
    CHECK(segmentAt(g, 1000) == nullptr);
}

TEST_CASE("Loop passes become takes with the newest playing and undo removes them", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {});
    const auto empty = p;
    // Two full passes and a partial third, as loop recording leaves them.
    stack.execute(p,
                  std::make_unique<RecordTakes>(1,
                                                std::vector<AudioClip>{pass("t1.wav", 1000, 500),
                                                                       pass("t2.wav", 1000, 500),
                                                                       pass("t3.wav", 1000, 200)}));
    const auto& g = onlyGroup(p);
    REQUIRE(g.takes.size() == 3);
    CHECK(g.takes[0].number == 1);
    CHECK(g.takes[2].number == 3);
    CHECK(g.takes[2].clip.name == "Take 3");
    CHECK(g.crossfade == 480);
    // Take 3 where it has audio, take 2 after it.
    REQUIRE(g.comp.size() == 2);
    CHECK(g.comp[0] == CompSegment{1000, 1200, g.takes[2].id()});
    CHECK(g.comp[1] == CompSegment{1200, 1500, g.takes[1].id()});
    const auto clips = compOf(p);
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].mediaPath == "t3.wav");
    CHECK(clips[1].mediaPath == "t2.wav");
    CHECK(p.tracks[0].clips.size() == 2); // only the comp plays: no overlapping takes
    const auto after = p;

    CHECK(stack.undo(p));
    CHECK(same(p, empty));
    CHECK(stack.redo(p));
    CHECK(p == after); // the same ids again
}

TEST_CASE("A recording over a take group joins it as the newest take", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {pass("t1.wav", 1000, 1000), pass("t2.wav", 1000, 1000)});
    CHECK(recordingMakesTakes(p.tracks[0], {pass("x.wav", 1500, 1000)}, false));
    CHECK_FALSE(recordingMakesTakes(p.tracks[0], {pass("x.wav", 5000, 1000)}, false));
    CHECK(recordingMakesTakes(p.tracks[0], {pass("x.wav", 5000, 1000)}, true)); // a punch
    CHECK_FALSE(recordingMakesTakes(p.tracks[0], std::vector<AudioClip>{}, true));

    const auto groupId = onlyGroup(p).id;
    stack.execute(p, std::make_unique<RecordTakes>(1, std::vector<AudioClip>{pass("t3.wav", 1500, 1000)}));
    const auto& g = onlyGroup(p);
    CHECK(g.id == groupId);
    REQUIRE(g.takes.size() == 3);
    CHECK(g.start() == 1000);
    CHECK(g.end() == 2500);
    REQUIRE(g.comp.size() == 2);
    CHECK(g.comp[0] == CompSegment{1000, 1500, g.takes[1].id()});
    CHECK(g.comp[1] == CompSegment{1500, 2500, g.takes[2].id()});
    CHECK(stack.undo(p));
    CHECK(onlyGroup(p).takes.size() == 2);
    CHECK(onlyGroup(p).comp.size() == 1);
}

TEST_CASE("Every comp command undoes and redoes exactly", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack,
                              {pass("t1.wav", 0, 48000), pass("t2.wav", 0, 48000), pass("t3.wav", 0, 48000)});
    const auto groupId = onlyGroup(p).id;
    const auto t1 = onlyGroup(p).takes[0].id(), t2 = onlyGroup(p).takes[1].id(),
               t3 = onlyGroup(p).takes[2].id();
    const auto firstClip = compOf(p).front().id;

    const auto roundTrip = [&](std::unique_ptr<Command> command)
    {
        const auto before = p;
        stack.execute(p, std::move(command));
        const auto after = p;
        CHECK(after != before);
        CHECK(stack.undo(p));
        CHECK(same(p, before));
        CHECK(stack.redo(p));
        CHECK(p == after);
    };

    SECTION("swipe")
    {
        roundTrip(std::make_unique<SelectTake>(1, groupId, t1, 10000, 20000));
        const auto clips = compOf(p);
        REQUIRE(clips.size() == 3);
        CHECK(clips[0].id == firstClip); // a rebuilt comp keeps its clip ids
        CHECK(clips[1].mediaPath == "t1.wav");
        CHECK(clips[1].start == 10000 - 240);
        CHECK(clips[1].end() == 20000 + 240);
    }
    SECTION("whole take")
    {
        roundTrip(std::make_unique<SelectTake>(1, groupId, t2));
        REQUIRE(compOf(p).size() == 1);
        CHECK(compOf(p)[0].mediaPath == "t2.wav");
    }
    SECTION("move boundary")
    {
        stack.execute(p, std::make_unique<SelectTake>(1, groupId, t1, 10000, 20000));
        roundTrip(std::make_unique<MoveCompBoundary>(1, groupId, 20000, 30000));
        CHECK(onlyGroup(p).comp[1] == CompSegment{10000, 30000, t1});
    }
    SECTION("delete take")
    {
        stack.execute(p, std::make_unique<SelectTake>(1, groupId, t1, 10000, 20000));
        roundTrip(std::make_unique<RemoveTake>(1, groupId, t1));
        CHECK(onlyGroup(p).takes.size() == 2);
        REQUIRE(onlyGroup(p).comp.size() == 1); // the newest take plays where take 1 did
        CHECK(onlyGroup(p).comp[0].take == t3);
    }
    SECTION("delete the last take")
    {
        stack.execute(p, std::make_unique<RemoveTake>(1, groupId, t1));
        stack.execute(p, std::make_unique<RemoveTake>(1, groupId, t2));
        roundTrip(std::make_unique<RemoveTake>(1, groupId, t3));
        CHECK(p.tracks[0].takeGroups.empty());
        CHECK(p.tracks[0].clips.empty());
    }
    SECTION("flatten")
    {
        stack.execute(p, std::make_unique<SelectTake>(1, groupId, t1, 10000, 20000));
        const auto clips = compOf(p);
        roundTrip(std::make_unique<FlattenComp>(1, groupId));
        CHECK(p.tracks[0].takeGroups.empty());
        CHECK(p.tracks[0].clips == clips); // the comp, as ordinary clips
    }
    SECTION("rename")
    {
        roundTrip(std::make_unique<RenameTake>(1, groupId, t3, "Keeper"));
        CHECK(onlyGroup(p).takes[2].clip.name == "Keeper");
        CHECK(compOf(p)[0].name == "Keeper");
    }
    SECTION("expand")
    {
        roundTrip(std::make_unique<SetTakeLanesExpanded>(1, groupId, true));
        CHECK(onlyGroup(p).expanded);
        CHECK(SetTakeLanesExpanded(1, groupId, true).isCosmetic());
    }
    SECTION("move to new track")
    {
        const auto before = p;
        stack.beginGroup("Move take to new track");
        for (auto& c : moveTakeToNewTrack(p, 1, groupId, t1))
            stack.execute(p, std::move(c));
        stack.endGroup();
        REQUIRE(p.tracks.size() == 2);
        CHECK(p.tracks[1].name == "Vox Take 1");
        REQUIRE(p.tracks[1].clips.size() == 1);
        CHECK(p.tracks[1].clips[0].mediaPath == "t1.wav");
        CHECK(onlyGroup(p).takes.size() == 2);
        const auto after = p;
        CHECK(stack.undo(p));
        CHECK(same(p, before));
        CHECK(stack.redo(p));
        CHECK(p == after);
        CHECK(moveTakeToNewTrack(p, 1, groupId, 9999).empty());
    }
    SECTION("refused edits change nothing")
    {
        const auto before = p;
        stack.execute(p, std::make_unique<SelectTake>(1, groupId, 9999));
        stack.execute(p, std::make_unique<MoveCompBoundary>(1, groupId, 1234, 5000));
        stack.execute(p, std::make_unique<FlattenComp>(1, 9999));
        CHECK(same(p, before));
        CHECK(stack.undo(p));
        CHECK(same(p, before));
    }
}

TEST_CASE("A take group moves and deletes as one, takes and all", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {pass("t1.wav", 1000, 1000), pass("t2.wav", 1000, 1000)});
    const auto groupId = onlyGroup(p).id;
    stack.execute(p, std::make_unique<SelectTake>(1, groupId, onlyGroup(p).takes[0].id(), 1200, 1400));
    Track other;
    other.id = 50;
    other.name = "Other";
    p.tracks.push_back(other);
    const auto before = p;

    // Along the track, pinned at zero.
    stack.execute(p, std::make_unique<MoveTakeGroup>(1, groupId, 1, -5000));
    CHECK(onlyGroup(p).start() == 0);
    CHECK(onlyGroup(p).comp.front().start == 0);
    CHECK(compOf(p).front().start == 0);
    CHECK(stack.undo(p));
    CHECK(same(p, before));

    // Onto another track: the group and its clips go there; the clips keep their ids.
    const auto clips = compOf(p);
    stack.execute(p, std::make_unique<MoveTakeGroup>(1, groupId, 50, 500));
    CHECK(p.tracks[0].takeGroups.empty());
    CHECK(p.tracks[0].clips.empty());
    REQUIRE(p.tracks[1].takeGroups.size() == 1);
    REQUIRE(p.tracks[1].clips.size() == clips.size());
    CHECK(p.tracks[1].clips[0].id == clips[0].id);
    CHECK(p.tracks[1].clips[0].start == clips[0].start + 500);
    CHECK(p.tracks[1].takeGroups[0].takes[0].clip.start == 1500);
    CHECK(stack.undo(p));
    CHECK(same(p, before));

    stack.execute(p, std::make_unique<RemoveTakeGroup>(1, groupId));
    CHECK(p.tracks[0].takeGroups.empty());
    CHECK(p.tracks[0].clips.empty());
    CHECK(stack.undo(p));
    CHECK(same(p, before));
}

TEST_CASE("Undoing a comp edit brings back the comp's clips as the user left them", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {pass("t1.wav", 0, 48000), pass("t2.wav", 0, 48000)});
    const auto groupId = onlyGroup(p).id;
    const auto clipId = compOf(p).front().id;
    stack.execute(p, std::make_unique<SetClipGain>(1, clipId, 0.5f)); // an ordinary edit on a comp clip
    const auto edited = p;
    stack.execute(p, std::make_unique<SelectTake>(1, groupId, onlyGroup(p).takes[0].id(), 100, 200));
    CHECK(stack.undo(p));
    CHECK(same(p, edited));
}

TEST_CASE("Ripple shifts move take groups with their clips", "[core][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {pass("t1.wav", 1000, 1000), pass("t2.wav", 1000, 1000)});
    const auto before = p;
    stack.execute(p, std::make_unique<ShiftClips>(1, 500, 300));
    CHECK(onlyGroup(p).start() == 1300);
    CHECK(onlyGroup(p).comp[0].start == 1300);
    CHECK(compOf(p)[0].start == 1300);
    CHECK(stack.undo(p));
    CHECK(same(p, before));
}

TEST_CASE("Take groups round-trip through JSON", "[core][json][comping]")
{
    UndoStack stack;
    auto p = projectWithTakes(stack, {pass("t1.wav", 0, 48000), pass("t2.wav", 0, 48000)});
    const auto groupId = onlyGroup(p).id;
    stack.execute(p, std::make_unique<SelectTake>(1, groupId, onlyGroup(p).takes[0].id(), 1000, 2000));
    stack.execute(p, std::make_unique<SetTakeLanesExpanded>(1, groupId, true));
    std::string error;
    const auto text = projectToJson(p);
    CHECK(text.find("\"takeGroups\"") != std::string::npos);
    const auto loaded = projectFromJson(text, &error);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == p);
}

// ----- MIDI takes ------------------------------------------------------------------------------

namespace
{
MidiClip
midiPass(SamplePosition start, SamplePosition length, std::vector<std::pair<int, SamplePosition>> notes)
{
    MidiClip c;
    c.name = "pass";
    c.start = start;
    c.length = length;
    for (const auto& [pitch, at] : notes)
    {
        MidiNote n;
        n.note = pitch;
        n.start = at;
        n.length = 100;
        n.velocity = 90;
        c.notes.push_back(n);
    }
    return c;
}

// A project with one MIDI track (id 1).
Project midiProject()
{
    Project p;
    p.sampleRate = 48000.0;
    p.nextId = 2;
    Track t;
    t.id = 1;
    t.kind = TrackKind::midi;
    t.name = "Keys";
    p.tracks.push_back(t);
    return p;
}
} // namespace

TEST_CASE("Loop-recorded MIDI passes become takes in a MIDI take group, comped as MIDI clips",
          "[core][comping][midi]")
{
    UndoStack stack;
    auto p = midiProject();
    const auto empty = p;
    std::vector<MidiClip> passes{midiPass(1000, 4000, {{60, 0}, {62, 2000}}),
                                 midiPass(1000, 4000, {{64, 500}, {65, 3000}})};
    CHECK(recordingMakesTakes(p.tracks[0], passes, false));
    REQUIRE(stack.execute(p, std::make_unique<RecordTakes>(1, passes)));
    const auto& g = onlyGroup(p);
    CHECK(g.midi);
    REQUIRE(g.takes.size() == 2);
    CHECK(g.takes[0].notes.size() == 2);
    CHECK(g.takes[1].clip.name == "Take 2");
    // The newest plays: one MIDI clip with its notes, no audio clips.
    REQUIRE(p.tracks[0].midiClips.size() == 1);
    CHECK(p.tracks[0].clips.empty());
    CHECK(g.ownsClip(p.tracks[0].midiClips[0].id));
    REQUIRE(p.tracks[0].midiClips[0].notes.size() == 2);
    CHECK(p.tracks[0].midiClips[0].notes[0].note == 64);
    const auto after = p;

    // Swipe the first take over the first half: two clips, each with its take's notes there.
    REQUIRE(stack.execute(p, std::make_unique<SelectTake>(1, g.id, g.takes[0].id(), 1000, 3000)));
    REQUIRE(p.tracks[0].midiClips.size() == 2);
    const auto& left = p.tracks[0].midiClips[0];
    const auto& right = p.tracks[0].midiClips[1];
    CHECK(left.start == 1000);
    CHECK(left.length == 2000);
    REQUIRE(left.notes.size() == 1);
    CHECK(left.notes[0].note == 60);
    CHECK(right.start == 3000);
    REQUIRE(right.notes.size() == 1);
    CHECK(right.notes[0].note == 65);
    CHECK(right.notes[0].start == 1000);

    // Undo and redo.
    CHECK(stack.undo(p));
    CHECK(same(p, after));
    CHECK(stack.undo(p));
    CHECK(same(p, empty));
    CHECK(stack.redo(p));
    CHECK(same(p, after));
}

TEST_CASE("A MIDI comp's segment cuts notes at its end and opens with the controllers' values",
          "[core][comping][midi]")
{
    TakeGroup g;
    g.midi = true;
    Take take;
    take.clip.id = 10;
    take.clip.start = 0;
    take.clip.length = 1000;
    MidiNote long_;
    long_.id = 20;
    long_.note = 60;
    long_.start = 100;
    long_.length = 800;
    take.notes.push_back(long_);
    take.controllers.push_back({21, MidiControllerType::controller, 1, 64, 50});
    g.takes.push_back(take);
    g.comp = {{0, 500, 10}, {500, 1000, 10}};
    const auto clips = compMidiClips(g);
    REQUIRE(clips.size() == 2);
    REQUIRE(clips[0].notes.size() == 1);
    CHECK(clips[0].notes[0].length == 400); // cut at the segment's end
    CHECK(clips[1].notes.empty());          // a note belongs to the segment it starts in
    REQUIRE(clips[1].controllers.size() == 1);
    CHECK(clips[1].controllers[0].time == 0); // the mod wheel's value at 500
    CHECK(clips[1].controllers[0].value == 64);
}

TEST_CASE("MIDI passes merge into the clip under them, or replace what it had there", "[core][comping][midi]")
{
    UndoStack stack;
    auto p = midiProject();
    MidiClip existing = midiPass(0, 4000, {{60, 0}, {62, 1500}, {64, 3500}});
    existing.id = p.allocateId();
    for (auto& n : existing.notes)
        n.id = p.allocateId();
    p.tracks[0].midiClips.push_back(existing);
    const auto before = p;

    SECTION("merge: the notes join, the clip grows to hold the pass")
    {
        REQUIRE(
            stack.execute(p,
                          std::make_unique<RecordMidiPasses>(
                              1, std::vector<MidiClip>{midiPass(1000, 4000, {{67, 0}, {69, 3500}})}, false)));
        REQUIRE(p.tracks[0].midiClips.size() == 1);
        const auto& c = p.tracks[0].midiClips[0];
        CHECK(c.id == existing.id);
        CHECK(c.length == 5000);
        CHECK(c.notes.size() == 5);
        CHECK(c.notes[1].note == 67);
        CHECK(c.notes[1].start == 1000);
    }
    SECTION("replace: the notes the pass covers go first")
    {
        REQUIRE(stack.execute(p,
                              std::make_unique<RecordMidiPasses>(
                                  1, std::vector<MidiClip>{midiPass(1000, 2000, {{67, 0}})}, true)));
        const auto& c = p.tracks[0].midiClips[0];
        REQUIRE(c.notes.size() == 3); // 60 and 64 stay, 62 (at 1500) is replaced by 67
        CHECK(c.notes[0].note == 60);
        CHECK(c.notes[1].note == 67);
        CHECK(c.notes[2].note == 64);
    }
    SECTION("a pass over no clip becomes one, and loop passes pile into it")
    {
        REQUIRE(stack.execute(
            p,
            std::make_unique<RecordMidiPasses>(
                1,
                std::vector<MidiClip>{midiPass(10000, 2000, {{60, 0}}), midiPass(10000, 2000, {{64, 500}})},
                false)));
        REQUIRE(p.tracks[0].midiClips.size() == 2);
        CHECK(p.tracks[0].midiClips[1].notes.size() == 2);
    }
    const auto after = p;
    CHECK(stack.undo(p));
    CHECK(same(p, before));
    CHECK(stack.redo(p));
    CHECK(p == after);
}

TEST_CASE("A MIDI take group moves to another MIDI track, not an audio one", "[core][comping][midi]")
{
    UndoStack stack;
    auto p = midiProject();
    Track other;
    other.id = 50;
    other.kind = TrackKind::midi;
    p.tracks.push_back(other);
    Track audio;
    audio.id = 51;
    p.tracks.push_back(audio);
    p.nextId = 60;
    stack.execute(
        p,
        std::make_unique<RecordTakes>(
            1, std::vector<MidiClip>{midiPass(1000, 1000, {{60, 0}}), midiPass(1000, 1000, {{62, 0}})}));
    const auto groupId = onlyGroup(p).id;
    REQUIRE(stack.execute(p, std::make_unique<MoveTakeGroup>(1, groupId, 51, 500)));
    CHECK(p.tracks[2].takeGroups.empty()); // refused: an audio track
    REQUIRE(stack.execute(p, std::make_unique<MoveTakeGroup>(1, groupId, 50, 500)));
    REQUIRE(p.tracks[1].takeGroups.size() == 1);
    CHECK(p.tracks[1].takeGroups[0].start() == 1500);
    REQUIRE(p.tracks[1].midiClips.size() == 1);
    CHECK(p.tracks[1].midiClips[0].start == 1500);
    CHECK(p.tracks[0].midiClips.empty());

    // Move take to new track: a MIDI track with the take as its clip.
    const auto commands = moveTakeToNewTrack(p, 50, groupId, p.tracks[1].takeGroups[0].takes[0].id());
    REQUIRE(commands.size() == 2);
}

TEST_CASE("MIDI take groups and muted notes round-trip through JSON", "[core][json][comping][midi]")
{
    UndoStack stack;
    auto p = midiProject();
    p.tracks[0].midiRecordMode = MidiRecordMode::replace;
    stack.execute(p,
                  std::make_unique<RecordTakes>(
                      1, std::vector<MidiClip>{midiPass(0, 1000, {{60, 0}}), midiPass(0, 1000, {{62, 10}})}));
    p.tracks[0].midiClips[0].notes[0].muted = true;
    std::string error;
    const auto loaded = projectFromJson(projectToJson(p), &error);
    INFO(error);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == p);
    CHECK(loaded->tracks[0].midiRecordMode == MidiRecordMode::replace);
    CHECK(loaded->tracks[0].takeGroups[0].midi);
    CHECK(loaded->tracks[0].midiClips[0].notes[0].muted);
}

TEST_CASE("Setting a MIDI track's record mode applies, undoes and redoes", "[core][command][midi]")
{
    UndoStack stack;
    auto p = midiProject();
    REQUIRE(stack.execute(p, std::make_unique<SetMidiRecordMode>(1, MidiRecordMode::merge)));
    CHECK(p.tracks[0].midiRecordMode == MidiRecordMode::merge);
    CHECK(stack.undo(p));
    CHECK(p.tracks[0].midiRecordMode == MidiRecordMode::takes);
    CHECK(stack.redo(p));
    CHECK(p.tracks[0].midiRecordMode == MidiRecordMode::merge);
}
