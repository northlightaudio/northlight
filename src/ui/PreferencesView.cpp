#include "ui/PreferencesView.h"

#include "ui/AuralLookAndFeel.h"
#include "ui/components/NumericField.h"

#include <algorithm>

namespace aural::ui
{

namespace
{
constexpr int labelWidth = 200;
constexpr int rowHeight = 28;

// The keyboard map (10-interaction.md 10.4 as implemented), for the table.
struct Shortcut
{
    const char* action;
    const char* keys;
    const char* group;
};
const Shortcut shortcuts[] = {
    {"Play / stop", "Space", "Transport"},
    {"Pause", "Shift+Space", "Transport"},
    {"Record", "R", "Transport"},
    {"Arm selected track", "Shift+R", "Transport"},
    {"Return to start", "Return, Home", "Transport"},
    {"Go to end", "Shift+Return, End", "Transport"},
    {"Loop", "L", "Transport"},
    {"Punch", "P", "Transport"},
    {"Click", "C", "Transport"},
    {"Zoom in / out", "Ctrl+= / Ctrl+-", "View"},
    {"Zoom to fit / to selection", "Z / Shift+Z", "View"},
    {"Snap / ripple", "N / Shift+N", "View"},
    {"Mixer, Editor, Browser, Inspector, Piano Roll, Diagnostics", "F2 to F7", "View"},
    {"Detach or reattach a panel", "Shift+F2 to Shift+F7", "View"},
    {"Mute / solo selected track", "Shift+M / Shift+S", "Tracks"},
    {"Add audio / MIDI track, bus", "Ctrl+T / Ctrl+Shift+T, Ctrl+B", "Tracks"},
    {"Delete selection", "Delete, Backspace", "Editing"},
    {"Duplicate", "Ctrl+D", "Editing"},
    {"Split at playhead / at selection", "S / Alt+S", "Editing"},
    {"Join, crossfade", "J, X", "Editing"},
    {"Normalise", "Ctrl+Shift+N", "Editing"},
    {"Nudge", "Left / Right", "Editing"},
    {"Select all / clear", "Ctrl+A / Esc", "Editing"},
    {"Marker / region, previous / next", "M / Alt+M, comma / full stop", "Editing"},
    {"New, open, save, save as", "Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S", "Files"},
    {"Import audio, export", "Ctrl+I, Ctrl+E", "Files"},
    {"Undo / redo", "Ctrl+Z / Ctrl+Y", "Files"},
    {"Audio Setup, Plugins, Preferences", "Ctrl+Shift+A, Ctrl+Shift+P, Ctrl+,", "App"},
};
} // namespace

// ----- a page ----------------------------------------------------------------------------------

class PreferencesView::Page final : public juce::Component
{
public:
    Page(const Theme& theme, const juce::String& title) : theme_(theme), title_(title) {}

    void addSection(const juce::String& title)
    {
        Row row;
        row.section = title;
        rows_.push_back(std::move(row));
    }
    // A labelled control; `height` for taller controls; `hint` under the control.
    template <typename T>
    T& add(const juce::String& label,
           std::unique_ptr<T> control,
           const juce::String& hint = {},
           int height = rowHeight)
    {
        T& ref = *control;
        Row row;
        row.label = label;
        row.hint = hint;
        row.height = height;
        row.control = std::move(control);
        addAndMakeVisible(*row.control);
        rows_.push_back(std::move(row));
        return ref;
    }

    int preferredHeight() const
    {
        int h = theme_.spacing.xl + static_cast<int>(theme_.fonts.size.title * 2) + theme_.spacing.md;
        for (const auto& row : rows_)
            h += row.section.isNotEmpty()
                     ? theme_.spacing.lg + theme_.sizes.controlCompact
                     : row.height + (row.hint.isNotEmpty() ? theme_.sizes.controlCompact - 6 : 0) +
                           theme_.spacing.md;
        return h + theme_.spacing.xl;
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        auto area = getLocalBounds().reduced(theme_.spacing.xl, theme_.spacing.xl);
        g.setFont(theme_.title());
        g.setColour(c.textPrimary);
        g.drawText(title_,
                   area.removeFromTop(static_cast<int>(theme_.fonts.size.title * 2)),
                   juce::Justification::centredLeft);
        area.removeFromTop(theme_.spacing.md);
        for (const auto& row : rows_)
        {
            if (row.section.isNotEmpty())
            {
                area.removeFromTop(theme_.spacing.lg);
                g.setFont(theme_.captionCaps());
                g.setColour(c.textTertiary);
                g.drawText(row.section.toUpperCase(),
                           area.removeFromTop(theme_.sizes.controlCompact),
                           juce::Justification::bottomLeft);
                continue;
            }
            auto r = area.removeFromTop(row.height);
            g.setFont(theme_.body());
            g.setColour(c.textSecondary);
            if (row.label.isNotEmpty())
                g.drawText(row.label,
                           r.removeFromLeft(labelWidth).withHeight(rowHeight),
                           juce::Justification::centredLeft,
                           true);
            if (row.hint.isNotEmpty())
            {
                auto hint = area.removeFromTop(theme_.sizes.controlCompact - 6).withTrimmedLeft(labelWidth);
                g.setFont(theme_.caption());
                g.setColour(c.textTertiary);
                g.drawText(row.hint, hint, juce::Justification::centredLeft, true);
            }
            area.removeFromTop(theme_.spacing.md);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.xl, theme_.spacing.xl);
        area.removeFromTop(static_cast<int>(theme_.fonts.size.title * 2) + theme_.spacing.md);
        for (auto& row : rows_)
        {
            if (row.section.isNotEmpty())
            {
                area.removeFromTop(theme_.spacing.lg + theme_.sizes.controlCompact);
                continue;
            }
            auto r = area.removeFromTop(row.height);
            if (row.label.isNotEmpty())
                r.removeFromLeft(labelWidth);
            row.control->setBounds(r);
            if (row.hint.isNotEmpty())
                area.removeFromTop(theme_.sizes.controlCompact - 6);
            area.removeFromTop(theme_.spacing.md);
        }
    }

private:
    struct Row
    {
        juce::String label, hint, section;
        int height = rowHeight;
        std::unique_ptr<juce::Component> control;
    };
    const Theme& theme_;
    juce::String title_;
    std::vector<Row> rows_;
};

// ----- the page list ---------------------------------------------------------------------------

class PreferencesView::PageList final : public juce::Component, private juce::ListBoxModel
{
public:
    PageList(const Theme& theme, std::vector<juce::String> titles) : theme_(theme), titles_(std::move(titles))
    {
        list_.setModel(this);
        list_.setRowHeight(theme_.sizes.controlLarge);
        list_.setColour(juce::ListBox::backgroundColourId, theme_.colours.surface0);
        addAndMakeVisible(list_);
    }
    std::function<void(int)> onSelected;
    void select(int index) { list_.selectRow(index); }
    void resized() override { list_.setBounds(getLocalBounds()); }

private:
    int getNumRows() override { return static_cast<int>(titles_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        const auto& c = theme_.colours;
        if (selected)
        {
            g.setColour(c.selectionFill);
            g.fillRect(0, 0, width, height);
            g.setColour(c.accent);
            g.fillRect(0, 0, 2, height);
        }
        g.setFont(theme_.body());
        g.setColour(selected ? c.textPrimary : c.textSecondary);
        g.drawText(titles_[static_cast<size_t>(row)],
                   theme_.spacing.lg,
                   0,
                   width - theme_.spacing.lg,
                   height,
                   juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override
    {
        if (row >= 0 && onSelected)
            onSelected(row);
    }
    const Theme& theme_;
    std::vector<juce::String> titles_;
    juce::ListBox list_;
};

// ----- accent swatches -------------------------------------------------------------------------

class PreferencesView::Swatches final : public juce::Component
{
public:
    explicit Swatches(const Theme& theme) : theme_(theme)
    {
        hex_.setFont(theme_.value());
        hex_.setInputRestrictions(7, "#0123456789abcdefABCDEF");
        hex_.setTextToShowWhenEmpty("#RRGGBB", theme_.colours.textTertiary);
        hex_.onReturnKey = [this]
        {
            if (onChanged)
                onChanged(hex_.getText().trim());
        };
        hex_.onFocusLost = hex_.onReturnKey;
        addAndMakeVisible(hex_);
    }
    std::function<void(const juce::String& hex)> onChanged;
    void setValue(const juce::String& hex)
    {
        value_ = hex;
        hex_.setText(hex, false);
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto area = getLocalBounds();
        for (size_t i = 0; i < presets_.size(); ++i)
        {
            const auto r =
                area.removeFromLeft(swatch).withSizeKeepingCentre(swatch - 6, swatch - 6).toFloat();
            g.setColour(presets_[i]);
            g.fillRoundedRectangle(r, theme_.radius.sm);
            const bool chosen = value_.isEmpty()
                                    ? i == 0
                                    : juce::Colour::fromString("ff" + value_.substring(1)) == presets_[i];
            if (chosen)
            {
                g.setColour(c.textPrimary);
                g.drawRoundedRectangle(r.expanded(2.0f), theme_.radius.sm + 1.0f, 1.5f);
            }
        }
    }
    void resized() override
    {
        hex_.setBounds(getLocalBounds()
                           .withTrimmedLeft(static_cast<int>(presets_.size()) * swatch + theme_.spacing.sm)
                           .withWidth(90));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int index = e.x / swatch;
        if (index >= 0 && static_cast<size_t>(index) < presets_.size() && onChanged)
            onChanged(index == 0 ? juce::String()
                                 : "#" + presets_[static_cast<size_t>(index)].toDisplayString(false));
    }

private:
    static constexpr int swatch = 28;
    const Theme& theme_;
    juce::String value_;
    juce::TextEditor hex_;
    // The default accent first, then five presets.
    const std::vector<juce::Colour> presets_{theme_.colours.accent,
                                             juce::Colour::fromString("ff5fc96f"),
                                             juce::Colour::fromString("ffe8a84f"),
                                             juce::Colour::fromString("ffd65f9e"),
                                             juce::Colour::fromString("ff9b6fe8"),
                                             juce::Colour::fromString("ff4fc9c9")};
};

// ----- the shortcuts table ---------------------------------------------------------------------

class PreferencesView::Shortcuts final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Shortcuts(const Theme& theme) : theme_(theme)
    {
        search_.setTextToShowWhenEmpty("Search actions", theme_.colours.textTertiary);
        search_.setFont(theme_.body());
        search_.onTextChange = [this] { filter(); };
        addAndMakeVisible(search_);
        list_.setModel(this);
        list_.setRowHeight(rowHeight);
        list_.setColour(juce::ListBox::backgroundColourId, theme_.colours.surface1);
        addAndMakeVisible(list_);
        filter();
    }
    void resized() override
    {
        auto area = getLocalBounds();
        search_.setBounds(area.removeFromTop(theme_.sizes.control));
        area.removeFromTop(theme_.spacing.sm);
        list_.setBounds(area);
    }

private:
    void filter()
    {
        rows_.clear();
        const auto needle = search_.getText().trim();
        for (const auto& s : shortcuts)
            if (needle.isEmpty() || juce::String(s.action).containsIgnoreCase(needle) ||
                juce::String(s.keys).containsIgnoreCase(needle) ||
                juce::String(s.group).containsIgnoreCase(needle))
                rows_.push_back(&s);
        list_.updateContent();
        list_.repaint();
    }
    int getNumRows() override { return static_cast<int>(rows_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto& c = theme_.colours;
        const auto* s = rows_[static_cast<size_t>(row)];
        auto area = juce::Rectangle<int>(0, 0, width, height).reduced(theme_.spacing.sm, 0);
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        g.drawText(s->group, area.removeFromLeft(72), juce::Justification::centredLeft);
        g.setFont(theme_.value());
        g.setColour(c.textPrimary);
        g.drawText(s->keys, area.removeFromRight(220), juce::Justification::centredLeft, true);
        g.setFont(theme_.body());
        g.setColour(c.textSecondary);
        g.drawText(s->action, area, juce::Justification::centredLeft, true);
        g.setColour(c.lineSubtle);
        g.fillRect(0, height - 1, width, 1);
    }
    const Theme& theme_;
    juce::TextEditor search_;
    juce::ListBox list_;
    std::vector<const Shortcut*> rows_;
};

// ----- the view ----------------------------------------------------------------------------------

PreferencesView::PreferencesView(const Theme& theme,
                                 audio::AudioDeviceLayer& audio,
                                 const hosting::PluginRegistry& registry)
    : theme_(theme), audio_(audio), registry_(registry)
{
    setOpaque(true);
    content_.setScrollBarsShown(true, false);
    content_.setScrollBarThickness(theme_.sizes.scrollbar);
    addAndMakeVisible(content_);
    setSize(preferredWidth, preferredHeight);
}

PreferencesView::~PreferencesView()
{
    content_.setViewedComponent(nullptr, false);
}

std::string PreferencesView::get(const char* key, const std::string& fallback) const
{
    const auto value = loadSetting ? loadSetting(key) : std::string{};
    return value.empty() ? fallback : value;
}

void PreferencesView::set(const char* key, const std::string& value)
{
    if (updating_)
        return;
    if (saveSetting)
        saveSetting(key, value);
    if (std::string(key) == Keys::contrast)
        writeTokenOverride();
    if (onChanged)
        onChanged();
}

void PreferencesView::writeAccentOverride(const juce::String& hex)
{
    set(Keys::accent, hex.toStdString());
    writeTokenOverride();
    if (onNotice)
        onNotice(hex.isEmpty() ? "Accent reset. Restart Aural to apply."
                               : "Accent set to " + hex + ". Restart Aural to apply.");
}

void PreferencesView::writeTokenOverride()
{
    if (!loadSetting)
        return;
    const juce::File file = juce::File(juce::String::fromUTF8(loadSetting("__settingsFolder").c_str()))
                                .getChildFile("tokens.json");
    auto* colours = new juce::DynamicObject();
    const auto accent = juce::String::fromUTF8(get(Keys::accent).c_str());
    if (accent.length() == 7 && accent.startsWithChar('#'))
    {
        const auto base = juce::Colour::fromString("ff" + accent.substring(1));
        const auto hex = [](juce::Colour colour, int alpha = 255)
        {
            return "#" + colour.withAlpha(static_cast<juce::uint8>(alpha)).toDisplayString(false) +
                   (alpha == 255 ? juce::String() : juce::String::toHexString(alpha).paddedLeft('0', 2));
        };
        colours->setProperty("accent", hex(base));
        colours->setProperty("accentHover", hex(base.brighter(0.15f)));
        colours->setProperty("accentPressed", hex(base.darker(0.15f)));
        colours->setProperty("accentSubtle", hex(base, 0x1f));
        colours->setProperty("lineFocus", hex(base, 0xb3));
    }
    if (get(Keys::contrast, "standard") == "high")
    {
        // Lines at 24%, text one step brighter (11.1).
        colours->setProperty("lineSubtle", "#FFFFFF3D");
        colours->setProperty("lineStrong", "#FFFFFF52");
        colours->setProperty("textSecondary", "#" + theme_.colours.textPrimary.toDisplayString(false));
        colours->setProperty("textTertiary", "#" + theme_.colours.textSecondary.toDisplayString(false));
    }
    if (colours->getProperties().isEmpty())
    {
        delete colours;
        file.deleteFile();
        return;
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("color", juce::var(colours));
    file.replaceWithText(juce::JSON::toString(juce::var(root), false));
}

void PreferencesView::buildPages()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    pages_.clear();
    keys_.clear();
    const auto combo = [this](const std::vector<std::pair<juce::String, std::string>>& items,
                              const char* key,
                              const std::string& fallback)
    {
        auto box = std::make_unique<juce::ComboBox>();
        const auto current = get(key, fallback);
        for (size_t i = 0; i < items.size(); ++i)
        {
            box->addItem(items[i].first, static_cast<int>(i) + 1);
            if (items[i].second == current)
                box->setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        if (box->getSelectedId() == 0)
            box->setSelectedId(1, juce::dontSendNotification);
        auto* raw = box.get();
        box->onChange = [this, raw, items, key]
        {
            const int index = raw->getSelectedId() - 1;
            if (index >= 0 && static_cast<size_t>(index) < items.size())
                set(key, items[static_cast<size_t>(index)].second);
        };
        return box;
    };
    const auto toggle = [this](const juce::String& text, const char* key, bool fallback)
    {
        auto t = std::make_unique<juce::ToggleButton>(text);
        t->setToggleState(get(key, fallback ? "1" : "0") == "1", juce::dontSendNotification);
        auto* raw = t.get();
        t->onClick = [this, raw, key] { set(key, raw->getToggleState() ? "1" : "0"); };
        return t;
    };
    const auto text = [this](const char* key, const std::string& fallback, const juce::String& placeholder)
    {
        auto e = std::make_unique<juce::TextEditor>();
        e->setFont(theme_.body());
        e->setText(juce::String::fromUTF8(get(key, fallback).c_str()), false);
        e->setTextToShowWhenEmpty(placeholder, theme_.colours.textTertiary);
        auto* raw = e.get();
        e->onReturnKey = [this, raw, key] { set(key, raw->getText().trim().toStdString()); };
        e->onFocusLost = e->onReturnKey;
        return e;
    };
    const auto number = [this](const char* key,
                               double fallback,
                               double lo,
                               double hi,
                               double step,
                               const juce::String& unit,
                               int decimals)
    {
        auto f = std::make_unique<NumericField>(theme_);
        f->setRange(lo, hi, step);
        f->setDecimals(decimals);
        f->setUnit(unit);
        f->setValue(juce::String(get(key, std::to_string(fallback))).getDoubleValue(),
                    juce::dontSendNotification);
        f->onValueChanged = [this, key](double v) { set(key, juce::String(v, 2).toStdString()); };
        return f;
    };
    const auto button = [](const juce::String& label, std::function<void()> action)
    {
        auto b = std::make_unique<juce::TextButton>(label);
        b->getProperties().set(AuralLookAndFeel::propertyVariant, "secondary");
        b->onClick = std::move(action);
        return b;
    };

    // General
    {
        auto page = std::make_unique<Page>(theme_, "General");
        page->add("Project folder",
                  text(Keys::projectFolder, {}, "Documents"),
                  "Where new projects are saved and opened from.");
        page->add("Recent files",
                  number(Keys::recentCount, 8, 0, 20, 1, "", 0),
                  "How many recent projects the Project menu keeps.");
        page->add("Autosave",
                  number(Keys::autosaveMinutes, 2, 0, 60, 1, "min", 0),
                  "Unsaved changes are kept every this many minutes, to restore after a crash; 0 is off.");
        page->addSection("Appearance");
        page->add("Theme", combo({{"Dark", "dark"}}, "general.theme", "dark"), "Light theme comes later.");
        auto swatches = std::make_unique<Swatches>(theme_);
        swatches->setValue(juce::String::fromUTF8(get(Keys::accent).c_str()));
        auto* raw = swatches.get();
        swatches->onChanged = [this, raw](const juce::String& hex)
        {
            if (hex.isNotEmpty() && !(hex.length() == 7 && hex.startsWithChar('#')))
                return;
            raw->setValue(hex);
            writeAccentOverride(hex);
        };
        page->add(
            "Accent colour", std::move(swatches), "A preset or a hex value; applied at the next launch.");
        page->add("Contrast",
                  combo({{"Standard", "standard"}, {"High", "high"}}, Keys::contrast, "standard"),
                  "High: lines at 24% and text one step brighter; applied at the next launch.");
        page->add(
            "UI scale",
            combo(
                {{"Follow system", "0"}, {"100%", "100"}, {"125%", "125"}, {"150%", "150"}, {"200%", "200"}},
                Keys::uiScale,
                "0"),
            "Applies at once.");
        pages_.push_back(std::move(page));
        keys_.push_back("general");
    }
    // Audio
    {
        auto page = std::make_unique<Page>(theme_, "Audio");
        auto setup = std::make_unique<AudioSetupView>(audio_);
        setup->onWarning = [this](const juce::String& message)
        {
            if (onWarning)
                onWarning(message);
        };
        page->add("", std::move(setup), {}, AudioSetupView::contentHeight);
        pages_.push_back(std::move(page));
        keys_.push_back("audio");
    }
    // Recording
    {
        auto page = std::make_unique<Page>(theme_, "Recording");
        page->add(
            "File format",
            combo({{"WAV 32-bit float", "float"}, {"WAV 24-bit", "24"}}, Keys::recordingFormat, "float"),
            "Takes are written as 32-bit float today; 24-bit comes with the file-format work.");
        page->add("Count-in",
                  combo({{"Off", "0"}, {"1 bar", "1"}, {"2 bars", "2"}, {"4 bars", "4"}}, Keys::countIn, "1"),
                  "The default when a project opens.");
        page->add("Pre-roll",
                  number(Keys::preRoll, 0, 0, 8, 1, "bars", 0),
                  "Stored; playback before the punch-in point comes later.");
        page->add(
            "Metronome level", number(Keys::metronomeLevel, -6, -40, 0, 0.5, "dB", 1), "Applies at once.");
        page->add("Take name pattern",
                  text(Keys::namePattern, "{track} {take}", "{track} {take}"),
                  "Stored; applied to new takes later.");
        pages_.push_back(std::move(page));
        keys_.push_back("recording");
    }
    // Editing
    {
        auto page = std::make_unique<Page>(theme_, "Editing");
        page->add("Snap", toggle("Snap to grid by default", Keys::snap, true));
        page->add("Snap grid",
                  combo({{"Adaptive", "0"},
                         {"Bar", "1"},
                         {"Beat", "4"},
                         {"1/8", "8"},
                         {"1/16", "16"},
                         {"1/32", "32"}},
                        Keys::snapGrid,
                        "0"));
        page->add("Default fade",
                  number(Keys::fadeMs, 10, 0, 2000, 1, "ms", 0),
                  "Stored; applied to new clips later.");
        page->add("Ripple", toggle("Ripple edits by default", Keys::ripple, false));
        page->add("Follow playhead",
                  toggle("Scroll the arrange view with the playhead", Keys::followPlayhead, true));
        pages_.push_back(std::move(page));
        keys_.push_back("editing");
    }
    // Plugins
    {
        auto page = std::make_unique<Page>(theme_, "Plugins");
        auto paths = std::make_unique<juce::TextEditor>();
        paths->setMultiLine(true, false);
        paths->setReturnKeyStartsNewLine(true);
        paths->setFont(theme_.body());
        paths->setTextToShowWhenEmpty(
            "One folder per line, scanned besides the standard VST3 and CLAP folders",
            theme_.colours.textTertiary);
        paths->setText(juce::String::fromUTF8(get(Keys::pluginPaths).c_str()).replace("\t", "\n"), false);
        auto* rawPaths = paths.get();
        paths->onFocusLost = [this, rawPaths]
        {
            juce::StringArray lines;
            lines.addLines(rawPaths->getText());
            lines.trim();
            lines.removeEmptyStrings();
            set(Keys::pluginPaths, lines.joinIntoString("\t").toStdString());
        };
        page->add("Scan folders",
                  std::move(paths),
                  "Extra folders; the standard locations are always scanned.",
                  3 * rowHeight);
        page->add("Scan on launch", toggle("Scan for changes when Aural starts", Keys::scanOnLaunch, false));
        page->add("Blocklist",
                  button(registry_.blocked().empty()
                             ? "No blocked plugins"
                             : "Clear " + juce::String(registry_.blocked().size()) + " blocked",
                         [this]
                         {
                             if (onClearBlocklist)
                                 onClearBlocklist();
                         }),
                  "Plugins that crashed or hung while scanning are skipped until cleared.");
        page->add("Scan now",
                  button("Scan for changes",
                         [this]
                         {
                             if (onScan)
                                 onScan();
                         }));
        page->add("Windows", toggle("Open plugin windows on top", Keys::pluginsOnTop, false));
        pages_.push_back(std::move(page));
        keys_.push_back("plugins");
    }
    // Shortcuts
    {
        auto page = std::make_unique<Page>(theme_, "Shortcuts");
        page->add("", std::make_unique<Shortcuts>(theme_), {}, 14 * rowHeight);
        pages_.push_back(std::move(page));
        keys_.push_back("shortcuts");
    }
    // Diagnostics
    {
        auto page = std::make_unique<Page>(theme_, "Diagnostics");
        page->add("Crash reports",
                  toggle("Send crash reports (opt in)", Keys::crashReports, false),
                  "A crash always leaves a report in the settings folder's Crashes folder, on this "
                  "computer only; sending them comes with the release work.");
        const auto folder = loadSetting
                                ? juce::File(juce::String::fromUTF8(loadSetting("__settingsFolder").c_str()))
                                : juce::File();
        page->add("Log folder",
                  button(folder.getFullPathName().isEmpty() ? "Open settings folder" : folder.getFileName(),
                         [folder] { folder.revealToUser(); }),
                  folder.getFullPathName());
        page->add("System report",
                  button("Copy system report",
                         [this]
                         {
                             if (systemReport)
                                 juce::SystemClipboard::copyTextToClipboard(systemReport());
                             if (onNotice)
                                 onNotice("System report copied.");
                         }));
        pages_.push_back(std::move(page));
        keys_.push_back("diagnostics");
    }
    list_ = std::make_unique<PageList>(
        theme_,
        std::vector<juce::String>{
            "General", "Audio", "Recording", "Editing", "Plugins", "Shortcuts", "Diagnostics"});
    list_->onSelected = [this](int index)
    {
        current_ = index;
        resized();
    };
    addAndMakeVisible(*list_);
    list_->select(current_);
}

void PreferencesView::showPage(const juce::String& key)
{
    if (pages_.empty())
        buildPages();
    for (size_t i = 0; i < keys_.size(); ++i)
        if (keys_[i] == key)
        {
            current_ = static_cast<int>(i);
            list_->select(current_);
        }
    resized();
}

juce::String PreferencesView::currentPage() const
{
    return current_ >= 0 && static_cast<size_t>(current_) < keys_.size()
               ? keys_[static_cast<size_t>(current_)]
               : juce::String();
}

void PreferencesView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    g.setColour(c.lineSubtle);
    g.fillRect(listWidth - 1, 0, 1, getHeight());
}

void PreferencesView::resized()
{
    if (pages_.empty())
        buildPages();
    auto area = getLocalBounds();
    list_->setBounds(area.removeFromLeft(listWidth));
    content_.setBounds(area);
    if (current_ >= 0 && static_cast<size_t>(current_) < pages_.size())
    {
        auto* page = pages_[static_cast<size_t>(current_)].get();
        const int width = area.getWidth() - theme_.sizes.scrollbar;
        page->setSize(width, std::max(area.getHeight(), page->preferredHeight()));
        if (content_.getViewedComponent() != page)
            content_.setViewedComponent(page, false);
    }
}

} // namespace aural::ui
