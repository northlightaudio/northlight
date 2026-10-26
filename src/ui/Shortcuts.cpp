#include "ui/Shortcuts.h"

#include <algorithm>
#include <array>
#include <utility>

namespace northlight::ui
{

namespace
{
using KP = juce::KeyPress;
using M = juce::ModifierKeys;

constexpr int ctrl = M::commandModifier;
constexpr int shift = M::shiftModifier;
constexpr int alt = M::altModifier;

// A key code with exact modifiers.
Key code(int keyCode, int modifiers = 0, bool listed = true)
{
    return {keyCode, modifiers, false, listed};
}
// The character typed, with exact modifiers ('R' with Shift is Shift+R).
Key typed(char ch, int modifiers = 0, bool listed = true)
{
    return {static_cast<int>(static_cast<unsigned char>(ch)), modifiers, true, listed};
}

bool isLetter(int c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
int lower(int c) noexcept
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

const std::vector<Shortcut>& table()
{
    static const std::vector<Shortcut> shortcuts = []
    {
        std::vector<Shortcut> s;
        const auto add = [&s](Command c, const char* name, std::vector<Key> keys)
        { s.push_back({c, name, std::move(keys)}); };
        // ----- the window's, in the order it tries them
        add(Command::renameTrack, "Rename the selected track", {code(KP::F2Key, 0, false)});
        add(Command::findPlugin, "Find a plugin", {code('F', ctrl, false)});
        add(Command::playStop, "Play / stop", {code(KP::spaceKey)});
        add(Command::pause, "Pause", {code(KP::spaceKey, shift)});
        add(Command::record, "Record", {typed('r')});
        add(Command::armSelected, "Arm selected track", {typed('R', shift)});
        add(Command::returnToStart, "Return to start", {code(KP::returnKey), code(KP::homeKey)});
        add(Command::goToEnd, "Go to end", {code(KP::returnKey, shift), code(KP::endKey)});
        add(Command::loop, "Loop", {typed('l')});
        add(Command::punch, "Punch", {typed('p')});
        add(Command::click, "Click", {typed('c')});
        add(Command::zoomIn, "Zoom in", {code('=', ctrl), code('+', ctrl, false)});
        add(Command::zoomOut, "Zoom out", {code('-', ctrl)});
        add(Command::zoomToFit, "Zoom to fit", {typed('z')});
        add(Command::zoomToSelection, "Zoom to selection", {typed('Z', shift)});
        add(Command::snap, "Snap", {typed('n')});
        add(Command::ripple, "Ripple", {typed('N', shift)});
        add(Command::muteTrack, "Mute selected track", {typed('M', shift)});
        add(Command::soloTrack, "Solo selected track", {typed('S', shift)});
        add(Command::newProject, "New project", {code('N', ctrl)});
        add(Command::open, "Open", {code('O', ctrl)});
        add(Command::save, "Save", {code('S', ctrl)});
        add(Command::saveAs, "Save as", {code('S', ctrl | shift)});
        add(Command::importAudio, "Import audio", {code('I', ctrl)});
        add(Command::exportMix, "Export", {code('E', ctrl)});
        add(Command::undo, "Undo", {code('Z', ctrl)});
        add(Command::redo, "Redo", {code('Y', ctrl), code('Z', ctrl | shift, false)});
        add(Command::audioSetup, "Audio Setup", {code('A', ctrl | shift)});
        add(Command::preferences, "Preferences", {code(',', ctrl)});
        add(Command::plugins, "Plugins", {code('P', ctrl | shift)});
        add(Command::addAudioTrack, "Add audio track", {code('T', ctrl)});
        add(Command::addMidiTrack, "Add MIDI track", {code('T', ctrl | shift)});
        add(Command::addBus, "Add bus", {code('B', ctrl)});
        // ----- the panels'
        add(Command::showMixer, "Mixer", {code(KP::F2Key)});
        add(Command::showEditor, "Editor", {code(KP::F3Key)});
        add(Command::showBrowser, "Browser", {code(KP::F4Key)});
        add(Command::showInspector, "Inspector", {code(KP::F5Key)});
        add(Command::showPianoRoll, "Piano Roll", {code(KP::F6Key)});
        add(Command::showDiagnostics, "Diagnostics", {code(KP::F7Key)});
        add(Command::showInstruments, "Instruments", {code(KP::F8Key)});
        add(Command::showAudioEditor, "Audio Editor", {code(KP::F9Key)});
        // ----- the arrange view's, in the order its keymap tries them
        add(Command::deleteSelection,
            "Delete the selected marker, else the selected clips",
            {code(KP::deleteKey), code(KP::backspaceKey)});
        add(Command::addMarker, "Add a marker at the playhead", {code('m')});
        add(Command::addRegion, "Add a region from the loop", {code('M', alt)});
        add(Command::previousMarker, "Jump to the previous marker", {code(',')});
        add(Command::nextMarker, "Jump to the next marker", {code('.')});
        add(Command::duplicate, "Duplicate", {code('D', ctrl)});
        add(Command::copy, "Copy", {code('C', ctrl)});
        add(Command::cut, "Cut", {code('X', ctrl)});
        add(Command::paste, "Paste at the playhead", {code('V', ctrl)});
        add(Command::pickTool,
            "Pick a tool (1 to 7)",
            {code('1'), code('2'), code('3'), code('4'), code('5'), code('6'), code('7')});
        add(Command::muteClips, "Mute or unmute the selected clips", {code('M', ctrl)});
        add(Command::cycleTake,
            "Play the next newer or older take",
            {code(KP::upKey, alt, false), code(KP::downKey, alt, false)});
        add(Command::takeLanes, "Show or hide the take lanes", {code('T', alt, false)});
        add(Command::nudgeFine,
            "Nudge by a grid step (Shift: a quarter of one)",
            {code(KP::leftKey, alt, false),
             code(KP::rightKey, alt, false),
             code(KP::leftKey, alt | shift, false),
             code(KP::rightKey, alt | shift, false)});
        add(Command::selectAll, "Select every clip", {code('A', ctrl)});
        add(Command::automationLanes,
            "Open or close the selected track's automation lanes",
            {code('a', 0, false)});
        add(Command::normalise, "Normalise", {code('N', ctrl | shift)});
        add(Command::clearSelection, "Clear the selection", {code(KP::escapeKey)});
        add(Command::splitAtPlayhead, "Split at the playhead", {code('s')});
        add(Command::splitAtSelection, "Split at the time selection", {code('S', alt)});
        add(Command::join, "Join", {code('j')});
        add(Command::crossfade, "Crossfade", {code('x')});
        add(Command::snapInArrange, "Snap on or off", {code('g', 0, false)});
        add(Command::rippleInArrange, "Ripple on or off", {code('e', 0, false)});
        add(Command::nudge,
            "Nudge the selected clips by a grid step",
            {code(KP::leftKey), code(KP::rightKey)});
        return s;
    }();
    return shortcuts;
}

// The listed keys of a command, as text.
juce::StringArray listedKeys(Command command)
{
    juce::StringArray out;
    for (const auto& key : Shortcuts::of(command).keys)
        if (key.listed)
            out.add(key.text());
    return out;
}

// How a page row joins its commands' keys.
enum class Join
{
    slash,        // the commands' keys "/"-separated; one command's alternatives with "or"
    eachKey,      // every key "/"-separated ("Left / Right")
    range,        // the first key "to" the last ("F2 to F9")
    rangeShifted, // the same with Shift ("Shift+F2 to Shift+F9")
};
struct PageRow
{
    const char* action;
    const char* group;
    std::vector<Command> commands;
    Join join = Join::slash;
};

juce::String rowKeys(const PageRow& row)
{
    juce::StringArray all;
    for (const auto c : row.commands)
        all.addArray(listedKeys(c));
    switch (row.join)
    {
        case Join::eachKey:
            return all.joinIntoString(" / ");
        case Join::range:
            return all.isEmpty() ? juce::String() : all[0] + " to " + all[all.size() - 1];
        case Join::rangeShifted:
            return all.isEmpty() ? juce::String() : "Shift+" + all[0] + " to Shift+" + all[all.size() - 1];
        case Join::slash:
            break;
    }
    juce::StringArray parts;
    for (const auto c : row.commands)
        parts.add(listedKeys(c).joinIntoString(" or "));
    return parts.joinIntoString(" / ");
}
} // namespace

bool Key::matches(const juce::KeyPress& press) const noexcept
{
    if (press.getModifiers().withoutMouseButtons().getRawFlags() != modifiers)
        return false;
    if (character)
        return press.getTextCharacter() == static_cast<juce::juce_wchar>(code);
    const int pressed = press.getKeyCode();
    return pressed == code || (isLetter(code) && isLetter(pressed) && lower(pressed) == lower(code));
}

juce::String Key::text() const
{
    juce::String out;
    if ((modifiers & ctrl) != 0)
        out << "Ctrl+";
    if ((modifiers & shift) != 0)
        out << "Shift+";
    if ((modifiers & alt) != 0)
        out << "Alt+";
    static const std::array<std::pair<int, const char*>, 11> named{{{KP::spaceKey, "Space"},
                                                                    {KP::returnKey, "Enter"},
                                                                    {KP::homeKey, "Home"},
                                                                    {KP::endKey, "End"},
                                                                    {KP::deleteKey, "Delete"},
                                                                    {KP::backspaceKey, "Backspace"},
                                                                    {KP::escapeKey, "Esc"},
                                                                    {KP::leftKey, "Left"},
                                                                    {KP::rightKey, "Right"},
                                                                    {KP::upKey, "Up"},
                                                                    {KP::downKey, "Down"}}};
    if (!character)
    {
        for (const auto& [keyCode, name] : named)
            if (keyCode == code)
                return out + name;
        static const std::array<int, 12> functionKeys{KP::F1Key,
                                                      KP::F2Key,
                                                      KP::F3Key,
                                                      KP::F4Key,
                                                      KP::F5Key,
                                                      KP::F6Key,
                                                      KP::F7Key,
                                                      KP::F8Key,
                                                      KP::F9Key,
                                                      KP::F10Key,
                                                      KP::F11Key,
                                                      KP::F12Key};
        for (size_t i = 0; i < functionKeys.size(); ++i)
            if (functionKeys[i] == code)
                return out + "F" + juce::String(static_cast<int>(i) + 1);
    }
    const auto ch = static_cast<juce::juce_wchar>(isLetter(code) ? lower(code) - 'a' + 'A' : code);
    return out + juce::String::charToString(ch);
}

const std::vector<Shortcut>& Shortcuts::all()
{
    return table();
}

const Shortcut& Shortcuts::of(Command command)
{
    for (const auto& s : table())
        if (s.command == command)
            return s;
    jassertfalse; // every command has an entry
    return table().front();
}

int Shortcuts::matchIndex(Command command, const juce::KeyPress& press)
{
    const auto& keys = of(command).keys;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i].matches(press))
            return static_cast<int>(i);
    return -1;
}

bool Shortcuts::matches(Command command, const juce::KeyPress& press)
{
    return matchIndex(command, press) >= 0;
}

juce::String Shortcuts::keyText(Command command)
{
    for (const auto& key : of(command).keys)
        if (key.listed)
            return key.text();
    return {};
}

juce::String Shortcuts::withKey(const juce::String& text, Command command)
{
    const auto key = keyText(command);
    return key.isEmpty() ? text : text + "  " + key;
}

std::vector<Command> Shortcuts::windowCommandsFor(const juce::KeyPress& press)
{
    std::vector<Command> out;
    for (const auto& s : table())
    {
        if (s.command == Command::showMixer)
            break; // the rest are the panels' and the arrange view's
        if (std::any_of(s.keys.begin(), s.keys.end(), [&](const Key& k) { return k.matches(press); }))
            out.push_back(s.command);
    }
    return out;
}

const std::vector<Shortcuts::Row>& Shortcuts::pageRows()
{
    static const std::vector<Row> rows = []
    {
        using C = Command;
        const std::vector<PageRow> page{
            {"Play / stop", "Transport", {C::playStop}},
            {"Pause", "Transport", {C::pause}},
            {"Record", "Transport", {C::record}},
            {"Arm selected track", "Transport", {C::armSelected}},
            {"Return to start", "Transport", {C::returnToStart}},
            {"Go to end", "Transport", {C::goToEnd}},
            {"Loop", "Transport", {C::loop}},
            {"Punch", "Transport", {C::punch}},
            {"Click", "Transport", {C::click}},
            {"Zoom in / out", "View", {C::zoomIn, C::zoomOut}},
            {"Zoom to fit / to selection", "View", {C::zoomToFit, C::zoomToSelection}},
            {"Snap / ripple", "View", {C::snap, C::ripple}},
            {"Mixer, Editor, Browser, Inspector, Piano Roll, Diagnostics",
             "View",
             {C::showMixer,
              C::showEditor,
              C::showBrowser,
              C::showInspector,
              C::showPianoRoll,
              C::showDiagnostics},
             Join::range},
            {"Instruments / Audio Editor", "View", {C::showInstruments, C::showAudioEditor}},
            {"Detach or reattach a panel", "View", {C::showMixer, C::showAudioEditor}, Join::rangeShifted},
            {"Mute / solo selected track", "Tracks", {C::muteTrack, C::soloTrack}},
            {"Add audio / MIDI track", "Tracks", {C::addAudioTrack, C::addMidiTrack}},
            {"Add bus", "Tracks", {C::addBus}},
            {"Delete selection", "Editing", {C::deleteSelection}},
            {"Duplicate", "Editing", {C::duplicate}},
            {"Split at playhead / at selection", "Editing", {C::splitAtPlayhead, C::splitAtSelection}},
            {"Join", "Editing", {C::join}},
            {"Crossfade", "Editing", {C::crossfade}},
            {"Mute / unmute selected clips", "Editing", {C::muteClips}},
            {"Select, range, pencil, erase, split, glue, mute tool", "Editing", {C::pickTool}, Join::range},
            {"Normalise", "Editing", {C::normalise}},
            {"Nudge left / right", "Editing", {C::nudge}, Join::eachKey},
            {"Cut / copy / paste", "Editing", {C::cut, C::copy, C::paste}},
            {"Select all", "Editing", {C::selectAll}},
            {"Clear selection", "Editing", {C::clearSelection}},
            {"Add marker / region", "Editing", {C::addMarker, C::addRegion}},
            {"Previous / next marker", "Editing", {C::previousMarker, C::nextMarker}},
            {"New project", "Files", {C::newProject}},
            {"Open", "Files", {C::open}},
            {"Save / save as", "Files", {C::save, C::saveAs}},
            {"Import audio", "Files", {C::importAudio}},
            {"Export", "Files", {C::exportMix}},
            {"Undo / redo", "Files", {C::undo, C::redo}},
            {"Audio Setup", "App", {C::audioSetup}},
            {"Plugins", "App", {C::plugins}},
            {"Preferences", "App", {C::preferences}},
        };
        std::vector<Row> out;
        for (const auto& row : page)
            out.push_back({row.action, rowKeys(row), row.group});
        return out;
    }();
    return rows;
}

} // namespace northlight::ui
