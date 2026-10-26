#pragma once

#include "app/ProjectLoad.h"
#include "app/ProjectSession.h"
#include "core/Model.h"
#include "ui/components/Toast.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace northlight
{
class LoadDriver;
class UserSettings;
namespace ui
{
class Sheet;
struct Theme;
} // namespace ui

// The Project menu's file work (screens/shell.md): new, from a template, open
// (with the loading sheet), close, revert, save and save as, templates, media
// management (gathering, removing unused, locating missing files), importing
// audio and MIDI and exporting MIDI, and the recent files. Message thread; the
// window hands it what it needs to show sheets, toasts and status.
//
// Everything that replaces or closes the project asks first when it has unsaved
// changes (confirmDiscard): New, Close, Open, a recent file, a dropped file, New
// from template, restoring an autosave, and quitting.
class FileActions
{
public:
    struct Host
    {
        juce::Component& window; // sheets go over it; it takes the focus back
        const ui::Theme& theme;
        ProjectSession& session;
        UserSettings& settings;
        LoadDriver& loader;
        ui::Sheet*& openSheet;              // the window's one sheet at a time
        std::function<void()> closeEditors; // before the project changes
        std::function<void()> refresh;
        std::function<void(ui::ToastStack::Kind, const juce::String&)> toast;
        std::function<void(const juce::String&)> status;
        std::function<void()> opened;              // a project was opened (recovery offers)
        std::function<core::Id()> selectedChannel; // a MIDI import plays it
        std::function<std::vector<std::pair<core::Id, core::Id>>()> midiClipsToExport;
        // A toast with one action (Locate… on missing media).
        std::function<void(
            ui::ToastStack::Kind, const juce::String&, const juce::String&, std::function<void()>)>
            actionToast;
    };
    explicit FileActions(Host host);
    ~FileActions();

    // Unsaved changes (interaction.md 10.6): the close-window sheet, "Save changes
    // to Song?" with Cancel, Don't save (destructive) and Save (primary). `then`
    // runs once the project may go: at once when nothing is unsaved, after a save
    // that succeeded, or after Don't save (which lets the session delete the
    // autosave). Cancel, a cancelled Save As or a failed save run nothing.
    // `consequence` ends "Unsaved changes will be lost if you …".
    void confirmDiscard(const juce::String& consequence, std::function<void()> then);

    void newProject();
    // The menu's Close project: an empty, untitled project, after asking.
    void closeProject();
    void newFromTemplate(const juce::File& file);
    // Restores a crashed session's autosave (the toast's Restore), with progress.
    void restoreAutosave(const ProjectSession::Autosave& autosave);
    void openProject();
    // Opens a file after asking about unsaved changes (a recent file, a file
    // dropped on the window or passed by Explorer).
    void open(const juce::File& file);
    // Opens at once, with the loading sheet; `holdAt` stops the load at a stage
    // (--show-loading). Callers ask first (open()).
    void openFile(const juce::File& file, std::optional<LoadProgress::Stage> holdAt = std::nullopt);
    // Reads the project's file again after a Danger sheet (the changes are lost).
    void revertToSaved();
    bool canRevert() const;
    // Quits after asking (the window's close button, the app menu's Quit, Windows
    // ending the session). With unsaved changes a fresh autosave is written first,
    // so even a session Windows ends while the sheet waits can be restored.
    void quit();
    // Saves; Save As (or a first save) asks for a file. `then` runs once saved.
    void saveProject(bool saveAs, std::function<void()> then = {});
    void saveTo(const juce::File& file, bool bringMedia = false, std::function<void()> then = {});
    void saveTemplate();
    void gatherMedia();
    void removeUnusedMedia();
    // Missing media (screens/arrange.md, "Empty and error states"): a toast when a
    // project opens with files missing, and Locate…, which asks for the file and,
    // when others are missing too and that folder has them, offers to relink them
    // as well. One undoable step.
    void offerMissingMedia();
    void locateMissing(const juce::File& missing);
    void importAudio();
    // `track`: the track they were dropped on (audio files go on it one after
    // another, a MIDI file plays what it plays); invalidId: new tracks.
    void importFiles(const std::vector<juce::File>& files,
                     core::SamplePosition at,
                     core::Id track = core::invalidId);
    void importMidi();
    void exportMidi();

    // The recent files, most recent first (up to the list's capacity).
    std::vector<juce::String> recentFiles() const;
    void rememberRecent(const juce::File& file);
    void clearRecent();
    // The folder new projects are saved to and opened from.
    juce::File projectFolder() const;
    juce::String projectName() const;

    // Whether a file is a project (dropped on the window, passed on the command line).
    static bool isProjectFile(const juce::File& file);

private:
    static constexpr int recentCount = 20; // the list's capacity; the menu shows the preferred count

    void startNewProject(const juce::String& status);
    void chooseSaveFile(std::function<void()> then);
    void writeRecent(const std::vector<juce::String>& files);

    Host host_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace northlight
