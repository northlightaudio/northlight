#pragma once

#include "ui/Shortcuts.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The Preferences Shortcuts page (screens/setup.md 8.2, design-system.md "Key
// binding"): a search field with Reset all beside it, a hint, and a card with
// the keyboard map grouped by section, one row per action with its keys as key
// caps. Clicking a rebindable action's keys (or Enter on its row) records the
// next key pressed; a key another action in an overlapping context uses opens a
// conflict line under the row naming that action, with Replace and Cancel. A
// rebound row has a reset button. Fixed families (the tools' 1 to 7, the arrows)
// are listed, not rebindable. Every change is applied to ui::Shortcuts at once
// and handed to onChanged as the settings text.
namespace northlight::ui
{

class ShortcutEditor final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ShortcutEditor(const Theme& theme);
    ~ShortcutEditor() override;

    // The bindings changed: their settings text (Shortcuts::save()).
    std::function<void(const std::string& bindings)> onChanged;

    // ----- what a click or key does (public for tests) --------------------------------
    void startRecording(Command command);
    void cancelRecording();
    bool isRecording() const noexcept { return recording_.has_value(); }
    // A key pressed while recording: binds it, or opens the conflict line.
    void recordKey(const juce::KeyPress& press);
    // The conflict line's text ("" when there is none) and its two choices.
    juce::String conflictText() const;
    void replace(); // takes the key from the other action(s) and binds it
    void reset(Command command);
    void resetAll();
    void setSearch(const juce::String& text);
    int rows() const noexcept { return static_cast<int>(rows_.size()); }
    // The visible rows' actions (null: a header or the conflict line), for tests.
    std::vector<const Shortcut*> listedActions() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusLost(FocusChangeType cause) override;

private:
    class Row;
    class SearchIcon;
    struct Entry
    {
        enum class Kind
        {
            header,
            action,
            conflict,
        } kind = Kind::header;
        juce::String section;
        const Shortcut* shortcut = nullptr;
    };
    struct Pending
    {
        Command command;
        Key key;
        std::vector<Command> others;
    };

    juce::Rectangle<int> cardBounds() const;
    void filter();
    void changed();

    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;
    void returnKeyPressed(int row) override;

    const Theme& theme_;
    juce::TextEditor search_;
    std::unique_ptr<SearchIcon> icon_;
    juce::TextButton resetAll_{"Reset all"};
    juce::ListBox list_;
    std::vector<Entry> rows_;
    std::optional<Command> recording_;
    std::optional<Pending> pending_;
};

} // namespace northlight::ui
