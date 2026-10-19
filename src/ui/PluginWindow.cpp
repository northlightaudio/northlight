#include "ui/PluginWindow.h"

#include "ui/Theme.h"

namespace aural::ui
{

PluginWindow::PluginWindow(core::Id channelId,
                           core::Id insertId,
                           juce::AudioPluginInstance& instance,
                           const juce::String& title)
    : juce::DocumentWindow(title, Theme::dark().colours.background, juce::DocumentWindow::closeButton),
      channelId_(channelId), insertId_(insertId)
{
    setUsingNativeTitleBar(true);
    juce::AudioProcessorEditor* editor =
        instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor(instance);
    const bool canResize = editor->isResizable();
    setContentOwned(editor, true);
    setResizable(canResize, false);
    setVisible(true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent(); // the editor goes before the instance it edits
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        juce::MessageManager::callAsync([close = onClose] { close(); });
}

} // namespace aural::ui
