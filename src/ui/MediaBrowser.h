#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

class PluginBrowser;

// The browser's Files tab (07-plugins.md 7.4g): audio files in a place (the
// project's folder, Music, or a folder the user added to the list, kept in the
// settings) and its subfolders. A tray at the top picks the place and goes up
// a level or adds a folder; the search field finds files anywhere under the
// place. Rows are 28 px: folders first, then files with their length and rate,
// read off the main thread. A click auditions a file through the engine's
// preview (another click, another file or Escape stops it), a double-click or
// Enter puts it on a new track at the playhead, and a row drags onto the
// arrange view (a new track where it lands) or onto the Sampler's waveform
// ("file:<path>"). The footer tray holds Stop and the preview volume.
class MediaBrowser final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    MediaBrowser();
    ~MediaBrowser() override;

    // Per-user values (the folder list, the last place, the preview volume); may be null.
    std::function<std::string(const std::string& key)> loadSetting;
    std::function<void(const std::string& key, const std::string& value)> saveSetting;
    // The saved project's folder (none while it is unsaved).
    std::function<juce::File()> projectFolder;
    // Audition a file (an error, or empty); stop; whether one plays; hand the
    // next one over (called while previewing); the preview gain (linear).
    std::function<std::string(const juce::File&)> onPreview;
    std::function<void()> onStopPreview;
    std::function<bool()> isPreviewing;
    std::function<void()> onPump;
    std::function<void(float)> onPreviewGain;
    // A file onto a new track at the playhead (double-click, Enter).
    std::function<void(const juce::File&)> onImport;

    // Re-reads the settings and the places (the project may have been saved
    // somewhere new) and lists the current folder again.
    void refresh();
    void focusSearch() { search_.grabKeyboardFocus(); }
    void stopPreview();
    // Shows a folder (a place of its own when it is in none).
    void showFolder(const juce::File& folder);
    // Whether a listing is still being read (tests).
    bool isListing() const noexcept { return listing_; }
    int entryCount() const noexcept { return static_cast<int>(entries_.size()); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    struct Entry
    {
        juce::File file;
        bool folder = false;
        juce::String name, detail; // detail: a search result's folder under the place
        double seconds = -1.0;     // unknown until read
        double sampleRate = 0.0;
        int channels = 0;
    };
    // Lists a folder, or finds files under `root` whose names hold every word of
    // `query`; reads each file's header. `cancelled` is polled (another listing
    // started). Any thread.
    static std::vector<Entry> list(const juce::File& folder,
                                   const juce::File& root,
                                   const juce::String& query,
                                   const std::function<bool()>& cancelled);

private:
    struct Place
    {
        juce::String name;
        juce::File root;
        bool removable = false;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    void returnKeyPressed(int row) override;
    void deleteKeyPressed(int row) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    void timerCallback() override;

    std::vector<Place> places() const;
    std::vector<juce::String> folderList() const;
    void saveFolderList(const std::vector<juce::String>& folders);
    void enter(const juce::File& folder, const juce::File& root);
    void goUp();
    void startListing();
    void showPlaceMenu();
    void addFolder();
    void preview(const Entry& entry);
    void updatePlaceButton();
    juce::Rectangle<int> topTray() const;
    juce::Rectangle<int> footerTray() const;

    const Theme& theme_ = Theme::dark();
    juce::TextEditor search_;
    juce::TextButton place_;
    juce::TextButton up_{juce::String::fromUTF8("\xe2\x86\x91")};
    IconButton add_, stop_;
    juce::Slider volume_;
    juce::ListBox list_{"files", this};
    std::vector<Entry> entries_;
    juce::File root_, folder_;
    juce::File playing_;
    double playingSeconds_ = 0.0;
    double playStartedMs_ = 0.0;
    juce::String message_; // an empty folder, a failed preview
    bool listing_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<std::atomic<int>> generation_ = std::make_shared<std::atomic<int>>(0);
    juce::ThreadPool pool_{1};
};

// The browser panel: Plugins and Files as two tabs (a segmented control at the
// top), one shown at a time.
class BrowserPanel final : public juce::Component
{
public:
    BrowserPanel(PluginBrowser& plugins, MediaBrowser& files);

    void showPlugins();
    void showFiles();
    bool showingFiles() const noexcept { return tabs_.selected() == 1; }
    void focusSearch();
    // The tab chosen, for the settings.
    std::function<void(bool files)> onTabChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void showTab(int tab);

    const Theme& theme_ = Theme::dark();
    PluginBrowser& plugins_;
    MediaBrowser& files_;
    SegmentedControl tabs_;
};

} // namespace aural::ui
