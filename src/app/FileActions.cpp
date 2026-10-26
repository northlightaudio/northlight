#include "app/FileActions.h"

#include "app/LoadDriver.h"
#include "app/UserSettings.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/components/Sheet.h"

#include <algorithm>

namespace northlight
{

FileActions::FileActions(Host host) : host_(std::move(host)) {}

FileActions::~FileActions() = default;

// The folder new projects are saved to and opened from.
juce::File FileActions::projectFolder() const
{
    const auto chosen = settings::Reader([this](const std::string& key) { return host_.settings.value(key); })
                            .text(settings::projectFolder);
    if (!chosen.empty())
        if (const juce::File folder(juce::String::fromUTF8(chosen.c_str())); folder.isDirectory())
            return folder;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}

juce::String FileActions::projectName() const
{
    return host_.session.projectFile().getFullPathName().isNotEmpty()
               ? host_.session.projectFile().getFileNameWithoutExtension()
               : juce::String("Untitled");
}

void FileActions::newProject()
{
    if (host_.session.isRecording())
        return;
    host_.session.stop();
    host_.closeEditors();
    host_.session.newProject();
    host_.refresh();
}

void FileActions::newFromTemplate(const juce::File& file)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    host_.session.stop();
    host_.closeEditors();
    if (!host_.session.beginTemplate(file))
        return;
    host_.loader.watch("New project from " + file.getFileNameWithoutExtension(),
                       [this, file](const std::string& error)
                       {
                           if (!error.empty())
                               host_.toast(ui::ToastStack::Kind::error,
                                           "Cannot use the template: " +
                                               juce::String::fromUTF8(error.c_str()));
                           else
                               host_.status("New project from " + file.getFileNameWithoutExtension());
                           host_.refresh();
                           host_.window.grabKeyboardFocus();
                       });
}

// Restores a crashed session's autosave (the toast's Restore), with progress.
void FileActions::restoreAutosave(const ProjectSession::Autosave& autosave)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    host_.session.stop();
    host_.closeEditors();
    if (!host_.session.beginRestore(autosave))
        return;
    host_.loader.watch("Restoring " + juce::String::fromUTF8(autosave.name.c_str()),
                       [this](const std::string& error)
                       {
                           if (!error.empty())
                               host_.toast(ui::ToastStack::Kind::error,
                                           "Cannot restore: " + juce::String::fromUTF8(error.c_str()));
                           host_.refresh();
                           host_.window.grabKeyboardFocus();
                       });
}

void FileActions::saveTemplate()
{
    auto field = std::make_unique<juce::TextEditor>();
    field->setText(juce::String::fromUTF8(host_.session.project().name.c_str()) == "Untitled"
                       ? juce::String("My template")
                       : juce::String::fromUTF8(host_.session.project().name.c_str()));
    field->selectAll();
    auto* raw = field.get();
    host_.openSheet = ui::Sheet::show(
        host_.window,
        host_.theme,
        "Save as template",
        "The tracks, buses, routing, inserts and channel strips, without clips or markers. "
        "New from template in the Project menu starts a project from it.",
        {{"Cancel", false, false}, {"Save", true, false}},
        [this, raw](int result)
        {
            const auto name = raw->getText();
            host_.openSheet = nullptr;
            if (result != 1)
                return;
            const auto error = host_.session.saveAsTemplate(name);
            if (!error.empty())
                host_.toast(ui::ToastStack::Kind::error,
                            "Cannot save the template: " + juce::String::fromUTF8(error.c_str()));
            else
                host_.toast(ui::ToastStack::Kind::success, "Saved the template \"" + name.trim() + "\"");
        },
        std::move(field),
        host_.theme.sizes.control);
}

void FileActions::gatherMedia()
{
    int copied = 0;
    const auto error = host_.session.consolidateMedia(copied);
    if (!error.empty())
        host_.toast(ui::ToastStack::Kind::error,
                    "Cannot gather media: " + juce::String::fromUTF8(error.c_str()));
    else if (copied == 0)
        host_.toast(ui::ToastStack::Kind::info, "All media is already in the project folder.");
    else
        host_.toast(ui::ToastStack::Kind::success,
                    "Copied " + juce::String(copied) + (copied == 1 ? " file" : " files") +
                        " into the project folder. Save to keep the new paths.");
}

void FileActions::removeUnusedMedia()
{
    const auto report = host_.session.mediaReport();
    if (report.unused.empty())
    {
        host_.toast(ui::ToastStack::Kind::info, "No unused media in the project folder.");
        return;
    }
    juce::int64 bytes = 0;
    for (const auto& f : report.unused)
        bytes += f.getSize();
    host_.openSheet =
        ui::Sheet::show(host_.window,
                        host_.theme,
                        "Remove unused media",
                        juce::String(static_cast<int>(report.unused.size())) +
                            (report.unused.size() == 1 ? " file" : " files") + " (" +
                            juce::File::descriptionOfSizeInBytes(bytes) +
                            ") in the project folder are not used by any clip. They go to the "
                            "Recycle Bin, so an undo that brings a clip back can still find them there.",
                        {{"Cancel", false, false}, {"Move to Recycle Bin", true, true}},
                        [this](int result)
                        {
                            host_.openSheet = nullptr;
                            if (result != 1)
                                return;
                            const int moved = host_.session.removeUnusedMedia();
                            host_.toast(ui::ToastStack::Kind::success,
                                        "Moved " + juce::String(moved) + (moved == 1 ? " file" : " files") +
                                            " to the Recycle Bin.");
                        });
}

void FileActions::openProject()
{
    if (host_.session.isRecording())
        return;
    chooser_ = std::make_unique<juce::FileChooser>("Open project", projectFolder(), "*.northlight;*.aural");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc)
                          {
                              if (const auto file = fc.getResult(); file != juce::File())
                                  openFile(file);
                          });
}

void FileActions::openFile(const juce::File& file, std::optional<LoadProgress::Stage> holdAt)
{
    if (host_.session.isLoading())
        return;
    host_.session.stop();
    host_.closeEditors();
    if (!host_.session.beginOpen(file))
        return;
    host_.loader.watch(
        "Opening " + file.getFileNameWithoutExtension(),
        [this, file](const std::string& error)
        {
            if (!error.empty())
                host_.toast(ui::ToastStack::Kind::error,
                            "Cannot open " + file.getFileName() + ": " +
                                juce::String::fromUTF8(error.c_str()));
            else
                rememberRecent(file);
            host_.refresh();
            host_.window.grabKeyboardFocus();
            host_.opened();
        },
        holdAt);
}

void FileActions::saveProject(bool saveAs, AfterSave after)
{
    if (!saveAs && host_.session.projectFile().getFullPathName().isNotEmpty())
    {
        saveTo(host_.session.projectFile(), after);
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save project", projectFolder().getChildFile("Untitled.northlight"), "*.northlight");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, after](const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (!file.hasFileExtension("northlight"))
                                  file = file.withFileExtension("northlight");
                              saveTo(file, after);
                              host_.window.grabKeyboardFocus();
                          });
}

void FileActions::saveTo(const juce::File& file, AfterSave after)
{
    const auto error = host_.session.save(file);
    if (!error.empty())
    {
        host_.toast(ui::ToastStack::Kind::error, "Cannot save: " + juce::String::fromUTF8(error.c_str()));
    }
    else
    {
        host_.status("Saved " + file.getFileName());
        rememberRecent(file);
        if (after == AfterSave::quit)
            juce::JUCEApplication::getInstance()->quit();
    }
    host_.refresh();
}

std::vector<juce::String> FileActions::recentFiles() const
{
    std::vector<juce::String> files;
    for (int i = 0; i < recentCount; ++i)
        if (const auto path = host_.settings.value("recent." + std::to_string(i)); !path.empty())
            files.push_back(juce::String::fromUTF8(path.c_str()));
    return files;
}

void FileActions::rememberRecent(const juce::File& file)
{
    auto files = recentFiles();
    files.erase(std::remove(files.begin(), files.end(), file.getFullPathName()), files.end());
    files.insert(files.begin(), file.getFullPathName());
    for (int i = 0; i < recentCount; ++i)
        host_.settings.setValue("recent." + std::to_string(i),
                                static_cast<size_t>(i) < files.size()
                                    ? files[static_cast<size_t>(i)].toStdString()
                                    : std::string{});
}

void FileActions::importAudio()
{
    chooser_ =
        std::make_unique<juce::FileChooser>("Import audio",
                                            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                            host_.session.mediaLoader().supportedExtensions());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& fc)
                          {
                              std::vector<juce::File> files;
                              for (const auto& f : fc.getResults())
                                  files.push_back(f);
                              importFiles(files, 0);
                          });
}

void FileActions::importFiles(const std::vector<juce::File>& files, core::SamplePosition at, core::Id track)
{
    for (const auto& file : files)
    {
        // A MIDI file plays the track it was dropped on, or the selected
        // instrument (or the one the selected MIDI track plays), else a new one.
        const bool midi = file.hasFileExtension("mid;midi");
        const auto error = midi ? host_.session.importMidiFile(
                                      file, at, track != core::invalidId ? track : host_.selectedChannel())
                                : host_.session.importAudioFile(file, at, track);
        // Audio files dropped on a track go one after another.
        if (!midi && error.empty())
            if (const auto* onto = host_.session.project().findTrack(track))
                for (const auto& clip : onto->clips)
                    if (clip.start == at)
                        at = std::max(at, clip.end());
        if (!error.empty())
            host_.toast(ui::ToastStack::Kind::error,
                        file.getFileName() + ": " + juce::String::fromUTF8(error.c_str()));
    }
    host_.refresh();
    host_.window.grabKeyboardFocus();
}

void FileActions::importMidi()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        "Import MIDI file", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.mid;*.midi");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& fc)
                          {
                              std::vector<juce::File> files;
                              for (const auto& f : fc.getResults())
                                  files.push_back(f);
                              importFiles(files, host_.session.position());
                          });
}

void FileActions::exportMidi()
{
    const auto clips = host_.midiClipsToExport();
    if (clips.empty())
    {
        host_.toast(ui::ToastStack::Kind::error, "Export MIDI: there are no MIDI clips");
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>(
        "Export MIDI", projectFolder().getChildFile(projectName() + ".mid"), "*.mid");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, clips](const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (!file.hasFileExtension("mid;midi"))
                                  file = file.withFileExtension("mid");
                              const auto error = host_.session.exportMidiFile(file, clips);
                              if (!error.empty())
                                  host_.toast(ui::ToastStack::Kind::error,
                                              "Export MIDI: " + juce::String::fromUTF8(error.c_str()));
                              else
                                  host_.toast(ui::ToastStack::Kind::success,
                                              "Exported " + file.getFileName());
                          });
}

} // namespace northlight
