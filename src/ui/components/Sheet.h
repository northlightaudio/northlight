#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace northlight::ui
{

// Optional parts of a sheet (design-system.md, "Dialog (sheet)").
struct SheetOptions
{
    // A small line above the title in caption caps ("Step 1 of 2").
    juce::String eyebrow;
    // A quiet line at the left of the button row ("You can change this later").
    juce::String footnote;
    // The welcome hero: a band of the brand photograph across the top of the
    // panel with "Welcome to" and the wordmark (the first-run sheets).
    bool hero = false;
    // The panel's width (narrower when the window is).
    int width = 480;
};

// A dialog without a modal loop (design-system.md, "Dialog (sheet)"): an
// overlay over the owning component, a surface0 panel with a title, a body and
// right-aligned buttons, the primary last. Esc cancels; Enter confirms the
// primary unless it is destructive. The result arrives through the callback
// (the button index, -1 for cancel) and the sheet removes itself.
class Sheet final : public juce::Component
{
public:
    struct Button
    {
        juce::String label;
        bool primary = false;
        bool destructive = false;
    };
    // Shows a sheet over `host` (which must outlive it) and returns it; the
    // sheet deletes itself after the callback.
    static Sheet* show(juce::Component& host,
                       const Theme& theme,
                       const juce::String& title,
                       const juce::String& body,
                       std::vector<Button> buttons,
                       std::function<void(int)> onResult,
                       std::unique_ptr<juce::Component> content = nullptr,
                       int contentHeight = 0,
                       SheetOptions options = SheetOptions());
    void close() { finish(-1); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void parentSizeChanged() override { resized(); }

    static constexpr int heroHeight = 132;

private:
    Sheet(const Theme& theme,
          const juce::String& title,
          const juce::String& body,
          std::vector<Button> buttons,
          std::function<void(int)> onResult);
    void finish(int result);
    juce::Rectangle<int> panelBounds() const;
    int bodyHeight(int width) const;
    int eyebrowHeight() const;
    void paintHero(juce::Graphics& g, juce::Rectangle<int> panel) const;
    // Entering, the sheet fades in with its panel rising 8 px into place; leaving,
    // it fades out before it goes (design-system.md 2.6).
    void enter();
    int rise_ = 0; // how far below its place the panel is drawn, while entering

    const Theme& theme_;
    juce::String title_, body_;
    SheetOptions options_;
    std::unique_ptr<juce::Drawable> wordmark_; // the hero's
    std::vector<Button> buttons_;
    std::vector<std::unique_ptr<juce::TextButton>> buttonComponents_;
    std::function<void(int)> onResult_;
    std::unique_ptr<juce::Component> content_;
    int contentHeight_ = 0;
    bool finished_ = false;
};

} // namespace northlight::ui
