#include "ui/ShortcutEditor.h"

#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"

#include <algorithm>

namespace northlight::ui
{

namespace
{
constexpr int keyCapHeight = 18;

juce::String quoted(const juce::String& text)
{
    return juce::String::fromUTF8("\xe2\x80\x9c") + text + juce::String::fromUTF8("\xe2\x80\x9d");
}

// What a row's keys draw: key caps, and joining words between them.
struct Piece
{
    juce::String text;
    bool cap = false;
    int width = 0;
};

std::vector<Piece> piecesFor(const Theme& theme, const juce::StringArray& keys, const juce::String& joiner)
{
    std::vector<Piece> pieces;
    const auto word = [&](const juce::String& text)
    {
        pieces.push_back(
            {text,
             false,
             juce::GlyphArrangement::getStringWidthInt(theme.small(), text) + 2 * theme.spacing.sm});
    };
    for (int k = 0; k < keys.size(); ++k)
    {
        if (k > 0)
            word(joiner);
        // A chord: one cap per key ("Ctrl++" is Ctrl and +).
        const auto& chord = keys[k];
        juce::StringArray parts;
        parts.addTokens(chord, "+", "");
        if (chord.endsWith("+") && chord.length() > 1)
            parts.add("+");
        parts.removeEmptyStrings();
        for (int p = 0; p < parts.size(); ++p)
        {
            const int text = juce::GlyphArrangement::getStringWidthInt(theme.caption(), parts[p]);
            pieces.push_back({parts[p], true, std::max(keyCapHeight, text + 2 * theme.spacing.sm)});
            if (p + 1 < parts.size())
                pieces.push_back({{}, false, theme.spacing.xxs});
        }
    }
    return pieces;
}

// A row's keys: alternatives joined by "or"; a fixed family of digits as a range
// ("1 to 7"), any other family with "/".
std::vector<Piece> keyPieces(const Theme& theme, const Shortcut& s)
{
    juce::StringArray keys;
    for (const auto& key : s.keys)
        if (key.listed)
            keys.add(key.text());
    if (s.fixed)
    {
        const bool digits =
            keys.size() > 2 &&
            std::all_of(keys.begin(),
                        keys.end(),
                        [](const juce::String& k) { return k.length() == 1 && k[0] >= '0' && k[0] <= '9'; });
        if (digits)
            return piecesFor(theme, {keys[0], keys[keys.size() - 1]}, "to");
        return piecesFor(theme, keys, "/");
    }
    return piecesFor(theme, keys, "or");
}

int widthOf(const std::vector<Piece>& pieces)
{
    int total = 0;
    for (const auto& p : pieces)
        total += p.width;
    return total;
}

void drawPieces(juce::Graphics& g,
                const Theme& theme,
                const std::vector<Piece>& pieces,
                juce::Rectangle<int> area)
{
    const auto& c = theme.colours;
    int x = area.getX();
    for (const auto& piece : pieces)
    {
        const auto r = juce::Rectangle<int>(x, area.getY(), piece.width, area.getHeight());
        if (piece.cap)
        {
            const auto cap = r.withSizeKeepingCentre(piece.width, keyCapHeight).toFloat();
            g.setColour(c.surface2);
            g.fillRoundedRectangle(cap, theme.radius.sm);
            g.setColour(c.lineStrong);
            g.drawRoundedRectangle(cap.reduced(0.5f), theme.radius.sm, 1.0f);
            g.setFont(theme.caption());
            g.setColour(c.textSecondary);
            g.drawText(piece.text, cap.toNearestInt(), juce::Justification::centred, false);
        }
        else if (piece.text.isNotEmpty())
        {
            g.setFont(theme.small());
            g.setColour(c.textTertiary);
            g.drawText(piece.text, r, juce::Justification::centred, false);
        }
        x += piece.width;
    }
}

int labelWidth(const Theme& theme, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidthInt(theme.label(), text) + 2 * theme.spacing.md;
}
} // namespace

// ----- the search icon -------------------------------------------------------------------------

class ShortcutEditor::SearchIcon final : public juce::Component
{
public:
    explicit SearchIcon(const Theme& theme) : theme_(theme) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override
    {
        Icons::draw(
            g,
            Icon::search,
            getLocalBounds().withSizeKeepingCentre(theme_.sizes.icon, theme_.sizes.icon).withX(0).toFloat(),
            theme_.colours.textTertiary);
    }

private:
    const Theme& theme_;
};

// ----- a row -----------------------------------------------------------------------------------

class ShortcutEditor::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Row(ShortcutEditor& owner)
        : owner_(owner), theme_(owner.theme_), reset_(theme_, Icon::undo, "Reset"), replace_("Replace"),
          cancel_("Cancel")
    {
        reset_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        reset_.onClick = [this]
        {
            if (entry_.shortcut != nullptr)
                owner_.reset(entry_.shortcut->command);
        };
        addChildComponent(reset_);
        replace_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "secondary");
        cancel_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        replace_.onClick = [this] { owner_.replace(); };
        cancel_.onClick = [this] { owner_.cancelRecording(); };
        addChildComponent(replace_);
        addChildComponent(cancel_);
    }

    void set(const Entry& entry, bool selected)
    {
        entry_ = entry;
        selected_ = selected;
        const bool action = entry_.kind == Entry::Kind::action && entry_.shortcut != nullptr;
        const bool rebound =
            action && !entry_.shortcut->fixed && !Shortcuts::isDefault(entry_.shortcut->command);
        reset_.setVisible(rebound);
        if (rebound)
        {
            juce::StringArray defaults;
            for (const auto& key : entry_.shortcut->defaults)
                if (key.listed)
                    defaults.add(key.text());
            reset_.setTooltip("Reset to " +
                              (defaults.isEmpty() ? juce::String("none") : defaults.joinIntoString(" or ")));
        }
        replace_.setVisible(entry_.kind == Entry::Kind::conflict);
        cancel_.setVisible(entry_.kind == Entry::Kind::conflict);
        setTooltip(action && entry_.shortcut->fixed ? "These keys work as a set and cannot be changed."
                   : action                         ? "Click the keys to change them."
                                                    : juce::String());
        setTitle(action ? juce::String(entry_.shortcut->name) : entry_.section);
        resized();
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.md, 0);
        reset_.setTopLeftPosition(area.getRight() - reset_.getWidth(),
                                  (getHeight() - reset_.getHeight()) / 2);
        if (entry_.kind == Entry::Kind::conflict)
        {
            const int h = theme_.sizes.controlCompact;
            const int y = (getHeight() - h) / 2;
            replace_.setBounds(
                area.getRight() - labelWidth(theme_, "Replace"), y, labelWidth(theme_, "Replace"), h);
            cancel_.setBounds(replace_.getX() - theme_.spacing.xs - labelWidth(theme_, "Cancel"),
                              y,
                              labelWidth(theme_, "Cancel"),
                              h);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto area = getLocalBounds();
        switch (entry_.kind)
        {
            case Entry::Kind::header:
                g.setFont(theme_.captionCaps());
                g.setColour(c.textTertiary);
                g.drawText(entry_.section.toUpperCase(),
                           area.reduced(theme_.spacing.md, 0).withTrimmedBottom(theme_.spacing.xs),
                           juce::Justification::bottomLeft,
                           false);
                return;
            case Entry::Kind::conflict:
            {
                g.setFont(theme_.small());
                g.setColour(c.warning);
                const auto text = area.reduced(theme_.spacing.md, 0)
                                      .withTrimmedLeft(theme_.spacing.lg)
                                      .withRight(cancel_.getX() - theme_.spacing.sm);
                g.drawFittedText(owner_.conflictText(), text, juce::Justification::centredLeft, 1, 1.0f);
                return;
            }
            case Entry::Kind::action:
                break;
        }
        if (entry_.shortcut == nullptr)
            return;
        const bool recording = owner_.recording_ == entry_.shortcut->command;
        if (hover_ || (selected_ && owner_.list_.hasKeyboardFocus(true)))
        {
            g.setColour(c.surface2);
            g.fillRoundedRectangle(area.reduced(theme_.spacing.xs, 0).toFloat(), theme_.radius.sm);
        }
        area = area.reduced(theme_.spacing.md, 0);
        const auto keys = keysArea();
        if (recording)
        {
            const auto field = keys.toFloat().withSizeKeepingCentre(static_cast<float>(keys.getWidth()),
                                                                    static_cast<float>(keyCapHeight + 4));
            g.setColour(c.surface2);
            g.fillRoundedRectangle(field, theme_.radius.sm);
            g.setColour(c.accent);
            g.drawRoundedRectangle(field.reduced(0.5f), theme_.radius.sm, 1.0f);
            g.setFont(theme_.small());
            g.setColour(c.textSecondary);
            g.drawText("Press a key" + juce::String::fromUTF8("\xe2\x80\xa6"),
                       field.toNearestInt(),
                       juce::Justification::centred,
                       false);
        }
        else
        {
            const auto pieces = keyPieces(theme_, *entry_.shortcut);
            if (overKeys_ && !entry_.shortcut->fixed)
            {
                g.setColour(c.surface3);
                g.fillRoundedRectangle(keys.expanded(theme_.spacing.xs, 0)
                                           .withSizeKeepingCentre(keys.getWidth() + 2 * theme_.spacing.xs,
                                                                  keyCapHeight + 2 * theme_.spacing.xxs)
                                           .toFloat(),
                                       theme_.radius.sm);
            }
            if (pieces.empty())
            {
                g.setFont(theme_.small());
                g.setColour(c.textTertiary);
                g.drawText("None", keys, juce::Justification::centredRight, false);
            }
            else
                drawPieces(g, theme_, pieces, keys);
        }
        area.setRight(keys.getX() - theme_.spacing.lg);
        g.setFont(theme_.body());
        g.setColour(c.textPrimary);
        g.drawText(entry_.shortcut->name, area, juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent& e) override { mouseMove(e); }
    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool over = entry_.kind == Entry::Kind::action &&
                          keysArea().expanded(theme_.spacing.xs).contains(e.getPosition());
        if (!hover_ || over != overKeys_)
        {
            hover_ = entry_.kind == Entry::Kind::action;
            overKeys_ = over;
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = overKeys_ = false;
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (entry_.kind != Entry::Kind::action || entry_.shortcut == nullptr || entry_.shortcut->fixed ||
            !e.mouseWasClicked() || !keysArea().expanded(theme_.spacing.xs).contains(e.getPosition()))
            return;
        owner_.startRecording(entry_.shortcut->command);
    }

private:
    // Where the keys draw: right aligned before the reset button's slot.
    juce::Rectangle<int> keysArea() const
    {
        auto area = getLocalBounds().reduced(theme_.spacing.md, 0);
        area.removeFromRight(theme_.sizes.controlCompact + theme_.spacing.xs);
        int width = theme_.spacing.xl;
        if (entry_.shortcut != nullptr)
        {
            const auto pieces = keyPieces(theme_, *entry_.shortcut);
            width = pieces.empty() ? juce::GlyphArrangement::getStringWidthInt(theme_.small(), "None")
                                   : widthOf(pieces);
            if (owner_.recording_ == entry_.shortcut->command)
                width = std::max(width, recordingWidth);
        }
        return area.removeFromRight(width);
    }
    static constexpr int recordingWidth = 112;

    ShortcutEditor& owner_;
    const Theme& theme_;
    Entry entry_;
    bool selected_ = false, hover_ = false, overKeys_ = false;
    IconButton reset_;
    juce::TextButton replace_, cancel_;
};

// ----- the editor ------------------------------------------------------------------------------

ShortcutEditor::ShortcutEditor(const Theme& theme) : theme_(theme), icon_(std::make_unique<SearchIcon>(theme))
{
    setWantsKeyboardFocus(true);
    search_.setFont(theme_.body());
    search_.setJustification(juce::Justification::centredLeft);
    search_.setIndents(theme_.spacing.sm + theme_.sizes.icon + theme_.spacing.sm, 0);
    search_.setSelectAllWhenFocused(false);
    search_.setTextToShowWhenEmpty("Search shortcuts", theme_.colours.textTertiary);
    search_.setTitle("Search shortcuts");
    search_.onTextChange = [this] { filter(); };
    search_.onEscapeKey = [this] { search_.clear(); };
    addAndMakeVisible(search_);
    addAndMakeVisible(*icon_);
    resetAll_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "secondary");
    resetAll_.setTooltip("Put every shortcut back to its default");
    resetAll_.onClick = [this] { resetAll(); };
    addAndMakeVisible(resetAll_);
    list_.setModel(this);
    list_.setRowHeight(theme_.sizes.rowHeight);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list_.setOutlineThickness(0);
    list_.setTitle("Shortcuts");
    addAndMakeVisible(list_);
    filter();
}

ShortcutEditor::~ShortcutEditor()
{
    list_.setModel(nullptr);
}

juce::Rectangle<int> ShortcutEditor::cardBounds() const
{
    const int hint = static_cast<int>(std::ceil(theme_.small().getHeight()));
    return getLocalBounds().withTrimmedTop(theme_.sizes.control + theme_.spacing.xs + hint +
                                           theme_.spacing.md);
}

void ShortcutEditor::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto card = cardBounds().toFloat();
    g.setColour(c.surface1);
    g.fillRoundedRectangle(card, theme_.radius.md);
    g.setColour(c.lineSubtle);
    g.drawRoundedRectangle(card.reduced(0.5f), theme_.radius.md, 1.0f);
    // The hint under the search.
    g.setFont(theme_.small());
    g.setColour(c.textTertiary);
    g.drawText("Click a shortcut's keys to change them. Shift with a panel's key detaches or reattaches it.",
               getLocalBounds()
                   .withTrimmedTop(theme_.sizes.control + theme_.spacing.xs)
                   .withHeight(static_cast<int>(std::ceil(theme_.small().getHeight()))),
               juce::Justification::centredLeft,
               true);
    if (rows_.empty())
    {
        g.setFont(theme_.body());
        g.setColour(c.textTertiary);
        g.drawText("No shortcuts match " + quoted(search_.getText().trim()) + ".",
                   cardBounds().reduced(theme_.spacing.lg),
                   juce::Justification::centred,
                   true);
    }
}

void ShortcutEditor::resized()
{
    auto top = getLocalBounds().removeFromTop(theme_.sizes.control);
    const int resetWidth = labelWidth(theme_, resetAll_.getButtonText());
    resetAll_.setBounds(top.removeFromRight(resetWidth));
    top.removeFromRight(theme_.spacing.sm);
    search_.setBounds(top);
    icon_->setBounds(search_.getBounds()
                         .withWidth(theme_.spacing.sm + theme_.sizes.icon)
                         .withTrimmedLeft(theme_.spacing.sm));
    list_.setBounds(cardBounds().reduced(theme_.spacing.xxs, theme_.spacing.sm));
}

void ShortcutEditor::filter()
{
    rows_.clear();
    const auto needle = search_.getText().trim();
    for (const auto* section : Shortcuts::sections())
    {
        bool headed = false;
        for (const auto& s : Shortcuts::all())
        {
            if (juce::String(s.section) != section)
                continue;
            juce::String keys;
            for (const auto& key : s.keys)
                keys << key.text() << " ";
            if (needle.isNotEmpty() && !juce::String(s.name).containsIgnoreCase(needle) &&
                !keys.containsIgnoreCase(needle) && !juce::String(section).containsIgnoreCase(needle))
                continue;
            if (!headed)
            {
                rows_.push_back({Entry::Kind::header, section, nullptr});
                headed = true;
            }
            rows_.push_back({Entry::Kind::action, section, &s});
            if (pending_.has_value() && pending_->command == s.command)
                rows_.push_back({Entry::Kind::conflict, section, &s});
        }
    }
    resetAll_.setEnabled(!Shortcuts::allDefault());
    list_.updateContent();
    list_.repaint();
    repaint();
}

void ShortcutEditor::changed()
{
    pending_.reset();
    filter();
    if (onChanged)
        onChanged(Shortcuts::save());
}

std::vector<const Shortcut*> ShortcutEditor::listedActions() const
{
    std::vector<const Shortcut*> out;
    for (const auto& row : rows_)
        out.push_back(row.kind == Entry::Kind::action ? row.shortcut : nullptr);
    return out;
}

void ShortcutEditor::startRecording(Command command)
{
    if (Shortcuts::of(command).fixed)
        return;
    pending_.reset();
    recording_ = command;
    filter();
    grabKeyboardFocus();
}

void ShortcutEditor::cancelRecording()
{
    recording_.reset();
    pending_.reset();
    filter();
}

void ShortcutEditor::recordKey(const juce::KeyPress& press)
{
    if (!recording_.has_value())
        return;
    const auto command = *recording_;
    recording_.reset();
    if (!Key::bindable(press))
    {
        filter();
        return;
    }
    const auto key = Key::fromPress(press);
    const auto& keys = Shortcuts::of(command).keys;
    if (keys.size() == 1 && keys.front().sameAs(key))
    {
        filter(); // already its key
        return;
    }
    const auto others = Shortcuts::conflicts(command, key);
    if (others.empty())
    {
        Shortcuts::bind(command, key);
        changed();
        return;
    }
    pending_ = Pending{command, key, others};
    filter();
}

juce::String ShortcutEditor::conflictText() const
{
    if (!pending_.has_value() || pending_->others.empty())
        return {};
    auto text = pending_->key.text() + " is used by " + quoted(Shortcuts::of(pending_->others.front()).name);
    if (pending_->others.size() > 1)
        text << " and " << static_cast<int>(pending_->others.size() - 1)
             << (pending_->others.size() == 2 ? " other" : " others");
    return text + ".";
}

void ShortcutEditor::replace()
{
    if (!pending_.has_value())
        return;
    const auto p = *pending_;
    Shortcuts::bind(p.command, p.key, p.others);
    changed();
}

void ShortcutEditor::reset(Command command)
{
    Shortcuts::reset(command);
    changed();
}

void ShortcutEditor::resetAll()
{
    recording_.reset();
    Shortcuts::resetAll();
    changed();
}

void ShortcutEditor::setSearch(const juce::String& text)
{
    search_.setText(text, false);
    filter(); // the field's own change message comes later, asynchronously
}

bool ShortcutEditor::keyPressed(const juce::KeyPress& key)
{
    if (recording_.has_value())
    {
        if (key.getKeyCode() == juce::KeyPress::tabKey)
        {
            cancelRecording();
            return false; // the focus moves on
        }
        recordKey(key);
        return true;
    }
    if (pending_.has_value() && key == juce::KeyPress::escapeKey)
    {
        cancelRecording();
        return true;
    }
    return false;
}

void ShortcutEditor::focusLost(FocusChangeType)
{
    // A click elsewhere ends the recording, as Esc does.
    if (recording_.has_value())
        cancelRecording();
}

int ShortcutEditor::getNumRows()
{
    return static_cast<int>(rows_.size());
}

juce::Component* ShortcutEditor::refreshComponentForRow(int row, bool selected, juce::Component* existing)
{
    if (row < 0 || static_cast<size_t>(row) >= rows_.size())
    {
        delete existing;
        return nullptr;
    }
    auto* r = dynamic_cast<Row*>(existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row(*this);
    }
    r->set(rows_[static_cast<size_t>(row)], selected);
    return r;
}

void ShortcutEditor::returnKeyPressed(int row)
{
    if (row >= 0 && static_cast<size_t>(row) < rows_.size() &&
        rows_[static_cast<size_t>(row)].kind == Entry::Kind::action)
        startRecording(rows_[static_cast<size_t>(row)].shortcut->command);
}

} // namespace northlight::ui
