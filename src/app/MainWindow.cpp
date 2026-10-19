#include "MainWindow.h"

#include "audio/AudioDeviceLayer.h"
#include "ui/AudioSetupView.h"
#include "ui/Theme.h"

namespace aural
{

namespace
{
// JUCE's String(const char*) is Latin-1; these UTF-8 source literals (/utf-8) go through fromUTF8.
const juce::String separator = juce::String::fromUTF8("  ·  "); // middle dot
const juce::String emDash = juce::String::fromUTF8("  —  ");
} // namespace

// Toolbar + placeholder body.
class MainWindow::Content final : public juce::Component, private juce::ChangeListener
{
public:
    Content(MainWindow& owner, audio::AudioDeviceLayer& audio) : owner_(owner), audio_(audio)
    {
        setSize(1280, 800);

        audioSetupButton_.setButtonText("Audio Setup...");
        audioSetupButton_.onClick = [this] { owner_.showAudioSetup(); };
        addAndMakeVisible(audioSetupButton_);

        deviceSummary_.setJustificationType(juce::Justification::centredLeft);
        deviceSummary_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        addAndMakeVisible(deviceSummary_);

        audio_.addChangeListener(this);
        refreshSummary();
    }

    ~Content() override { audio_.removeChangeListener(this); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme_.colours.background);

        auto bar = getLocalBounds().removeFromTop(toolbarHeight);
        g.setColour(theme_.colours.surface);
        g.fillRect(bar);
        g.setColour(theme_.colours.border);
        g.fillRect(bar.removeFromBottom(1));

        g.setColour(theme_.colours.textDisabled);
        g.setFont(juce::FontOptions{theme_.type.heading});
        g.drawText("Aural", getLocalBounds().withTrimmedTop(toolbarHeight), juce::Justification::centred);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop(toolbarHeight).reduced(theme_.spacing.md, 0);
        audioSetupButton_.setBounds(
            bar.removeFromRight(130).withSizeKeepingCentre(130, theme_.sizes.controlHeight));
        bar.removeFromRight(theme_.spacing.md);
        deviceSummary_.setBounds(bar);
    }

private:
    static constexpr int toolbarHeight = 44;

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshSummary(); }

    void refreshSummary()
    {
        const auto s = audio_.status();
        juce::String text;
        if (!s.open)
        {
            text = "No audio device";
        }
        else
        {
            text << s.deviceType << separator << s.outputDevice << separator
                 << juce::String(s.sampleRate / 1000.0, 1) << " kHz" << separator << s.bufferSize
                 << " samples" << separator
                 << juce::String(s.inputLatencyMilliseconds() + s.outputLatencyMilliseconds(), 1)
                 << " ms round trip (reported)";
        }
        if (!s.message.empty())
            text << emDash << s.message;
        deviceSummary_.setText(text, juce::dontSendNotification);
        deviceSummary_.setColour(juce::Label::textColourId,
                                 s.message.empty() ? theme_.colours.textSecondary : theme_.colours.warning);
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    const ui::Theme& theme_ = ui::Theme::dark();
    juce::TextButton audioSetupButton_;
    juce::Label deviceSummary_;
};

// Non-modal secondary window hosting the Audio Setup view. Closing it asks the main
// window to destroy it on the next message-loop turn, never from inside its own
// callback.
class MainWindow::AudioSetupWindow final : public juce::DocumentWindow
{
public:
    AudioSetupWindow(MainWindow& owner, audio::AudioDeviceLayer& audio)
        : juce::DocumentWindow("Audio Setup",
                               ui::Theme::dark().colours.background,
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner_(owner)
    {
        setUsingNativeTitleBar(true);
        auto* view = new ui::AudioSetupView(audio);
        view->setSize(ui::AudioSetupView::preferredWidth, ui::AudioSetupView::preferredHeight);
        setContentOwned(view, true);
        setResizable(true, false);
        setResizeLimits(ui::AudioSetupView::minimumWidth, ui::AudioSetupView::minimumHeight, 4000, 4000);
        centreAroundComponent(&owner, getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::Component::SafePointer<MainWindow> owner(&owner_);
        juce::MessageManager::callAsync(
            [owner]
            {
                if (owner != nullptr)
                    owner->closeAudioSetup();
            });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButtonPressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

private:
    MainWindow& owner_;
};

MainWindow::MainWindow(const juce::String& name, audio::AudioDeviceLayer& audio)
    : juce::DocumentWindow(name, ui::Theme::dark().colours.background, juce::DocumentWindow::allButtons),
      audio_(audio)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new Content(*this, audio_), true);
    setResizable(true, false);
    setResizeLimits(640, 400, 10000, 10000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainWindow::~MainWindow()
{
    audioSetupWindow_.reset();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::showAudioSetup()
{
    if (audioSetupWindow_ == nullptr)
        audioSetupWindow_ = std::make_unique<AudioSetupWindow>(*this, audio_);
    audioSetupWindow_->toFront(true);
}

void MainWindow::closeAudioSetup()
{
    audioSetupWindow_.reset();
}

} // namespace aural
