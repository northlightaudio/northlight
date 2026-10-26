#pragma once

#include "core/Model.h"
#include "engine/ExportSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace northlight
{
class ProjectSession;
class UserSettings;
namespace ui
{
class Sheet;
struct Theme;
} // namespace ui

// The export sheet (Ctrl+E; screens/shell.md): the format, range and source last
// used (kept in the user settings as "export.*"), then a file or folder chooser,
// then the session's bounce. The bounce's progress and outcome reach the window
// as notices.
class ExportController
{
public:
    struct Host
    {
        juce::Component& sheetHost;
        const ui::Theme& theme;
        ProjectSession& session;
        UserSettings& settings;
        ui::Sheet*& openSheet; // the window's one sheet at a time (null when none is up)
        // The tracks "selected tracks" exports.
        std::function<std::vector<core::Id>()> selectedTracks;
        std::function<juce::File()> folder;        // where the chooser starts
        std::function<juce::String()> projectName; // the file name's start
        std::function<void(const juce::String& message)> failed;
        std::function<void()> refresh;
    };
    explicit ExportController(Host host);
    ~ExportController();

    void showSheet();

private:
    engine::ExportSettings lastSettings() const;
    void start(const engine::ExportSettings& chosen, std::optional<core::Marker> region);

    Host host_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace northlight
