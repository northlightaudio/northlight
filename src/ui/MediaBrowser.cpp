#include "ui/MediaBrowser.h"

#include "ui/Db.h"
#include "ui/MediaFiles.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/PluginBrowser.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int rowHeight = 28;
constexpr int iconWidth = 24;
constexpr int maxResults = 2000;  // a search stops listing here
constexpr int maxVisited = 50000; // and stops looking here
constexpr const char* foldersKey = "browser.files.folders";
constexpr const char* lastFolderKey = "browser.files.folder";
constexpr const char* lastRootKey = "browser.files.root";
constexpr const char* gainKey = "browser.files.previewGain";

juce::String lengthText(double seconds)
{
    if (seconds < 0.0)
        return {};
    if (seconds < 60.0)
        return juce::String(seconds, seconds < 10.0 ? 2 : 1) + " s";
    const int whole = static_cast<int>(seconds);
    return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

// A tray (docs/design/design-system.md): surface1, radius md, a subtle rim.
void drawTray(juce::Graphics& g, const Theme& theme, juce::Rectangle<int> r)
{
    g.setColour(theme.colours.surface1);
    g.fillRoundedRectangle(r.toFloat(), theme.radius.md);
    g.setColour(theme.colours.lineSubtle);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), theme.radius.md, 1.0f);
}
} // namespace

// ----- listing ----------------------------------------------------------------------------------

std::vector<MediaBrowser::Entry> MediaBrowser::list(const juce::File& folder,
                                                    const juce::File& root,
                                                    const juce::String& query,
                                                    const std::function<bool()>& cancelled)
{
    std::vector<Entry> folders, files;
    const auto words = juce::StringArray::fromTokens(query.trim(), " ", "");
    const auto matches = [&](const juce::String& name)
    {
        for (const auto& w : words)
            if (w.isNotEmpty() && !name.containsIgnoreCase(w))
                return false;
        return true;
    };
    if (words.isEmpty())
    {
        for (const auto& entry : juce::RangedDirectoryIterator(
                 folder, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            if (cancelled())
                return {};
            const auto& file = entry.getFile();
            if (entry.isDirectory())
                folders.push_back({file, true, file.getFileName(), {}, -1.0, 0.0, 0});
            else if (isAudioFile(file))
                files.push_back({file, false, file.getFileName(), {}, -1.0, 0.0, 0});
        }
    }
    else
    {
        int visited = 0;
        for (const auto& entry : juce::RangedDirectoryIterator(
                 root, true, "*", juce::File::findFiles | juce::File::ignoreHiddenFiles))
        {
            if (cancelled() || ++visited > maxVisited || static_cast<int>(files.size()) >= maxResults)
                break;
            const auto& file = entry.getFile();
            if (!isAudioFile(file) || !matches(file.getFileName()))
                continue;
            const auto parent = file.getParentDirectory();
            files.push_back({file,
                             false,
                             file.getFileName(),
                             parent == root ? juce::String() : parent.getRelativePathFrom(root),
                             -1.0,
                             0.0,
                             0});
        }
    }
    const auto byName = [](const Entry& a, const Entry& b) { return a.name.compareNatural(b.name) < 0; };
    std::sort(folders.begin(), folders.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    // The headers: length, rate and channels.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (auto& f : files)
    {
        if (cancelled())
            return {};
        if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f.file));
            reader != nullptr && reader->sampleRate > 0.0)
        {
            f.sampleRate = reader->sampleRate;
            f.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            f.channels = static_cast<int>(reader->numChannels);
        }
    }
    folders.insert(folders.end(), files.begin(), files.end());
    return folders;
}

// ----- the panel --------------------------------------------------------------------------------

MediaBrowser::MediaBrowser()
    : add_(theme_, Icon::add, "Add a folder to the list"),
      stop_(theme_, Icon::stop, "Stop the preview (Escape)")
{
    setOpaque(true);
    search_.setTextToShowWhenEmpty("Search files", theme_.colours.textTertiary);
    search_.setFont(theme_.body());
    search_.onTextChange = [this] { startListing(); };
    search_.onEscapeKey = [this]
    {
        stopPreview();
        search_.clear();
        startListing();
    };
    search_.onReturnKey = [this]
    {
        // Enter in the field auditions the first file.
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            if (!entries_[static_cast<size_t>(i)].folder)
            {
                list_.selectRow(i);
                preview(entries_[static_cast<size_t>(i)]);
                return;
            }
    };
    addAndMakeVisible(search_);

    place_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    place_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    place_.setTooltip("The place to browse: the project's folder, Music, or a folder you added");
    place_.onClick = [this] { showPlaceMenu(); };
    addAndMakeVisible(place_);
    up_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    up_.setTooltip("Up a folder (Backspace)");
    up_.onClick = [this] { goUp(); };
    addAndMakeVisible(up_);
    add_.onClick = [this] { addFolder(); };
    addAndMakeVisible(add_);
    stop_.onClick = [this] { stopPreview(); };
    addAndMakeVisible(stop_);

    volume_.setSliderStyle(juce::Slider::LinearHorizontal);
    volume_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volume_.setRange(0.0, 1.0);
    volume_.setValue(0.7, juce::dontSendNotification);
    volume_.setTooltip("Preview volume");
    volume_.onValueChange = [this]
    {
        const auto gain = static_cast<float>(volume_.getValue() * volume_.getValue());
        if (onPreviewGain)
            onPreviewGain(gain);
        if (saveSetting)
            saveSetting(gainKey, juce::String(volume_.getValue(), 3).toStdString());
        repaint(footerTray());
    };
    addAndMakeVisible(volume_);

    list_.setRowHeight(rowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, theme_.colours.surface0);
    list_.setMultipleSelectionEnabled(false);
    addAndMakeVisible(list_);
}

MediaBrowser::~MediaBrowser()
{
    generation_->fetch_add(1);
    pool_.removeAllJobs(true, 4000);
}

std::vector<juce::String> MediaBrowser::folderList() const
{
    std::vector<juce::String> folders;
    if (loadSetting)
        for (const auto& item :
             juce::StringArray::fromTokens(juce::String::fromUTF8(loadSetting(foldersKey).c_str()), "\t", ""))
            if (item.isNotEmpty())
                folders.push_back(item);
    return folders;
}

void MediaBrowser::saveFolderList(const std::vector<juce::String>& folders)
{
    if (!saveSetting)
        return;
    juce::String joined;
    for (const auto& f : folders)
        joined << (joined.isEmpty() ? "" : "\t") << f;
    saveSetting(foldersKey, joined.toStdString());
}

std::vector<MediaBrowser::Place> MediaBrowser::places() const
{
    std::vector<Place> out;
    if (projectFolder)
        if (const auto project = projectFolder(); project.isDirectory())
            out.push_back({"Project", project, false});
    if (const auto music = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
        music.isDirectory())
        out.push_back({"Music", music, false});
    for (const auto& path : folderList())
        if (juce::File::isAbsolutePath(path))
        {
            const juce::File folder(path);
            out.push_back({folder.getFileName(), folder, true});
        }
    return out;
}

void MediaBrowser::refresh()
{
    if (loadSetting)
    {
        if (const auto gain = loadSetting(gainKey); !gain.empty())
            volume_.setValue(juce::jlimit(0.0, 1.0, juce::String(gain).getDoubleValue()),
                             juce::dontSendNotification);
        if (onPreviewGain)
            onPreviewGain(static_cast<float>(volume_.getValue() * volume_.getValue()));
    }
    if (!root_.isDirectory())
    {
        // The last place, else the first one there is.
        const auto lastRoot =
            loadSetting ? juce::String::fromUTF8(loadSetting(lastRootKey).c_str()) : juce::String();
        const auto lastFolder =
            loadSetting ? juce::String::fromUTF8(loadSetting(lastFolderKey).c_str()) : juce::String();
        juce::File root, folder;
        if (juce::File::isAbsolutePath(lastRoot) && juce::File(lastRoot).isDirectory())
        {
            root = juce::File(lastRoot);
            folder = juce::File::isAbsolutePath(lastFolder) && juce::File(lastFolder).isDirectory() &&
                             juce::File(lastFolder).isAChildOf(root)
                         ? juce::File(lastFolder)
                         : root;
        }
        else if (const auto all = places(); !all.empty())
            root = folder = all.front().root;
        root_ = root;
        folder_ = folder;
    }
    updatePlaceButton();
    startListing();
}

void MediaBrowser::showFolder(const juce::File& folder)
{
    if (!folder.isDirectory())
        return;
    for (const auto& p : places())
        if (folder == p.root || folder.isAChildOf(p.root))
        {
            enter(folder, p.root);
            return;
        }
    enter(folder, folder);
}

void MediaBrowser::enter(const juce::File& folder, const juce::File& root)
{
    root_ = root;
    folder_ = folder;
    if (saveSetting)
    {
        saveSetting(lastRootKey, root_.getFullPathName().toStdString());
        saveSetting(lastFolderKey, folder_.getFullPathName().toStdString());
    }
    updatePlaceButton();
    search_.clear();
    startListing();
}

void MediaBrowser::goUp()
{
    if (folder_ != root_ && folder_.isAChildOf(root_))
        enter(folder_.getParentDirectory(), root_);
}

void MediaBrowser::updatePlaceButton()
{
    juce::String text = "No folder";
    for (const auto& p : places())
        if (p.root == root_)
            text = p.name;
    if (root_.isDirectory() && text == "No folder")
        text = root_.getFileName();
    if (folder_ != root_ && folder_.isAChildOf(root_))
        text << juce::String::fromUTF8("  \xe2\x80\xba  ")
             << folder_.getRelativePathFrom(root_).replace("\\", juce::String::fromUTF8(" \xe2\x80\xba "));
    place_.setButtonText(text);
    up_.setEnabled(folder_ != root_ && folder_.isAChildOf(root_));
}

void MediaBrowser::startListing()
{
    const int generation = generation_->fetch_add(1) + 1;
    pool_.removeAllJobs(false, 0);
    if (!folder_.isDirectory())
    {
        entries_.clear();
        listing_ = false;
        message_ = "Add a folder with the + button";
        list_.updateContent();
        repaint();
        return;
    }
    listing_ = true;
    message_.clear();
    const auto folder = folder_, root = root_;
    const auto query = search_.getText();
    const auto counter = generation_;
    juce::Component::SafePointer<MediaBrowser> safe(this);
    pool_.addJob(
        [folder, root, query, counter, generation, safe]
        {
            auto result = list(folder, root, query, [&] { return counter->load() != generation; });
            if (counter->load() != generation)
                return;
            juce::MessageManager::callAsync(
                [safe, generation, counter, result = std::move(result)]() mutable
                {
                    if (safe == nullptr || counter->load() != generation)
                        return;
                    safe->entries_ = std::move(result);
                    safe->listing_ = false;
                    safe->message_ = safe->entries_.empty()
                                         ? (safe->search_.getText().trim().isEmpty() ? "No audio files here"
                                                                                     : "No files match")
                                         : juce::String();
                    safe->list_.deselectAllRows();
                    safe->list_.updateContent();
                    safe->list_.repaint();
                    safe->repaint();
                });
        });
}

void MediaBrowser::showPlaceMenu()
{
    juce::PopupMenu menu;
    const auto all = places();
    for (size_t i = 0; i < all.size(); ++i)
        menu.addItem(static_cast<int>(i) + 1,
                     all[i].name + "  " + juce::String::fromUTF8("\xc2\xb7  ") +
                         all[i].root.getFullPathName(),
                     true,
                     all[i].root == root_);
    menu.addSeparator();
    menu.addItem(1000, juce::String::fromUTF8("Add a folder\xe2\x80\xa6"));
    bool removable = false;
    for (const auto& p : all)
        removable = removable || (p.removable && p.root == root_);
    menu.addItem(1001, "Remove this folder from the list", removable);
    if (folder_.isDirectory())
        menu.addItem(1002, "Show in Explorer");
    juce::Component::SafePointer<MediaBrowser> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(place_),
                       [safe, all](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice > 0 && static_cast<size_t>(choice) <= all.size())
                               safe->enter(all[static_cast<size_t>(choice - 1)].root,
                                           all[static_cast<size_t>(choice - 1)].root);
                           else if (choice == 1000)
                               safe->addFolder();
                           else if (choice == 1001)
                           {
                               auto folders = safe->folderList();
                               const auto path = safe->root_.getFullPathName();
                               folders.erase(std::remove(folders.begin(), folders.end(), path),
                                             folders.end());
                               safe->saveFolderList(folders);
                               safe->root_ = safe->folder_ = juce::File();
                               safe->refresh();
                           }
                           else if (choice == 1002)
                               safe->folder_.revealToUser();
                       });
}

void MediaBrowser::addFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        "Add a folder to the browser",
        folder_.isDirectory() ? folder_ : juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    juce::Component::SafePointer<MediaBrowser> safe(this);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& chooser)
        {
            const auto folder = chooser.getResult();
            if (safe == nullptr || !folder.isDirectory())
                return;
            auto folders = safe->folderList();
            if (std::find(folders.begin(), folders.end(), folder.getFullPathName()) == folders.end())
                folders.push_back(folder.getFullPathName());
            safe->saveFolderList(folders);
            safe->enter(folder, folder);
        });
}

// ----- previewing -------------------------------------------------------------------------------

void MediaBrowser::preview(const Entry& entry)
{
    if (entry.folder || !onPreview)
        return;
    if (playing_ == entry.file)
    {
        stopPreview(); // a second click stops it
        return;
    }
    const auto error = onPreview(entry.file);
    if (!error.empty())
    {
        message_ = entry.name + ": " + juce::String::fromUTF8(error.c_str());
        playing_ = juce::File();
        repaint();
        return;
    }
    message_.clear();
    playing_ = entry.file;
    playingSeconds_ = entry.seconds;
    playStartedMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
    list_.repaint();
    repaint(footerTray());
}

void MediaBrowser::stopPreview()
{
    if (playing_ != juce::File() && onStopPreview)
        onStopPreview();
    playing_ = juce::File();
    list_.repaint();
    repaint(footerTray());
}

void MediaBrowser::timerCallback()
{
    if (onPump)
        onPump();
    const bool previewing = isPreviewing ? isPreviewing() : false;
    if (!previewing)
    {
        // Ended, or stopped: once the fade is handed over, the timer rests.
        if (playing_ != juce::File())
        {
            playing_ = juce::File();
            repaint(footerTray());
        }
        list_.repaint();
        stopTimer();
        return;
    }
    list_.repaint(); // the playing row's progress
}

// ----- the list ---------------------------------------------------------------------------------

int MediaBrowser::getNumRows()
{
    return static_cast<int>(entries_.size());
}

void MediaBrowser::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || static_cast<size_t>(index) >= entries_.size())
        return;
    const auto& c = theme_.colours;
    const auto& entry = entries_[static_cast<size_t>(index)];
    const bool playing = !entry.folder && entry.file == playing_;
    if (selected)
    {
        g.setColour(c.selectionFill);
        g.fillRect(0, 0, width, height);
    }
    if (playing && playingSeconds_ > 0.0)
    {
        // How far the preview has played, as a band under the row.
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - playStartedMs_) * 0.001;
        const float f = static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / playingSeconds_));
        g.setColour(c.accentSubtle);
        g.fillRect(0.0f, 0.0f, static_cast<float>(width) * f, static_cast<float>(height));
    }
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(theme_.spacing.sm, 0);
    const auto icon = area.removeFromLeft(iconWidth).toFloat().withSizeKeepingCentre(16.0f, 16.0f);
    Icons::draw(g,
                entry.folder ? Icon::folder
                : playing    ? Icon::play
                             : Icon::fileAudio,
                icon,
                playing        ? c.accent
                : entry.folder ? c.textSecondary
                               : c.textTertiary);
    if (!entry.folder)
    {
        // Length and rate at the right, in the value face.
        g.setFont(theme_.value());
        g.setColour(c.textSecondary);
        const auto rate = sampleRateText(entry.sampleRate);
        auto right = area.removeFromRight(118);
        g.setColour(c.textTertiary);
        g.drawText(rate, right.removeFromRight(64), juce::Justification::centredRight);
        g.setColour(c.textSecondary);
        g.drawText(lengthText(entry.seconds), right, juce::Justification::centredRight);
        area.removeFromRight(theme_.spacing.sm);
    }
    g.setFont(theme_.body());
    g.setColour(playing ? c.accent : c.textPrimary);
    const int nameW =
        std::min(area.getWidth(),
                 juce::GlyphArrangement::getStringWidthInt(theme_.body(), entry.name) + theme_.spacing.sm);
    g.drawText(entry.name, area.removeFromLeft(nameW), juce::Justification::centredLeft, true);
    if (entry.detail.isNotEmpty())
    {
        g.setFont(theme_.small());
        g.setColour(c.textTertiary);
        g.drawText(entry.detail, area, juce::Justification::centredLeft, true);
    }
}

void MediaBrowser::listBoxItemClicked(int index, const juce::MouseEvent& e)
{
    if (index < 0 || static_cast<size_t>(index) >= entries_.size() || e.mods.isPopupMenu())
        return;
    const auto entry = entries_[static_cast<size_t>(index)];
    if (entry.folder)
        enter(entry.file, root_);
    else
        preview(entry);
}

void MediaBrowser::listBoxItemDoubleClicked(int index, const juce::MouseEvent&)
{
    returnKeyPressed(index);
}

void MediaBrowser::returnKeyPressed(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= entries_.size())
        return;
    const auto entry = entries_[static_cast<size_t>(index)];
    if (entry.folder)
        enter(entry.file, root_);
    else if (onImport)
    {
        stopPreview();
        onImport(entry.file);
    }
}

void MediaBrowser::deleteKeyPressed(int)
{
    goUp(); // Backspace: up a folder (nothing here deletes files)
}

juce::var MediaBrowser::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    if (rows.size() != 1)
        return {};
    const int index = rows[0];
    if (index < 0 || static_cast<size_t>(index) >= entries_.size() ||
        entries_[static_cast<size_t>(index)].folder)
        return {};
    return fileDragDescription(entries_[static_cast<size_t>(index)].file);
}

bool MediaBrowser::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        stopPreview();
        return true;
    }
    if (key == juce::KeyPress::backspaceKey && !search_.hasKeyboardFocus(false))
    {
        goUp();
        return true;
    }
    if (key == juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0))
    {
        search_.grabKeyboardFocus();
        return true;
    }
    return false;
}

// ----- layout -----------------------------------------------------------------------------------

juce::Rectangle<int> MediaBrowser::topTray() const
{
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.sm);
    area.removeFromTop(theme_.sizes.control + theme_.spacing.sm);
    return area.removeFromTop(theme_.sizes.controlCompact + 2 * theme_.spacing.xs);
}

juce::Rectangle<int> MediaBrowser::footerTray() const
{
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.sm);
    return area.removeFromBottom(theme_.sizes.controlCompact + 2 * theme_.spacing.xs);
}

void MediaBrowser::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    drawTray(g, theme_, topTray());
    const auto footer = footerTray();
    drawTray(g, theme_, footer);
    // The footer's caption and the volume in dB beside the slider.
    g.setFont(theme_.caption());
    g.setColour(c.textTertiary);
    const auto gain = volume_.getValue() * volume_.getValue();
    const auto db = db::fromGain(static_cast<float>(gain));
    g.drawText(db <= db::silence ? juce::String::fromUTF8("-\xe2\x88\x9e dB") : juce::String(db, 1) + " dB",
               volume_.getBounds()
                   .withX(volume_.getRight())
                   .withWidth(footer.getRight() - volume_.getRight() - theme_.spacing.sm),
               juce::Justification::centredRight);
    // An empty or failed listing says so in the list's place.
    if (entries_.empty() || message_.isNotEmpty())
    {
        auto r = list_.getBounds();
        if (!entries_.empty())
            r = r.removeFromBottom(rowHeight); // a failed preview, under the rows
        g.setFont(theme_.small());
        g.setColour(c.textTertiary);
        g.drawText(listing_ && entries_.empty() ? juce::String::fromUTF8("Reading\xe2\x80\xa6") : message_,
                   r.reduced(theme_.spacing.sm),
                   entries_.empty() ? juce::Justification::centredTop : juce::Justification::centredLeft,
                   true);
    }
}

void MediaBrowser::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.sm);
    search_.setBounds(area.removeFromTop(theme_.sizes.control));
    area.removeFromTop(theme_.spacing.sm);
    auto tray =
        area.removeFromTop(theme_.sizes.controlCompact + 2 * theme_.spacing.xs).reduced(theme_.spacing.xs);
    add_.setBounds(tray.removeFromRight(theme_.sizes.controlCompact));
    tray.removeFromRight(theme_.spacing.xxs);
    up_.setBounds(tray.removeFromRight(theme_.sizes.controlCompact));
    tray.removeFromRight(theme_.spacing.xxs);
    place_.setBounds(tray);
    area.removeFromTop(theme_.spacing.sm);
    auto footer =
        area.removeFromBottom(theme_.sizes.controlCompact + 2 * theme_.spacing.xs).reduced(theme_.spacing.xs);
    stop_.setBounds(footer.removeFromLeft(theme_.sizes.controlCompact));
    footer.removeFromLeft(theme_.spacing.sm);
    footer.removeFromRight(56); // the dB readout
    volume_.setBounds(footer);
    area.removeFromBottom(theme_.spacing.sm);
    list_.setBounds(area);
}

// ----- the panel's tabs -------------------------------------------------------------------------

BrowserPanel::BrowserPanel(PluginBrowser& plugins, MediaBrowser& files)
    : plugins_(plugins), files_(files), tabs_(theme_)
{
    setOpaque(true);
    tabs_.setSegments({"Plugins", "Files"});
    tabs_.setSelected(0, juce::dontSendNotification);
    tabs_.onChange = [this](int tab)
    {
        showTab(tab);
        if (onTabChanged)
            onTabChanged(tab == 1);
    };
    addAndMakeVisible(tabs_);
    addAndMakeVisible(plugins_);
    addChildComponent(files_);
}

void BrowserPanel::showTab(int tab)
{
    plugins_.setVisible(tab == 0);
    files_.setVisible(tab == 1);
    if (tab != 1)
        files_.stopPreview();
    else
        files_.refresh();
}

void BrowserPanel::showPlugins()
{
    tabs_.setSelected(0, juce::dontSendNotification);
    showTab(0);
}

void BrowserPanel::showFiles()
{
    tabs_.setSelected(1, juce::dontSendNotification);
    showTab(1);
}

void BrowserPanel::focusSearch()
{
    if (showingFiles())
        files_.focusSearch();
    else
        plugins_.focusSearch();
}

void BrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.surface0);
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds();
    auto top =
        area.removeFromTop(theme_.sizes.controlCompact + theme_.spacing.sm).reduced(theme_.spacing.md, 0);
    top.removeFromTop(theme_.spacing.sm);
    tabs_.setBounds(top.withWidth(std::min(top.getWidth(), tabs_.preferredWidth())));
    plugins_.setBounds(area);
    files_.setBounds(area);
}

} // namespace northlight::ui
