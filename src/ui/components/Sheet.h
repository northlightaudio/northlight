#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// A dialog without a modal loop (02-design-system.md, "Dialog (sheet)"): an
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
                       std::function<void(int)> onResult);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void parentSizeChanged() override { resized(); }

private:
    Sheet(const Theme& theme,
          const juce::String& title,
          const juce::String& body,
          std::vector<Button> buttons,
          std::function<void(int)> onResult);
    void finish(int result);
    juce::Rectangle<int> panelBounds() const;

    const Theme& theme_;
    juce::String title_, body_;
    std::vector<Button> buttons_;
    std::vector<std::unique_ptr<juce::TextButton>> buttonComponents_;
    std::function<void(int)> onResult_;
    bool finished_ = false;
};

} // namespace aural::ui
