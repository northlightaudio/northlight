#include "ui/PreferencesView.h"

#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/NumericField.h"
#include "ui/components/SegmentedControl.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int comboWidth = 200;
constexpr int numberWidth = 96;
constexpr int textWidth = 200;
constexpr int hexWidth = 96;
constexpr int swatchSize = 20;
constexpr int cardTitleHeight = 22;
constexpr int badgeHeight = 18;
constexpr int keyCapHeight = 18;
const char* const comingSoonTooltip = "Saved now, used in a later version.";

// The keyboard map (interaction.md 10.4 as implemented). Keys are space-separated
// tokens: a chord ("Ctrl+Shift+S", one cap per key) or a joining word ("/" pairs
// with the action's own "/", "or" is an alternative, "to" a range).
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
    {"Return to start", "Return or Home", "Transport"},
    {"Go to end", "Shift+Return or End", "Transport"},
    {"Loop", "L", "Transport"},
    {"Punch", "P", "Transport"},
    {"Click", "C", "Transport"},
    {"Zoom in / out", "Ctrl+= / Ctrl+-", "View"},
    {"Zoom to fit / to selection", "Z / Shift+Z", "View"},
    {"Snap / ripple", "N / Shift+N", "View"},
    {"Mixer, Editor, Browser, Inspector, Piano Roll, Diagnostics", "F2 to F7", "View"},
    {"Detach or reattach a panel", "Shift+F2 to Shift+F7", "View"},
    {"Mute / solo selected track", "Shift+M / Shift+S", "Tracks"},
    {"Add audio / MIDI track", "Ctrl+T / Ctrl+Shift+T", "Tracks"},
    {"Add bus", "Ctrl+B", "Tracks"},
    {"Delete selection", "Delete or Backspace", "Editing"},
    {"Duplicate", "Ctrl+D", "Editing"},
    {"Split at playhead / at selection", "S / Alt+S", "Editing"},
    {"Join", "J", "Editing"},
    {"Crossfade", "X", "Editing"},
    {"Mute / unmute selected clips", "Ctrl+M", "Editing"},
    {"Select, range, pencil, erase, split, glue, mute tool", "1 to 7", "Editing"},
    {"Normalise", "Ctrl+Shift+N", "Editing"},
    {"Nudge left / right", "Left / Right", "Editing"},
    {"Select all", "Ctrl+A", "Editing"},
    {"Clear selection", "Esc", "Editing"},
    {"Add marker / region", "M / Alt+M", "Editing"},
    {"Previous / next marker", ", / .", "Editing"},
    {"New project", "Ctrl+N", "Files"},
    {"Open", "Ctrl+O", "Files"},
    {"Save / save as", "Ctrl+S / Ctrl+Shift+S", "Files"},
    {"Import audio", "Ctrl+I", "Files"},
    {"Export", "Ctrl+E", "Files"},
    {"Undo / redo", "Ctrl+Z / Ctrl+Y", "Files"},
    {"Audio Setup", "Ctrl+Shift+A", "App"},
    {"Plugins", "Ctrl+Shift+P", "App"},
    {"Preferences", "Ctrl+,", "App"},
};

juce::String ellipsis()
{
    return juce::String::fromUTF8("\xe2\x80\xa6");
}

NorthlightLookAndFeel* lookAndFeelOf(juce::Component& component)
{
    return dynamic_cast<NorthlightLookAndFeel*>(&component.getLookAndFeel());
}

// The text with its start replaced by an ellipsis until it fits, so a path keeps
// its folder name.
juce::String elideStart(const juce::Font& font, const juce::String& text, float width)
{
    if (juce::GlyphArrangement::getStringWidth(font, text) <= width)
        return text;
    for (int start = 1; start < text.length(); ++start)
    {
        const auto candidate = ellipsis() + text.substring(start);
        if (juce::GlyphArrangement::getStringWidth(font, candidate) <= width)
            return candidate;
    }
    return ellipsis();
}

juce::TextLayout wrapped(const Theme& theme, const juce::String& text, juce::Colour colour, float width)
{
    juce::AttributedString string;
    string.append(text, theme.small(), colour);
    string.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(string, width);
    return layout;
}

int buttonWidth(const Theme& theme, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidthInt(theme.label(), text) + 2 * theme.spacing.md;
}

std::unique_ptr<juce::TextButton> makeButton(const juce::String& text)
{
    auto button = std::make_unique<juce::TextButton>(text);
    button->getProperties().set(NorthlightLookAndFeel::propertyVariant, "secondary");
    return button;
}

void setUpTextField(const Theme& theme, juce::TextEditor& editor, const juce::Font& font)
{
    editor.setFont(font);
    editor.setJustification(juce::Justification::centredLeft);
    editor.setIndents(theme.spacing.sm, 0);
    editor.setSelectAllWhenFocused(false);
}

// ----- small parts ---------------------------------------------------------------------------

// "Coming soon": a badge (info at 20% with info text) after a setting that is kept
// but not used yet.
class Badge final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Badge(const Theme& theme) : theme_(theme)
    {
        setTooltip(comingSoonTooltip);
        setSize(preferredWidth(), badgeHeight);
    }
    static juce::String text() { return "COMING SOON"; }
    int preferredWidth() const
    {
        return juce::GlyphArrangement::getStringWidthInt(theme_.captionCaps(), text()) +
               2 * theme_.spacing.sm;
    }
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(theme_.colours.info.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
        g.setColour(theme_.colours.info);
        g.setFont(theme_.captionCaps());
        g.drawText(text(), getLocalBounds(), juce::Justification::centred, false);
    }

private:
    const Theme& theme_;
};

// A path field and its Choose button.
class PathField final : public juce::Component
{
public:
    PathField(const Theme& theme, const juce::String& placeholder)
        : theme_(theme), choose_(makeButton("Choose" + ellipsis()))
    {
        setUpTextField(theme_, field_, theme_.body());
        field_.setTextToShowWhenEmpty(placeholder, theme_.colours.textTertiary);
        field_.onReturnKey = [this] { commit(); };
        field_.onFocusLost = [this] { commit(); };
        choose_->onClick = [this]
        {
            if (onChoose)
                onChoose();
        };
        addAndMakeVisible(field_);
        addAndMakeVisible(*choose_);
    }
    std::function<void()> onChoose;
    std::function<void(const juce::String&)> onChanged;
    void setPath(const juce::String& path, bool notify)
    {
        field_.setText(path, false);
        if (notify)
            commit();
    }
    juce::String path() const { return field_.getText().trim(); }
    void resized() override
    {
        auto area = getLocalBounds();
        choose_->setBounds(area.removeFromRight(buttonWidth(theme_, choose_->getButtonText())));
        area.removeFromRight(theme_.spacing.sm);
        field_.setBounds(area);
    }

private:
    void commit()
    {
        if (onChanged)
            onChanged(path());
    }
    const Theme& theme_;
    juce::TextEditor field_;
    std::unique_ptr<juce::TextButton> choose_;
};

// A read-only path (small, ellipsis at the start) and a button after it.
class PathReadout final : public juce::Component
{
public:
    PathReadout(const Theme& theme, juce::String path, const juce::String& buttonText)
        : theme_(theme), path_(std::move(path)), button_(makeButton(buttonText))
    {
        addAndMakeVisible(*button_);
    }
    juce::TextButton& button() noexcept { return *button_; }
    void paint(juce::Graphics& g) override
    {
        g.setFont(theme_.small());
        g.setColour(theme_.colours.textSecondary);
        g.drawText(elideStart(theme_.small(), path_, static_cast<float>(text_.getWidth())),
                   text_,
                   juce::Justification::centredLeft,
                   false);
    }
    void resized() override
    {
        // The button follows the path; a long path gives way (its start elided).
        auto area = getLocalBounds();
        const int button = buttonWidth(theme_, button_->getButtonText());
        const int room = std::max(0, area.getWidth() - button - theme_.spacing.md);
        const int pathWidth =
            static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(theme_.small(), path_)));
        text_ = area.removeFromLeft(std::min(room, pathWidth));
        area.removeFromLeft(theme_.spacing.md);
        button_->setBounds(area.removeFromLeft(button));
    }

private:
    const Theme& theme_;
    juce::String path_;
    std::unique_ptr<juce::TextButton> button_;
    juce::Rectangle<int> text_;
};

// The blocklist: "None" or "3 plugins", then Clear.
class BlocklistRow final : public juce::Component
{
public:
    explicit BlocklistRow(const Theme& theme) : theme_(theme), clear_(makeButton("Clear"))
    {
        clear_->onClick = [this]
        {
            if (onClear)
                onClear();
        };
        addAndMakeVisible(*clear_);
    }
    std::function<void()> onClear;
    void setCount(size_t count)
    {
        count_ = count;
        clear_->setVisible(count_ > 0); // nothing to clear: just "None"
        resized();
        repaint();
    }
    juce::String text() const
    {
        return count_ == 0 ? juce::String("None")
                           : juce::String(static_cast<int>(count_)) + (count_ == 1 ? " plugin" : " plugins");
    }
    int preferredWidth() const
    {
        return juce::GlyphArrangement::getStringWidthInt(theme_.body(), "000 plugins") + theme_.spacing.md +
               buttonWidth(theme_, "Clear");
    }
    void paint(juce::Graphics& g) override
    {
        g.setFont(theme_.body());
        g.setColour(theme_.colours.textPrimary);
        g.drawText(text(), text_, juce::Justification::centredLeft, false);
    }
    void resized() override
    {
        auto area = getLocalBounds();
        text_ = area.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(theme_.body(), text()));
        area.removeFromLeft(theme_.spacing.md);
        clear_->setBounds(area.removeFromLeft(buttonWidth(theme_, "Clear")));
    }

private:
    const Theme& theme_;
    std::unique_ptr<juce::TextButton> clear_;
    size_t count_ = 0;
    juce::Rectangle<int> text_;
};

// ----- accent swatches -------------------------------------------------------------------------

class Swatches final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Swatches(const Theme& theme) : theme_(theme)
    {
        for (const auto& name : theme_.track.accentPresets)
            for (const auto& entry : theme_.track.palette)
                if (entry.name == name)
                    presets_.push_back(entry);
        if (presets_.empty())
            presets_.push_back({"Overcast", theme_.colours.accent});
        setWantsKeyboardFocus(true);
        setTitle("Accent");
        setUpTextField(theme_, hex_, theme_.value());
        hex_.setInputRestrictions(7, "#0123456789abcdefABCDEF");
        hex_.setTitle("Accent hex value");
        hex_.onTextChange = [this]
        { markInvalid(!valid(hex_.getText().trim()) && hex_.getText().isNotEmpty()); };
        hex_.onReturnKey = [this] { commitHex(); };
        hex_.onFocusLost = [this] { commitHex(); };
        addAndMakeVisible(hex_);
    }
    std::function<void(const juce::String& hex)> onChanged; // empty: the default

    static bool valid(const juce::String& hex)
    {
        return hex.length() == 7 && hex.startsWithChar('#') &&
               hex.substring(1).containsOnly("0123456789abcdefABCDEF");
    }
    void setValue(const juce::String& hex)
    {
        value_ = hex.toUpperCase();
        hex_.setText(effectiveHex(), false);
        markInvalid(false);
        repaint();
    }
    int preferredWidth() const { return swatchesWidth() + theme_.spacing.md + hexWidth; }
    juce::String getTooltip() override
    {
        const int index = indexAt(getMouseXYRelative());
        return index >= 0 ? juce::String(presets_[static_cast<size_t>(index)].name) : juce::String();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const int chosen = chosenIndex();
        for (int i = 0; i < static_cast<int>(presets_.size()); ++i)
        {
            const auto r = swatchBounds(i);
            g.setColour(presets_[static_cast<size_t>(i)].value);
            g.fillEllipse(r);
            if (i == chosen)
            {
                g.setColour(c.textPrimary);
                g.drawEllipse(r.expanded(3.0f), 2.0f);
            }
            else if (i == hover_)
            {
                g.setColour(c.lineStrong);
                g.drawEllipse(r.expanded(2.5f), 1.0f);
            }
        }
        if (auto* lnf = lookAndFeelOf(*this))
        {
            const auto row = swatchBounds(0).getUnion(swatchBounds(static_cast<int>(presets_.size()) - 1));
            lnf->drawFocusRing(g, *this, row.expanded(5.0f), row.getHeight());
        }
    }
    void resized() override
    {
        hex_.setBounds(
            getLocalBounds().withTrimmedLeft(swatchesWidth() + theme_.spacing.md).withWidth(hexWidth));
    }
    void mouseMove(const juce::MouseEvent& e) override { setHover(indexAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int index = indexAt(e.getPosition());
        if (index >= 0)
            choose(index);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        const int chosen = std::max(0, chosenIndex());
        if (key == juce::KeyPress::leftKey && chosen > 0)
            choose(chosen - 1);
        else if (key == juce::KeyPress::rightKey && chosen + 1 < static_cast<int>(presets_.size()))
            choose(chosen + 1);
        else
            return key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    int swatchesWidth() const
    {
        const int n = static_cast<int>(presets_.size());
        return 2 * ringRoom + n * swatchSize + (n - 1) * theme_.spacing.sm;
    }
    juce::Rectangle<float> swatchBounds(int index) const
    {
        const float x = static_cast<float>(ringRoom + index * (swatchSize + theme_.spacing.sm));
        const float y = (static_cast<float>(getHeight()) - swatchSize) * 0.5f;
        return {x, y, static_cast<float>(swatchSize), static_cast<float>(swatchSize)};
    }
    int indexAt(juce::Point<int> p) const
    {
        for (int i = 0; i < static_cast<int>(presets_.size()); ++i)
            if (swatchBounds(i).expanded(static_cast<float>(theme_.spacing.sm) * 0.5f).contains(p.toFloat()))
                return i;
        return -1;
    }
    juce::String hexOf(int index) const
    {
        return "#" + presets_[static_cast<size_t>(index)].value.toDisplayString(false).toUpperCase();
    }
    juce::String effectiveHex() const { return value_.isEmpty() ? hexOf(0) : value_; }
    int chosenIndex() const
    {
        const auto hex = effectiveHex();
        for (int i = 0; i < static_cast<int>(presets_.size()); ++i)
            if (hexOf(i) == hex)
                return i;
        return -1; // a custom colour
    }
    void choose(int index)
    {
        if (onChanged)
            onChanged(index == 0 ? juce::String() : hexOf(index));
    }
    void commitHex()
    {
        const auto text = hex_.getText().trim().toUpperCase();
        if (!valid(text))
        {
            setValue(value_); // revert
            return;
        }
        if (text != effectiveHex() && onChanged)
            onChanged(text == hexOf(0) ? juce::String() : text);
    }
    void markInvalid(bool invalid)
    {
        hex_.getProperties().set("invalid", invalid);
        hex_.repaint();
    }
    void setHover(int index)
    {
        if (index != hover_)
        {
            hover_ = index;
            repaint();
        }
    }

    static constexpr int ringRoom = 4; // the chosen ring sits 3 px outside, 2 px wide
    const Theme& theme_;
    std::vector<Theme::TrackColour> presets_;
    juce::String value_;
    juce::TextEditor hex_;
    int hover_ = -1;
};

// ----- the scan folder list ----------------------------------------------------------------------

class FolderList final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FolderList(const Theme& theme) : theme_(theme), add_(makeButton("Add folder" + ellipsis()))
    {
        list_.setModel(this);
        list_.setRowHeight(theme_.sizes.rowHeight);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list_.setOutlineThickness(0);
        list_.setTitle("Scan folders");
        add_->onClick = [this]
        {
            if (onAdd)
                onAdd();
        };
        addAndMakeVisible(list_);
        addAndMakeVisible(*add_);
    }
    std::function<void()> onAdd;
    std::function<void(const juce::StringArray&)> onChanged;

    void setFolders(juce::StringArray folders)
    {
        folders_ = std::move(folders);
        list_.updateContent();
        list_.repaint();
        repaint();
    }
    void addFolder(const juce::String& folder)
    {
        if (folder.isEmpty() || folders_.contains(folder, true))
            return;
        folders_.add(folder);
        changed();
    }
    const juce::StringArray& folders() const noexcept { return folders_; }
    int preferredHeight() const
    {
        return visibleRows * theme_.sizes.rowHeight + 2 * theme_.spacing.xxs + theme_.spacing.sm +
               theme_.sizes.control;
    }
    void paint(juce::Graphics& g) override
    {
        const auto well = wellBounds().toFloat();
        g.setColour(theme_.colours.surface0);
        g.fillRoundedRectangle(well, theme_.radius.sm);
        if (folders_.isEmpty())
        {
            g.setFont(theme_.body());
            g.setColour(theme_.colours.textTertiary);
            g.drawText("Only the standard VST3 and CLAP folders are scanned.",
                       wellBounds().reduced(theme_.spacing.md, 0),
                       juce::Justification::centred,
                       true);
        }
    }
    void resized() override
    {
        auto area = getLocalBounds();
        auto buttons = area.removeFromBottom(theme_.sizes.control);
        add_->setBounds(buttons.removeFromLeft(buttonWidth(theme_, add_->getButtonText())));
        list_.setBounds(wellBounds().reduced(0, theme_.spacing.xxs));
    }

private:
    static constexpr int visibleRows = 4;

    juce::Rectangle<int> wellBounds() const
    {
        return getLocalBounds().withHeight(visibleRows * theme_.sizes.rowHeight + 2 * theme_.spacing.xxs);
    }
    void changed()
    {
        list_.updateContent();
        list_.repaint();
        repaint();
        if (onChanged)
            onChanged(folders_);
    }

    class Row final : public juce::Component
    {
    public:
        Row(FolderList& owner, const Theme& theme)
            : owner_(owner), theme_(theme), remove_(theme, Icon::close, "Remove folder")
        {
            setRepaintsOnMouseActivity(true);
            remove_.onClick = [this]
            {
                if (row_ >= 0 && row_ < owner_.folders_.size())
                {
                    owner_.folders_.remove(row_);
                    owner_.changed();
                }
            };
            addAndMakeVisible(remove_);
        }
        void update(int row)
        {
            row_ = row;
            remove_.setVisible(row_ >= 0 && row_ < owner_.folders_.size());
            repaint();
        }
        void paint(juce::Graphics& g) override
        {
            if (row_ < 0 || row_ >= owner_.folders_.size())
                return;
            const auto& c = theme_.colours;
            auto area = getLocalBounds().reduced(theme_.spacing.xxs, 0);
            if (isMouseOver(true))
            {
                g.setColour(c.surface2);
                g.fillRoundedRectangle(area.toFloat(), theme_.radius.sm);
            }
            area.removeFromLeft(theme_.spacing.sm);
            Icons::draw(g,
                        Icon::folder,
                        area.removeFromLeft(theme_.sizes.icon)
                            .withSizeKeepingCentre(theme_.sizes.icon, theme_.sizes.icon)
                            .toFloat(),
                        c.textSecondary);
            area.removeFromLeft(theme_.spacing.sm);
            area.removeFromRight(remove_.getWidth() + theme_.spacing.xs);
            g.setFont(theme_.body());
            g.setColour(c.textPrimary);
            g.drawText(elideStart(theme_.body(), owner_.folders_[row_], static_cast<float>(area.getWidth())),
                       area,
                       juce::Justification::centredLeft,
                       false);
        }
        void resized() override
        {
            const int size = theme_.sizes.controlCompact;
            remove_.setBounds(
                getLocalBounds().removeFromRight(size + theme_.spacing.xs).withSizeKeepingCentre(size, size));
        }

    private:
        FolderList& owner_;
        const Theme& theme_;
        IconButton remove_;
        int row_ = -1;
    };

    int getNumRows() override { return folders_.size(); }
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override
    {
        auto* component = dynamic_cast<Row*>(existing);
        if (component == nullptr)
        {
            delete existing;
            component = new Row(*this, theme_);
        }
        component->update(row);
        return component;
    }

    const Theme& theme_;
    juce::StringArray folders_;
    juce::ListBox list_;
    std::unique_ptr<juce::TextButton> add_;
};

// ----- the shortcuts table ---------------------------------------------------------------------

class ShortcutTable final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ShortcutTable(const Theme& theme) : theme_(theme), icon_(theme)
    {
        setUpTextField(theme_, search_, theme_.body());
        search_.setIndents(theme_.spacing.sm + theme_.sizes.icon + theme_.spacing.sm, 0);
        search_.setTextToShowWhenEmpty("Search shortcuts", theme_.colours.textTertiary);
        search_.setTitle("Search shortcuts");
        search_.onTextChange = [this] { filter(); };
        search_.onEscapeKey = [this] { search_.clear(); };
        addAndMakeVisible(search_);
        icon_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(icon_);
        list_.setModel(this);
        list_.setRowHeight(theme_.sizes.rowHeight);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list_.setOutlineThickness(0);
        list_.setTitle("Shortcuts");
        list_.addMouseListener(this, true);
        addAndMakeVisible(list_);
        filter();
    }
    int rows() const noexcept { return static_cast<int>(rows_.size()); }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto card = cardBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(card, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(card.reduced(0.5f), theme_.radius.md, 1.0f);
        if (rows_.empty())
        {
            g.setFont(theme_.body());
            g.setColour(c.textTertiary);
            g.drawText("No shortcuts match " + juce::String::fromUTF8("\xe2\x80\x9c") +
                           search_.getText().trim() + juce::String::fromUTF8("\xe2\x80\x9d") + ".",
                       cardBounds().reduced(theme_.spacing.lg),
                       juce::Justification::centred,
                       true);
        }
    }
    void resized() override
    {
        auto area = getLocalBounds();
        search_.setBounds(area.removeFromTop(theme_.sizes.control));
        icon_.setBounds(search_.getBounds()
                            .withWidth(theme_.spacing.sm + theme_.sizes.icon)
                            .withTrimmedLeft(theme_.spacing.sm));
        list_.setBounds(cardBounds().reduced(theme_.spacing.xxs, theme_.spacing.sm));
    }
    void mouseMove(const juce::MouseEvent& e) override { setHover(e); }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = -1;
        list_.repaint();
    }

private:
    struct Entry
    {
        const Shortcut* shortcut = nullptr; // null: a group header
        const char* group = "";
    };
    class SearchIcon final : public juce::Component
    {
    public:
        explicit SearchIcon(const Theme& theme) : theme_(theme) {}
        void paint(juce::Graphics& g) override
        {
            Icons::draw(g,
                        Icon::search,
                        getLocalBounds()
                            .withSizeKeepingCentre(theme_.sizes.icon, theme_.sizes.icon)
                            .withX(0)
                            .toFloat(),
                        theme_.colours.textTertiary);
        }

    private:
        const Theme& theme_;
    };

    juce::Rectangle<int> cardBounds() const
    {
        return getLocalBounds().withTrimmedTop(theme_.sizes.control + theme_.spacing.md);
    }
    void setHover(const juce::MouseEvent& e)
    {
        const auto p = e.getEventRelativeTo(&list_).getPosition();
        const int row = list_.getRowContainingPosition(p.x, p.y);
        if (row != hover_)
        {
            hover_ = row;
            list_.repaint();
        }
    }
    void filter()
    {
        rows_.clear();
        const auto needle = search_.getText().trim();
        const char* group = nullptr;
        for (const auto& s : shortcuts)
        {
            if (needle.isNotEmpty() && !juce::String(s.action).containsIgnoreCase(needle) &&
                !juce::String(s.keys).containsIgnoreCase(needle) &&
                !juce::String(s.group).containsIgnoreCase(needle))
                continue;
            if (group == nullptr || juce::String(group) != s.group)
            {
                group = s.group;
                rows_.push_back({nullptr, s.group});
            }
            rows_.push_back({&s, s.group});
        }
        hover_ = -1;
        list_.updateContent();
        list_.repaint();
        repaint();
    }

    // The key caps for a key string, right aligned in `area`; returns their width.
    int drawKeys(juce::Graphics& g, const juce::String& keys, juce::Rectangle<int> area) const
    {
        const auto& c = theme_.colours;
        struct Piece
        {
            juce::String text;
            bool cap;
            int width;
        };
        std::vector<Piece> pieces;
        juce::StringArray tokens;
        tokens.addTokens(keys, " ", "");
        for (int t = 0; t < tokens.size(); ++t)
        {
            const auto& token = tokens[t];
            if (token == "/" || token == "or" || token == "to")
            {
                pieces.push_back({token,
                                  false,
                                  juce::GlyphArrangement::getStringWidthInt(theme_.small(), token) +
                                      2 * theme_.spacing.sm});
                continue;
            }
            // A chord: one cap per key ("Ctrl++" would be Ctrl and +).
            juce::StringArray keysInChord;
            keysInChord.addTokens(token, "+", "");
            if (token.endsWith("+") && token.length() > 1)
                keysInChord.add("+");
            keysInChord.removeEmptyStrings();
            for (int k = 0; k < keysInChord.size(); ++k)
            {
                const int capText =
                    juce::GlyphArrangement::getStringWidthInt(theme_.caption(), keysInChord[k]);
                pieces.push_back(
                    {keysInChord[k], true, std::max(keyCapHeight, capText + 2 * theme_.spacing.sm)});
                if (k + 1 < keysInChord.size())
                    pieces.push_back({{}, false, theme_.spacing.xxs});
            }
        }
        int total = 0;
        for (const auto& piece : pieces)
            total += piece.width;
        int x = area.getRight() - total;
        for (const auto& piece : pieces)
        {
            const auto r = juce::Rectangle<int>(x, area.getY(), piece.width, area.getHeight());
            if (piece.cap)
            {
                const auto cap = r.withSizeKeepingCentre(piece.width, keyCapHeight).toFloat();
                g.setColour(c.surface2);
                g.fillRoundedRectangle(cap, theme_.radius.sm);
                g.setColour(c.lineStrong);
                g.drawRoundedRectangle(cap.reduced(0.5f), theme_.radius.sm, 1.0f);
                g.setFont(theme_.caption());
                g.setColour(c.textSecondary);
                g.drawText(piece.text, cap.toNearestInt(), juce::Justification::centred, false);
            }
            else if (piece.text.isNotEmpty())
            {
                g.setFont(theme_.small());
                g.setColour(c.textTertiary);
                g.drawText(piece.text, r, juce::Justification::centred, false);
            }
            x += piece.width;
        }
        return total;
    }

    int getNumRows() override { return static_cast<int>(rows_.size()); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool) override
    {
        if (row < 0 || static_cast<size_t>(row) >= rows_.size())
            return;
        const auto& c = theme_.colours;
        const auto& entry = rows_[static_cast<size_t>(row)];
        auto area = juce::Rectangle<int>(0, 0, width, height);
        if (entry.shortcut == nullptr)
        {
            g.setFont(theme_.captionCaps());
            g.setColour(c.textTertiary);
            g.drawText(juce::String(entry.group).toUpperCase(),
                       area.reduced(theme_.spacing.md, 0).withTrimmedBottom(theme_.spacing.xs),
                       juce::Justification::bottomLeft,
                       false);
            return;
        }
        if (row == hover_)
        {
            g.setColour(c.surface2);
            g.fillRoundedRectangle(area.reduced(theme_.spacing.xs, 0).toFloat(), theme_.radius.sm);
        }
        area = area.reduced(theme_.spacing.md, 0);
        // The keys at the right; the action takes all the width they leave.
        const int keys = drawKeys(g, entry.shortcut->keys, area);
        area.removeFromRight(keys + theme_.spacing.lg);
        g.setFont(theme_.body());
        g.setColour(c.textPrimary);
        g.drawText(entry.shortcut->action, area, juce::Justification::centredLeft, true);
    }

    const Theme& theme_;
    juce::TextEditor search_;
    SearchIcon icon_;
    juce::ListBox list_;
    std::vector<Entry> rows_;
    int hover_ = -1;
};

} // namespace

// ----- a page ----------------------------------------------------------------------------------

class PreferencesView::Page final : public juce::Component
{
public:
    Page(const Theme& theme, const juce::String& title) : theme_(theme), title_(title) {}

    void addCard(const juce::String& title)
    {
        Card card;
        card.title = title;
        cards_.push_back(std::move(card));
    }
    // A card with no chrome that holds one component as it is (the Audio page).
    void addBare(std::unique_ptr<juce::Component> component, int height)
    {
        Card card;
        card.bare = true;
        cards_.push_back(std::move(card));
        add({}, std::move(component), 0, {}, false, height);
    }
    // A card that fills the page's height with one component (the Shortcuts table).
    void addFill(std::unique_ptr<juce::Component> component)
    {
        Card card;
        card.bare = true;
        card.fill = true;
        cards_.push_back(std::move(card));
        add({}, std::move(component), 0, {}, false, 0);
    }
    // A labelled control in the last card: `width` 0 fills the control column.
    template <typename T>
    T& add(const juce::String& label,
           std::unique_ptr<T> control,
           int width,
           const juce::String& hint = {},
           bool comingSoon = false,
           int height = 0)
    {
        jassert(!cards_.empty());
        T& ref = *control;
        Row row;
        row.label = label;
        row.hint = hint;
        row.width = width;
        row.height = height > 0 ? height : theme_.sizes.control;
        if (label.isNotEmpty() && control->getTitle().isEmpty())
            control->setTitle(label);
        row.toggle = dynamic_cast<juce::ToggleButton*>(control.get());
        row.control = std::move(control);
        addAndMakeVisible(*row.control);
        if (comingSoon)
        {
            row.badge = std::make_unique<Badge>(theme_);
            addAndMakeVisible(*row.badge);
        }
        cards_.back().rows.push_back(std::move(row));
        return ref;
    }

    void setViewportHeight(int height) noexcept { viewportHeight_ = height; }

    // Lays out everything for `width`; returns the page's height.
    int layout(int width)
    {
        const auto& sp = theme_.spacing;
        const int x = sp.xl;
        int y = sp.xl;
        titleBounds_ = {x, y, width - 2 * x, theme_.sizes.controlLarge};
        y += theme_.sizes.controlLarge + sp.lg;
        const int cardWidth = std::max(0, std::min(maxCardWidth, width - 2 * x));
        for (auto& card : cards_)
        {
            if (card.bare && !card.fill)
            {
                // The component insets its own cards by space.xl, like a page.
                auto& row = card.rows.front();
                card.bounds = {0, y - sp.xl, std::min(width, cardWidth + 2 * sp.xl), row.height};
                row.controlBounds = card.bounds;
                y = card.bounds.getBottom() - sp.xl + sp.md;
                continue;
            }
            if (card.fill)
            {
                auto& row = card.rows.front();
                const int height = std::max(6 * theme_.sizes.rowHeight, viewportHeight_ - y - sp.xl);
                card.bounds = {x, y, cardWidth, height};
                row.controlBounds = card.bounds;
                y = card.bounds.getBottom() + sp.md;
                continue;
            }
            const int top = y;
            const int innerX = x + sp.lg;
            const int innerRight = x + cardWidth - sp.lg;
            y += sp.lg + cardTitleHeight + sp.md;
            for (auto& row : card.rows)
            {
                const int controlX = row.label.isEmpty() ? innerX : innerX + theme_.sizes.labelWidth;
                const int column = std::max(0, innerRight - controlX);
                row.labelBounds =
                    row.label.isEmpty()
                        ? juce::Rectangle<int>()
                        : juce::Rectangle<int>(innerX, y, theme_.sizes.labelWidth, theme_.sizes.control);
                const int controlWidth = row.width > 0 ? std::min(row.width, column) : column;
                row.controlBounds = {controlX, y, controlWidth, row.height};
                if (row.badge != nullptr)
                    row.badgeBounds = juce::Rectangle<int>(row.controlBounds.getRight() + sp.sm,
                                                           y,
                                                           row.badge->preferredWidth(),
                                                           theme_.sizes.control)
                                          .withSizeKeepingCentre(row.badge->preferredWidth(), badgeHeight);
                y += row.height;
                row.hintBounds = {};
                if (row.hint.isNotEmpty())
                {
                    const auto text =
                        wrapped(theme_, row.hint, theme_.colours.textTertiary, static_cast<float>(column));
                    const int height = static_cast<int>(std::ceil(text.getHeight()));
                    row.hintBounds = {controlX, y + sp.xs, column, height};
                    y += sp.xs + height;
                }
                y += sp.sm;
            }
            y += sp.lg - sp.sm;
            card.bounds = {x, top, cardWidth, y - top};
            y += sp.md;
        }
        return y - sp.md + sp.xl;
    }

    std::vector<RowLayout> rowLayout() const
    {
        std::vector<RowLayout> out;
        for (const auto& card : cards_)
            for (const auto& row : card.rows)
                out.push_back({row.label,
                               row.hint,
                               card.bounds,
                               row.labelBounds,
                               row.controlBounds,
                               row.hintBounds,
                               row.control.get(),
                               row.badge != nullptr,
                               card.bare});
        return out;
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        g.setFont(theme_.title());
        g.setColour(c.textPrimary);
        g.drawText(title_, titleBounds_, juce::Justification::centredLeft, false);
        for (const auto& card : cards_)
        {
            if (card.bare)
                continue;
            const auto bounds = card.bounds.toFloat();
            g.setColour(c.surface1);
            g.fillRoundedRectangle(bounds, theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textTertiary);
            g.drawText(card.title.toUpperCase(),
                       card.bounds.reduced(theme_.spacing.lg, 0)
                           .withTrimmedTop(theme_.spacing.md)
                           .withHeight(cardTitleHeight),
                       juce::Justification::centredLeft,
                       false);
            for (const auto& row : card.rows)
            {
                if (row.label.isNotEmpty())
                {
                    g.setFont(theme_.body());
                    g.setColour(row.control->isEnabled() ? c.textSecondary : c.textDisabled);
                    g.drawText(row.label, row.labelBounds, juce::Justification::centredLeft, false);
                }
                if (row.hint.isNotEmpty())
                    wrapped(theme_, row.hint, c.textTertiary, static_cast<float>(row.hintBounds.getWidth()))
                        .draw(g, row.hintBounds.toFloat());
            }
        }
    }

    void resized() override
    {
        layout(getWidth());
        for (auto& card : cards_)
            for (auto& row : card.rows)
            {
                row.control->setBounds(row.controlBounds);
                if (row.badge != nullptr)
                    row.badge->setBounds(row.badgeBounds);
            }
    }

    // Clicking a checkbox's label toggles it (the whole row is the hit target).
    void mouseUp(const juce::MouseEvent& e) override
    {
        for (auto& card : cards_)
            for (auto& row : card.rows)
                if (row.toggle != nullptr && row.toggle->isEnabled() &&
                    row.labelBounds.contains(e.getPosition()))
                    row.toggle->triggerClick();
    }

    bool fills() const
    {
        return std::any_of(cards_.begin(), cards_.end(), [](const Card& card) { return card.fill; });
    }

private:
    struct Row
    {
        juce::String label, hint;
        int width = 0, height = 28;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<Badge> badge;
        juce::ToggleButton* toggle = nullptr;
        juce::Rectangle<int> labelBounds, controlBounds, hintBounds, badgeBounds;
    };
    struct Card
    {
        juce::String title;
        bool bare = false, fill = false;
        std::vector<Row> rows;
        juce::Rectangle<int> bounds;
    };

    const Theme& theme_;
    juce::String title_;
    std::vector<Card> cards_;
    juce::Rectangle<int> titleBounds_;
    int viewportHeight_ = 0;
};

// ----- the page list ---------------------------------------------------------------------------

class PreferencesView::PageList final : public juce::Component
{
public:
    struct Item
    {
        juce::String title;
        Icon icon;
    };
    PageList(const Theme& theme, std::vector<Item> items) : theme_(theme), items_(std::move(items))
    {
        setWantsKeyboardFocus(true);
        setTitle("Preferences pages");
    }
    std::function<void(int)> onSelected;
    void select(int index)
    {
        index = juce::jlimit(0, static_cast<int>(items_.size()) - 1, index);
        if (index == selected_)
            return;
        selected_ = index;
        setDescription(items_[static_cast<size_t>(selected_)].title);
        repaint();
        if (onSelected)
            onSelected(selected_);
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.canvas);
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
        {
            const auto r = itemBounds(i);
            const bool selected = i == selected_;
            const bool hover = i == hover_;
            if (selected || hover)
            {
                g.setColour(selected ? c.surface2 : c.surface1);
                g.fillRoundedRectangle(r.toFloat(), theme_.radius.sm);
            }
            auto content = r.withTrimmedLeft(theme_.spacing.md);
            Icons::draw(g,
                        items_[static_cast<size_t>(i)].icon,
                        content.removeFromLeft(theme_.sizes.icon)
                            .withSizeKeepingCentre(theme_.sizes.icon, theme_.sizes.icon)
                            .toFloat(),
                        selected ? c.accent : c.textSecondary);
            content.removeFromLeft(theme_.spacing.sm);
            g.setFont(theme_.body());
            g.setColour(selected || hover ? c.textPrimary : c.textSecondary);
            g.drawText(
                items_[static_cast<size_t>(i)].title, content, juce::Justification::centredLeft, false);
        }
        if (auto* lnf = lookAndFeelOf(*this))
            lnf->drawFocusRing(g, *this, itemBounds(selected_).toFloat(), theme_.radius.sm);
    }
    void mouseMove(const juce::MouseEvent& e) override { setHover(indexAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int index = indexAt(e.getPosition());
        if (index >= 0)
            select(index);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey)
            select(selected_ - 1);
        else if (key == juce::KeyPress::downKey)
            select(selected_ + 1);
        else if (key == juce::KeyPress::homeKey)
            select(0);
        else if (key == juce::KeyPress::endKey)
            select(static_cast<int>(items_.size()) - 1);
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    juce::Rectangle<int> itemBounds(int index) const
    {
        const int h = theme_.sizes.controlLarge;
        return {theme_.spacing.sm, theme_.spacing.xl + index * h, getWidth() - 2 * theme_.spacing.sm, h};
    }
    int indexAt(juce::Point<int> p) const
    {
        for (int i = 0; i < static_cast<int>(items_.size()); ++i)
            if (itemBounds(i).contains(p))
                return i;
        return -1;
    }
    void setHover(int index)
    {
        if (index != hover_)
        {
            hover_ = index;
            repaint();
        }
    }

    const Theme& theme_;
    std::vector<Item> items_;
    int selected_ = 0, hover_ = -1;
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

std::string PreferencesView::get(const settings::Setting& setting) const
{
    return settings::Reader(loadSetting).text(setting);
}

void PreferencesView::set(const settings::Setting& setting, const std::string& value)
{
    if (updating_)
        return;
    if (saveSetting)
        saveSetting(std::string(setting.key), value);
    if (setting.key == settings::contrast.key)
        writeTokenOverride();
    if (onChanged)
        onChanged();
}

void PreferencesView::writeAccentOverride(const juce::String& hex)
{
    set(settings::accent, hex.toStdString());
    writeTokenOverride();
    if (onNotice)
        onNotice(hex.isEmpty() ? "Accent reset. Restart Northlight to apply."
                               : "Accent set to " + hex + ". Restart Northlight to apply.");
}

void PreferencesView::writeTokenOverride()
{
    if (settingsFolder == juce::File())
        return;
    const juce::File file = settingsFolder.getChildFile(Theme::preferencesOverrideName);
    // "#RRGGBB", or "#RRGGBBAA" when translucent: the token file's format.
    const auto hex = [](juce::Colour colour)
    {
        const auto alpha = colour.getAlpha();
        return "#" + colour.withAlpha(static_cast<juce::uint8>(255)).toDisplayString(false) +
               (alpha == 255 ? juce::String()
                             : juce::String::toHexString(static_cast<int>(alpha)).paddedLeft('0', 2));
    };
    auto* colours = new juce::DynamicObject();
    const auto accent = juce::String::fromUTF8(get(settings::accent).c_str());
    if (accent.length() == 7 && accent.startsWithChar('#'))
        for (const auto& [key, colour] :
             Theme::accentTokens(juce::Colour::fromString("ff" + accent.substring(1))))
            colours->setProperty(juce::Identifier(key.c_str()), hex(colour));
    if (get(settings::contrast) == "high")
        for (const auto& [key, colour] : theme_.highContrastTokens())
            colours->setProperty(juce::Identifier(key.c_str()), hex(colour));
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

void PreferencesView::chooseFolder(const juce::String& title,
                                   const juce::File& start,
                                   std::function<void(const juce::File&)> chosen)
{
    chooser_ = std::make_unique<juce::FileChooser>(title, start);
    juce::Component::SafePointer<PreferencesView> safe(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
                          [safe, chosen = std::move(chosen)](const juce::FileChooser& chooser)
                          {
                              const auto folder = chooser.getResult();
                              if (safe != nullptr && folder != juce::File())
                                  chosen(folder);
                          });
}

// The pages in list order: each one's key (showPage), its title, its icon in the
// list and what builds it. pageKeys(), the list and the pages all come from here.
struct PreferencesView::PageInfo
{
    juce::String key, title;
    Icon icon;
    void (PreferencesView::*build)(Page&);
};

const std::vector<PreferencesView::PageInfo>& PreferencesView::pageTable()
{
    static const std::vector<PageInfo> pages{
        {"general", "General", Icon::settings, &PreferencesView::buildGeneral},
        {"audio", "Audio", Icon::trackAudio, &PreferencesView::buildAudio},
        {"recording", "Recording", Icon::record, &PreferencesView::buildRecording},
        {"editing", "Editing", Icon::pencil, &PreferencesView::buildEditing},
        {"plugins", "Plugins", Icon::plugin, &PreferencesView::buildPlugins},
        {"shortcuts", "Shortcuts", Icon::keyboard, &PreferencesView::buildShortcuts},
        {"diagnostics", "Diagnostics", Icon::diagnostics, &PreferencesView::buildDiagnostics},
    };
    return pages;
}

const std::vector<juce::String>& PreferencesView::pageKeys()
{
    static const std::vector<juce::String> keys = []
    {
        std::vector<juce::String> out;
        for (const auto& page : pageTable())
            out.push_back(page.key);
        return out;
    }();
    return keys;
}

void PreferencesView::buildPages()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    pages_.clear();
    std::vector<PageList::Item> items;
    for (const auto& info : pageTable())
    {
        auto page = std::make_unique<Page>(theme_, info.title);
        (this->*info.build)(*page);
        pages_.push_back(std::move(page));
        items.push_back({info.title, info.icon});
    }
    list_ = std::make_unique<PageList>(theme_, std::move(items));
    list_->onSelected = [this](int index)
    {
        current_ = index;
        resized();
    };
    addAndMakeVisible(*list_);
}

// ----- the controls a row binds to a setting ----------------------------------------------------

std::unique_ptr<juce::ComboBox> PreferencesView::makeCombo(const std::vector<Choice>& items,
                                                           const settings::Setting& key)
{
    auto box = std::make_unique<juce::ComboBox>();
    const auto current = get(key);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int id = static_cast<int>(i) + 1;
        box->addItem(items[i].comingSoon ? items[i].text + " (coming soon)" : items[i].text, id);
        box->setItemEnabled(id, !items[i].comingSoon);
        if (items[i].value == current && !items[i].comingSoon)
            box->setSelectedId(id, juce::dontSendNotification);
    }
    if (box->getSelectedId() == 0)
        box->setSelectedId(1, juce::dontSendNotification);
    auto* raw = box.get();
    box->onChange = [this, raw, items, &key]
    {
        const int index = raw->getSelectedId() - 1;
        if (index >= 0 && static_cast<size_t>(index) < items.size())
            set(key, items[static_cast<size_t>(index)].value);
    };
    return box;
}

std::unique_ptr<SegmentedControl> PreferencesView::makeSegmented(const std::vector<Choice>& items,
                                                                 const settings::Setting& key)
{
    auto control = std::make_unique<SegmentedControl>(theme_);
    control->setTrackColour(theme_.colours.surface0); // a well in the surface1 card
    std::vector<juce::String> labels;
    for (const auto& item : items)
        labels.push_back(item.text);
    control->setSegments(labels);
    const auto current = get(key);
    for (size_t i = 0; i < items.size(); ++i)
    {
        control->setSegmentEnabled(static_cast<int>(i), !items[i].comingSoon);
        if (items[i].value == current)
            control->setSelected(static_cast<int>(i), juce::dontSendNotification);
    }
    control->onChange = [this, items, &key](int index)
    {
        if (index >= 0 && static_cast<size_t>(index) < items.size())
            set(key, items[static_cast<size_t>(index)].value);
    };
    return control;
}

std::unique_ptr<juce::ToggleButton> PreferencesView::makeCheckbox(const settings::Setting& key)
{
    auto t = std::make_unique<juce::ToggleButton>();
    t->setToggleState(settings::Reader(loadSetting).flag(key), juce::dontSendNotification);
    auto* raw = t.get();
    t->onClick = [this, raw, &key] { set(key, settings::flagText(raw->getToggleState())); };
    return t;
}

std::unique_ptr<juce::TextEditor> PreferencesView::makeText(const settings::Setting& key,
                                                            const juce::String& placeholder)
{
    auto e = std::make_unique<juce::TextEditor>();
    setUpTextField(theme_, *e, theme_.body());
    e->setText(juce::String::fromUTF8(get(key).c_str()), false);
    e->setTextToShowWhenEmpty(placeholder, theme_.colours.textTertiary);
    auto* raw = e.get();
    e->onReturnKey = [this, raw, &key] { set(key, raw->getText().trim().toStdString()); };
    e->onFocusLost = e->onReturnKey;
    return e;
}

std::unique_ptr<NumericField> PreferencesView::makeNumber(
    const settings::Setting& key, double lo, double hi, double step, const juce::String& unit, int decimals)
{
    auto f = std::make_unique<NumericField>(theme_);
    f->setRange(lo, hi, step);
    f->setDecimals(decimals);
    f->setUnit(unit);
    f->setValue(settings::Reader(loadSetting).number(key), juce::dontSendNotification);
    f->onValueChanged = [this, &key](double v) { set(key, juce::String(v, 2).toStdString()); };
    return f;
}

std::pair<std::unique_ptr<juce::TextButton>, int> PreferencesView::makeButtonRow(const juce::String& label,
                                                                                 std::function<void()> action)
{
    auto b = makeButton(label);
    b->onClick = std::move(action);
    const int width = buttonWidth(theme_, label);
    return std::make_pair(std::move(b), width);
}

// ----- the pages -------------------------------------------------------------------------------

namespace
{
const juce::String restartNote = "Applies the next time Northlight starts.";
} // namespace

void PreferencesView::buildGeneral(Page& page)
{
    page.addCard("Projects");
    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    auto folder = std::make_unique<PathField>(theme_, documents.getFullPathName());
    folder->setPath(juce::String::fromUTF8(get(settings::projectFolder).c_str()), false);
    auto* rawFolder = folder.get();
    folder->onChanged = [this](const juce::String& path)
    { set(settings::projectFolder, path.toStdString()); };
    folder->onChoose = [this, rawFolder, documents]
    {
        const juce::File start(rawFolder->path().isNotEmpty() ? juce::File(rawFolder->path()) : documents);
        juce::Component::SafePointer<PathField> safe(rawFolder);
        chooseFolder("Project folder",
                     start,
                     [safe](const juce::File& chosen)
                     {
                         if (safe != nullptr)
                             safe->setPath(chosen.getFullPathName(), true);
                     });
    };
    page.add("Folder", std::move(folder), 0, "New projects are saved here.");
    page.add("Recent",
             makeNumber(settings::recentCount, 0, 20, 1, "", 0),
             numberWidth,
             "Shown in the Project menu.");
    page.add("Autosave",
             makeNumber(settings::autosaveMinutes, 0, 60, 1, "min", 0),
             numberWidth,
             "Keeps unsaved changes for recovery after a crash. 0 turns it off.");

    page.addCard("Appearance");
    auto themeControl = makeSegmented({{"Dark", "dark"}, {"Light", "light", true}}, settings::theme);
    themeControl->setSegmentTooltip(1, "The light theme is coming soon.");
    const int themeWidth = themeControl->preferredWidth();
    page.add("Theme", std::move(themeControl), themeWidth);
    auto swatches = std::make_unique<Swatches>(theme_);
    swatches->setValue(juce::String::fromUTF8(get(settings::accent).c_str()));
    auto* rawSwatches = swatches.get();
    swatches->onChanged = [this, rawSwatches](const juce::String& hex)
    {
        rawSwatches->setValue(hex);
        writeAccentOverride(hex);
    };
    const int swatchesWidth = swatches->preferredWidth();
    page.add("Accent", std::move(swatches), swatchesWidth, restartNote);
    auto contrast = makeSegmented({{"Standard", "standard"}, {"High", "high"}}, settings::contrast);
    const int contrastWidth = contrast->preferredWidth();
    page.add("Contrast",
             std::move(contrast),
             contrastWidth,
             "High brightens lines and secondary text. " + restartNote);
    page.add("UI scale",
             makeCombo(
                 {{"Follow system", "0"}, {"100%", "100"}, {"125%", "125"}, {"150%", "150"}, {"200%", "200"}},
                 settings::uiScale),
             comboWidth);
}

void PreferencesView::buildAudio(Page& page)
{
    auto setup = std::make_unique<AudioSetupView>(audio_);
    setup->onWarning = [this](const juce::String& message)
    {
        if (onWarning)
            onWarning(message);
    };
    page.addBare(std::move(setup), AudioSetupView::contentHeight);
}

void PreferencesView::buildRecording(Page& page)
{
    page.addCard("Files");
    page.add(
        "Format",
        makeCombo({{"WAV 32-bit float", "float"}, {"WAV 24-bit", "24", true}}, settings::recordingFormat),
        comboWidth,
        {},
        settings::recordingFormat.comingSoon);
    page.add("Take names",
             makeText(settings::namePattern, "{track} {take}"),
             textWidth,
             "{track} and {take} become the track name and take number.",
             settings::namePattern.comingSoon);
    page.addCard("Count-in and click");
    auto countIn =
        makeSegmented({{"Off", "0"}, {"1 bar", "1"}, {"2 bars", "2"}, {"4 bars", "4"}}, settings::countIn);
    const int countInWidth = countIn->preferredWidth();
    page.add("Count-in", std::move(countIn), countInWidth, "Used when a project opens.");
    page.add("Pre-roll",
             makeNumber(settings::preRoll, 0, 8, 1, "bars", 0),
             numberWidth,
             {},
             settings::preRoll.comingSoon);
    page.add("Click level", makeNumber(settings::metronomeLevel, -40, 0, 0.5, "dB", 1), numberWidth);
}

void PreferencesView::buildEditing(Page& page)
{
    page.addCard("Snap");
    page.add("Snap", makeCheckbox(settings::snap), theme_.sizes.icon, "On when a project opens.");
    page.add(
        "Grid",
        makeCombo(
            {{"Adaptive", "0"}, {"Bar", "1"}, {"Beat", "4"}, {"1/8", "8"}, {"1/16", "16"}, {"1/32", "32"}},
            settings::snapGrid),
        comboWidth);
    page.addCard("Edits");
    page.add("Ripple", makeCheckbox(settings::ripple), theme_.sizes.icon, "On when a project opens.");
    page.add("Fade",
             makeNumber(settings::fadeMs, 0, 2000, 1, "ms", 0),
             numberWidth,
             "The fade new clips start with.",
             settings::fadeMs.comingSoon);
    page.addCard("View");
    page.add("Follow playhead",
             makeCheckbox(settings::followPlayhead),
             theme_.sizes.icon,
             "Scrolls the arrange view to keep the playhead in sight.");
}

void PreferencesView::buildPlugins(Page& page)
{
    page.addCard("Folders");
    auto folders = std::make_unique<FolderList>(theme_);
    juce::StringArray paths;
    paths.addTokens(juce::String::fromUTF8(get(settings::pluginPaths).c_str()), "\t", "");
    paths.removeEmptyStrings();
    folders->setFolders(paths);
    auto* rawFolders = folders.get();
    folders->onChanged = [this](const juce::StringArray& list)
    { set(settings::pluginPaths, list.joinIntoString("\t").toStdString()); };
    folders->onAdd = [this, rawFolders]
    {
        juce::Component::SafePointer<FolderList> safe(rawFolders);
        chooseFolder("Add a plugin folder",
                     juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory),
                     [safe](const juce::File& chosen)
                     {
                         if (safe != nullptr)
                             safe->addFolder(chosen.getFullPathName());
                     });
    };
    const int foldersHeight = folders->preferredHeight();
    page.add({},
             std::move(folders),
             0,
             "Scanned as well as the standard VST3 and CLAP folders.",
             false,
             foldersHeight);

    page.addCard("Scanning");
    page.add("Scan on launch",
             makeCheckbox(settings::scanOnLaunch),
             theme_.sizes.icon,
             "Looks for new and changed plugins when Northlight starts.");
    auto [scan, scanWidth] = makeButtonRow("Scan for changes",
                                           [this]
                                           {
                                               if (onScan)
                                                   onScan();
                                           });
    page.add("Scan", std::move(scan), scanWidth);
    auto blocklist = std::make_unique<BlocklistRow>(theme_);
    blocklist->setCount(registry_.blocked().size());
    auto* rawBlocklist = blocklist.get();
    blocklist->onClear = [this, rawBlocklist]
    {
        if (onClearBlocklist)
            onClearBlocklist();
        rawBlocklist->setCount(registry_.blocked().size());
    };
    const int blocklistWidth = blocklist->preferredWidth();
    page.add("Blocklist",
             std::move(blocklist),
             blocklistWidth,
             "Plugins that crashed or hung while being scanned are skipped until cleared.");

    page.addCard("Windows");
    page.add("Keep on top",
             makeCheckbox(settings::pluginsOnTop),
             theme_.sizes.icon,
             "Plugin windows stay above the main window.");
}

void PreferencesView::buildShortcuts(Page& page)
{
    page.addFill(std::make_unique<ShortcutTable>(theme_));
}

void PreferencesView::buildDiagnostics(Page& page)
{
    page.addCard("Crash reports");
    page.add("Send reports",
             makeCheckbox(settings::crashReports),
             theme_.sizes.icon,
             "Reports are always kept on this computer, in the settings folder's Crashes folder.",
             settings::crashReports.comingSoon);
    page.addCard("Support");
    const auto folder = settingsFolder;
    auto readout = std::make_unique<PathReadout>(theme_, folder.getFullPathName(), "Show in Explorer");
    readout->button().onClick = [folder] { folder.revealToUser(); };
    readout->button().setEnabled(folder != juce::File());
    page.add("Settings", std::move(readout), 0);
    auto [copy, copyWidth] = makeButtonRow("Copy system report",
                                           [this]
                                           {
                                               if (systemReport)
                                                   juce::SystemClipboard::copyTextToClipboard(systemReport());
                                               if (onNotice)
                                                   onNotice("System report copied.");
                                           });
    page.add(
        "Report", std::move(copy), copyWidth, "Copies device, plugin and system details for a bug report.");
}

void PreferencesView::showPage(const juce::String& key)
{
    if (pages_.empty())
        buildPages();
    const auto& keys = pageKeys();
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
        {
            current_ = static_cast<int>(i);
            list_->select(current_);
        }
    resized();
}

juce::String PreferencesView::currentPage() const
{
    const auto& keys = pageKeys();
    return current_ >= 0 && static_cast<size_t>(current_) < keys.size() ? keys[static_cast<size_t>(current_)]
                                                                        : juce::String();
}

std::vector<PreferencesView::RowLayout> PreferencesView::rowLayout() const
{
    if (current_ < 0 || static_cast<size_t>(current_) >= pages_.size())
        return {};
    return pages_[static_cast<size_t>(current_)]->rowLayout();
}

void PreferencesView::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.surface0);
}

void PreferencesView::parentHierarchyChanged()
{
    if (pages_.empty() && loadSetting)
        resized();
}

void PreferencesView::resized()
{
    // The pages read the settings, so they wait for the owner's callbacks.
    if (pages_.empty())
    {
        if (!loadSetting)
            return;
        buildPages();
    }
    auto area = getLocalBounds();
    list_->setBounds(area.removeFromLeft(listWidth));
    content_.setBounds(area);
    if (current_ >= 0 && static_cast<size_t>(current_) < pages_.size())
    {
        auto* page = pages_[static_cast<size_t>(current_)].get();
        page->setViewportHeight(area.getHeight());
        // Leave room for the scrollbar only when the page will scroll.
        int width = area.getWidth();
        int height = page->layout(width);
        if (height > area.getHeight() && !page->fills())
        {
            width -= theme_.sizes.scrollbar;
            height = page->layout(width);
        }
        page->setSize(width, page->fills() ? area.getHeight() : std::max(area.getHeight(), height));
        if (content_.getViewedComponent() != page)
            content_.setViewedComponent(page, false);
    }
}

} // namespace northlight::ui
