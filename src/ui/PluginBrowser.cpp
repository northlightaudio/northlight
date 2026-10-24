#include "ui/PluginBrowser.h"

#include "plugins/BuiltIns.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"

#include <algorithm>

namespace northlight::ui
{

namespace
{
constexpr int rowHeight = 28;
constexpr int starWidth = 24;
constexpr int retryWidth = 56;

bool containsIgnoreCase(const juce::String& text, const juce::String& needle)
{
    return needle.isEmpty() || text.containsIgnoreCase(needle);
}
} // namespace

PluginBrowser::PluginBrowser(const hosting::PluginRegistry& registry)
    : registry_(registry), format_(theme_), category_(theme_)
{
    setOpaque(true);
    search_.setTextToShowWhenEmpty("Search plugins", theme_.colours.textTertiary);
    search_.setFont(theme_.body());
    search_.onTextChange = [this] { applyFilter(); };
    search_.onReturnKey = [this]
    {
        // Enter in the field inserts the first (or the highlighted) plugin.
        const int row = list_.getSelectedRow() >= 0 ? list_.getSelectedRow() : 0;
        for (int i = row; i < static_cast<int>(rows_.size()); ++i)
            if (rows_[static_cast<size_t>(i)].kind == Row::Kind::plugin)
            {
                insertRow(i);
                return;
            }
    };
    search_.onEscapeKey = [this] { search_.clear(); };
    addAndMakeVisible(search_);

    format_.setSegments({"All", "VST3", "CLAP", "Built-in"});
    format_.setSelected(0, juce::dontSendNotification);
    format_.onChange = [this](int) { applyFilter(); };
    addAndMakeVisible(format_);
    category_.setSegments({"All", "Effects", "Instruments"});
    category_.setSelected(0, juce::dontSendNotification);
    category_.onChange = [this](int) { applyFilter(); };
    addAndMakeVisible(category_);

    list_.setRowHeight(rowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, theme_.colours.surface0);
    list_.setMultipleSelectionEnabled(false);
    addAndMakeVisible(list_);

    scan_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    scan_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    scan_.setTooltip("Scan the VST3 and CLAP folders for new or changed plugins");
    scan_.onClick = [this] { showScanMenu(); };
    addAndMakeVisible(scan_);
    cancel_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    cancel_.onClick = [this]
    {
        if (onCancelScan)
            onCancelScan();
    };
    addChildComponent(cancel_);
    addChildComponent(progressBar_);
    progressBar_.setPercentageDisplay(false);

    setSize(preferredWidth, preferredHeight);
    refresh();
}

std::vector<std::string> PluginBrowser::listSetting(const std::string& key) const
{
    std::vector<std::string> values;
    if (!loadSetting)
        return values;
    for (const auto& item :
         juce::StringArray::fromTokens(juce::String::fromUTF8(loadSetting(key).c_str()), "\t", ""))
        if (item.isNotEmpty())
            values.push_back(item.toStdString());
    return values;
}

void PluginBrowser::saveList(const std::string& key, const std::vector<std::string>& values)
{
    if (!saveSetting)
        return;
    std::string joined;
    for (const auto& v : values)
        joined += (joined.empty() ? "" : "\t") + v;
    saveSetting(key, joined);
}

void PluginBrowser::setStatus(const std::string& status)
{
    status_ = juce::String::fromUTF8(status.c_str());
    const bool scanning = !status.empty();
    if (scanning && scanProgress)
    {
        const auto [done, total] = scanProgress();
        progress_ = total > 0 ? static_cast<double>(done) / total : -1.0;
        status_ = "Scanning " + juce::String(done) + " of " + juce::String(total);
        const auto file = juce::String::fromUTF8(status.c_str()).fromLastOccurrenceOf(": ", false, false);
        if (file.isNotEmpty() && file != status_)
            status_ << juce::String::fromUTF8("  \xc2\xb7  ") << juce::File(file).getFileName();
    }
    if (scanning != scanning_)
    {
        scanning_ = scanning;
        scan_.setVisible(!scanning);
        cancel_.setVisible(scanning);
        progressBar_.setVisible(scanning);
        resized();
    }
    repaint();
}

void PluginBrowser::refresh()
{
    favourites_ = listSetting("plugins.favourites");
    recent_ = listSetting("plugins.recent");
    plugins_.clear();
    for (const auto& type : plugins::BuiltInFactory().typeNames())
    {
        Row row;
        row.type = type;
        row.name = plugins::BuiltInFactory::displayName(type);
        row.manufacturer = "Built-in";
        row.format = "Built-in";
        row.instrument = plugins::BuiltInFactory::isInstrument(type);
        plugins_.push_back(std::move(row));
    }
    for (const auto& p : registry_.plugins())
    {
        Row row;
        row.type = hosting::PluginRegistry::typeFor(p);
        row.name = juce::String::fromUTF8(p.name.c_str());
        row.manufacturer = juce::String::fromUTF8(p.manufacturer.c_str());
        row.format = juce::String(p.format);
        row.instrument = p.isInstrument;
        row.path = p.path;
        plugins_.push_back(std::move(row));
    }
    for (auto& row : plugins_)
        row.favourite = std::find(favourites_.begin(), favourites_.end(), row.type) != favourites_.end();
    std::sort(plugins_.begin(),
              plugins_.end(),
              [](const Row& a, const Row& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    blocked_.clear();
    for (const auto& [path, reason] : registry_.blocked())
    {
        Row row;
        row.kind = Row::Kind::blocked;
        row.name = juce::File(juce::String::fromUTF8(path.c_str())).getFileName();
        row.reason = juce::String::fromUTF8(reason.c_str());
        row.path = path;
        blocked_.push_back(std::move(row));
    }
    applyFilter();
}

void PluginBrowser::rememberRecent(const std::string& type)
{
    recent_.erase(std::remove(recent_.begin(), recent_.end(), type), recent_.end());
    recent_.insert(recent_.begin(), type);
    if (recent_.size() > 8)
        recent_.resize(8);
    saveList("plugins.recent", recent_);
    applyFilter();
}

bool PluginBrowser::rowMatches(const Row& row) const
{
    const int format = format_.selected();
    if (format == 1 && row.format != "VST3")
        return false;
    if (format == 2 && row.format != "CLAP")
        return false;
    if (format == 3 && row.format != "Built-in")
        return false;
    const int category = category_.selected();
    if (category == 1 && row.instrument)
        return false;
    if (category == 2 && !row.instrument)
        return false;
    const auto needle = search_.getText().trim();
    return containsIgnoreCase(row.name, needle) || containsIgnoreCase(row.manufacturer, needle) ||
           containsIgnoreCase(row.format, needle);
}

void PluginBrowser::applyFilter()
{
    rows_.clear();
    const auto section = [this](const juce::String& title, auto predicate)
    {
        std::vector<Row> matches;
        for (const auto& row : plugins_)
            if (predicate(row) && rowMatches(row))
                matches.push_back(row);
        if (matches.empty())
            return;
        Row header;
        header.kind = Row::Kind::header;
        header.title = title + " (" + juce::String(matches.size()) + ")";
        rows_.push_back(std::move(header));
        for (auto& m : matches)
            rows_.push_back(std::move(m));
    };
    section("Favourites", [](const Row& r) { return r.favourite; });
    if (search_.getText().trim().isEmpty())
    {
        std::vector<Row> recent;
        for (const auto& type : recent_)
            for (const auto& row : plugins_)
                if (row.type == type && rowMatches(row))
                    recent.push_back(row);
        if (!recent.empty())
        {
            Row header;
            header.kind = Row::Kind::header;
            header.title = "Recent";
            rows_.push_back(std::move(header));
            for (auto& r : recent)
                rows_.push_back(std::move(r));
        }
    }
    section("Effects", [](const Row& r) { return !r.instrument; });
    section("Instruments", [](const Row& r) { return r.instrument; });
    if (!blocked_.empty())
    {
        Row header;
        header.kind = Row::Kind::header;
        header.title = "Blocked (" + juce::String(blocked_.size()) + ")";
        rows_.push_back(std::move(header));
        for (const auto& b : blocked_)
            rows_.push_back(b);
    }
    list_.updateContent();
    list_.repaint();
    repaint();
}

int PluginBrowser::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void PluginBrowser::paintListBoxItem(int index, juce::Graphics& g, int width, int height, bool selected)
{
    if (index < 0 || static_cast<size_t>(index) >= rows_.size())
        return;
    const auto& c = theme_.colours;
    const auto& row = rows_[static_cast<size_t>(index)];
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(theme_.spacing.sm, 0);
    if (row.kind == Row::Kind::header)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText(row.title.toUpperCase(), area, juce::Justification::bottomLeft);
        return;
    }
    if (selected)
    {
        g.setColour(c.selectionFill);
        g.fillRect(0, 0, width, height);
    }
    if (row.kind == Row::Kind::blocked)
    {
        Icons::draw(
            g, Icon::warning, area.removeFromLeft(starWidth).toFloat().reduced(4.0f, 6.0f), c.warning);
        g.setFont(theme_.small());
        g.setColour(c.accent);
        g.drawText("Retry", area.removeFromRight(retryWidth), juce::Justification::centred);
        g.setColour(c.textSecondary);
        g.setFont(theme_.body());
        const int nameW =
            std::min(area.getWidth() / 2,
                     juce::GlyphArrangement::getStringWidthInt(theme_.body(), row.name) + theme_.spacing.sm);
        g.drawText(row.name, area.removeFromLeft(nameW), juce::Justification::centredLeft, true);
        g.setColour(c.warning);
        g.setFont(theme_.small());
        g.drawText(row.reason, area, juce::Justification::centredLeft, true);
        return;
    }
    // The star, the name, the manufacturer and the format badge.
    const auto star = area.removeFromLeft(starWidth);
    g.setColour(row.favourite ? c.warning : c.textTertiary.withAlpha(0.6f));
    g.setFont(theme_.body());
    g.drawText(juce::String::fromUTF8(row.favourite ? "\xe2\x98\x85" : "\xe2\x98\x86"),
               star,
               juce::Justification::centred);
    g.setFont(theme_.captionCaps());
    const int badgeW =
        juce::GlyphArrangement::getStringWidthInt(theme_.captionCaps(), row.format) + 2 * theme_.spacing.sm;
    auto badge = area.removeFromRight(badgeW)
                     .withSizeKeepingCentre(badgeW, theme_.sizes.icon + theme_.spacing.xxs)
                     .toFloat();
    g.setColour(c.accent.withAlpha(0.2f));
    g.fillRoundedRectangle(badge, badge.getHeight() * 0.5f);
    g.setColour(c.accent);
    g.drawText(row.format, badge.toNearestInt(), juce::Justification::centred);
    area.removeFromRight(theme_.spacing.sm);
    g.setFont(theme_.body());
    g.setColour(c.textPrimary);
    // The name takes up to three fifths of the row, and the maker always starts a
    // gap after it, even where the name ends in an ellipsis.
    const int nameW = std::min(area.getWidth() * 3 / 5,
                               juce::GlyphArrangement::getStringWidthInt(theme_.body(), row.name) + 1);
    g.drawText(row.name, area.removeFromLeft(nameW), juce::Justification::centredLeft, true);
    area.removeFromLeft(theme_.spacing.sm);
    g.setFont(theme_.small());
    g.setColour(c.textSecondary);
    g.drawText(row.manufacturer, area, juce::Justification::centredLeft, true);
}

void PluginBrowser::listBoxItemClicked(int index, const juce::MouseEvent& e)
{
    if (index < 0 || static_cast<size_t>(index) >= rows_.size())
        return;
    const auto& row = rows_[static_cast<size_t>(index)];
    if (row.kind == Row::Kind::plugin && e.x < theme_.spacing.sm + starWidth)
        toggleFavourite(row.type);
    else if (row.kind == Row::Kind::blocked && e.x > list_.getWidth() - theme_.spacing.sm - retryWidth &&
             onRetry)
        onRetry(row.path);
}

void PluginBrowser::listBoxItemDoubleClicked(int index, const juce::MouseEvent&)
{
    insertRow(index);
}

void PluginBrowser::returnKeyPressed(int index)
{
    insertRow(index);
}

juce::var PluginBrowser::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    if (rows.size() != 1)
        return {};
    const int index = rows[0];
    if (index < 0 || static_cast<size_t>(index) >= rows_.size() ||
        rows_[static_cast<size_t>(index)].kind != Row::Kind::plugin)
        return {};
    return "plugin:" + juce::String(rows_[static_cast<size_t>(index)].type);
}

void PluginBrowser::insertRow(int index)
{
    if (index < 0 || static_cast<size_t>(index) >= rows_.size() || !onInsert)
        return;
    const auto& row = rows_[static_cast<size_t>(index)];
    if (row.kind == Row::Kind::plugin)
        onInsert(row.type);
}

void PluginBrowser::toggleFavourite(const std::string& type)
{
    const auto it = std::find(favourites_.begin(), favourites_.end(), type);
    if (it == favourites_.end())
        favourites_.push_back(type);
    else
        favourites_.erase(it);
    saveList("plugins.favourites", favourites_);
    for (auto& row : plugins_)
        row.favourite = std::find(favourites_.begin(), favourites_.end(), row.type) != favourites_.end();
    applyFilter();
}

void PluginBrowser::showScanMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Scan for changes");
    menu.addItem(2, "Rescan all");
    menu.addSeparator();
    menu.addItem(3, "Clear blocklist", !registry_.blocked().empty());
    juce::Component::SafePointer<PluginBrowser> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(scan_),
                       [safe](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           if ((choice == 1 || choice == 2) && safe->onScan)
                               safe->onScan(choice == 2);
                           if (choice == 3 && safe->onClearBlocklist)
                               safe->onClearBlocklist();
                       });
}

bool PluginBrowser::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0))
    {
        search_.grabKeyboardFocus();
        return true;
    }
    return false;
}

void PluginBrowser::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    // The status line at the bottom: the scan text, or the count.
    auto bottom =
        getLocalBounds().removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
    g.setFont(theme_.caption());
    g.setColour(scanning_ ? c.textSecondary : c.textTertiary);
    if (scanning_)
        g.drawText(status_,
                   bottom.withTrimmedLeft(progressBar_.getRight() + theme_.spacing.sm - theme_.spacing.md),
                   juce::Justification::centredLeft,
                   true);
    else
        g.drawText(juce::String(registry_.plugins().size()) + " plugins" +
                       (registry_.blocked().empty()
                            ? juce::String()
                            : ", " + juce::String(registry_.blocked().size()) + " blocked"),
                   bottom,
                   juce::Justification::centredRight);
}

void PluginBrowser::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.sm);
    auto top = area.removeFromTop(theme_.sizes.control);
    search_.setBounds(top);
    area.removeFromTop(theme_.spacing.sm);
    auto filters = area.removeFromTop(theme_.sizes.controlCompact);
    const bool narrow =
        filters.getWidth() < format_.preferredWidth() + category_.preferredWidth() + theme_.spacing.sm;
    format_.setBounds(filters.removeFromLeft(std::min(filters.getWidth(), format_.preferredWidth())));
    if (narrow)
    {
        area.removeFromTop(theme_.spacing.xs);
        filters = area.removeFromTop(theme_.sizes.controlCompact);
    }
    else
        filters.removeFromLeft(theme_.spacing.sm);
    category_.setBounds(filters.removeFromLeft(std::min(filters.getWidth(), category_.preferredWidth())));
    area.removeFromTop(theme_.spacing.sm);
    auto bottom = area.removeFromBottom(theme_.sizes.controlCompact);
    if (scanning_)
    {
        cancel_.setBounds(bottom.removeFromRight(64));
        bottom.removeFromRight(theme_.spacing.sm);
        progressBar_.setBounds(bottom.removeFromLeft(std::min(160, bottom.getWidth() / 2))
                                   .withSizeKeepingCentre(std::min(160, bottom.getWidth() / 2), 6));
    }
    else
        scan_.setBounds(bottom.removeFromLeft(std::min(bottom.getWidth(), 150)));
    area.removeFromBottom(theme_.spacing.xs);
    list_.setBounds(area);
}

} // namespace northlight::ui
