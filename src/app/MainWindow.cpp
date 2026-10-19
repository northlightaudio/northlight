#include "MainWindow.h"

#include "ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
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
        button(importButton_, "Import...", [this] { importAudio(); });
        button(undoButton_, "Undo", [this] { session_.undo(); });
        button(redoButton_, "Redo", [this] { session_.redo(); });
        button(newButton_, "New", [this] { newProject(); });
        button(openButton_, "Open...", [this] { openProject(); });
        button(saveButton_, "Save", [this] { saveProject(false); });
        button(returnButton_, "|<", [this] { session_.returnToStart(); });
        button(playButton_, "Play", [this] { session_.togglePlay(); });
        button(recordButton_, "Rec", [this] { toggleRecord(); });
        button(clickButton_, "Click", [this] { session_.setMetronome(!session_.metronomeEnabled()); });
        button(countInButton_,
               "Count-in 1",
               [this] { session_.setCountInBars((session_.countInBars() + 1) % 3); });
        button(loopButton_, "Loop", [this] { session_.setLoopEnabled(!session_.loopEnabled()); });
        button(punchButton_, "Punch", [this] { session_.setPunchEnabled(!session_.punchEnabled()); });
        for (auto* b : {&clickButton_, &loopButton_, &punchButton_})
        {
            b->setColour(juce::TextButton::buttonOnColourId, theme_.colours.accent);
            b->setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
        }
        punchButton_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.danger);
        punchButton_.setColour(juce::TextButton::textColourOnId, theme_.colours.textPrimary);
        recordButton_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.danger);
        recordButton_.setColour(juce::TextButton::textColourOnId, theme_.colours.textPrimary);
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
        tracks_.onRegionChanged = [this](core::SamplePosition start, core::SamplePosition end)
        { session_.setRegion(start, end); };
        tracks_.onTrackInputChanged = [this](core::Id id, int channel)
        { session_.execute(std::make_unique<core::SetTrackInput>(id, channel)); };
        tracks_.onTrackArmChanged = [this](core::Id id, bool armed)
        { session_.execute(std::make_unique<core::SetTrackArmed>(id, armed)); };
        tracks_.onTrackMonitorChanged = [this](core::Id id, core::MonitorMode mode)
        { session_.execute(std::make_unique<core::SetTrackMonitor>(id, mode)); };
        tracks_.onImportFiles = [this](const std::vector<juce::File>& files, core::SamplePosition at)
        { importFiles(files, at); };
        tracks_.setAcceptedExtensions(session_.mediaLoader().supportedExtensions());
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
        audioSetupButton_.setBounds(bar.removeFromRight(110).withSizeKeepingCentre(110, h));
        bar.removeFromRight(gap);

        place(newButton_, 48);
        place(openButton_, 62);
        place(saveButton_, 48);
        place(importButton_, 96);
        place(undoButton_, 52);
        place(redoButton_, 52);
        bar.removeFromLeft(theme_.spacing.sm);
        place(returnButton_, 36);
        place(playButton_, 60);
        place(recordButton_, 48);
        place(positionLabel_, 176);
        place(clickButton_, 50);
        place(countInButton_, 80);
        place(loopButton_, 50);
        place(punchButton_, 54);

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
        if (key == juce::KeyPress('r', juce::ModifierKeys::commandModifier, 0))
        {
            toggleRecord();
            return true;
        }
        if (key == juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0))
        {
            saveProject(false);
            return true;
        }
        if (key ==
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        {
            saveProject(true);
            return true;
        }
        if (key == juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0))
        {
            openProject();
            return true;
        }
        if (key == juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0))
        {
            newProject();
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
        if (++ticks_ % 15 == 0)
            updateStatusText();
        const bool recording = session_.isRecording();
        tracks_.setPlayhead(session_.position(), session_.isPlaying(), recording);
        positionLabel_.setText(formatPosition(session_.project(), session_.position()),
                               juce::dontSendNotification);
        const bool playing = session_.isPlaying();
        if (playing != lastPlaying_ || recording != lastRecording_)
        {
            lastPlaying_ = playing;
            lastRecording_ = recording;
            playButton_.setButtonText(playing ? "Stop" : "Play");
            recordButton_.setButtonText(session_.isCountingIn() ? "..." : "Rec");
            playButton_.setToggleState(playing, juce::dontSendNotification);
            recordButton_.setToggleState(recording, juce::dontSendNotification);
        }
    }

    void toggleRecord()
    {
        if (session_.isRecording())
        {
            session_.stop();
            return;
        }
        const auto error = session_.startRecording();
        if (!error.empty())
        {
            lastImportError_ = "Cannot record: " + juce::String(error);
            refresh();
        }
    }

    void newProject()
    {
        if (session_.isRecording())
            return;
        session_.stop();
        session_.newProject();
        lastImportError_.clear();
        refresh();
    }

    void openProject()
    {
        if (session_.isRecording())
            return;
        chooser_ = std::make_unique<juce::FileChooser>(
            "Open project", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.aural");
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File())
                    return;
                session_.stop();
                const auto error = session_.open(file);
                lastImportError_ = error.empty() ? juce::String() : "Cannot open: " + juce::String(error);
                session_.recoverPartialRecordings();
                refresh();
                grabKeyboardFocus();
            });
    }

    void saveProject(bool saveAs)
    {
        if (!saveAs && session_.projectFile().getFullPathName().isNotEmpty())
        {
            const auto error = session_.save(session_.projectFile());
            lastImportError_ = error.empty() ? juce::String() : "Cannot save: " + juce::String(error);
            refresh();
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save project",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.aural"),
            "*.aural");
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                if (!file.hasFileExtension("aural"))
                    file = file.withFileExtension("aural");
                const auto error = session_.save(file);
                lastImportError_ = error.empty() ? juce::String() : "Cannot save: " + juce::String(error);
                refresh();
                grabKeyboardFocus();
            });
    }

    void refresh()
    {
        const auto s = audio_.status();
        {
            // Names of the active inputs in callback order.
            const auto all = audio_.inputChannelNames();
            std::vector<std::string> names;
            for (int channel : s.activeInputs)
                names.push_back(channel >= 0 && static_cast<size_t>(channel) < all.size()
                                    ? all[static_cast<size_t>(channel)]
                                    : "Input " + std::to_string(channel + 1));
            tracks_.setInputNames(std::move(names));
        }
        tracks_.setProject(&session_.project());
        tracks_.setRegion(
            session_.regionStart(), session_.regionEnd(), session_.loopEnabled(), session_.punchEnabled());
        undoButton_.setEnabled(session_.canUndo());
        redoButton_.setEnabled(session_.canRedo());
        saveButton_.setEnabled(!session_.isRecording());
        clickButton_.setToggleState(session_.metronomeEnabled(), juce::dontSendNotification);
        countInButton_.setButtonText("Count-in " + juce::String(session_.countInBars()));
        loopButton_.setToggleState(session_.loopEnabled(), juce::dontSendNotification);
        punchButton_.setToggleState(session_.punchEnabled(), juce::dontSendNotification);

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
        if (warning.isEmpty() && !session_.lastRecordingMessage().empty())
            warning = juce::String(session_.lastRecordingMessage());
        if (!lastImportError_.isEmpty())
            warning = lastImportError_;
        if (session_.projectFile().getFullPathName().isNotEmpty())
            text = session_.projectFile().getFileNameWithoutExtension() + (session_.isModified() ? "*" : "") +
                   separator + text;
        baseStatus_ = text;
        statusWarning_ = warning;
        updateStatusText();
    }

    // Appends the live load figures (p50/p99/max of callback time against the
    // deadline, overruns and driver-reported dropouts) to the status line.
    void updateStatusText()
    {
        juce::String text = baseStatus_;
        const auto stats = audio_.callback().stats().summary();
        if (stats.callbacks > 0)
        {
            text << separator << "load p50 " << juce::String(stats.p50 * 100.0, 1) << "%  p99 "
                 << juce::String(stats.p99 * 100.0, 1) << "%  max " << juce::String(stats.max * 100.0, 1)
                 << "%";
            if (stats.overruns > 0)
                text << separator << juce::String(stats.overruns) << " overruns";
            const int xruns = audio_.status().xruns;
            if (xruns > 0)
                text << separator << juce::String(xruns) << " dropouts";
        }
        if (statusWarning_.isNotEmpty())
            text << emDash << statusWarning_;
        deviceSummary_.setText(text, juce::dontSendNotification);
        deviceSummary_.setColour(juce::Label::textColourId,
                                 statusWarning_.isEmpty() ? theme_.colours.textSecondary
                                                          : theme_.colours.warning);
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
                                  std::vector<juce::File> files;
                                  for (const auto& f : fc.getResults())
                                      files.push_back(f);
                                  importFiles(files, 0);
                              });
    }

    void importFiles(const std::vector<juce::File>& files, core::SamplePosition at)
    {
        lastImportError_.clear();
        for (const auto& file : files)
        {
            const auto error = session_.importAudioFile(file, at);
            if (!error.empty())
                lastImportError_ = file.getFileName() + ": " + juce::String(error);
        }
        refresh();
        grabKeyboardFocus();
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    const ui::Theme& theme_ = ui::Theme::dark();
    juce::TextButton newButton_, openButton_, saveButton_, importButton_, undoButton_, redoButton_,
        returnButton_, playButton_, recordButton_, clickButton_, countInButton_, loopButton_, punchButton_,
        audioSetupButton_;
    juce::Label positionLabel_, deviceSummary_;
    ui::ArrangeView tracks_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String lastImportError_;
    juce::String baseStatus_, statusWarning_;
    bool lastPlaying_ = false;
    bool lastRecording_ = false;
    int ticks_ = 0;
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
