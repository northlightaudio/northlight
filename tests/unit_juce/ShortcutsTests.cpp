#include "ui/Shortcuts.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace northlight;
using ui::Command;
using ui::Context;
using ui::Shortcuts;

namespace
{
using Key = juce::KeyPress;
using Mods = juce::ModifierKeys;

// Puts the defaults back however a test leaves the bindings.
struct Defaults
{
    Defaults() { Shortcuts::resetAll(); }
    ~Defaults() { Shortcuts::resetAll(); }
};
} // namespace

TEST_CASE("Every command has one entry, in the order of the enum, with a unique id", "[ui][shortcuts]")
{
    const auto& all = Shortcuts::all();
    std::set<juce::String> ids;
    for (size_t i = 0; i < all.size(); ++i)
    {
        INFO(all[i].id);
        CHECK(static_cast<size_t>(all[i].command) == i);
        CHECK(ids.insert(all[i].id).second);
        CHECK(Shortcuts::find(all[i].id) == &all[i]);
        CHECK(juce::String(all[i].name).isNotEmpty());
        const auto& sections = Shortcuts::sections();
        CHECK(std::any_of(sections.begin(),
                          sections.end(),
                          [&](const char* section) { return juce::String(section) == all[i].section; }));
    }
    CHECK(all.back().command == Command::audioEditorClearSelection);
}

TEST_CASE("No two commands in a context share a default key", "[ui][shortcuts]")
{
    const Defaults defaults;
    const auto& all = Shortcuts::all();
    for (size_t a = 0; a < all.size(); ++a)
        for (size_t b = a + 1; b < all.size(); ++b)
        {
            if (all[a].context != all[b].context)
                continue;
            for (const auto& x : all[a].defaults)
                for (const auto& y : all[b].defaults)
                {
                    INFO(all[a].id << " and " << all[b].id << ": " << x.text());
                    CHECK_FALSE(x.sameAs(y));
                }
        }
    // Across contexts, the one deliberate overlap: F2 renames the selected track in
    // the arrange view and shows the mixer elsewhere (interaction.md 10.4).
    for (const auto& s : Shortcuts::all())
        for (const auto& key : s.defaults)
        {
            INFO(s.id << ": " << key.text());
            const auto others = Shortcuts::conflicts(s.command, key);
            if (s.command == Command::showMixer)
                CHECK(others == std::vector<Command>{Command::renameTrack});
            else if (s.command == Command::renameTrack)
                CHECK(others == std::vector<Command>{Command::showMixer});
            else
                CHECK(others.empty());
        }
}

TEST_CASE("Every global key posted to the window's handler runs the command bound to it", "[ui][shortcuts]")
{
    const Defaults defaults;
    std::vector<Command> ran;
    const auto post = [&](const juce::KeyPress& press)
    {
        ran.clear();
        // The window's handler offers the press to each command bound to it; this
        // perform takes the first, as MainWindow's does for every global command.
        return Shortcuts::dispatch(press,
                                   [&](Command c)
                                   {
                                       ran.push_back(c);
                                       return true;
                                   });
    };
    const auto everyKeyRunsItsCommand = [&]
    {
        for (const auto& s : Shortcuts::all())
        {
            if (s.context != Context::global)
                continue;
            for (const auto& key : s.keys)
            {
                INFO(s.id << ": " << key.text());
                CHECK(post(key.keyPress()));
                CHECK(ran == std::vector<Command>{s.command});
            }
        }
    };
    everyKeyRunsItsCommand();
    CHECK_FALSE(post(Key('q', Mods(), 'q')));
    CHECK(post(Key('Z', Mods::commandModifier | Mods::shiftModifier, 0)));
    CHECK(ran == std::vector<Command>{Command::redo});
    CHECK(post(Key('R', Mods::shiftModifier, 'R')));
    CHECK(ran == std::vector<Command>{Command::armSelected});
    CHECK(post(Key(Key::numberPad0)));
    CHECK(ran == std::vector<Command>{Command::playStop});
    CHECK(post(Key(Key::spaceKey, Mods::shiftModifier, ' ')));
    CHECK(ran == std::vector<Command>{Command::pause});

    // After a rebinding the new key runs the command and the old one nothing.
    Shortcuts::bind(Command::loop, ui::Key{'L', Mods::commandModifier | Mods::altModifier});
    everyKeyRunsItsCommand();
    CHECK_FALSE(post(Key('l', Mods(), 'l')));
    CHECK(post(Key('L', Mods::commandModifier | Mods::altModifier, 0)));
    CHECK(ran == std::vector<Command>{Command::loop});
}

TEST_CASE("Each view's keys match the commands bound to them", "[ui][shortcuts]")
{
    const Defaults defaults;
    for (const auto& s : Shortcuts::all())
        for (const auto& key : s.keys)
        {
            INFO(s.id << ": " << key.text());
            const auto commands = Shortcuts::commandsFor(key.keyPress(), s.context);
            REQUIRE_FALSE(commands.empty());
            CHECK(commands.front() == s.command);
        }
    CHECK(Shortcuts::matchIndex(Command::pickTool, Key('5')) == 4);
    CHECK(Shortcuts::matchIndex(Command::audioEditorTool, Key('2')) == 1);
    CHECK(Shortcuts::matches(Command::pianoRollTransposeUp, Key('=', Mods::shiftModifier, '+')));
    // Ctrl+= is the window's zoom, not a transpose.
    CHECK_FALSE(Shortcuts::matches(Command::pianoRollTransposeUp, Key('=', Mods::commandModifier, 0)));
}

TEST_CASE("Tooltips and menus print a command's first listed key", "[ui][shortcuts]")
{
    const Defaults defaults;
    CHECK(Shortcuts::withKey("Undo", Command::undo) == "Undo  Ctrl+Z");
    CHECK(Shortcuts::withKey("Redo", Command::redo) == "Redo  Ctrl+Y");
    CHECK(Shortcuts::keyText(Command::normalise) == "Ctrl+Shift+N");
    CHECK(Shortcuts::keyText(Command::showMixer) == "F2");
    CHECK(Shortcuts::keyText(Command::returnToStart) == "Enter");
    CHECK(ui::Key{juce::KeyPress::numberPad1}.text() == "Num1");
    Shortcuts::bind(Command::undo, ui::Key{'U', Mods::commandModifier});
    CHECK(Shortcuts::withKey("Undo", Command::undo) == "Undo  Ctrl+U");
}

TEST_CASE("A rebinding finds the actions it collides with, and Replace takes the key", "[ui][shortcuts]")
{
    const Defaults defaults;
    const ui::Key ctrlM{'M', Mods::commandModifier};
    // Ctrl+M mutes clips in the arrange view and notes in the piano roll: a global
    // command taking it collides with both.
    CHECK(Shortcuts::conflicts(Command::loop, ctrlM) ==
          std::vector<Command>{Command::muteClips, Command::pianoRollMuteNotes});
    // The piano roll and the arrange view never see the same press: Ctrl+Shift+N
    // normalises in the arrange view only.
    const ui::Key ctrlShiftN{'N', Mods::commandModifier | Mods::shiftModifier};
    CHECK(Shortcuts::conflicts(Command::pianoRollSnap, ctrlShiftN).empty());
    Shortcuts::bind(Command::loop, ctrlM, Shortcuts::conflicts(Command::loop, ctrlM));
    CHECK(Shortcuts::keyText(Command::loop) == "Ctrl+M");
    CHECK(Shortcuts::of(Command::muteClips).keys.empty());
    CHECK(Shortcuts::keyText(Command::muteClips).isEmpty());
    CHECK_FALSE(Shortcuts::isDefault(Command::muteClips));
    Shortcuts::reset(Command::muteClips);
    CHECK(Shortcuts::isDefault(Command::muteClips));
    // A fixed family keeps its keys.
    Shortcuts::bind(Command::pickTool, ui::Key{'Q'});
    CHECK(Shortcuts::isDefault(Command::pickTool));
}

TEST_CASE("Rebound keys save as settings text and load back; the defaults save as nothing", "[ui][shortcuts]")
{
    const Defaults defaults;
    CHECK(Shortcuts::save().empty());
    CHECK(Shortcuts::allDefault());
    Shortcuts::bind(Command::loop, ui::Key{'L', Mods::commandModifier});
    Shortcuts::bind(Command::muteClips, ui::Key{'L', Mods::commandModifier}, {Command::loop}); // loop: none
    const auto text = Shortcuts::save();
    CHECK(text.find("loop=") != std::string::npos);
    CHECK(text.find("muteClips=") != std::string::npos);
    Shortcuts::resetAll();
    CHECK(Shortcuts::allDefault());
    Shortcuts::load(text + " nosuchCommand=2.65 zoomIn=junk pickTool=0.81");
    CHECK(Shortcuts::of(Command::loop).keys.empty());
    CHECK(Shortcuts::keyText(Command::muteClips) == "Ctrl+L");
    CHECK(Shortcuts::isDefault(Command::zoomIn));   // a bad key is skipped
    CHECK(Shortcuts::isDefault(Command::pickTool)); // fixed families do not load
    CHECK(Shortcuts::save() == text);
    Shortcuts::load({});
    CHECK(Shortcuts::allDefault());
}

TEST_CASE("Tagged tooltips print the current key after a rebinding", "[ui][shortcuts]")
{
    const Defaults defaults;
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component root;
    juce::TextButton mute;
    root.addAndMakeVisible(mute);
    Shortcuts::tag(mute, "Mute", Command::muteTrack);
    CHECK(mute.getTooltip() == "Mute  Shift+M");
    Shortcuts::bind(Command::muteTrack, ui::Key{'M', Mods::altModifier | Mods::shiftModifier});
    Shortcuts::retag(root);
    CHECK(mute.getTooltip() == "Mute  Shift+Alt+M");
}
