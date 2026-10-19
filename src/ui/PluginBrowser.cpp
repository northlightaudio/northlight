#include "ui/PluginBrowser.h"

#include <algorithm>

namespace aural::ui
{

namespace
{
enum Column
{
    nameColumn = 1,
    manufacturerColumn,
    categoryColumn,
    versionColumn,
    pathColumn,
};

bool containsIgnoreCase(const std::string& text, const juce::String& needle)
{
    return needle.isEmpty() || juce::String::fromUTF8(text.c_str()).containsIgnoreCase(needle);
}
} // namespace

PluginBrowser::PluginBrowser(const hosting::PluginRegistry& registry) : registry_(registry)
{
    setOpaque(true);
    search_.setTextToShowWhenEmpty("Search", theme_.colours.textSecondary);
    search_.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(search_);

    auto& header = table_.getHeader();
    header.addColumn("Name", nameColumn, 200, 80, 400);
    header.addColumn("Manufacturer", manufacturerColumn, 140, 60, 300);
    header.addColumn("Category", categoryColumn, 110, 60, 200);
    header.addColumn("Version", versionColumn, 70, 50, 120);
    header.addColumn("File", pathColumn, 220, 100, 800);
    header.setStretchToFitActive(true);
    table_.setRowHeight(22);
    table_.setColour(juce::ListBox::backgroundColourId, theme_.colours.surface);
    addAndMakeVisible(table_);

    scanButton_.onClick = [this]
    {
        if (onScan)
            onScan(false);
    };
    rescanButton_.onClick = [this]
    {
        if (onScan)
            onScan(true);
    };
    clearButton_.onClick = [this]
    {
        if (onClearBlocklist)
            onClearBlocklist();
    };
    addAndMakeVisible(scanButton_);
    addAndMakeVisible(rescanButton_);
    addAndMakeVisible(clearButton_);

    status_.setColour(juce::Label::textColourId, theme_.colours.warning);
    status_.setFont(juce::FontOptions{theme_.type.caption});
    addAndMakeVisible(status_);
    summary_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    summary_.setFont(juce::FontOptions{theme_.type.caption});
    summary_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(summary_);

    setSize(preferredWidth, preferredHeight);
    refresh();
}

void PluginBrowser::setStatus(const std::string& status)
{
    status_.setText(juce::String::fromUTF8(status.c_str()), juce::dontSendNotification);
    scanButton_.setEnabled(status.empty());
    rescanButton_.setEnabled(status.empty());
}

void PluginBrowser::refresh()
{
    all_.clear();
    for (const auto& p : registry_.plugins())
        all_.push_back(Row{p.name, p.manufacturer, p.category, p.version, p.path, false});
    for (const auto& [path, reason] : registry_.blocked())
    {
        const auto file = juce::File(juce::String::fromUTF8(path.c_str())).getFileNameWithoutExtension();
        all_.push_back(Row{file.toStdString(), "", "blocked: " + reason, "", path, true});
    }
    summary_.setText(juce::String(registry_.plugins().size()) + " plugin(s), " +
                         juce::String(registry_.blocked().size()) + " blocked",
                     juce::dontSendNotification);
    clearButton_.setEnabled(!registry_.blocked().empty());
    applyFilter();
}

void PluginBrowser::applyFilter()
{
    const auto needle = search_.getText().trim();
    rows_.clear();
    for (const auto& row : all_)
        if (containsIgnoreCase(row.name, needle) || containsIgnoreCase(row.manufacturer, needle) ||
            containsIgnoreCase(row.category, needle))
            rows_.push_back(row);
    table_.updateContent();
    table_.repaint();
}

int PluginBrowser::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void PluginBrowser::paintRowBackground(juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll(selected       ? theme_.colours.accent.withAlpha(0.35f)
              : row % 2 == 0 ? theme_.colours.surface
                             : theme_.colours.surface.brighter(0.04f));
}

void PluginBrowser::paintCell(juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || static_cast<size_t>(row) >= rows_.size())
        return;
    const auto& r = rows_[static_cast<size_t>(row)];
    const std::string* text = &r.name;
    switch (column)
    {
        case manufacturerColumn:
            text = &r.manufacturer;
            break;
        case categoryColumn:
            text = &r.category;
            break;
        case versionColumn:
            text = &r.version;
            break;
        case pathColumn:
            text = &r.path;
            break;
        default:
            break;
    }
    g.setColour(r.blocked ? theme_.colours.danger : theme_.colours.textPrimary);
    g.setFont(juce::FontOptions{theme_.type.body});
    g.drawText(juce::String::fromUTF8(text->c_str()),
               4,
               0,
               width - 8,
               height,
               juce::Justification::centredLeft,
               true);
}

void PluginBrowser::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.background);
}

void PluginBrowser::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.md);
    auto top = area.removeFromTop(theme_.sizes.controlHeight);
    clearButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(theme_.spacing.sm);
    rescanButton_.setBounds(top.removeFromRight(100));
    top.removeFromRight(theme_.spacing.sm);
    scanButton_.setBounds(top.removeFromRight(130));
    top.removeFromRight(theme_.spacing.sm);
    search_.setBounds(top);
    area.removeFromTop(theme_.spacing.sm);
    auto bottom = area.removeFromBottom(20);
    summary_.setBounds(bottom.removeFromRight(220));
    status_.setBounds(bottom);
    area.removeFromBottom(theme_.spacing.xs);
    table_.setBounds(area);
}

} // namespace aural::ui
