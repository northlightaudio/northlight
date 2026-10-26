#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Pulsar.h"
#include "ui/PianoRoll.h"
#include "ui/panels/InspectorPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
// A MIDI track (its notes) aimed at an instrument channel, the clip's notes at
// beats 0, 1, 2 and 3 (120 bpm at 48 kHz: 24 000 samples a beat).
constexpr core::SamplePosition beat = 24000;

struct Fixture
{
    core::Project project;
    core::UndoStack undo;
    core::Id midiTrack = core::invalidId, clipId = core::invalidId;
    std::vector<std::string> names;

    explicit Fixture(const std::string& instrument = "northlight.aurora")
    {
        project.sampleRate = 48000.0;
        core::Track channel;
        channel.kind = core::TrackKind::instrument;
        channel.name = "Synth";
        channel.instrument.type = instrument;
        channel.instrument.params = plugins::defaultParameters(instrument);
        core::Track midi;
        midi.kind = core::TrackKind::midi;
        midi.name = "Keys";
        undo.execute(project, std::make_unique<core::AddInstrumentTrack>(channel, midi));
        midiTrack = project.tracks[0].id;
        core::MidiClip clip;
        clip.name = "Riff";
        clip.length = 16 * beat;
        for (int i = 0; i < 4; ++i)
        {
            core::MidiNote n;
            n.note = 60 + i * 2;
            n.start = i * beat;
            n.length = beat / 2;
            n.velocity = 60 + i * 10;
            clip.notes.push_back(n);
        }
        auto add = std::make_unique<core::AddMidiClip>(midiTrack, clip);
        undo.execute(project, std::move(add));
        clipId = project.tracks[0].midiClips[0].id;
    }
    const core::MidiClip& clip() const { return project.tracks[0].midiClips[0]; }
    void wire(ui::PianoRoll& roll)
    {
        roll.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        {
            names.push_back(name);
            undo.beginGroup(name);
            for (auto& command : commands)
                undo.execute(project, std::move(command));
            undo.endGroup();
        };
    }
};

juce::KeyPress key(int code, int modifiers = 0)
{
    return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0);
}
constexpr int ctrl = juce::ModifierKeys::commandModifier;
constexpr int shift = juce::ModifierKeys::shiftModifier;
constexpr int alt = juce::ModifierKeys::altModifier;
} // namespace

TEST_CASE("The piano roll copies, cuts, pastes and duplicates notes", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    f.wire(roll);
    roll.setSelection({f.clip().notes[0].id, f.clip().notes[1].id});

    SECTION("Ctrl+C then Ctrl+V pastes at the playhead, selecting the copies")
    {
        CHECK(roll.handleEditKey(key('c', ctrl)));
        CHECK_FALSE(roll.clipboardEmpty());
        roll.setPlayhead(8 * beat);
        CHECK(roll.handleEditKey(key('v', ctrl)));
        roll.setClip(&f.project, f.midiTrack, f.clipId);
        CHECK(f.names.back() == "Paste notes");
        REQUIRE(f.clip().notes.size() == 6);
        CHECK(f.clip().notes[4].start == 8 * beat);
        CHECK(f.clip().notes[5].start == 9 * beat);
        CHECK(roll.selection().size() == 2);
        f.undo.undo(f.project);
        CHECK(f.clip().notes.size() == 4);
    }
    SECTION("Ctrl+X removes the notes and keeps them to paste")
    {
        CHECK(roll.handleEditKey(key('x', ctrl)));
        roll.setClip(&f.project, f.midiTrack, f.clipId);
        CHECK(f.clip().notes.size() == 2);
        roll.pasteAt(12 * beat);
        CHECK(f.clip().notes.size() == 4);
    }
    SECTION("Ctrl+D duplicates right after the selection")
    {
        CHECK(roll.handleEditKey(key('d', ctrl)));
        roll.setClip(&f.project, f.midiTrack, f.clipId);
        REQUIRE(f.clip().notes.size() == 6);
        // The two notes span 1.5 beats (whole grid steps): the copies start there.
        CHECK(std::any_of(f.clip().notes.begin(),
                          f.clip().notes.end(),
                          [](const core::MidiNote& n) { return n.start == beat * 3 / 2 && n.note == 60; }));
    }
}

TEST_CASE("The piano roll's keys step, lengthen, mute, split and join notes", "[ui][pianoroll][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    f.wire(roll);

    SECTION("Ctrl+Right and Ctrl+Left step through the notes; Shift extends")
    {
        CHECK(roll.handleEditKey(key(juce::KeyPress::rightKey, ctrl)));
        CHECK(roll.selection() == std::vector<core::Id>{f.clip().notes[0].id});
        CHECK(roll.handleEditKey(key(juce::KeyPress::rightKey, ctrl)));
        CHECK(roll.selection() == std::vector<core::Id>{f.clip().notes[1].id});
        CHECK(roll.handleEditKey(key(juce::KeyPress::rightKey, ctrl | shift)));
        CHECK(roll.selection().size() == 2);
        CHECK(roll.handleEditKey(key(juce::KeyPress::leftKey, ctrl)));
        CHECK(roll.selection() == std::vector<core::Id>{f.clip().notes[0].id});
    }
    SECTION("Alt+Right lengthens by a grid step, Alt+Left shortens")
    {
        roll.setSelection({f.clip().notes[0].id});
        CHECK(roll.handleEditKey(key(juce::KeyPress::rightKey, alt)));
        CHECK(f.clip().notes[0].length == beat / 2 + roll.gridSamples());
        CHECK(roll.handleEditKey(key(juce::KeyPress::leftKey, alt)));
        CHECK(f.clip().notes[0].length == beat / 2);
    }
    SECTION("Ctrl+M mutes the selection and again unmutes it")
    {
        roll.selectAll();
        CHECK(roll.handleEditKey(key('m', ctrl)));
        for (const auto& n : f.clip().notes)
            CHECK(n.muted);
        CHECK(f.names.back() == "Mute notes");
        CHECK(roll.handleEditKey(key('m', ctrl)));
        for (const auto& n : f.clip().notes)
            CHECK_FALSE(n.muted);
    }
    SECTION("S splits the selection at the playhead and J joins it back")
    {
        roll.setSelection({f.clip().notes[0].id});
        roll.setPlayhead(beat / 4);
        CHECK(roll.handleEditKey(key('s')));
        roll.setClip(&f.project, f.midiTrack, f.clipId);
        REQUIRE(f.clip().notes.size() == 5);
        CHECK(f.clip().notes[0].length == beat / 4);
        CHECK(f.clip().notes[1].start == beat / 4);
        roll.setSelection({f.clip().notes[0].id, f.clip().notes[1].id});
        CHECK(roll.handleEditKey(key('j')));
        CHECK(f.clip().notes.size() == 4);
        CHECK(f.clip().notes[0].length == beat / 2);
    }
    SECTION("5, 6 and 7 pick the split, glue and mute tools")
    {
        CHECK(roll.handleEditKey(key('5')));
        CHECK(roll.tool() == ui::PianoRoll::Tool::split);
        CHECK(roll.handleEditKey(key('6')));
        CHECK(roll.tool() == ui::PianoRoll::Tool::glue);
        CHECK(roll.handleEditKey(key('7')));
        CHECK(roll.tool() == ui::PianoRoll::Tool::mute);
    }
    SECTION("[ and ] step the grid; triplet grids snap to thirds of a beat")
    {
        roll.setGrid(ui::defaultNoteGrid);
        CHECK(roll.handleEditKey(key(']')));
        CHECK(roll.gridSamples() == beat / 8);
        CHECK(roll.handleEditKey(key('[')));
        CHECK(roll.handleEditKey(key('[')));
        CHECK(roll.gridSamples() == beat / 2);
        for (std::size_t i = 0; i < ui::noteGrids.size(); ++i)
            if (ui::noteGrids[i].label == "1/8T")
                roll.setGrid(i);
        CHECK(roll.gridSamples() == beat / 3);
    }
}

TEST_CASE("Quantise swing and humanise go out as one step each", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    f.wire(roll);
    roll.selectAll();
    roll.humaniseSelection(480, 6);
    CHECK(f.names.back() == "Humanise notes");
    const auto steps = f.undo.undoCount();
    ui::noteops::QuantiseOptions options;
    roll.quantiseSelection(options);
    CHECK(f.undo.undoCount() == steps + 1);
    for (const auto& n : f.clip().notes)
        CHECK(n.start % roll.gridSamples() == 0);
}

TEST_CASE("New notes take the default velocity, which follows the last velocity set", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    CHECK(roll.defaultVelocity() == 100);
    roll.setDefaultVelocity(72);
    CHECK(roll.defaultVelocity() == 72);
    roll.setDefaultVelocity(400);
    CHECK(roll.defaultVelocity() == 127);
}

TEST_CASE("The piano roll's lanes stack, resize and go", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 700);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    CHECK(roll.laneCount() == 1);
    CHECK(roll.laneHeight(0) == ui::PianoRoll::defaultLaneHeight);
    roll.addLane(ui::PianoRoll::laneForKey("mod"));
    REQUIRE(roll.laneCount() == 2);
    CHECK(roll.laneAt(0).velocity);
    CHECK_FALSE(roll.laneAt(1).velocity);
    roll.setLaneHeight(0, 150);
    CHECK(roll.laneHeight(0) == 150);
    roll.setLaneHeight(0, 5);
    CHECK(roll.laneHeight(0) == ui::PianoRoll::minLaneHeight);
    for (int i = 0; i < 6; ++i)
        roll.addLane(ui::PianoRoll::laneForKey("bend"));
    CHECK(roll.laneCount() == ui::PianoRoll::maxLanes);
    while (roll.laneCount() > 1)
        roll.removeLane(roll.laneCount() - 1);
    roll.removeLane(0); // the last lane stays
    CHECK(roll.laneCount() == 1);
}

TEST_CASE("The piano roll zooms its rows and fits the notes", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    roll.setRowHeight(20);
    CHECK(roll.rowHeight() == 20);
    roll.setRowHeight(100);
    CHECK(roll.rowHeight() == ui::PianoRoll::maxRowHeight);
    CHECK(roll.handleEditKey(juce::KeyPress('z', juce::ModifierKeys(), 'z'))); // as typed
    // Four notes over seven semitones fill the grid: the rows grow.
    CHECK(roll.rowHeight() > ui::PianoRoll::defaultRowHeight);
}

TEST_CASE("A drum kit names the piano roll's rows and can hide the rest", "[ui][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f(plugins::pulsar::type);
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    REQUIRE(roll.hasNoteNames());
    namespace pu = plugins::pulsar;
    CHECK(roll.rowName(pu::padNote(pu::kick)) == "Kick");
    CHECK(roll.rowName(pu::padNote(pu::snare)) == "Snare");
    CHECK(roll.rowName(100).isEmpty());
    CHECK(plugins::builtInNoteName(pu::type, pu::padNote(pu::rim)) == "Rim");
    CHECK(plugins::builtInNoteName("northlight.aurora", 60).empty());

    // Only the named rows (and the clip's own pitches) show.
    roll.setNamedRowsOnly(true);
    CHECK(roll.namedRowsOnly());
    CHECK(roll.verticalScroll() == 0); // the few rows start at the top
    roll.setNamedRowsOnly(false);
    CHECK_FALSE(roll.namedRowsOnly());
}

TEST_CASE("Selecting notes in the piano roll shows them in the inspector, editable",
          "[ui][pianoroll][inspector]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&f.project, f.midiTrack, f.clipId);
    ui::InspectorPanel inspector(ui::Theme::dark());
    inspector.setSize(300, 600);
    inspector.setProject(&f.project);
    roll.onSelectionChanged = [&] { inspector.setNotes(roll.trackId(), roll.clipId(), roll.selection()); };
    CHECK(inspector.shows() == ui::InspectorPanel::Shows::nothing);
    roll.setSelection({f.clip().notes[1].id});
    CHECK(inspector.shows() == ui::InspectorPanel::Shows::notes);
    roll.clearSelection();
    CHECK(inspector.shows() == ui::InspectorPanel::Shows::nothing);
    roll.setSelection({f.clip().notes[1].id});
    inspector.setSelection(f.midiTrack, core::invalidId); // a track selected in the arrange
    CHECK(inspector.shows() == ui::InspectorPanel::Shows::track);
}
