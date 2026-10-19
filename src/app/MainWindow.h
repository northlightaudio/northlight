#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace aural
{

// Top-level document window. Phase 0: an empty, resizable window with a placeholder.
// The arrange view, transport and mixer land here in Phases 2 and 4.
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name);

    void closeButtonPressed() override;

private:
    class PlaceholderContent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace aural
