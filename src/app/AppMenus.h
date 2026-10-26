#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

// The main window's menus (screens/shell.md): the app menu, the project menu and
// the snap menu. Every item carries its own action, so a menu has no ids to keep
// in ranges (the project menu once gave templates 200 on and FX tracks 300 to
// 319, so the 101st template opened an FX track) and nothing to switch over when
// it is chosen. The window fills in the actions; they run on the message thread
// when an item is clicked, after the menu has closed.

namespace northlight::menus
{

struct AppMenu
{
    std::function<void()> about, audioSetup, plugins, preferences;
    std::function<void(const juce::String& preset)> layout; // "Edit", "Mix"
    std::function<void()> resetLayout, quit;
};
juce::PopupMenu appMenu(const AppMenu& actions);

struct ProjectMenu
{
    // What the menu lists.
    std::vector<juce::File> templates;
    std::vector<juce::File> recent;    // most recent first, as many as the menu shows
    std::vector<std::string> fxTracks; // the FX track choices' names, in order
    bool saved = false;                // the project has a file (media management needs one)
    // What its items do.
    std::function<void()> newProject, open, save, saveAs, saveTemplate, gatherMedia, removeUnusedMedia;
    std::function<void()> importAudio, importMidi, addAudioTrack, addMidiTrack, addBus, exportMix, exportMidi;
    std::function<void(const juce::File&)> newFromTemplate, openRecent;
    std::function<void(size_t choice)> addFxTrack; // an index into fxTracks
};
juce::PopupMenu projectMenu(const ProjectMenu& menu);

// The snap grids the header's snap menu offers (divisions of a bar; 0 is adaptive).
struct SnapGrid
{
    int divisions;
    const char* name;
};
const std::vector<SnapGrid>& snapGrids();
// The snap menu: each grid (ticked when snap is on at it) and Off. `chosen` gets a
// grid's divisions, or -1 for Off.
juce::PopupMenu snapMenu(bool snapOn, int grid, std::function<void(int divisions)> chosen);

} // namespace northlight::menus
