#include "app/FileActions.h"

#include "app/LoadDriver.h"
#include "app/UserSettings.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/components/Sheet.h"

#include <algorithm>

namespace northlight
{

namespace
{
juce::String filesText(int count)
{
    return juce::String(count) + (count == 1 ? " file" : " files");
}
} // namespace

FileActions::FileActions(Host host) : host_(std::move(host)) {}

FileActions::~FileActions() = default;

bool FileActions::isProjectFile(const juce::File& file)
{
    return file.hasFileExtension("northlight");
}

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

// ----- unsaved changes ------------------------------------------------------------------------

void FileActions::confirmDiscard(const juce::String& consequence, std::function<void()> then)
{
    if (!host_.session.isModified())
    {
        then();
        return;
    }
    if (host_.openSheet != nullptr)
        return; // one question at a time (a second close while the sheet shows)
    auto shared = std::make_shared<std::function<void()>>(std::move(then));
    host_.openSheet =
        ui::Sheet::show(host_.window,
                        host_.theme,
                        "Save changes to " + projectName() + "?",
                        "Unsaved changes will be lost if you " + consequence + " without saving.",
                        {{"Cancel", false, false}, {"Don't save", false, true}, {"Save", true, false}},
                        [this, shared](int result)
                        {
                            host_.openSheet = nullptr;
                            if (result == 1)
                            {
                                host_.session.discardChanges(); // the user chose: the autosave may go
                                (*shared)();
                            }
                            else if (result == 2)
                                saveProject(false, [shared] { (*shared)(); });
                            else
                                host_.window.grabKeyboardFocus();
                        });
}

void FileActions::quit()
{
    if (host_.session.isModified())
        host_.session.autosaveNow(); // Windows may end the session while the sheet waits
    confirmDiscard("quit", [] { juce::JUCEApplication::getInstance()->quit(); });
}

// ----- new, open, close, revert -----------------------------------------------------------------

void FileActions::startNewProject(const juce::String& status)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    host_.session.stop();
    host_.closeEditors();
    host_.session.newProject();
    if (status.isNotEmpty())
        host_.status(status);
    host_.refresh();
    host_.window.grabKeyboardFocus();
}

void FileActions::newProject()
{
    if (host_.session.isRecording())
        return;
    confirmDiscard("start a new project", [this] { startNewProject({}); });
}

void FileActions::closeProject()
{
    if (host_.session.isRecording())
        return;
    const auto name = projectName();
    confirmDiscard("close it", [this, name] { startNewProject("Closed " + name); });
}

void FileActions::newFromTemplate(const juce::File& file)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    confirmDiscard("start a new project",
                   [this, file]
                   {
                       if (host_.session.isRecording() || !host_.session.beginTemplate(file))
                           return;
                       host_.session.stop();
                       host_.closeEditors();
                       host_.loader.watch("New project from " + file.getFileNameWithoutExtension(),
                                          [this, file](const std::string& error)
                                          {
                                              if (!error.empty())
                                                  host_.toast(ui::ToastStack::Kind::error,
                                                              "Cannot use the template: " +
                                                                  juce::String::fromUTF8(error.c_str()));
                                              else
                                                  host_.status("New project from " +
                                                               file.getFileNameWithoutExtension());
                                              host_.refresh();
                                              host_.window.grabKeyboardFocus();
                                          });
                   });
}

// Restores a crashed session's autosave (the toast's Restore), with progress.
void FileActions::restoreAutosave(const ProjectSession::Autosave& autosave)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    confirmDiscard("restore the autosave",
                   [this, autosave]
                   {
                       if (host_.session.isRecording() || !host_.session.beginRestore(autosave))
                           return;
                       host_.session.stop();
                       host_.closeEditors();
                       host_.loader.watch("Restoring " + juce::String::fromUTF8(autosave.name.c_str()),
                                          [this](const std::string& error)
                                          {
                                              if (!error.empty())
                                                  host_.toast(ui::ToastStack::Kind::error,
                                                              "Cannot restore: " +
                                                                  juce::String::fromUTF8(error.c_str()));
                                              host_.refresh();
                                              host_.window.grabKeyboardFocus();
                                              if (error.empty())
                                                  offerMissingMedia();
                                          });
                   });
}

void FileActions::openProject()
{
    if (host_.session.isRecording())
        return;
    chooser_ = std::make_unique<juce::FileChooser>("Open project", projectFolder(), "*.northlight");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc)
                          {
                              if (const auto file = fc.getResult(); file != juce::File())
                                  open(file);
                          });
}

void FileActions::open(const juce::File& file)
{
    if (host_.session.isRecording() || host_.session.isLoading())
        return;
    if (!file.existsAsFile())
    {
        host_.toast(ui::ToastStack::Kind::error,
                    "Cannot open " + file.getFileName() +
                        ": file not found. It may have been moved or deleted.");
        return;
    }
    confirmDiscard("open another project", [this, file] { openFile(file); });
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
            if (error.empty())
                offerMissingMedia();
        },
        holdAt);
}

bool FileActions::canRevert() const
{
    return host_.session.projectFile() != juce::File() && host_.session.projectFile().existsAsFile() &&
           host_.session.isModified() && !host_.session.isRecording();
}

void FileActions::revertToSaved()
{
    if (!canRevert() || host_.openSheet != nullptr)
        return;
    const auto file = host_.session.projectFile();
    host_.openSheet = ui::Sheet::show(host_.window,
                                      host_.theme,
                                      "Revert to the saved " + projectName() + "?",
                                      "The changes since it was last saved (" +
                                          file.getLastModificationTime().formatted("%H:%M") +
                                          ") will be lost. This cannot be undone.",
                                      {{"Cancel", false, false}, {"Revert", true, true}},
                                      [this, file](int result)
                                      {
                                          host_.openSheet = nullptr;
                                          if (result != 1)
                                          {
                                              host_.window.grabKeyboardFocus();
                                              return;
                                          }
                                          host_.session.discardChanges();
                                          openFile(file);
                                      });
}

// ----- saving -------------------------------------------------------------------------------------

void FileActions::saveProject(bool saveAs, std::function<void()> then)
{
    if (!saveAs && host_.session.projectFile().getFullPathName().isNotEmpty())
    {
        saveTo(host_.session.projectFile(), false, std::move(then));
        return;
    }
    chooseSaveFile(std::move(then));
}

void FileActions::chooseSaveFile(std::function<void()> then)
{
    const bool untitled = host_.session.projectFile() == juce::File();
    const auto start =
        untitled ? projectFolder().getChildFile("Untitled.northlight") : host_.session.projectFile();
    chooser_ = std::make_unique<juce::FileChooser>("Save project", start, "*.northlight");
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [this, untitled, then = std::move(then)](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (!file.hasFileExtension("northlight"))
                file = file.withFileExtension("northlight");
            const auto read =
                settings::Reader([this](const std::string& key) { return host_.settings.value(key); });
            // A first save brings the media along when imports are copied (the
            // recordings in the shared folder always come).
            if (untitled)
            {
                saveTo(file, read.flag(settings::copyMedia), then);
                host_.window.grabKeyboardFocus();
                return;
            }
            // Save As somewhere else: offer to bring the media along.
            const auto [count, bytes] = host_.session.mediaToBring(file);
            if (count == 0 || host_.openSheet != nullptr)
            {
                saveTo(file, false, then);
                host_.window.grabKeyboardFocus();
                return;
            }
            host_.openSheet = ui::Sheet::show(
                host_.window,
                host_.theme,
                "Bring the media along?",
                filesText(count) + " (" + juce::File::descriptionOfSizeInBytes(bytes) +
                    ") this project uses " + (count == 1 ? "is" : "are") + " outside \"" +
                    file.getFileNameWithoutExtension() +
                    " Media\". Copy them in so the new project's folder holds everything it plays, or keep "
                    "using them where they are.",
                {{"Cancel", false, false},
                 {"Keep where they are", false, false},
                 {"Copy media", true, false}},
                [this, file, then](int result)
                {
                    host_.openSheet = nullptr;
                    if (result == 1 || result == 2)
                        saveTo(file, result == 2, then);
                    host_.window.grabKeyboardFocus();
                });
        });
}

void FileActions::saveTo(const juce::File& file, bool bringMedia, std::function<void()> then)
{
    const auto read = settings::Reader([this](const std::string& key) { return host_.settings.value(key); });
    host_.session.setKeepBackup(read.flag(settings::keepBackup));
    const auto error = host_.session.save(file, bringMedia);
    if (!error.empty())
    {
        host_.toast(ui::ToastStack::Kind::error,
                    "Cannot save: " + juce::String::fromUTF8(error.c_str()) +
                        ". The project is still open; try Save As to another folder.");
    }
    else
    {
        host_.status("Saved " + file.getFileName());
        rememberRecent(file);
    }
    host_.refresh();
    if (error.empty() && then)
        then();
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

// ----- media ----------------------------------------------------------------------------------

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
                    "Copied " + filesText(copied) + " into the project folder. Save to keep the new paths.");
}

void FileActions::removeUnusedMedia()
{
    const auto report = host_.session.mediaReport();
    const bool untitled = host_.session.projectFile() == juce::File();
    if (report.unused.empty())
    {
        host_.toast(ui::ToastStack::Kind::info,
                    untitled ? "No unused recordings from this project."
                             : "No unused media in the project folder.");
        return;
    }
    juce::int64 bytes = 0;
    for (const auto& f : report.unused)
        bytes += f.getSize();
    host_.openSheet = ui::Sheet::show(
        host_.window,
        host_.theme,
        "Remove unused media",
        filesText(static_cast<int>(report.unused.size())) + " (" +
            juce::File::descriptionOfSizeInBytes(bytes) +
            (untitled ? ") recorded for this project in the shared Recordings folder are"
                      : ") in the project folder are") +
            " not used by any clip. They go to the Recycle Bin, so an undo that brings a clip back can still "
            "find them there.",
        {{"Cancel", false, false}, {"Move to Recycle Bin", true, true}},
        [this](int result)
        {
            host_.openSheet = nullptr;
            if (result != 1)
                return;
            const int moved = host_.session.removeUnusedMedia();
            host_.toast(ui::ToastStack::Kind::success, "Moved " + filesText(moved) + " to the Recycle Bin.");
        });
}

void FileActions::offerMissingMedia()
{
    const auto missing = host_.session.mediaReport().missing;
    if (missing.empty() || !host_.actionToast)
        return;
    const auto first = missing.front();
    const int count = static_cast<int>(missing.size());
    host_.actionToast(ui::ToastStack::Kind::warning,
                      (count == 1 ? "\"" + first.getFileName() + "\" is missing"
                                  : filesText(count) + " this project uses are missing") +
                          ". " + (count == 1 ? "Its clips play" : "Their clips play") +
                          " silence until you locate " + (count == 1 ? "it." : "them."),
                      juce::String::fromUTF8("Locate\xe2\x80\xa6"),
                      [this, first] { locateMissing(first); });
}

void FileActions::locateMissing(const juce::File& missing)
{
    if (host_.session.isRecording())
        return;
    // Start where it was, or as near as still exists, else the project's folder.
    auto start = missing.getParentDirectory();
    while (start != juce::File() && !start.isDirectory() && start.getParentDirectory() != start)
        start = start.getParentDirectory();
    if (!start.isDirectory())
        start = host_.session.projectFile() != juce::File() ? host_.session.projectFile().getParentDirectory()
                                                            : projectFolder();
    const auto extension = missing.getFileExtension();
    chooser_ = std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Locate ") + missing.getFileName(),
        start.getChildFile(missing.getFileName()),
        extension.isNotEmpty() ? "*" + extension : host_.session.mediaLoader().supportedExtensions());
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, missing](const juce::FileChooser& fc)
        {
            const auto found = fc.getResult();
            if (found == juce::File() || !found.existsAsFile())
                return;
            const auto relink = [this, missing, found](bool all)
            {
                const int relinked = host_.session.relinkMedia(missing, found, all);
                host_.refresh();
                host_.window.grabKeyboardFocus();
                const int left = static_cast<int>(host_.session.mediaReport().missing.size());
                host_.toast(ui::ToastStack::Kind::success,
                            "Relinked " + filesText(relinked) + "." +
                                (left > 0 ? " " + filesText(left) + " still missing." : juce::String()));
            };
            const auto folder = found.getParentDirectory();
            const int others = host_.session.missingFilesIn(folder, missing);
            if (others == 0 || host_.openSheet != nullptr)
            {
                relink(false);
                return;
            }
            host_.openSheet = ui::Sheet::show(
                host_.window,
                host_.theme,
                "Relink the other missing files?",
                juce::String(others) + " of the other missing files " + (others == 1 ? "is" : "are") +
                    " in \"" + folder.getFileName() + "\" under the same " +
                    (others == 1 ? "name" : "names") + ". Relink them as well? You can undo it.",
                {{"Just this one", false, false}, {"Relink all", true, false}},
                [this, relink](int result)
                {
                    host_.openSheet = nullptr;
                    relink(result == 1);
                });
        });
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
    const auto read = settings::Reader([this](const std::string& key) { return host_.settings.value(key); });
    host_.session.setCopyImportedMedia(read.flag(settings::copyMedia));
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

// ----- recent files ---------------------------------------------------------------------------

std::vector<juce::String> FileActions::recentFiles() const
{
    std::vector<juce::String> files;
    for (int i = 0; i < recentCount; ++i)
        if (const auto path = host_.settings.value("recent." + std::to_string(i)); !path.empty())
            files.push_back(juce::String::fromUTF8(path.c_str()));
    return files;
}

void FileActions::writeRecent(const std::vector<juce::String>& files)
{
    for (int i = 0; i < recentCount; ++i)
        host_.settings.setValue("recent." + std::to_string(i),
                                static_cast<size_t>(i) < files.size()
                                    ? files[static_cast<size_t>(i)].toStdString()
                                    : std::string{});
}

void FileActions::rememberRecent(const juce::File& file)
{
    auto files = recentFiles();
    files.erase(std::remove(files.begin(), files.end(), file.getFullPathName()), files.end());
    files.insert(files.begin(), file.getFullPathName());
    writeRecent(files);
}

void FileActions::clearRecent()
{
    writeRecent({});
    host_.status("Cleared the recent projects");
}

} // namespace northlight
