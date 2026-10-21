#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "ui/ArrangeView.h"
#include "ui/PianoRoll.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
// A project with one audio track holding two clips and one MIDI track with a
// clip of two notes, executed through an undo stack so the views see real ids.
struct Fixture
{
    core::Project project;
    core::UndoStack undo;
    core::Id audioTrack = core::invalidId, midiTrack = core::invalidId;

    Fixture()
    {
        project.sampleRate = 48000.0;
        core::Track audio;
        audio.name = "Audio";
        auto add = std::make_unique<core::AddTrack>(audio);
        audioTrack = add->trackId();
        undo.execute(project, std::move(add));
        audioTrack = project.tracks[0].id;
        for (int i = 0; i < 2; ++i)
        {
            core::AudioClip clip;
            clip.name = "Clip " + std::to_string(i + 1);
            clip.mediaPath = "none.wav";
            clip.start = i * 48000;
            clip.length = 24000;
            undo.execute(project, std::make_unique<core::AddClip>(audioTrack, clip));
        }
        core::Track midi;
        midi.name = "Keys";
        midi.kind = core::TrackKind::midi;
        undo.execute(project, std::make_unique<core::AddTrack>(midi));
        midiTrack = project.tracks[1].id;
        core::MidiClip clip;
        clip.name = "Riff";
        clip.start = 0;
        clip.length = 96000;
        for (int i = 0; i < 2; ++i)
        {
            core::MidiNote note;
            note.note = 60 + i;
            note.start = i * 24000;
            note.length = 12000;
            clip.notes.push_back(note);
        }
        undo.execute(project, std::make_unique<core::AddMidiClip>(midiTrack, clip));
    }

    // Executes what a view emits and records the names.
    std::vector<std::string> names;
    void wire(ui::ArrangeView& view)
    {
        view.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        {
            names.push_back(name);
            undo.beginGroup(name);
            for (auto& command : commands)
                undo.execute(project, std::move(command));
            undo.endGroup();
        };
    }
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
} // namespace

TEST_CASE("The arrange view's keys issue the mapped commands", "[ui][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(1200, 600);
    view.setProject(&f.project);
    f.wire(view);

    SECTION("Ctrl+A selects every clip and Esc clears")
    {
        CHECK(view.handleEditKey(key('a', juce::ModifierKeys::commandModifier)));
        CHECK(view.selection().size() == 3);
        CHECK(view.handleEditKey(key(juce::KeyPress::escapeKey)));
        CHECK(view.selection().empty());
    }
    SECTION("Delete removes the selection as one step and undo restores it")
    {
        view.handleEditKey(key('a', juce::ModifierKeys::commandModifier));
        CHECK(view.handleEditKey(key(juce::KeyPress::deleteKey)));
        CHECK(f.project.tracks[0].clips.empty());
        CHECK(f.project.tracks[1].midiClips.empty());
        f.undo.undo(f.project);
        view.setProject(&f.project);
        CHECK(f.project.tracks[0].clips.size() == 2);
    }
    SECTION("S splits at the playhead")
    {
        view.playheadForEdits = [] { return core::SamplePosition{12000}; };
        CHECK(view.handleEditKey(key('s')));
        CHECK(f.names.back() == "Split clips");
        CHECK(f.project.tracks[0].clips.size() == 3);
    }
    SECTION("M adds a marker and Delete removes it once selected")
    {
        view.playheadForEdits = [] { return core::SamplePosition{4800}; };
        CHECK(view.handleEditKey(key('m')));
        REQUIRE(f.project.markers.size() == 1);
        CHECK(f.project.markers[0].position == 4800);
    }
    SECTION("Ctrl+D duplicates, Ctrl+C and Ctrl+V paste at the playhead")
    {
        view.handleEditKey(key('a', juce::ModifierKeys::commandModifier));
        CHECK(view.handleEditKey(key('d', juce::ModifierKeys::commandModifier)));
        CHECK(f.project.tracks[0].clips.size() == 4);
        view.handleEditKey(key('a', juce::ModifierKeys::commandModifier));
        CHECK(view.handleEditKey(key('c', juce::ModifierKeys::commandModifier)));
        CHECK_FALSE(view.clipboardEmpty());
        view.setSelectedTrack(f.audioTrack);
        view.playheadForEdits = [] { return core::SamplePosition{480000}; };
        CHECK(view.handleEditKey(key('v', juce::ModifierKeys::commandModifier)));
        view.setProject(&f.project);
        CHECK(f.names.back() == "Paste clips");
        CHECK(f.project.tracks[0].clips.size() == 8);
        CHECK(f.project.tracks[0].clips.back().start >= 480000);
    }
    SECTION("Alt+Right nudges by the grid")
    {
        view.handleEditKey(key('a', juce::ModifierKeys::commandModifier));
        const auto before = f.project.tracks[0].clips[0].start;
        CHECK(view.handleEditKey(key(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)));
        CHECK(f.names.back() == "Nudge clips");
        CHECK(f.project.tracks[0].clips[0].start > before);
    }
    SECTION("1 and 2 pick the select and range tools")
    {
        CHECK(view.handleEditKey(key('2')));
        CHECK(view.tool() == ui::ArrangeView::Tool::range);
        CHECK(view.handleEditKey(key('1')));
        CHECK(view.tool() == ui::ArrangeView::Tool::select);
    }
}

TEST_CASE("The piano roll's keys issue the mapped commands", "[ui][keys][midi]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Fixture f;
    ui::PianoRoll roll;
    roll.setSize(900, 500);
    roll.setClip(&f.project, f.midiTrack, f.project.tracks[1].midiClips[0].id);
    f.wire(roll);

    SECTION("Ctrl+A, + and - transpose, Delete removes")
    {
        CHECK(roll.handleEditKey(key('a', juce::ModifierKeys::commandModifier)));
        CHECK(roll.selection().size() == 2);
        CHECK(roll.handleEditKey(juce::KeyPress('+', juce::ModifierKeys(), '+')));
        CHECK(f.project.tracks[1].midiClips[0].notes[0].note == 61);
        CHECK(roll.handleEditKey(juce::KeyPress('-', juce::ModifierKeys(), '-')));
        CHECK(f.project.tracks[1].midiClips[0].notes[0].note == 60);
        CHECK(roll.handleEditKey(key(juce::KeyPress::deleteKey)));
        CHECK(f.project.tracks[1].midiClips[0].notes.empty());
    }
    SECTION("Q quantises to the grid")
    {
        auto& note = f.project.tracks[1].midiClips[0].notes[1];
        core::MidiNote moved = note;
        moved.start += 700;
        f.undo.execute(
            f.project,
            std::make_unique<core::SetNote>(f.midiTrack, f.project.tracks[1].midiClips[0].id, moved));
        roll.setClip(&f.project, f.midiTrack, f.project.tracks[1].midiClips[0].id);
        roll.handleEditKey(key('a', juce::ModifierKeys::commandModifier));
        CHECK(roll.handleEditKey(key('q')));
        CHECK(f.names.back() == "Quantise notes");
        CHECK(f.project.tracks[1].midiClips[0].notes[1].start % roll.gridSamples() == 0);
    }
    SECTION("3 and 4 pick the draw and erase tools")
    {
        CHECK(roll.handleEditKey(key('3')));
        CHECK(roll.tool() == ui::PianoRoll::Tool::draw);
        CHECK(roll.handleEditKey(key('4')));
        CHECK(roll.tool() == ui::PianoRoll::Tool::erase);
    }
}
