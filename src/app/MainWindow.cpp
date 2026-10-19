#include "MainWindow.h"

#include "ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Model.h"
#include "ui/ArrangeView.h"
#include "ui/AudioSetupView.h"
#include "ui/Theme.h"

namespace aural
{

namespace
{
// JUCE's String(const char*) is Latin-1; these UTF-8 source literals (/utf-8) go through fromUTF8.
const juce::String separator = juce::String::fromUTF8("  ·  "); // middle dot
const juce::String emDash = juce::String::fromUTF8("  —  ");

juce::String formatPosition(const core::Project& project, core::SamplePosition position)
{
    const auto bb =
        core::samplesToBarsBeats(position, project.sampleRate, project.tempoBpm, project.timeSignature);
    const double seconds =
        project.sampleRate > 0.0 ? static_cast<double>(position) / project.sampleRate : 0.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    juce::String time = juce::String(minutes) + ":" + juce::String(rest, 3).paddedLeft('0', 6);
    return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." +
           juce::String(bb.tick).paddedLeft('0', 3) + separator + time;
}
} // namespace

// Toolbar + track list.
class MainWindow::Content final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    Content(MainWindow& owner, audio::AudioDeviceLayer& audio, ProjectSession& session)
        : owner_(owner), audio_(audio), session_(session), tracks_(session.peakStore())
    {
        setSize(1280, 800);
        setWantsKeyboardFocus(true);

        const auto button = [this](juce::TextButton& b, const char* text, std::function<void()> action)
        {
            b.setButtonText(text);
            b.onClick = std::move(action);
            addAndMakeVisible(b);
        };
        button(importButton_, "Import Audio...", [this] { importAudio(); });
        button(undoButton_, "Undo", [this] { session_.undo(); });
        button(redoButton_, "Redo", [this] { session_.redo(); });
        button(returnButton_, "|<", [this] { session_.returnToStart(); });
        button(playButton_, "Play", [this] { session_.togglePlay(); });
        button(audioSetupButton_, "Audio Setup...", [this] { owner_.showAudioSetup(); });

        positionLabel_.setJustificationType(juce::Justification::centred);
        positionLabel_.setFont(juce::FontOptions{theme_.type.title});
        positionLabel_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
        addAndMakeVisible(positionLabel_);

        deviceSummary_.setJustificationType(juce::Justification::centredLeft);
        deviceSummary_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        deviceSummary_.setMinimumHorizontalScale(1.0f);
        addAndMakeVisible(deviceSummary_);

        tracks_.onLocate = [this](core::SamplePosition p) { session_.locate(p); };
        addAndMakeVisible(tracks_);

        audio_.addChangeListener(this);
        session_.addChangeListener(this);
        refresh();
        startTimerHz(30);
    }

    ~Content() override
    {
        stopTimer();
        session_.removeChangeListener(this);
        audio_.removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme_.colours.background);
        auto bar = getLocalBounds().removeFromTop(toolbarHeight);
        g.setColour(theme_.colours.surface);
        g.fillRect(bar);
        g.setColour(theme_.colours.border);
        g.fillRect(bar.removeFromBottom(1));

        auto status = getLocalBounds().removeFromBottom(statusHeight);
        g.setColour(theme_.colours.surface);
        g.fillRect(status);
        g.setColour(theme_.colours.border);
        g.fillRect(status.removeFromTop(1));
    }

    void resized() override
    {
        const int h = theme_.sizes.controlHeight;
        const int gap = theme_.spacing.sm;
        auto bar = getLocalBounds().removeFromTop(toolbarHeight).reduced(theme_.spacing.md, 0);
        const auto place = [&](juce::Component& c, int width)
        {
            c.setBounds(bar.removeFromLeft(width).withSizeKeepingCentre(width, h));
            bar.removeFromLeft(gap);
        };
        place(importButton_, 130);
        place(undoButton_, 64);
        place(redoButton_, 64);
        bar.removeFromLeft(theme_.spacing.lg);
        place(returnButton_, 40);
        place(playButton_, 72);
        place(positionLabel_, 220);

        audioSetupButton_.setBounds(bar.removeFromRight(130).withSizeKeepingCentre(130, h));

        auto status = getLocalBounds().removeFromBottom(statusHeight).reduced(theme_.spacing.md, 0);
        deviceSummary_.setBounds(status);

        tracks_.setBounds(getLocalBounds().withTrimmedTop(toolbarHeight).withTrimmedBottom(statusHeight));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::spaceKey)
        {
            session_.togglePlay();
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::homeKey)
        {
            session_.returnToStart();
            return true;
        }
        if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
        {
            session_.undo();
            return true;
        }
        if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0) ||
            key == juce::KeyPress(
                       'z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        {
            session_.redo();
            return true;
        }
        return false;
    }

private:
    static constexpr int toolbarHeight = 44;
    static constexpr int statusHeight = 28;

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    void timerCallback() override
    {
        tracks_.setPlayhead(session_.position(), session_.isPlaying());
        positionLabel_.setText(formatPosition(session_.project(), session_.position()),
                               juce::dontSendNotification);
        const bool playing = session_.isPlaying();
        if (playing != lastPlaying_)
        {
            lastPlaying_ = playing;
            playButton_.setButtonText(playing ? "Stop" : "Play");
            playButton_.setToggleState(playing, juce::dontSendNotification);
        }
    }

    void refresh()
    {
        tracks_.setProject(&session_.project());
        undoButton_.setEnabled(session_.canUndo());
        redoButton_.setEnabled(session_.canRedo());

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
        juce::String warning = s.message;
        if (warning.isEmpty())
            warning = session_.sampleRateWarning();
        if (!lastImportError_.isEmpty())
            warning = lastImportError_;
        if (warning.isNotEmpty())
            text << emDash << warning;
        deviceSummary_.setText(text, juce::dontSendNotification);
        deviceSummary_.setColour(juce::Label::textColourId,
                                 warning.isEmpty() ? theme_.colours.textSecondary : theme_.colours.warning);
    }

    void importAudio()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Import audio",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            session_.mediaLoader().supportedExtensions());
        const int chooserFlags = juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::canSelectMultipleItems;
        chooser_->launchAsync(chooserFlags,
                              [this](const juce::FileChooser& fc)
                              {
                                  lastImportError_.clear();
                                  for (const auto& file : fc.getResults())
                                  {
                                      const auto error = session_.importAudioFile(file, 0);
                                      if (!error.empty())
                                          lastImportError_ = file.getFileName() + ": " + juce::String(error);
                                  }
                                  refresh();
                                  grabKeyboardFocus();
                              });
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    const ui::Theme& theme_ = ui::Theme::dark();
    juce::TextButton importButton_, undoButton_, redoButton_, returnButton_, playButton_, audioSetupButton_;
    juce::Label positionLabel_, deviceSummary_;
    ui::ArrangeView tracks_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastImportError_;
    bool lastPlaying_ = false;
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

MainWindow::MainWindow(const juce::String& name, audio::AudioDeviceLayer& audio, ProjectSession& session)
    : juce::DocumentWindow(name, ui::Theme::dark().colours.background, juce::DocumentWindow::allButtons),
      audio_(audio), session_(session)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new Content(*this, audio_, session_), true);
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
