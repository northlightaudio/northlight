#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// A panel's header row: an uppercase caption title (textTertiary, tracked) at
// the left and ghost icon buttons at the right (detach, close), on surface0 with
// a lineSubtle rule beneath. Height: size.control.
class PanelHeader final : public juce::Component
{
public:
    explicit PanelHeader(const Theme& theme);

    void setTitle(const juce::String& title);
    // Optional actions; a button is shown only when its handler is set.
    std::function<void()> onDetach, onClose;
    void refreshButtons();
    int preferredHeight() const noexcept { return theme_.sizes.control; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const Theme& theme_;
    juce::String title_;
    juce::TextButton detach_{juce::CharPointer_UTF8("\xe2\x86\x97")},
        close_{juce::CharPointer_UTF8("\xc3\x97")};
};

} // namespace northlight::ui
