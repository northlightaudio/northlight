#pragma once

#include "hosting/PluginRegistry.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace aural::ui
{

// The plugin browser: everything the scanner knows, searchable, with the
// blocklist and the scan controls. It reads the registry it is given and asks
// the owner (through the callbacks) to scan or clear the blocklist; refresh()
// re-reads after a change.
class PluginBrowser final : public juce::Component, private juce::TableListBoxModel
{
public:
    explicit PluginBrowser(const hosting::PluginRegistry& registry);

    std::function<void(bool all)> onScan;
    std::function<void()> onClearBlocklist;
    // Shown while a scan runs (empty otherwise).
    void setStatus(const std::string& status);
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 760;
    static constexpr int preferredHeight = 460;

private:
    struct Row
    {
        std::string name, manufacturer, category, version, path;
        bool blocked = false;
    };

    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell(juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void applyFilter();

    const Theme& theme_ = Theme::dark();
    const hosting::PluginRegistry& registry_;
    std::vector<Row> all_, rows_;
    juce::TextEditor search_;
    juce::TableListBox table_{"plugins", this};
    juce::TextButton scanButton_{"Scan for changes"}, rescanButton_{"Rescan all"},
        clearButton_{"Clear blocklist"};
    juce::Label status_, summary_;
};

} // namespace aural::ui
