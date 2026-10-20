#pragma once

#include "hosting/PluginRegistry.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aural::ui
{

// The plugin browser (07-plugins.md 7.4): a search field with format and
// category filters, then sections (Favourites, Recent, Effects, Instruments,
// Blocked) of 28 px rows: name, manufacturer, format badge and a star. Enter or
// a double-click inserts the highlighted plugin on the selected track; a row
// drags onto the insert chain or a track header ("plugin:<type>"). Blocked rows
// carry the reason and a Retry. The scan button has a chevron menu; while a
// scan runs a progress bar with the current file and Cancel replaces it. The
// panel reads the registry it is given and asks the owner, through the
// callbacks, to scan, cancel, retry or clear the blocklist; refresh() re-reads.
class PluginBrowser final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PluginBrowser(const hosting::PluginRegistry& registry);

    std::function<void(bool all)> onScan;
    std::function<void()> onCancelScan;
    std::function<void()> onClearBlocklist;
    std::function<void(const std::string& path)> onRetry;
    // Insert the plugin type on the selected track (Enter, double-click).
    std::function<void(const std::string& type)> onInsert;
    // Per-user values: favourites and recent plugin types; may be null.
    std::function<std::string(const std::string& key)> loadSetting;
    std::function<void(const std::string& key, const std::string& value)> saveSetting;
    // The scan's progress (done, total) while one runs; may be null.
    std::function<std::pair<int, int>()> scanProgress;

    // Shown while a scan runs (empty otherwise).
    void setStatus(const std::string& status);
    void refresh();
    // Remembers a type as recently used (the owner calls it on any insert).
    void rememberRecent(const std::string& type);
    void focusSearch() { search_.grabKeyboardFocus(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int preferredWidth = 720;
    static constexpr int preferredHeight = 560;

private:
    struct Row
    {
        enum class Kind
        {
            header,
            plugin,
            blocked,
        };
        Kind kind = Kind::plugin;
        juce::String title; // the section title (header rows)
        std::string type;   // the insert type (plugin rows)
        juce::String name, manufacturer, format, reason;
        std::string path; // the file (blocked rows)
        bool instrument = false;
        bool favourite = false;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    void returnKeyPressed(int row) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    void applyFilter();
    void insertRow(int row);
    void toggleFavourite(const std::string& type);
    std::vector<std::string> listSetting(const std::string& key) const;
    void saveList(const std::string& key, const std::vector<std::string>& values);
    void showScanMenu();
    bool rowMatches(const Row& row) const;

    const Theme& theme_ = Theme::dark();
    const hosting::PluginRegistry& registry_;
    std::vector<Row> plugins_, blocked_, rows_;
    std::vector<std::string> favourites_, recent_;
    juce::TextEditor search_;
    SegmentedControl format_, category_;
    juce::ListBox list_{"plugins", this};
    juce::TextButton scan_{"Scan for changes"}, cancel_{"Cancel"};
    double progress_ = 0.0;
    juce::ProgressBar progressBar_{progress_};
    juce::String status_;
    bool scanning_ = false;
};

} // namespace aural::ui
