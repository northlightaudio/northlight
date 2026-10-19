#include "ui/PluginWindow.h"

namespace aural::ui
{

// The bar plus the plugin's editor. Follows the editor's size (a plugin may
// resize itself) and tells the window.
class PluginWindow::Holder final : public juce::Component
{
public:
    Holder(PluginWindow& window, juce::AudioProcessorEditor* editor) : window_(window), editor_(editor)
    {
        setOpaque(true);
        bypass_.setButtonText("Bypass");
        bypass_.setClickingTogglesState(true);
        bypass_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.warning);
        bypass_.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
        bypass_.onClick = [this]
        {
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertBypass>(
                    window_.channelId(), window_.insertId(), bypass_.getToggleState()));
        };
        addAndMakeVisible(bypass_);

        mixLabel_.setText("Mix", juce::dontSendNotification);
        mixLabel_.setFont(juce::FontOptions{theme_.type.caption});
        mixLabel_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        addAndMakeVisible(mixLabel_);
        mix_.setSliderStyle(juce::Slider::LinearHorizontal);
        mix_.setRange(0.0, 100.0, 1.0);
        mix_.setTextValueSuffix("%");
        mix_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
        mix_.setDoubleClickReturnValue(true, 100.0);
        mix_.setValue(100.0, juce::dontSendNotification);
        mix_.onValueChange = [this]
        {
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertMix>(
                    window_.channelId(), window_.insertId(), static_cast<float>(mix_.getValue() / 100.0)));
        };
        addAndMakeVisible(mix_);

        pin_.setButtonText("Pin");
        pin_.setTooltip("Keep this window above the others");
        pin_.setClickingTogglesState(true);
        pin_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.accent);
        pin_.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
        pin_.onClick = [this]
        {
            window_.setAlwaysOnTop(pin_.getToggleState());
            if (window_.onPinChanged)
                window_.onPinChanged(pin_.getToggleState());
        };
        addAndMakeVisible(pin_);

        addAndMakeVisible(editor_);
        setSize(std::max(editor_->getWidth(), 260), editor_->getHeight() + PluginWindow::barHeight);
    }

    ~Holder() override
    {
        // The editor must go before the plugin instance it edits; its destructor
        // tells the plugin.
        removeChildComponent(editor_);
        delete editor_;
    }

    void setPinned(bool pinned) { pin_.setToggleState(pinned, juce::dontSendNotification); }
    void setInsert(const core::Insert& insert)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        bypass_.setToggleState(insert.bypass, juce::dontSendNotification);
        mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme_.colours.background);
        g.setColour(theme_.colours.border);
        g.fillRect(0, PluginWindow::barHeight - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop(PluginWindow::barHeight).reduced(theme_.spacing.sm, 4);
        bypass_.setBounds(bar.removeFromLeft(64));
        bar.removeFromLeft(theme_.spacing.sm);
        pin_.setBounds(bar.removeFromRight(44));
        bar.removeFromRight(theme_.spacing.sm);
        mixLabel_.setBounds(bar.removeFromLeft(28));
        mix_.setBounds(bar);
        const juce::ScopedValueSetter<bool> guard(layingOut_, true);
        editor_->setTopLeftPosition(0, PluginWindow::barHeight);
    }

    // The plugin resized its editor: grow or shrink the window with it.
    void childBoundsChanged(juce::Component* child) override
    {
        if (child != editor_ || layingOut_)
            return;
        const int w = std::max(editor_->getWidth(), 260);
        const int h = editor_->getHeight() + PluginWindow::barHeight;
        if (w != getWidth() || h != getHeight())
            window_.setContentComponentSize(w, h);
    }

private:
    const Theme& theme_ = Theme::dark();
    PluginWindow& window_;
    juce::AudioProcessorEditor* editor_;
    juce::TextButton bypass_, pin_;
    juce::Label mixLabel_;
    juce::Slider mix_;
    bool updating_ = false;
    bool layingOut_ = false;
};

PluginWindow::PluginWindow(core::Id channelId,
                           core::Id insertId,
                           juce::AudioPluginInstance& instance,
                           const juce::String& title,
                           bool pinned)
    : juce::DocumentWindow(title, Theme::dark().colours.background, juce::DocumentWindow::closeButton),
      channelId_(channelId), insertId_(insertId)
{
    setUsingNativeTitleBar(true);
    juce::AudioProcessorEditor* editor =
        instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor(instance);
    const bool canResize = editor->isResizable();
    auto holder = std::make_unique<Holder>(*this, editor);
    holder_ = holder.get();
    holder_->setPinned(pinned);
    setContentOwned(holder.release(), true);
    setResizable(canResize, false);
    setAlwaysOnTop(pinned);
    setVisible(true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent(); // deletes the holder, which deletes the editor first
    holder_ = nullptr;
}

void PluginWindow::setInsert(const core::Insert& insert)
{
    if (holder_ != nullptr)
        holder_->setInsert(insert);
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        juce::MessageManager::callAsync([close = onClose] { close(); });
}

void PluginWindow::moved()
{
    juce::DocumentWindow::moved();
    if (onMoved && isVisible())
        onMoved(getPosition());
}

} // namespace aural::ui
