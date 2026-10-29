#include "core/ArrangeCommands.h"
#include "core/Command.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace northlight::core;

namespace
{
// A MIDI track (id 1) and an audio track (id 2).
Project midiProject()
{
    Project p;
    Track midi;
    midi.id = 1;
    midi.name = "Keys";
    midi.kind = TrackKind::midi;
    p.tracks.push_back(midi);
    Track audio;
    audio.id = 2;
    audio.name = "Guitar";
    p.tracks.push_back(audio);
    p.nextId = 10;
    return p;
}

Insert midiEffect(const char* type, float semitones = 0.0f)
{
    Insert insert;
    insert.type = type;
    insert.params = {0.0f, semitones, 0.0f};
    return insert;
}

std::vector<std::string> midiTypes(const Project& p)
{
    std::vector<std::string> out;
    for (const auto& i : p.tracks[0].midiInserts)
        out.push_back(i.type);
    return out;
}

// Adds a MIDI effect to the MIDI track and returns its id.
Id addMidi(Project& p, UndoStack& stack, const char* type, int index = -1)
{
    auto add = std::make_unique<AddInsert>(1, midiEffect(type), index, InsertChain::midi);
    const auto* ptr = add.get();
    REQUIRE(stack.execute(p, std::move(add)));
    return ptr->insertId();
}
} // namespace

TEST_CASE("A MIDI effect joins a MIDI track's MIDI FX, and undo and redo take it out and back",
          "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    Insert keyed = midiEffect("northlight.transpose");
    keyed.mix = 0.5f;
    keyed.sidechain = 2;
    auto add = std::make_unique<AddInsert>(1, keyed, -1, InsertChain::midi);
    const auto* ptr = add.get();
    REQUIRE(stack.execute(p, std::move(add)));
    CHECK(stack.undoName() == "Add MIDI effect");
    const Id id = ptr->insertId();
    REQUIRE(p.tracks[0].midiInserts.size() == 1);
    CHECK(p.tracks[0].inserts.empty());
    const auto& added = p.tracks[0].midiInserts[0];
    CHECK(added.mix == 1.0f); // no wet/dry or key on a MIDI effect
    CHECK(added.sidechain == invalidId);
    CHECK(p.tracks[0].findInsert(id) == &added);
    CHECK(p.tracks[0].isMidiInsert(id));
    // It comes first in the processing order.
    REQUIRE_FALSE(p.tracks[0].processingChain().empty());
    CHECK(p.tracks[0].processingChain().front()->id == id);

    CHECK(stack.undo(p));
    CHECK(p.tracks[0].midiInserts.empty());
    CHECK(stack.redo(p));
    REQUIRE(p.tracks[0].midiInserts.size() == 1);
    CHECK(p.tracks[0].midiInserts[0].id == id); // the same effect, back

    // Only a MIDI track has MIDI FX.
    CHECK_FALSE(stack.execute(
        p, std::make_unique<AddInsert>(2, midiEffect("northlight.transpose"), -1, InsertChain::midi)));
    CHECK(p.tracks[1].midiInserts.empty());
}

TEST_CASE("Removing a MIDI effect is undone into its place", "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    addMidi(p, stack, "a");
    const Id b = addMidi(p, stack, "b");
    addMidi(p, stack, "c");
    // The audio chain does not hold it.
    CHECK_FALSE(stack.execute(p, std::make_unique<RemoveInsert>(1, b)));
    REQUIRE(stack.execute(p, std::make_unique<RemoveInsert>(1, b, InsertChain::midi)));
    CHECK(stack.undoName() == "Remove MIDI effect");
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "c"});
    CHECK(stack.undo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "b", "c"});
    CHECK(stack.redo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "c"});
}

TEST_CASE("Moving a MIDI effect reorders the MIDI FX, undo and redo too", "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    const Id a = addMidi(p, stack, "a");
    addMidi(p, stack, "b");
    addMidi(p, stack, "c");
    REQUIRE(stack.execute(p, std::make_unique<MoveInsert>(1, a, 2, InsertChain::midi)));
    CHECK(midiTypes(p) == std::vector<std::string>{"b", "c", "a"});
    CHECK(stack.undo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "b", "c"});
    CHECK(stack.redo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"b", "c", "a"});
}

TEST_CASE("Replacing a MIDI effect keeps its id and place, undo and redo too", "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    addMidi(p, stack, "a");
    const Id b = addMidi(p, stack, "b");
    REQUIRE(
        stack.execute(p, std::make_unique<ReplaceInsert>(1, b, midiEffect("z", 5.0f), InsertChain::midi)));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "z"});
    CHECK(p.tracks[0].midiInserts[1].id == b);
    CHECK(p.tracks[0].midiInserts[1].params[1] == 5.0f);
    CHECK(stack.undo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "b"});
    CHECK(stack.redo(p));
    CHECK(midiTypes(p) == std::vector<std::string>{"a", "z"});
}

TEST_CASE("Pasting MIDI effects gives each copy an id of its own, undo and redo too",
          "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    const Id a = addMidi(p, stack, "a");
    const auto copies = p.tracks[0].midiInserts;
    auto paste = std::make_unique<AddInserts>(1, copies, 0, "Paste MIDI effects", InsertChain::midi);
    const auto* ptr = paste.get();
    REQUIRE(stack.execute(p, std::move(paste)));
    REQUIRE(p.tracks[0].midiInserts.size() == 2);
    CHECK(p.tracks[0].midiInserts[0].id == ptr->inserts()[0].id);
    CHECK(p.tracks[0].midiInserts[0].id != a);
    CHECK(stack.undo(p));
    CHECK(p.tracks[0].midiInserts.size() == 1);
    CHECK(stack.redo(p));
    CHECK(p.tracks[0].midiInserts.size() == 2);
    // Not onto an audio track's MIDI FX.
    CHECK_FALSE(stack.execute(p, std::make_unique<AddInserts>(2, copies, -1, "Paste", InsertChain::midi)));
}

TEST_CASE("A MIDI effect's parameters and bypass use the insert commands", "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    const Id id = addMidi(p, stack, "northlight.transpose");
    REQUIRE(stack.execute(p, std::make_unique<SetInsertParameter>(1, id, 1, 7.0f)));
    CHECK(p.tracks[0].midiInserts[0].params[1] == 7.0f);
    REQUIRE(stack.execute(p, std::make_unique<SetInsertBypass>(1, id, true)));
    CHECK(p.tracks[0].midiInserts[0].bypass);
    CHECK(stack.undo(p));
    CHECK_FALSE(p.tracks[0].midiInserts[0].bypass);
    CHECK(stack.undo(p));
    CHECK(p.tracks[0].midiInserts[0].params[1] == 0.0f);
}

TEST_CASE("MIDI FX round-trip through the project file; a file without them opens with none",
          "[core][json][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    addMidi(p, stack, "northlight.transpose");
    p.tracks[0].midiInserts[0].params = {2.0f, -3.0f, 1.0f};
    p.tracks[0].midiInserts[0].bypass = true;
    const auto text = projectToJson(p);
    CHECK(text.find("\"midiInserts\"") != std::string::npos);
    const auto loaded = projectFromJson(text);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->tracks.size() == 2);
    CHECK(loaded->tracks[0].midiInserts == p.tracks[0].midiInserts);
    CHECK(loaded->tracks[1].midiInserts.empty());

    // A track saved without MIDI FX writes no field and reads back with none.
    Project plain = midiProject();
    const auto plainText = projectToJson(plain);
    CHECK(plainText.find("\"midiInserts\"") == std::string::npos);
    const auto plainLoaded = projectFromJson(plainText);
    REQUIRE(plainLoaded.has_value());
    CHECK(plainLoaded->tracks[0].midiInserts.empty());
}

TEST_CASE("Duplicating a MIDI track gives its MIDI FX ids of their own", "[core][command][midifx]")
{
    Project p = midiProject();
    UndoStack stack;
    const Id original = addMidi(p, stack, "northlight.transpose");
    auto duplicate = std::make_unique<DuplicateTrack>(1);
    const auto* ptr = duplicate.get();
    REQUIRE(stack.execute(p, std::move(duplicate)));
    const auto* copy = p.findTrack(ptr->copyId());
    REQUIRE(copy != nullptr);
    REQUIRE(copy->midiInserts.size() == 1);
    CHECK(copy->midiInserts[0].type == "northlight.transpose");
    CHECK(copy->midiInserts[0].id != original);
    CHECK(copy->midiInserts[0].id != invalidId);
}
