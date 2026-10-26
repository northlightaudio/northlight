#include "ui/Shortcuts.h"

#include <algorithm>
#include <array>
#include <sstream>
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
constexpr int keyModifiers = ctrl | shift | alt;

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
int upper(int c) noexcept
{
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

constexpr const char* transport = "Transport";
constexpr const char* view = "View";
constexpr const char* tracks = "Tracks";
constexpr const char* editing = "Editing";
constexpr const char* files = "Files";
constexpr const char* app = "App";
constexpr const char* pianoRoll = "Piano Roll";
constexpr const char* audioEditor = "Audio Editor";

std::vector<Shortcut>& table()
{
    static std::vector<Shortcut> shortcuts = []
    {
        std::vector<Shortcut> s;
        Context context = Context::global;
        const auto add =
            [&s, &context](
                Command c, const char* id, const char* name, const char* section, std::vector<Key> keys)
        { s.push_back({c, id, name, section, context, keys, false, keys}); };
        const auto family =
            [&s, &context](
                Command c, const char* id, const char* name, const char* section, std::vector<Key> keys)
        { s.push_back({c, id, name, section, context, keys, true, keys}); };
        // ----- the window's, in the order it tries them
        add(Command::findPlugin, "findPlugin", "Search the browser", view, {code('F', ctrl)});
        add(Command::playStop,
            "playStop",
            "Play / stop",
            transport,
            {code(KP::spaceKey), code(KP::numberPad0)});
        add(Command::pause, "pause", "Pause", transport, {code(KP::spaceKey, shift)});
        add(Command::record, "record", "Record", transport, {typed('r'), code(KP::numberPad3)});
        add(Command::armSelected, "armSelected", "Arm the selected track", tracks, {typed('R', shift)});
        add(Command::returnToStart,
            "returnToStart",
            "Go to start",
            transport,
            {code(KP::returnKey), code(KP::homeKey), code(KP::numberPad1)});
        add(Command::goToEnd,
            "goToEnd",
            "Go to end",
            transport,
            {code(KP::returnKey, shift), code(KP::endKey)});
        add(Command::loop, "loop", "Loop on or off", transport, {typed('l')});
        add(Command::punch, "punch", "Punch on or off", transport, {typed('p')});
        add(Command::click, "click", "Click on or off", transport, {typed('c')});
        add(Command::zoomIn, "zoomIn", "Zoom in", view, {code('=', ctrl), code('+', ctrl, false)});
        add(Command::zoomOut, "zoomOut", "Zoom out", view, {code('-', ctrl)});
        add(Command::zoomToFit, "zoomToFit", "Zoom to fit", view, {typed('z')});
        add(Command::zoomToSelection, "zoomToSelection", "Zoom to the selection", view, {typed('Z', shift)});
        add(Command::snap, "snap", "Snap on or off", editing, {typed('n')});
        add(Command::ripple, "ripple", "Ripple on or off", editing, {typed('N', shift)});
        add(Command::muteTrack, "muteTrack", "Mute the selected track", tracks, {typed('M', shift)});
        add(Command::soloTrack, "soloTrack", "Solo the selected track", tracks, {typed('S', shift)});
        add(Command::newProject, "newProject", "New project", files, {code('N', ctrl)});
        add(Command::open, "open", "Open", files, {code('O', ctrl)});
        add(Command::save, "save", "Save", files, {code('S', ctrl)});
        add(Command::saveAs, "saveAs", "Save as", files, {code('S', ctrl | shift)});
        add(Command::importAudio, "importAudio", "Import audio", files, {code('I', ctrl)});
        add(Command::exportMix, "exportMix", "Export", files, {code('E', ctrl)});
        add(Command::undo, "undo", "Undo", editing, {code('Z', ctrl)});
        add(Command::redo, "redo", "Redo", editing, {code('Y', ctrl), code('Z', ctrl | shift)});
        add(Command::audioSetup, "audioSetup", "Audio Setup", app, {code('A', ctrl | shift)});
        add(Command::preferences, "preferences", "Preferences", app, {code(',', ctrl)});
        add(Command::plugins, "plugins", "Plugins", app, {code('P', ctrl | shift)});
        add(Command::addAudioTrack, "addAudioTrack", "Add an audio track", tracks, {code('T', ctrl)});
        add(Command::addMidiTrack, "addMidiTrack", "Add a MIDI track", tracks, {code('T', ctrl | shift)});
        add(Command::addBus, "addBus", "Add a bus", tracks, {code('B', ctrl)});
        // ----- the panels'
        add(Command::showMixer, "showMixer", "Mixer", view, {code(KP::F2Key)});
        add(Command::showEditor, "showEditor", "Editor", view, {code(KP::F3Key)});
        add(Command::showBrowser, "showBrowser", "Browser", view, {code(KP::F4Key)});
        add(Command::showInspector, "showInspector", "Inspector", view, {code(KP::F5Key)});
        add(Command::showPianoRoll, "showPianoRoll", "Piano Roll", view, {code(KP::F6Key)});
        add(Command::showDiagnostics, "showDiagnostics", "Diagnostics", view, {code(KP::F7Key)});
        add(Command::showInstruments, "showInstruments", "Instruments", view, {code(KP::F8Key)});
        add(Command::showAudioEditor, "showAudioEditor", "Audio Editor", view, {code(KP::F9Key)});
        // ----- the arrange view's, in the order its keymap tries them
        context = Context::arrange;
        add(Command::renameTrack, "renameTrack", "Rename the selected track", tracks, {code(KP::F2Key)});
        add(Command::deleteSelection,
            "deleteSelection",
            "Delete the selected marker, else the selected clips",
            editing,
            {code(KP::deleteKey), code(KP::backspaceKey)});
        add(Command::addMarker, "addMarker", "Add a marker at the playhead", editing, {code('m')});
        add(Command::addRegion, "addRegion", "Add a region from the loop", editing, {code('M', alt)});
        add(Command::previousMarker, "previousMarker", "Jump to the previous marker", transport, {code(',')});
        add(Command::nextMarker, "nextMarker", "Jump to the next marker", transport, {code('.')});
        add(Command::duplicate, "duplicate", "Duplicate", editing, {code('D', ctrl)});
        add(Command::copy, "copy", "Copy", editing, {code('C', ctrl)});
        add(Command::cut, "cut", "Cut", editing, {code('X', ctrl)});
        add(Command::paste, "paste", "Paste at the playhead", editing, {code('V', ctrl)});
        family(Command::pickTool,
               "pickTool",
               "Tools: select, range, pencil, erase, split, glue, mute",
               editing,
               {code('1'), code('2'), code('3'), code('4'), code('5'), code('6'), code('7')});
        add(Command::muteClips, "muteClips", "Mute or unmute the selected clips", editing, {code('M', ctrl)});
        family(Command::cycleTake,
               "cycleTake",
               "Play the newer or older take",
               editing,
               {code(KP::upKey, alt), code(KP::downKey, alt)});
        add(Command::takeLanes, "takeLanes", "Show or hide the take lanes", editing, {code('T', alt)});
        family(Command::nudgeFine,
               "nudgeFine",
               "Nudge by a grid step (Shift: a quarter of one)",
               editing,
               {code(KP::leftKey, alt),
                code(KP::rightKey, alt),
                code(KP::leftKey, alt | shift, false),
                code(KP::rightKey, alt | shift, false)});
        add(Command::selectAll, "selectAll", "Select every clip", editing, {code('A', ctrl)});
        add(Command::automationLanes,
            "automationLanes",
            "Open or close the selected track's automation lanes",
            tracks,
            {code('a')});
        add(Command::normalise, "normalise", "Normalise", editing, {code('N', ctrl | shift)});
        add(Command::clearSelection, "clearSelection", "Clear the selection", editing, {code(KP::escapeKey)});
        add(Command::splitAtPlayhead, "splitAtPlayhead", "Split at the playhead", editing, {code('s')});
        add(Command::splitAtSelection,
            "splitAtSelection",
            "Split at the time selection",
            editing,
            {code('S', alt)});
        add(Command::join, "join", "Join", editing, {code('j')});
        add(Command::crossfade, "crossfade", "Crossfade", editing, {code('x')});
        family(Command::nudge,
               "nudge",
               "Nudge the selected clips by a grid step",
               editing,
               {code(KP::leftKey), code(KP::rightKey)});
        // ----- the piano roll's
        context = Context::pianoRoll;
        add(Command::pianoRollDelete,
            "pianoRollDelete",
            "Delete the selected notes",
            pianoRoll,
            {code(KP::deleteKey), code(KP::backspaceKey)});
        add(Command::pianoRollSelectTool, "pianoRollSelectTool", "Select tool", pianoRoll, {code('1')});
        add(Command::pianoRollDrawTool, "pianoRollDrawTool", "Draw tool", pianoRoll, {code('3')});
        add(Command::pianoRollEraseTool, "pianoRollEraseTool", "Erase tool", pianoRoll, {code('4')});
        add(Command::pianoRollSelectAll,
            "pianoRollSelectAll",
            "Select every note",
            pianoRoll,
            {code('A', ctrl)});
        add(Command::pianoRollDuplicate,
            "pianoRollDuplicate",
            "Duplicate notes",
            pianoRoll,
            {code('D', ctrl)});
        add(Command::pianoRollClearSelection,
            "pianoRollClearSelection",
            "Clear the note selection",
            pianoRoll,
            {code(KP::escapeKey)});
        add(Command::pianoRollSnap, "pianoRollSnap", "Snap notes on or off", pianoRoll, {code('g')});
        add(Command::pianoRollQuantise, "pianoRollQuantise", "Quantise note starts", pianoRoll, {code('q')});
        add(Command::pianoRollQuantiseEnds,
            "pianoRollQuantiseEnds",
            "Quantise note starts and ends",
            pianoRoll,
            {code('Q', shift)});
        add(Command::pianoRollFinerGrid, "pianoRollFinerGrid", "Finer grid", pianoRoll, {code(']')});
        add(Command::pianoRollCoarserGrid, "pianoRollCoarserGrid", "Coarser grid", pianoRoll, {code('[')});
        add(Command::pianoRollTransposeUp,
            "pianoRollTransposeUp",
            "Transpose up a semitone",
            pianoRoll,
            {code('='), typed('+', 0, false), typed('+', shift, false), code(KP::numberPadAdd, 0, false)});
        add(Command::pianoRollTransposeDown,
            "pianoRollTransposeDown",
            "Transpose down a semitone",
            pianoRoll,
            {code('-'), code(KP::numberPadSubtract, 0, false)});
        family(Command::pianoRollNudge,
               "pianoRollNudge",
               "Nudge notes by a grid step (Shift: fine)",
               pianoRoll,
               {code(KP::leftKey),
                code(KP::rightKey),
                code(KP::leftKey, shift, false),
                code(KP::rightKey, shift, false)});
        family(Command::pianoRollNudgePitch,
               "pianoRollNudgePitch",
               "Move notes up or down a semitone (Ctrl: an octave)",
               pianoRoll,
               {code(KP::upKey),
                code(KP::downKey),
                code(KP::upKey, ctrl, false),
                code(KP::downKey, ctrl, false)});
        // ----- the audio editor's (pitch mode)
        context = Context::audioEditor;
        family(Command::audioEditorTool,
               "audioEditorTool",
               "Tools: pitch, split, drift, vibrato, breath",
               audioEditor,
               {code('1'), code('2'), code('3'), code('4'), code('5')});
        family(Command::audioEditorNudgePitch,
               "audioEditorNudgePitch",
               "Move notes up or down a semitone (Shift: 10 cents)",
               audioEditor,
               {code(KP::upKey),
                code(KP::downKey),
                code(KP::upKey, shift, false),
                code(KP::downKey, shift, false)});
        add(Command::audioEditorMute,
            "audioEditorMute",
            "Mute or unmute the selected notes",
            audioEditor,
            {code('m')});
        add(Command::audioEditorSelectAll,
            "audioEditorSelectAll",
            "Select every note",
            audioEditor,
            {code('A', ctrl)});
        add(Command::audioEditorClearSelection,
            "audioEditorClearSelection",
            "Clear the note selection",
            audioEditor,
            {code(KP::escapeKey)});
        return s;
    }();
    return shortcuts;
}

Shortcut& entry(Command command)
{
    auto& t = table();
    const auto index = static_cast<size_t>(command);
    if (index < t.size() && t[index].command == command)
        return t[index];
    for (auto& s : t)
        if (s.command == command)
            return s;
    jassertfalse; // every command has an entry
    return t.front();
}

int changes = 0;

bool sameKeys(const std::vector<Key>& a, const std::vector<Key>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!a[i].sameAs(b[i]) || a[i].listed != b[i].listed)
            return false;
    return true;
}
} // namespace

// ----- Key -------------------------------------------------------------------------------------

bool Key::matches(const juce::KeyPress& press) const noexcept
{
    if ((press.getModifiers().withoutMouseButtons().getRawFlags() & keyModifiers) != modifiers)
        return false;
    if (character)
        return press.getTextCharacter() == static_cast<juce::juce_wchar>(code);
    const int pressed = press.getKeyCode();
    return pressed == code || (isLetter(code) && isLetter(pressed) && lower(pressed) == lower(code));
}

bool Key::sameAs(const Key& other) const noexcept
{
    return modifiers == other.modifiers && upper(code) == upper(other.code);
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
    static const std::array<std::pair<int, const char*>, 16> named{{{KP::spaceKey, "Space"},
                                                                    {KP::returnKey, "Enter"},
                                                                    {KP::homeKey, "Home"},
                                                                    {KP::endKey, "End"},
                                                                    {KP::deleteKey, "Delete"},
                                                                    {KP::backspaceKey, "Backspace"},
                                                                    {KP::escapeKey, "Esc"},
                                                                    {KP::leftKey, "Left"},
                                                                    {KP::rightKey, "Right"},
                                                                    {KP::upKey, "Up"},
                                                                    {KP::downKey, "Down"},
                                                                    {KP::pageUpKey, "PgUp"},
                                                                    {KP::pageDownKey, "PgDn"},
                                                                    {KP::insertKey, "Insert"},
                                                                    {KP::tabKey, "Tab"},
                                                                    {KP::numberPadDecimalPoint, "Num."}}};
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
        static const std::array<int, 10> numberPad{KP::numberPad0,
                                                   KP::numberPad1,
                                                   KP::numberPad2,
                                                   KP::numberPad3,
                                                   KP::numberPad4,
                                                   KP::numberPad5,
                                                   KP::numberPad6,
                                                   KP::numberPad7,
                                                   KP::numberPad8,
                                                   KP::numberPad9};
        for (size_t i = 0; i < numberPad.size(); ++i)
            if (numberPad[i] == code)
                return out + "Num" + juce::String(static_cast<int>(i));
    }
    const auto ch = static_cast<juce::juce_wchar>(isLetter(code) ? upper(code) : code);
    return out + juce::String::charToString(ch);
}

juce::KeyPress Key::keyPress() const
{
    // What a keyboard sends: the key code (a letter in upper case) and, for a
    // printable key without Ctrl or Alt, the character it types.
    const bool printable = code >= ' ' && code < 127 && (modifiers & (ctrl | alt)) == 0;
    juce::juce_wchar text = 0;
    if (character || printable)
        text = static_cast<juce::juce_wchar>(
            isLetter(code) ? ((modifiers & shift) != 0 ? upper(code) : lower(code)) : code);
    return juce::KeyPress(isLetter(code) ? upper(code) : code, juce::ModifierKeys(modifiers), text);
}

Key Key::fromPress(const juce::KeyPress& press)
{
    const int pressed = press.getKeyCode();
    return {isLetter(pressed) ? upper(pressed) : pressed,
            press.getModifiers().withoutMouseButtons().getRawFlags() & keyModifiers,
            false,
            true};
}

bool Key::bindable(const juce::KeyPress& press)
{
    const int c = press.getKeyCode();
    return c != 0 && c != KP::tabKey && c != KP::escapeKey;
}

// ----- the registry ----------------------------------------------------------------------------

const std::vector<Shortcut>& Shortcuts::all()
{
    return table();
}

const Shortcut& Shortcuts::of(Command command)
{
    return entry(command);
}

const Shortcut* Shortcuts::find(const juce::String& id)
{
    for (const auto& s : table())
        if (id == s.id)
            return &s;
    return nullptr;
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

namespace
{
const juce::Identifier tagCommand{"northlightShortcut"};
const juce::Identifier tagText{"northlightShortcutText"};
} // namespace

void Shortcuts::tag(juce::Component& control, const juce::String& text, Command command)
{
    control.getProperties().set(tagCommand, static_cast<int>(command));
    control.getProperties().set(tagText, text);
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&control))
        client->setTooltip(withKey(text, command));
}

void Shortcuts::retag(juce::Component& root)
{
    const auto& properties = root.getProperties();
    if (properties.contains(tagCommand))
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&root))
            client->setTooltip(withKey(properties[tagText].toString(),
                                       static_cast<Command>(static_cast<int>(properties[tagCommand]))));
    for (auto* child : root.getChildren())
        retag(*child);
}

std::vector<Command> Shortcuts::commandsFor(const juce::KeyPress& press, Context context)
{
    std::vector<Command> out;
    for (const auto& s : table())
        if (s.context == context &&
            std::any_of(s.keys.begin(), s.keys.end(), [&](const Key& k) { return k.matches(press); }))
            out.push_back(s.command);
    return out;
}

std::vector<Command> Shortcuts::windowCommandsFor(const juce::KeyPress& press)
{
    return commandsFor(press, Context::global);
}

bool Shortcuts::overlaps(Context a, Context b) noexcept
{
    return a == b || a == Context::global || b == Context::global;
}

std::vector<Command> Shortcuts::conflicts(Command command, const Key& key)
{
    std::vector<Command> out;
    const auto context = of(command).context;
    for (const auto& s : table())
        if (s.command != command && overlaps(s.context, context) &&
            std::any_of(s.keys.begin(), s.keys.end(), [&](const Key& k) { return k.sameAs(key); }))
            out.push_back(s.command);
    return out;
}

void Shortcuts::bind(Command command, const Key& key, const std::vector<Command>& takeFrom)
{
    auto& target = entry(command);
    if (target.fixed)
        return;
    for (const auto other : takeFrom)
    {
        auto& o = entry(other);
        if (o.fixed || other == command)
            continue;
        std::erase_if(o.keys, [&](const Key& k) { return k.sameAs(key); });
    }
    auto bound = key;
    bound.character = false;
    bound.listed = true;
    target.keys = {bound};
    ++changes;
}

void Shortcuts::reset(Command command)
{
    auto& s = entry(command);
    s.keys = s.defaults;
    ++changes;
}

void Shortcuts::resetAll()
{
    for (auto& s : table())
        s.keys = s.defaults;
    ++changes;
}

bool Shortcuts::isDefault(Command command)
{
    const auto& s = of(command);
    return sameKeys(s.keys, s.defaults);
}

bool Shortcuts::allDefault()
{
    return std::all_of(
        table().begin(), table().end(), [](const Shortcut& s) { return sameKeys(s.keys, s.defaults); });
}

std::string Shortcuts::save()
{
    std::string out;
    for (const auto& s : table())
    {
        if (s.fixed || sameKeys(s.keys, s.defaults))
            continue;
        if (!out.empty())
            out += ' ';
        out += s.id;
        out += '=';
        for (size_t i = 0; i < s.keys.size(); ++i)
        {
            if (i > 0)
                out += ',';
            out += std::to_string(s.keys[i].modifiers) + "." + std::to_string(s.keys[i].code);
        }
    }
    return out;
}

void Shortcuts::load(const std::string& text)
{
    for (auto& s : table())
        s.keys = s.defaults;
    std::istringstream entries(text);
    std::string item;
    while (entries >> item)
    {
        const auto equals = item.find('=');
        if (equals == std::string::npos)
            continue;
        const auto* found = find(juce::String(item.substr(0, equals)));
        if (found == nullptr || found->fixed)
            continue;
        std::vector<Key> keys;
        bool valid = true;
        std::istringstream list(item.substr(equals + 1));
        std::string one;
        while (std::getline(list, one, ','))
        {
            const auto dot = one.find('.');
            if (dot == std::string::npos)
            {
                valid = false;
                break;
            }
            const int modifiers = std::atoi(one.substr(0, dot).c_str());
            const int keyCode = std::atoi(one.substr(dot + 1).c_str());
            if (keyCode <= 0 || (modifiers & ~keyModifiers) != 0)
            {
                valid = false;
                break;
            }
            keys.push_back({keyCode, modifiers, false, true});
        }
        if (valid)
            entry(found->command).keys = std::move(keys);
    }
    ++changes;
}

int Shortcuts::generation() noexcept
{
    return changes;
}

const std::vector<const char*>& Shortcuts::sections()
{
    static const std::vector<const char*> list{
        transport, view, tracks, editing, files, app, pianoRoll, audioEditor};
    return list;
}

} // namespace northlight::ui
