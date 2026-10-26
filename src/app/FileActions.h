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
// (with the loading sheet), save and save as, templates, media management,
// importing audio and MIDI and exporting MIDI, and the recent files. Message
// thread; the window hands it what it needs to show sheets, toasts and status.
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
    };
    explicit FileActions(Host host);
    ~FileActions();

    // What happens once a save succeeds. Passed along rather than kept in a member,
    // so a cancelled or failed save leaves nothing behind for the next one.
    enum class AfterSave
    {
        stay,
        quit,
    };

    void newProject();
    void newFromTemplate(const juce::File& file);
    // Restores a crashed session's autosave (the toast's Restore), with progress.
    void restoreAutosave(const ProjectSession::Autosave& autosave);
    void openProject();
    void openFile(const juce::File& file, std::optional<LoadProgress::Stage> holdAt = std::nullopt);
    void saveProject(bool saveAs, AfterSave after = AfterSave::stay);
    void saveTo(const juce::File& file, AfterSave after = AfterSave::stay);
    void saveTemplate();
    void gatherMedia();
    void removeUnusedMedia();
    void importAudio();
    void importFiles(const std::vector<juce::File>& files, core::SamplePosition at);
    void importMidi();
    void exportMidi();

    // The recent files, most recent first (up to the list's capacity).
    std::vector<juce::String> recentFiles() const;
    void rememberRecent(const juce::File& file);
    // The folder new projects are saved to and opened from.
    juce::File projectFolder() const;
    juce::String projectName() const;

private:
    static constexpr int recentCount = 20; // the list's capacity; the menu shows the preferred count

    Host host_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace northlight
