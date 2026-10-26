#include "ui/Shortcuts.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace northlight;
using ui::Command;
using ui::Shortcuts;

namespace
{
using Key = juce::KeyPress;
using Mods = juce::ModifierKeys;

juce::String rowKeys(const juce::String& action)
{
    for (const auto& row : Shortcuts::pageRows())
        if (row.action == action)
            return row.keys;
    return "(no row)";
}
} // namespace

TEST_CASE("The Shortcuts page lists the keys the table dispatches", "[ui][shortcuts]")
{
    CHECK(rowKeys("Play / stop") == "Space");
    CHECK(rowKeys("Return to start") == "Enter or Home");
    CHECK(rowKeys("Zoom in / out") == "Ctrl+= / Ctrl+-");
    CHECK(rowKeys("Undo / redo") == "Ctrl+Z / Ctrl+Y");
    CHECK(rowKeys("Add marker / region") == "M / Alt+M");
    CHECK(rowKeys("Nudge left / right") == "Left / Right");
    CHECK(rowKeys("Select, range, pencil, erase, split, glue, mute tool") == "1 to 7");
    CHECK(rowKeys("Detach or reattach a panel") == "Shift+F2 to Shift+F9");
    CHECK(rowKeys("Preferences") == "Ctrl+,");
}

TEST_CASE("Every listed key is on the Shortcuts page", "[ui][shortcuts]")
{
    juce::String page;
    for (const auto& row : Shortcuts::pageRows())
        page << " " << row.keys << " ";
    for (const auto& s : Shortcuts::all())
        for (const auto& key : s.keys)
            if (key.listed && s.command != Command::pickTool && // ranges: "1 to 7", "F2 to F9"
                (s.command < Command::showMixer || s.command > Command::showAudioEditor))
            {
                INFO(s.name << ": " << key.text());
                CHECK(page.contains(" " + key.text() + " "));
            }
}

TEST_CASE("A key press finds the window command it is bound to", "[ui][shortcuts]")
{
    const auto commands = [](const Key& k) { return Shortcuts::windowCommandsFor(k); };
    CHECK(commands(Key(Key::spaceKey)) == std::vector<Command>{Command::playStop});
    CHECK(commands(Key('Z', Mods::commandModifier | Mods::shiftModifier, 0)) ==
          std::vector<Command>{Command::redo});
    CHECK(commands(Key('y', Mods::commandModifier, 0)) == std::vector<Command>{Command::redo});
    CHECK(commands(Key('R', Mods::shiftModifier, 'R')) == std::vector<Command>{Command::armSelected});
    CHECK(commands(Key('r', Mods(), 'r')) == std::vector<Command>{Command::record});
    // F2: renaming first (when the arrange view has the focus), then the mixer's panel key.
    CHECK(commands(Key(Key::F2Key)) == std::vector<Command>{Command::renameTrack});
    CHECK(commands(Key('q', Mods(), 'q')).empty());
}

TEST_CASE("Tooltips and menus print a command's first listed key", "[ui][shortcuts]")
{
    CHECK(Shortcuts::withKey("Undo", Command::undo) == "Undo  Ctrl+Z");
    CHECK(Shortcuts::withKey("Redo", Command::redo) == "Redo  Ctrl+Y");
    CHECK(Shortcuts::keyText(Command::normalise) == "Ctrl+Shift+N");
    CHECK(Shortcuts::keyText(Command::showMixer) == "F2");
    CHECK(Shortcuts::withKey("Find", Command::findPlugin) == "Find"); // none listed
    CHECK(Shortcuts::matchIndex(Command::pickTool, Key('5')) == 4);
}

TEST_CASE("No two window commands share a key", "[ui][shortcuts]")
{
    std::set<std::pair<int, int>> seen;
    for (const auto& s : Shortcuts::all())
    {
        if (s.command == Command::showMixer)
            break;
        for (const auto& key : s.keys)
        {
            INFO(s.name << ": " << key.text());
            CHECK(seen.insert({key.character ? -key.code : key.code, key.modifiers}).second);
        }
    }
}
