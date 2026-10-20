#include "MainWindow.h"

#include "ProjectSession.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/Midi.h"
#include "engine/Snapshot.h"
#include "plugins/BuiltIns.h"
#include "ui/ArrangeView.h"
#include "ui/AudioSetupView.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/MixerView.h"
#include "ui/PianoRoll.h"
#include "ui/PluginBrowser.h"
#include "ui/PreferencesView.h"
#include "ui/Theme.h"
#include "ui/components/Meter.h"
#include "ui/components/Sheet.h"
#include "ui/components/Toast.h"
#include "ui/panels/DiagnosticsPanel.h"
#include "ui/panels/EditorPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/shell/HeaderBar.h"
#include "ui/shell/Panels.h"
#include "ui/shell/StatusBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <optional>

namespace aural
{

namespace
{
juce::String barsText(const core::Project& project, core::SamplePosition position)
{
    const auto bb = core::samplesToBarsBeats(
        std::abs(position), project.sampleRate, project.tempoBpm, project.timeSignature);
    return juce::String(position < 0 ? "-" : "") + juce::String(bb.bar) + "." + juce::String(bb.beat) + "." +
           juce::String(bb.tick).paddedLeft('0', 3);
}

juce::String timeText(const core::Project& project, core::SamplePosition position)
{
    const double seconds =
        project.sampleRate > 0.0 ? static_cast<double>(std::abs(position)) / project.sampleRate : 0.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - minutes * 60.0;
    return juce::String(position < 0 ? "-" : "") + juce::String(minutes) + ":" +
           juce::String(rest, 3).paddedLeft('0', 6);
}

// "b.b.t" -> samples, or -1 when the text is not a position.
core::SamplePosition parseBars(const core::Project& project, const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens(text.trim(), ".", "");
    if (parts.isEmpty() || parts[0].getIntValue() < 1)
        return -1;
    core::BarsBeats bb;
    bb.bar = parts[0].getIntValue();
    bb.beat = parts.size() > 1 ? std::max(1, parts[1].getIntValue()) : 1;
    bb.tick = parts.size() > 2 ? std::max(0, parts[2].getIntValue()) : 0;
    return core::barsBeatsToSamples(bb, project.sampleRate, project.tempoBpm, project.timeSignature);
}

// "m:ss.mmm" or "ss.mmm" -> samples, or -1.
core::SamplePosition parseTime(const core::Project& project, const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens(text.trim(), ":", "");
    double seconds = 0.0;
    if (parts.size() == 2)
        seconds = parts[0].getDoubleValue() * 60.0 + parts[1].getDoubleValue();
    else if (parts.size() == 1 && parts[0].isNotEmpty() && parts[0].containsOnly("0123456789."))
        seconds = parts[0].getDoubleValue();
    else
        return -1;
    return static_cast<core::SamplePosition>(seconds * project.sampleRate);
}

// A splitter between the arrange view and a host (02-design-system.md): a 5 px
// hit area over a 1 px line, accent on hover, double-click resets the size.
class Splitter final : public juce::Component
{
public:
    Splitter(const ui::Theme& theme, bool horizontal) : theme_(theme), horizontal_(horizontal)
    {
        setMouseCursor(horizontal ? juce::MouseCursor::UpDownResizeCursor
                                  : juce::MouseCursor::LeftRightResizeCursor);
        setRepaintsOnMouseActivity(true);
    }
    std::function<void(int delta)> onDragged;
    std::function<void()> onReset;

    void paint(juce::Graphics& g) override
    {
        g.setColour(isMouseOverOrDragging() ? theme_.colours.accent : theme_.colours.lineSubtle);
        if (horizontal_)
            g.fillRect(0, getHeight() / 2, getWidth(), 1);
        else
            g.fillRect(getWidth() / 2, 0, 1, getHeight());
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        last_ = horizontal_ ? e.getScreenY() : e.getScreenX();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int now = horizontal_ ? e.getScreenY() : e.getScreenX();
        if (onDragged && now != last_)
            onDragged(now - last_);
        last_ = now;
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onReset)
            onReset();
    }

private:
    const ui::Theme& theme_;
    bool horizontal_;
    int last_ = 0;
};

// The first-run sheet's content (08-setup.md 8.3): driver, device, buffer size
// and a live input meter so the user sees signal before the project opens.
class FirstRunContent final : public juce::Component, private juce::Timer
{
public:
    FirstRunContent(const ui::Theme& theme, audio::AudioDeviceLayer& audio)
        : theme_(theme), audio_(audio), meter_(theme, 1, false)
    {
        for (auto* label : {&driverLabel_, &deviceLabel_, &bufferLabel_, &meterLabel_})
        {
            label->setFont(theme_.body());
            label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
            addAndMakeVisible(*label);
        }
        driverLabel_.setText("Driver", juce::dontSendNotification);
        deviceLabel_.setText("Device", juce::dontSendNotification);
        bufferLabel_.setText("Buffer size", juce::dontSendNotification);
        meterLabel_.setText("Input", juce::dontSendNotification);
        addAndMakeVisible(driver_);
        addAndMakeVisible(device_);
        addAndMakeVisible(buffer_);
        addAndMakeVisible(meter_);
        types_ = audio_.deviceTypes();
        const auto status = audio_.status();
        for (size_t i = 0; i < types_.size(); ++i)
        {
            driver_.addItem(types_[i].name, static_cast<int>(i) + 1);
            if (types_[i].name == status.deviceType ||
                (status.deviceType.empty() && types_[i].name == "ASIO"))
                driver_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        if (driver_.getSelectedId() == 0 && !types_.empty())
            driver_.setSelectedId(1, juce::dontSendNotification);
        driver_.onChange = [this] { fillDevices(); };
        fillDevices();
        for (const int size : {32, 64, 128, 256, 512, 1024})
        {
            buffer_.addItem(juce::String(size), size);
            if (size == (status.bufferSize > 0 ? status.bufferSize : 128))
                buffer_.setSelectedId(size, juce::dontSendNotification);
        }
        startTimerHz(20);
    }
    ~FirstRunContent() override { stopTimer(); }

    // Opens the chosen device; the error, if any.
    std::optional<std::string> apply()
    {
        audio::AudioSettings settings = audio_.settings();
        const int type = driver_.getSelectedId() - 1;
        if (type >= 0 && static_cast<size_t>(type) < types_.size())
            settings.deviceType = types_[static_cast<size_t>(type)].name;
        const int device = device_.getSelectedId() - 1;
        if (device >= 0 && static_cast<size_t>(device) < names_.outputs.size())
        {
            settings.outputDevice = names_.outputs[static_cast<size_t>(device)];
            settings.inputDevice =
                names_.inputs.empty()
                    ? settings.outputDevice
                    : names_.inputs[std::min(names_.inputs.size() - 1, static_cast<size_t>(device))];
        }
        settings.bufferSize = buffer_.getSelectedId();
        return audio_.apply(settings);
    }

    static int preferredHeight(const ui::Theme& theme)
    {
        return 4 * (theme.sizes.control + theme.spacing.sm);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int labelW = 120;
        const auto row = [&](juce::Label& label, juce::Component& control, int height)
        {
            auto r = area.removeFromTop(theme_.sizes.control + theme_.spacing.sm)
                         .withTrimmedBottom(theme_.spacing.sm);
            label.setBounds(r.removeFromLeft(labelW));
            control.setBounds(r.withSizeKeepingCentre(r.getWidth(), height));
        };
        row(driverLabel_, driver_, theme_.sizes.control);
        row(deviceLabel_, device_, theme_.sizes.control);
        row(bufferLabel_, buffer_, theme_.sizes.control);
        row(meterLabel_, meter_, theme_.sizes.meterWidth * 2 + theme_.spacing.xs);
    }

private:
    void fillDevices()
    {
        const int type = driver_.getSelectedId() - 1;
        device_.clear(juce::dontSendNotification);
        if (type < 0 || static_cast<size_t>(type) >= types_.size())
            return;
        names_ = audio_.deviceNames(types_[static_cast<size_t>(type)].name);
        for (size_t i = 0; i < names_.outputs.size(); ++i)
            device_.addItem(names_.outputs[i], static_cast<int>(i) + 1);
        const auto current = audio_.status().outputDevice;
        for (size_t i = 0; i < names_.outputs.size(); ++i)
            if (names_.outputs[i] == current)
                device_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        if (device_.getSelectedId() == 0 && names_.defaultOutput >= 0)
            device_.setSelectedId(names_.defaultOutput + 1, juce::dontSendNotification);
    }
    void timerCallback() override
    {
        const float peak = audio_.callback().takeInputPeak();
        meter_.push(&peak, 1, 50.0);
    }

    const ui::Theme& theme_;
    audio::AudioDeviceLayer& audio_;
    juce::Label driverLabel_, deviceLabel_, bufferLabel_, meterLabel_;
    juce::ComboBox driver_, device_, buffer_;
    ui::Meter meter_;
    std::vector<audio::AudioDeviceLayer::DeviceType> types_;
    audio::AudioDeviceLayer::DeviceNames names_;
};
} // namespace

// ----- the content ---------------------------------------------------------------------------

class MainWindow::Content final : public juce::Component,
                                  public juce::DragAndDropContainer,
                                  private juce::ChangeListener,
                                  private juce::Timer
{
public:
    friend class MainWindow;

    Content(MainWindow& owner,
            audio::AudioDeviceLayer& audio,
            ProjectSession& session,
            UserSettings& settings)
        : owner_(owner), audio_(audio), session_(session), settings_(settings), header_(theme_),
          tracks_(session.peakStore()), dock_(theme_, ui::HostKind::dock), side_(theme_, ui::HostKind::side),
          status_(theme_), toasts_(theme_), dockSplitter_(theme_, true), sideSplitter_(theme_, false),
          mixer_(
              session.engine().meters(),
              [&session] { return session.engine().loudness(); },
              [&session] { return session.engine().masterMeterSlot(); }),
          editor_(theme_), diagnostics_(theme_), browser_(session.pluginRegistry()), inspector_(theme_),
          panels_(theme_, dock_, side_)
    {
        setSize(1280, 800);
        setOpaque(true);
        setWantsKeyboardFocus(true);
        wireHeader();
        wireArrange();
        wireMixer();
        wirePanels();
        for (auto* c : std::initializer_list<juce::Component*>{
                 &header_, &tracks_, &dock_, &side_, &status_, &dockSplitter_, &sideSplitter_, &toasts_})
            addAndMakeVisible(c);
        status_.onDeviceClicked = [this] { owner_.showAudioSetup(); };
        status_.onDropoutsClicked = [this]
        {
            dropoutBaseline_ = audio_.status().xruns;
            overrunBaseline_ = static_cast<std::int64_t>(audio_.callback().stats().summary().overruns);
            refreshStatus();
        };
        audio_.addChangeListener(this);
        session_.addChangeListener(this);
        applyPreferences(true);
        refresh();
        startTimerHz(30);
    }

    // The preferences with an immediate effect (08-setup.md 8.2); `atLaunch` also
    // applies the defaults a project starts with.
    void applyPreferences(bool atLaunch)
    {
        using Keys = ui::PreferencesView::Keys;
        const auto value = [this](const char* key, const std::string& fallback)
        {
            const auto v = settings_.value(key);
            return v.empty() ? fallback : v;
        };
        const double scale = juce::String(value(Keys::uiScale, "0")).getDoubleValue();
        juce::Desktop::getInstance().setGlobalScaleFactor(
            static_cast<float>(scale > 0.0 ? scale / 100.0 : 1.0));
        const double levelDb = juce::String(value(Keys::metronomeLevel, "-6")).getDoubleValue();
        session_.setMetronomeLevel(static_cast<float>(std::pow(10.0, levelDb / 20.0)));
        std::vector<std::string> paths;
        for (const auto& item : juce::StringArray::fromTokens(
                 juce::String::fromUTF8(value(Keys::pluginPaths, "").c_str()), "\t", ""))
            if (item.isNotEmpty())
                paths.push_back(item.toStdString());
        session_.setPluginSearchPaths(std::move(paths));
        tracks_.setFollowPlayhead(value(Keys::followPlayhead, "1") == "1");
        if (atLaunch)
        {
            session_.setCountInBars(juce::String(value(Keys::countIn, "1")).getIntValue());
            tracks_.setSnapEnabled(value(Keys::snap, "1") == "1");
            tracks_.setSnapGrid(juce::String(value(Keys::snapGrid, "0")).getIntValue());
            tracks_.setRippleEnabled(value(Keys::ripple, "0") == "1");
            if (value(Keys::scanOnLaunch, "0") == "1")
                session_.scanPlugins(false);
        }
        refreshHeader();
    }

    // --benchmark-ui (11.8): plays the project with the mixer open, scrolls the
    // arrange view a screen every second, and after `seconds` prints the frame
    // interval and paint percentiles to stdout and quits.
    void runBenchmark(int seconds)
    {
        benchmarkEndMs_ = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        panels_.show(ui::PanelId::mixer);
        ui::FrameStats::reset();
        frames_.clear();
        session_.returnToStart();
        session_.play();
    }

    void benchmarkTick()
    {
        if (benchmarkEndMs_ <= 0.0)
            return;
        if (ticks_ % 30 == 0)
            tracks_.setScroll(tracks_.scrollPosition() + tracks_.visibleSamplesForBenchmark());
        if (juce::Time::getMillisecondCounterHiRes() < benchmarkEndMs_)
            return;
        benchmarkEndMs_ = 0.0;
        session_.stop();
        std::vector<double> sorted(frames_.begin(), frames_.end());
        std::sort(sorted.begin(), sorted.end());
        const auto at = [&](double q)
        {
            return sorted.empty()
                       ? 0.0
                       : sorted[std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * q))];
        };
        const auto paints = ui::FrameStats::summary();
        const auto stats = audio_.callback().stats().summary();
        juce::String report;
        report << "benchmark-ui: tracks " << session_.project().tracks.size() << ", " << getWidth() << "x"
               << getHeight() << " at "
               << juce::String(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale * 100.0,
                               0)
               << "%\n"
               << "frame interval ms: p50 " << juce::String(at(0.5), 2) << " p99 "
               << juce::String(at(0.99), 2) << " max "
               << juce::String(sorted.empty() ? 0.0 : sorted.back(), 2) << " (" << sorted.size()
               << " frames)\n"
               << "paint ms (arrange + mixer): p50 " << juce::String(paints.p50Ms, 2) << " p99 "
               << juce::String(paints.p99Ms, 2) << " max " << juce::String(paints.maxMs, 2) << " ("
               << paints.frames << " paints)\n"
               << "audio load: p50 " << juce::String(stats.p50 * 100.0, 1) << "% p99 "
               << juce::String(stats.p99 * 100.0, 1) << "% max " << juce::String(stats.max * 100.0, 1)
               << "%\n";
        std::printf("%s", report.toRawUTF8());
        std::fflush(stdout);
        juce::JUCEApplication::getInstance()->quit();
    }

    // The folder new projects are saved to and opened from.
    juce::File projectFolder() const
    {
        const auto chosen = settings_.value(ui::PreferencesView::Keys::projectFolder);
        if (!chosen.empty())
            if (const juce::File folder(juce::String::fromUTF8(chosen.c_str())); folder.isDirectory())
                return folder;
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    ~Content() override
    {
        stopTimer();
        panels_.saveTo(layoutFile());
        session_.removeChangeListener(this);
        audio_.removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme_.colours.canvas); }

    void resized() override
    {
        auto area = getLocalBounds();
        header_.setBounds(area.removeFromTop(theme_.sizes.headerBar));
        status_.setBounds(area.removeFromBottom(theme_.sizes.statusBar));
        const int splitter = theme_.sizes.splitter;
        // The side panel at the right, the dock below the arrange view; both sizes
        // come from the manager, which animates them. The arrange view keeps a
        // minimum of 480×240.
        const int sideShown = std::min(panels_.sideWidth(), std::max(0, area.getWidth() - 480 - splitter));
        const int dockShown = std::min(panels_.dockHeight(), std::max(0, area.getHeight() - 240 - splitter));
        side_.setVisible(sideShown > 0);
        sideSplitter_.setVisible(sideShown > 0);
        if (sideShown > 0)
        {
            side_.setBounds(area.removeFromRight(sideShown));
            sideSplitter_.setBounds(area.removeFromRight(splitter));
        }
        dock_.setVisible(dockShown > 0);
        dockSplitter_.setVisible(dockShown > 0);
        if (dockShown > 0)
        {
            dock_.setBounds(area.removeFromBottom(dockShown));
            dockSplitter_.setBounds(area.removeFromBottom(splitter));
        }
        tracks_.setBounds(area);
        const int toastW = std::min(420, getWidth() - 2 * theme_.spacing.md);
        toasts_.setBounds(theme_.spacing.md,
                          header_.getBottom() + theme_.spacing.md,
                          toastW,
                          status_.getY() - header_.getBottom() - 2 * theme_.spacing.md);
        if (sheet_ != nullptr)
            sheet_->setBounds(getLocalBounds());
    }

    // ----- the keyboard map (10-interaction.md 10.4) ---------------------------------------
    bool keyPressed(const juce::KeyPress& key) override
    {
        using M = juce::ModifierKeys;
        const int mods = key.getModifiers().withoutMouseButtons().getRawFlags();
        const auto is = [&](int code, int with = 0) { return key.getKeyCode() == code && mods == with; };
        const auto chr = [&](char c, int with = 0)
        { return key.getTextCharacter() == static_cast<juce::juce_wchar>(c) && mods == with; };
        if (key.getKeyCode() == juce::KeyPress::tabKey)
            ui::AuralLookAndFeel::setKeyboardFocusVisible(true);
        // F2 renames the selected track while the arrange view has the focus
        // (10.4); elsewhere it is the mixer's panel key.
        if (key.getKeyCode() == juce::KeyPress::F2Key && mods == 0 && tracks_.hasKeyboardFocus(true) &&
            tracks_.selectedTrack() != core::invalidId)
        {
            tracks_.renameSelectedTrack();
            return true;
        }
        if (is('F', M::ctrlModifier))
        {
            panels_.show(ui::PanelId::browser);
            browser_.focusSearch();
            return true;
        }
        if (panels_.handleKey(key))
            return true;
        // Transport.
        if (is(juce::KeyPress::spaceKey))
        {
            session_.togglePlay();
            return true;
        }
        if (is(juce::KeyPress::spaceKey, M::shiftModifier))
        {
            session_.stop();
            return true;
        }
        if (chr('r'))
        {
            toggleRecord();
            return true;
        }
        if (chr('R', M::shiftModifier))
        {
            armSelected();
            return true;
        }
        if (is(juce::KeyPress::returnKey) || is(juce::KeyPress::homeKey))
        {
            session_.returnToStart();
            return true;
        }
        if (is(juce::KeyPress::returnKey, M::shiftModifier) || is(juce::KeyPress::endKey))
        {
            session_.locate(session_.project().length());
            return true;
        }
        if (chr('l'))
        {
            session_.setLoopEnabled(!session_.loopEnabled());
            return true;
        }
        if (chr('p'))
        {
            session_.setPunchEnabled(!session_.punchEnabled());
            return true;
        }
        if (chr('c'))
        {
            session_.setMetronome(!session_.metronomeEnabled());
            return true;
        }
        // Zoom.
        if (is('=', M::ctrlModifier) || is('+', M::ctrlModifier))
        {
            tracks_.zoomBy(0.75);
            return true;
        }
        if (is('-', M::ctrlModifier))
        {
            tracks_.zoomBy(1.0 / 0.75);
            return true;
        }
        if (chr('z'))
        {
            tracks_.zoomToFit();
            return true;
        }
        if (chr('Z', M::shiftModifier))
        {
            tracks_.zoomToSelection();
            return true;
        }
        // Snap and ripple.
        if (chr('n'))
        {
            tracks_.setSnapEnabled(!tracks_.snapEnabled());
            refreshHeader();
            return true;
        }
        if (chr('N', M::shiftModifier))
        {
            tracks_.setRippleEnabled(!tracks_.rippleEnabled());
            refreshHeader();
            return true;
        }
        // The selected track's state. (The spec gives Shift+M and Shift+S to both the
        // track state and the marker region / split at selection; the edits take Alt.)
        if (chr('M', M::shiftModifier))
        {
            if (const auto* t = selectedTrack())
                session_.execute(std::make_unique<core::SetTrackMute>(t->id, !t->mute));
            return true;
        }
        if (chr('S', M::shiftModifier))
        {
            if (const auto* t = selectedTrack())
                session_.execute(std::make_unique<core::SetTrackSolo>(t->id, !t->solo));
            return true;
        }
        if (is('M', M::altModifier))
            return tracks_.handleEditKey(juce::KeyPress('M', M::shiftModifier, 0));
        if (is('S', M::altModifier))
            return tracks_.handleEditKey(juce::KeyPress('S', M::shiftModifier, 0));
        // Files and windows.
        if (is('N', M::ctrlModifier))
        {
            newProject();
            return true;
        }
        if (is('O', M::ctrlModifier))
        {
            openProject();
            return true;
        }
        if (is('S', M::ctrlModifier))
        {
            saveProject(false);
            return true;
        }
        if (is('S', M::ctrlModifier | M::shiftModifier))
        {
            saveProject(true);
            return true;
        }
        if (is('I', M::ctrlModifier))
        {
            importAudio();
            return true;
        }
        if (is('E', M::ctrlModifier))
        {
            exportMenu();
            return true;
        }
        if (is('Z', M::ctrlModifier))
        {
            undo();
            return true;
        }
        if (is('Y', M::ctrlModifier) || is('Z', M::ctrlModifier | M::shiftModifier))
        {
            redo();
            return true;
        }
        if (is('A', M::ctrlModifier | M::shiftModifier))
        {
            owner_.showAudioSetup();
            return true;
        }
        if (is(',', M::ctrlModifier))
        {
            owner_.showPreferences();
            return true;
        }
        if (is('P', M::ctrlModifier | M::shiftModifier))
        {
            owner_.showPluginBrowser();
            return true;
        }
        if (is('T', M::ctrlModifier))
        {
            addAudioTrack();
            return true;
        }
        if (is('T', M::ctrlModifier | M::shiftModifier))
        {
            addInstrumentTrack();
            return true;
        }
        if (is('B', M::ctrlModifier))
        {
            addBus();
            return true;
        }
        return tracks_.handleEditKey(key);
    }

private:
    // ----- wiring --------------------------------------------------------------------------
    void wireHeader()
    {
        header_.onAppMenu = [this] { appMenu(); };
        header_.onProjectMenu = [this] { projectMenu(); };
        header_.onUndo = [this] { undo(); };
        header_.onRedo = [this] { redo(); };
        header_.onGoToStart = [this] { session_.returnToStart(); };
        header_.onPlayStop = [this] { session_.togglePlay(); };
        header_.onStop = [this] { session_.stop(); };
        header_.onRecord = [this] { toggleRecord(); };
        header_.onDeviceWarning = [this] { owner_.showAudioSetup(); };
        header_.onToggleLoop = [this] { session_.setLoopEnabled(!session_.loopEnabled()); };
        header_.onTogglePunch = [this] { session_.setPunchEnabled(!session_.punchEnabled()); };
        header_.onToggleClick = [this] { session_.setMetronome(!session_.metronomeEnabled()); };
        header_.onCountInBars = [this](int bars) { session_.setCountInBars(bars); };
        header_.onBarsTyped = [this](const juce::String& text)
        {
            if (const auto at = parseBars(session_.project(), text); at >= 0)
                session_.locate(at);
        };
        header_.onTimeTyped = [this](const juce::String& text)
        {
            if (const auto at = parseTime(session_.project(), text); at >= 0)
                session_.locate(at);
        };
        header_.onBarsNudged = [this](int segment, int steps)
        {
            const auto& p = session_.project();
            const double beat = core::samplesPerBeat(p.sampleRate, p.tempoBpm, p.timeSignature);
            const double unit = segment == 0   ? beat * std::max(1, p.timeSignature.numerator)
                                : segment == 1 ? beat
                                               : beat / core::BarsBeats::ticksPerBeat;
            nudge(unit * steps);
        };
        header_.onTimeNudged = [this](int segment, int steps)
        {
            const double rate = session_.project().sampleRate;
            const double unit = segment == 0 ? 60.0 * rate : segment == 1 ? rate : rate / 1000.0;
            nudge(unit * steps);
        };
        header_.onTempo = [this](double bpm) { session_.execute(std::make_unique<core::SetTempo>(bpm)); };
        header_.onSignature = [this](int n, int d)
        { session_.execute(std::make_unique<core::SetTimeSignature>(n, d)); };
        header_.onMasterClipClicked = [this] { mixer_.clearMasterClip(); };
        header_.onToggleSnap = [this]
        {
            tracks_.setSnapEnabled(!tracks_.snapEnabled());
            refreshHeader();
        };
        header_.onSnapMenu = [this] { snapMenu(); };
        header_.onToggleRipple = [this]
        {
            tracks_.setRippleEnabled(!tracks_.rippleEnabled());
            refreshHeader();
        };
        header_.onZoomFit = [this] { tracks_.zoomToFit(); };
        header_.onToggleMixer = [this] { panels_.toggle(ui::PanelId::mixer); };
        header_.onToggleEditor = [this] { panels_.toggle(ui::PanelId::editor); };
        header_.onToggleBrowser = [this] { panels_.toggle(ui::PanelId::browser); };
    }

    void nudge(double samples)
    {
        session_.locate(std::max<core::SamplePosition>(
            0, session_.position() + static_cast<core::SamplePosition>(std::llround(samples))));
    }

    void wireArrange()
    {
        tracks_.onLocate = [this](core::SamplePosition p) { session_.locate(p); };
        tracks_.onRegionChanged = [this](core::SamplePosition start, core::SamplePosition end)
        { session_.setRegion(start, end); };
        tracks_.onTrackInputChanged = [this](core::Id id, int channel)
        { session_.execute(std::make_unique<core::SetTrackInput>(id, channel)); };
        tracks_.onTrackMidiInputChanged = [this](core::Id id, const std::string& name)
        { session_.execute(std::make_unique<core::SetTrackMidiInput>(id, name)); };
        tracks_.onTrackArmChanged = [this](core::Id id, bool armed)
        { session_.execute(std::make_unique<core::SetTrackArmed>(id, armed)); };
        tracks_.onTrackMonitorChanged = [this](core::Id id, core::MonitorMode mode)
        { session_.execute(std::make_unique<core::SetTrackMonitor>(id, mode)); };
        tracks_.onImportFiles = [this](const std::vector<juce::File>& files, core::SamplePosition at)
        { importFiles(files, at); };
        tracks_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        { session_.executeGroup(name, std::move(commands)); };
        tracks_.onOpenMidiClip = [this](core::Id trackId, core::Id clipId)
        { owner_.openPianoRoll(trackId, clipId); };
        tracks_.onNormalise = [this](core::Id trackId, core::Id clipId)
        {
            const auto error = session_.normaliseClip(trackId, clipId);
            if (!error.empty())
                toast(ui::ToastStack::Kind::error,
                      "Normalise failed: " + juce::String::fromUTF8(error.c_str()));
        };
        tracks_.playheadForEdits = [this] { return session_.position(); };
        tracks_.setAcceptedExtensions(session_.mediaLoader().supportedExtensions());
        tracks_.onTrackSelected = [this](core::Id id) { selectionChanged(id); };
        tracks_.onSelectionChanged = [this] { selectionChanged(tracks_.selectedTrack()); };
        tracks_.onAddAudioTrack = [this] { addAudioTrack(); };
        tracks_.onAddInstrumentTrack = [this] { addInstrumentTrack(); };
        tracks_.onAddBus = [this] { addBus(); };
        tracks_.onImportRequested = [this] { importAudio(); };
        tracks_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        tracks_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        tracks_.onGestureEnd = [this] { session_.endGesture(); };
        tracks_.trackMeter = [this](int index) { return mixer_.lastReading(engine::trackMeterSlot(index)); };
    }

    void wireMixer()
    {
        mixer_.onCommand = [this](std::unique_ptr<core::Command> command)
        {
            if (auto* removal = dynamic_cast<core::RemoveBus*>(command.get()))
                if (confirmBusRemoval(removal->busId(), command))
                    return; // the sheet took the command
            session_.execute(std::move(command));
        };
        mixer_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        mixer_.onGestureEnd = [this] { session_.endGesture(); };
        mixer_.setHost(&session_.insertFactory());
        mixer_.liveInsert = [this](core::Id insertId) { return session_.liveInsert(insertId); };
        mixer_.onScanPlugins = [this] { session_.scanPlugins(true); };
        mixer_.onSelectChannel = [this](core::Id id)
        {
            tracks_.setSelectedTrack(id);
            selectionChanged(id);
        };
        mixer_.onOpenPluginBrowser = [this] { owner_.showPluginBrowser(); };
        mixer_.loadSetting = [this](const std::string& key) { return settings_.value(key); };
        mixer_.saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };

        editor_.setRegistry(&session_.pluginRegistry());
        editor_.liveInsert = [this](core::Id insertId) { return session_.liveInsert(insertId); };
        editor_.displayName = [this](const std::string& type) { return mixer_.insertDisplayName(type); };
        editor_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        editor_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        editor_.onGestureEnd = [this] { session_.endGesture(); };
        editor_.onOpenEditor = [this](core::Id channelId, core::Id insertId)
        { owner_.openInsertEditor(channelId, insertId); };

        inspector_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        inspector_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        inspector_.onGestureEnd = [this] { session_.endGesture(); };

        pianoRoll_.onEdit =
            [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        { session_.executeGroup(name, std::move(commands)); };

        pianoRoll_.onPreviewNote = [this](core::Id, int note, bool on)
        {
            engine::MidiEvent event;
            event.type = on ? engine::MidiEvent::Type::noteOn : engine::MidiEvent::Type::noteOff;
            event.note = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
            event.velocity = on ? 100 : 0;
            session_.engine().pushMidiInput(event, "Piano Roll");
        };
        browser_.onScan = [this](bool all) { session_.scanPlugins(all); };
        browser_.onCancelScan = [this] { session_.cancelPluginScan(); };
        browser_.onClearBlocklist = [this] { session_.clearPluginBlocklist(); };
        browser_.onRetry = [this](const std::string& path) { session_.retryBlockedPlugin(path); };
        browser_.scanProgress = [this] { return session_.pluginScanProgress(); };
        browser_.loadSetting = [this](const std::string& key) { return settings_.value(key); };
        browser_.saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };
        browser_.onInsert = [this](const std::string& type) { insertPlugin(tracks_.selectedTrack(), type); };
        browser_.setStatus(session_.pluginScanStatus());
        tracks_.onInsertPlugin = [this](core::Id trackId, const std::string& type)
        { insertPlugin(trackId, type); };
        editor_.isEditorOpen = [this](core::Id insertId) { return mixer_.isEditorOpen(insertId); };
        editor_.listSetting = [this](const std::string& key)
        {
            std::vector<std::string> values;
            for (const auto& item : juce::StringArray::fromTokens(
                     juce::String::fromUTF8(settings_.value(key).c_str()), "\t", ""))
                if (item.isNotEmpty())
                    values.push_back(item.toStdString());
            return values;
        };
        editor_.onInserted = [this](const std::string& type) { browser_.rememberRecent(type); };
    }

    void wirePanels()
    {
        using ui::HostKind;
        using ui::Icon;
        using ui::PanelId;
        using ui::PanelInfo;
        const auto key = [](int code) { return juce::KeyPress(code, juce::ModifierKeys(), 0); };
        panels_.registerPanel(PanelInfo{PanelId::mixer,
                                        "Mixer",
                                        Icon::mixer,
                                        HostKind::dock,
                                        480,
                                        240,
                                        true,
                                        false,
                                        key(juce::KeyPress::F2Key)},
                              mixer_);
        panels_.registerPanel(PanelInfo{PanelId::editor,
                                        "Editor",
                                        Icon::plugin,
                                        HostKind::dock,
                                        320,
                                        200,
                                        true,
                                        true,
                                        key(juce::KeyPress::F3Key)},
                              editor_);
        panels_.registerPanel(PanelInfo{PanelId::browser,
                                        "Browser",
                                        Icon::browser,
                                        HostKind::side,
                                        240,
                                        200,
                                        true,
                                        true,
                                        key(juce::KeyPress::F4Key)},
                              browser_);
        panels_.registerPanel(PanelInfo{PanelId::inspector,
                                        "Inspector",
                                        Icon::inspector,
                                        HostKind::side,
                                        240,
                                        200,
                                        false,
                                        true,
                                        key(juce::KeyPress::F5Key)},
                              inspector_);
        panels_.registerPanel(PanelInfo{PanelId::pianoRoll,
                                        "Piano Roll",
                                        Icon::pianoRoll,
                                        HostKind::dock,
                                        480,
                                        240,
                                        true,
                                        false,
                                        key(juce::KeyPress::F6Key)},
                              pianoRoll_);
        panels_.registerPanel(PanelInfo{PanelId::diagnostics,
                                        "Diagnostics",
                                        Icon::diagnostics,
                                        HostKind::dock,
                                        320,
                                        200,
                                        true,
                                        false,
                                        key(juce::KeyPress::F7Key)},
                              diagnostics_);
        panels_.setMainWindow(&owner_);
        panels_.onLayoutChanged = [this] { resized(); };
        panels_.onPanelVisibilityChanged = [this](PanelId, bool) { refreshHeader(); };
        dockSplitter_.onDragged = [this](int delta) { panels_.setDockHeight(panels_.dockHeight() - delta); };
        dockSplitter_.onReset = [this] { panels_.setDockHeight(ui::PanelManager::defaultDockHeight); };
        sideSplitter_.onDragged = [this](int delta) { panels_.setSideWidth(panels_.sideWidth() - delta); };
        sideSplitter_.onReset = [this] { panels_.setSideWidth(ui::PanelManager::defaultSideWidth); };
        panels_.setWindowDefaults(getWidth(), getHeight());
        if (!panels_.loadFrom(layoutFile()))
            panels_.resetLayout();
    }

    juce::File layoutFile() const { return settings_.file().getSiblingFile("layout.json"); }

    // ----- refresh -------------------------------------------------------------------------
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        const auto s = audio_.status();
        // The device went away: say what happened, what it means and what to do (10.6).
        if (lastDeviceOpen_ && !s.open)
        {
            const bool wasPlaying = session_.isPlaying();
            session_.stop();
            toast(ui::ToastStack::Kind::error,
                  "The " + juce::String::fromUTF8(lastDeviceName_.c_str()) + " was disconnected. " +
                      (wasPlaying ? "Playback stopped. " : juce::String()) +
                      "Reconnect it or choose another device in Audio Setup.",
                  "Audio Setup",
                  [this] { owner_.showAudioSetup(); });
        }
        lastDeviceOpen_ = s.open;
        if (s.open)
            lastDeviceName_ = s.outputDevice;
        {
            const auto all = audio_.inputChannelNames();
            std::vector<std::string> names;
            for (int channel : s.activeInputs)
                names.push_back(channel >= 0 && static_cast<size_t>(channel) < all.size()
                                    ? all[static_cast<size_t>(channel)]
                                    : "Input " + std::to_string(channel + 1));
            mixer_.setInputNames(names, audio_.midi().openInputNames());
            tracks_.setInputNames(std::move(names));
            tracks_.setMidiInputNames(audio_.midi().openInputNames());
        }
        tracks_.setProject(&session_.project());
        // There is always a selected track while the project has one (the editor
        // and inspector follow it).
        if (tracks_.selectedTrack() == core::invalidId && !session_.project().tracks.empty())
        {
            tracks_.setSelectedTrack(session_.project().tracks.front().id);
            selectionChanged(session_.project().tracks.front().id);
        }
        mixer_.setProject(&session_.project());
        editor_.setProject(&session_.project());
        inspector_.setProject(&session_.project());
        pianoRoll_.setClip(&session_.project(), pianoRoll_.trackId(), pianoRoll_.clipId());
        browser_.setStatus(session_.pluginScanStatus());
        browser_.refresh();
        tracks_.setRegion(
            session_.regionStart(), session_.regionEnd(), session_.loopEnabled(), session_.punchEnabled());
        refreshHeader();
        refreshStatus();
        refreshTitle();
        // The session's accumulated messages become toasts once.
        const auto recording = session_.lastRecordingMessage();
        if (recording != lastRecordingMessage_)
        {
            lastRecordingMessage_ = recording;
            if (!recording.empty())
            {
                const bool trouble = recording.find("failed") != std::string::npos ||
                                     recording.find("overrun") != std::string::npos;
                toast(trouble ? ui::ToastStack::Kind::warning : ui::ToastStack::Kind::success,
                      juce::String::fromUTF8(recording.c_str()));
            }
        }
        const auto bounce = session_.lastBounceMessage();
        if (bounce != lastBounceMessage_)
        {
            lastBounceMessage_ = bounce;
            if (!bounce.empty())
                status_.showMessage(juce::String::fromUTF8(bounce.c_str()));
        }
    }

    juce::String projectName() const
    {
        return session_.projectFile().getFullPathName().isNotEmpty()
                   ? session_.projectFile().getFileNameWithoutExtension()
                   : juce::String("Untitled");
    }

    void refreshTitle()
    {
        owner_.setName((session_.isRecording() ? juce::String::fromUTF8("\xe2\x97\x8f ") : juce::String()) +
                       projectName() + (session_.isModified() ? "*" : "") + " - Aural");
    }

    void refreshHeader()
    {
        ui::HeaderBar::State state;
        const auto& p = session_.project();
        state.projectName = projectName();
        state.projectPath = session_.projectFile().getFullPathName();
        state.modified = session_.isModified();
        state.canUndo = session_.canUndo();
        state.canRedo = session_.canRedo();
        state.undoName = juce::String::fromUTF8(session_.undoName().c_str());
        state.redoName = juce::String::fromUTF8(session_.redoName().c_str());
        state.playing = session_.isPlaying();
        state.recording = session_.isRecording();
        state.countingIn = session_.isCountingIn();
        state.loop = session_.loopEnabled();
        state.loopPossible = session_.regionEnd() > session_.regionStart() || session_.loopEnabled();
        state.punch = session_.punchEnabled();
        state.click = session_.metronomeEnabled();
        state.countInBars = session_.countInBars();
        state.deviceReady = audio_.status().open;
        state.barsText = barsText(p, heardPosition());
        state.timeText = timeText(p, heardPosition());
        state.tempo = p.tempoBpm;
        state.signatureNumerator = p.timeSignature.numerator;
        state.signatureDenominator = p.timeSignature.denominator;
        state.masterClipped = mixer_.masterClipped();
        state.snap = tracks_.snapEnabled();
        state.snapName = tracks_.snapGridName();
        state.ripple = tracks_.rippleEnabled();
        state.mixerVisible = panels_.isVisible(ui::PanelId::mixer);
        state.editorVisible = panels_.isVisible(ui::PanelId::editor);
        state.browserVisible = panels_.isVisible(ui::PanelId::browser);
        header_.setState(state);
    }

    // What is heard lags the engine by the delay compensation on the master: the
    // playhead and the readouts follow the sound, not the engine.
    core::SamplePosition heardPosition() const
    {
        return std::max<core::SamplePosition>(0,
                                              session_.position() - session_.engine().outputLatencySamples());
    }

    void refreshStatus()
    {
        ui::StatusBar::State state;
        const auto s = audio_.status();
        state.deviceOpen = s.open;
        state.devicePreferredUnavailable = s.open && s.message.find("preferred") != std::string::npos;
        state.driver = juce::String::fromUTF8(s.deviceType.c_str());
        state.device = juce::String::fromUTF8(s.outputDevice.c_str());
        state.sampleRate = s.sampleRate;
        state.bufferSize = s.bufferSize;
        const auto calibration = audio_.currentCalibration();
        state.roundTripMeasured = calibration.has_value();
        state.roundTripMs = calibration.has_value()
                                ? calibration->roundTripMilliseconds()
                                : s.inputLatencyMilliseconds() + s.outputLatencyMilliseconds();
        const auto stats = audio_.callback().stats().summary();
        state.loadP50 = stats.p50;
        state.loadP99 = stats.p99;
        state.loadMax = stats.max;
        state.dropouts = std::max(0, s.xruns - dropoutBaseline_) +
                         static_cast<int>(std::max<std::int64_t>(
                             0, static_cast<std::int64_t>(stats.overruns) - overrunBaseline_));
        // Dropouts over the threshold: a toast with the fix, at most once a minute.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (state.dropouts - dropoutsToasted_ >= 10 && now - lastDropoutToastMs_ > 60000.0)
        {
            dropoutsToasted_ = state.dropouts;
            lastDropoutToastMs_ = now;
            toast(ui::ToastStack::Kind::warning,
                  "Audio dropouts. The buffer is too small for this project on this device. Raise the buffer "
                  "size in Audio Setup.",
                  "Audio Setup",
                  [this] { owner_.showAudioSetup(); });
        }
        state.recording = session_.isRecording();
        if (state.recording)
            state.diskFreeGb = static_cast<double>(session_.mediaFolder().getBytesFreeOnVolume()) / 1.0e9;
        state.warning = juce::String::fromUTF8(session_.sampleRateWarning().c_str());
        if (session_.isScanningPlugins())
            state.warning = juce::String::fromUTF8(session_.pluginScanStatus().c_str());
        if (session_.isBouncing())
            state.warning =
                "Bouncing " + juce::String(static_cast<int>(session_.bounceProgress() * 100.0f)) + "%";
        status_.setState(state);
    }

    void timerCallback() override
    {
        ++ticks_;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastTickMs_ > 0.0)
        {
            frames_.push_back(now - lastTickMs_);
            if (frames_.size() > 120)
                frames_.pop_front();
        }
        lastTickMs_ = now;
        const bool recording = session_.isRecording();
        tracks_.setPlayhead(heardPosition(), session_.isPlaying(), recording);
        // The takes being recorded, drawn growing (04-arrange.md "Recording clip").
        session_.updateLiveRecording(tracks_.liveRecording());
        tracks_.liveRecordingChanged();
        tracks_.pushMeters();
        if (panels_.isVisible(ui::PanelId::pianoRoll))
        {
            pianoRoll_.setPlayhead(session_.position());
            if (pianoRoll_.zoomLinked())
                pianoRoll_.setTimeView(tracks_.samplesPerPixel(), tracks_.scrollPosition());
        }
        refreshHeader();
        if (ticks_ % 5 == 0)
        {
            std::vector<bool> monitoring;
            for (const auto& track : session_.project().tracks)
                monitoring.push_back(session_.isTrackMonitoring(track));
            tracks_.setMonitoringStates(monitoring, recording);
        }
        if (ticks_ % 15 == 0)
            refreshStatus();
        updateHoverHint();
        benchmarkTick();
        if (recording != lastRecording_)
        {
            lastRecording_ = recording;
            refreshTitle();
        }
        if (ticks_ % 30 == 0)
            pushDiagnostics();
    }

    // The status bar's right side describes the control under the pointer after
    // a second (10.5): its tooltip, which carries the name, value and shortcut.
    void updateHoverHint()
    {
        auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
        juce::String tip;
        for (auto* comp = under; comp != nullptr && tip.isEmpty(); comp = comp->getParentComponent())
            if (auto* client = dynamic_cast<juce::TooltipClient*>(comp))
                tip = client->getTooltip();
        if (under != hoverComponent_)
        {
            hoverComponent_ = under;
            hoverSinceMs_ = juce::Time::getMillisecondCounterHiRes();
            status_.setHint({});
            return;
        }
        const bool dragging = juce::Desktop::getInstance().getMainMouseSource().isDragging();
        if (!dragging && tip.isNotEmpty() &&
            juce::Time::getMillisecondCounterHiRes() - hoverSinceMs_ >= 1000.0)
            status_.setHint(tip.replace("\n", "  "));
    }

    void pushDiagnostics()
    {
        ui::DiagnosticsPanel::Sample sample;
        const auto s = audio_.status();
        const auto stats = audio_.callback().stats().summary();
        sample.loadP50 = stats.p50;
        sample.loadP99 = stats.p99;
        sample.loadMax = stats.max;
        sample.overruns = static_cast<int>(stats.overruns);
        sample.dropouts = s.xruns;
        sample.device = s.open ? juce::String::fromUTF8(s.deviceType.c_str()) + " " +
                                     juce::String::fromUTF8(s.outputDevice.c_str())
                               : juce::String();
        sample.sampleRate = s.sampleRate;
        sample.bufferSize = s.bufferSize;
        sample.workerThreads = session_.engine().workerThreads();
        sample.outputLatencySamples = session_.engine().outputLatencySamples();
        sample.recording = session_.isRecording();
        const auto paints = ui::FrameStats::summary();
        sample.paintP50Ms = paints.p50Ms;
        sample.paintP99Ms = paints.p99Ms;
        sample.paintMaxMs = paints.maxMs;
        if (!frames_.empty())
        {
            std::vector<double> sorted(frames_.begin(), frames_.end());
            std::sort(sorted.begin(), sorted.end());
            sample.frameP50Ms = sorted[sorted.size() / 2];
            sample.frameP99Ms = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
            sample.frameMaxMs = sorted.back();
        }
        for (const auto& track : session_.project().tracks)
        {
            int latency = 0;
            for (const auto& insert : track.inserts)
                if (const auto live = session_.liveInsert(insert.id))
                    latency += live->latencySamples();
            if (latency > 0)
                sample.trackLatencies.emplace_back(juce::String::fromUTF8(track.name.c_str()), latency);
        }
        diagnostics_.push(sample);
    }

    // ----- selection -----------------------------------------------------------------------
    const core::Track* selectedTrack() const { return session_.project().findTrack(tracks_.selectedTrack()); }

    void selectionChanged(core::Id trackId)
    {
        core::Id clipId = core::invalidId;
        if (!tracks_.selection().empty() && !tracks_.selection().front().midi)
        {
            clipId = tracks_.selection().front().clip;
            trackId = tracks_.selection().front().track;
        }
        editor_.setChannel(trackId);
        inspector_.setSelection(trackId, clipId);
        mixer_.setSelectedChannel(trackId);
    }

    // A bus that tracks route to is removed only after a sheet (10.6). Returns
    // true when the sheet took the command; otherwise the command is left with
    // the caller to execute at once.
    bool confirmBusRemoval(core::Id busId, std::unique_ptr<core::Command>& command)
    {
        const auto& project = session_.project();
        const auto* bus = project.findBus(busId);
        if (bus == nullptr)
            return false;
        int routed = 0;
        const auto count = [&](const core::Channel& ch)
        {
            if (ch.outputBus == busId)
                ++routed;
            for (const auto& send : ch.sends)
                if (send.bus == busId)
                    ++routed;
        };
        for (const auto& t : project.tracks)
            count(t);
        for (const auto& b : project.buses)
            if (b.id != busId)
                count(b);
        if (routed == 0)
            return false;
        auto shared = std::make_shared<std::unique_ptr<core::Command>>(std::move(command));
        sheet_ =
            ui::Sheet::show(*this,
                            theme_,
                            "Delete " + juce::String::fromUTF8(bus->name.c_str()) + "?",
                            juce::String(routed) + (routed == 1 ? " channel routes" : " channels route") +
                                " to it and will go to the master instead. This can be undone.",
                            {{"Cancel", false, false}, {"Delete", false, true}},
                            [this, shared](int result)
                            {
                                sheet_ = nullptr;
                                if (result == 1 && *shared != nullptr)
                                    session_.execute(std::move(*shared));
                            });
        return true;
    }

    // Unsaved changes on close (10.6): a sheet with Save as the primary action.
    void confirmClose()
    {
        if (!session_.isModified())
        {
            juce::JUCEApplication::getInstance()->quit();
            return;
        }
        sheet_ =
            ui::Sheet::show(*this,
                            theme_,
                            "Save changes to " + projectName() + "?",
                            "Unsaved changes will be lost if you close without saving.",
                            {{"Cancel", false, false}, {"Don't save", false, true}, {"Save", true, false}},
                            [this](int result)
                            {
                                sheet_ = nullptr;
                                if (result == 1)
                                    juce::JUCEApplication::getInstance()->quit();
                                else if (result == 2)
                                {
                                    quitAfterSave_ = true;
                                    saveProject(false);
                                }
                            });
    }

    // Inserts a plugin type on a track (from the browser, a drop or the chain).
    void insertPlugin(core::Id trackId, const std::string& type)
    {
        if (trackId == core::invalidId || session_.project().findChannel(trackId) == nullptr)
        {
            toast(ui::ToastStack::Kind::info, "Select a track to insert the plugin on.");
            return;
        }
        core::Insert insert;
        insert.type = type;
        insert.params = plugins::defaultParameters(type);
        session_.execute(std::make_unique<core::AddInsert>(trackId, insert));
        browser_.rememberRecent(type);
        tracks_.setSelectedTrack(trackId);
        selectionChanged(trackId);
    }

    void armSelected()
    {
        if (const auto* t = selectedTrack())
            session_.execute(std::make_unique<core::SetTrackArmed>(t->id, !t->armed));
    }

    // ----- actions -------------------------------------------------------------------------
    void toast(ui::ToastStack::Kind kind,
               const juce::String& message,
               const juce::String& action = {},
               std::function<void()> onAction = {},
               const juce::String& second = {},
               std::function<void()> onSecond = {})
    {
        toasts_.show(kind, message, action, std::move(onAction), second, std::move(onSecond));
        toasts_.toFront(false);
    }

    void undo()
    {
        const auto name = session_.undoName();
        session_.undo();
        if (!name.empty())
            status_.showMessage("Undid " + juce::String::fromUTF8(name.c_str()));
    }

    void redo()
    {
        const auto name = session_.redoName();
        session_.redo();
        if (!name.empty())
            status_.showMessage("Redid " + juce::String::fromUTF8(name.c_str()));
    }

    void toggleRecord()
    {
        if (session_.isRecording())
        {
            session_.stop();
            return;
        }
        if (!audio_.status().open)
        {
            toast(ui::ToastStack::Kind::warning,
                  "No audio device is open.",
                  "Audio Setup",
                  [this] { owner_.showAudioSetup(); });
            return;
        }
        bool armed = false;
        for (const auto& t : session_.project().tracks)
            armed = armed || t.armed;
        if (!armed)
        {
            toast(ui::ToastStack::Kind::warning,
                  "Arm a track to record.",
                  "Arm selected",
                  [this] { armSelected(); });
            return;
        }
        const auto error = session_.startRecording();
        if (!error.empty())
            toast(ui::ToastStack::Kind::error,
                  "Cannot record: " + juce::String::fromUTF8(error.c_str()),
                  "Audio Setup",
                  [this] { owner_.showAudioSetup(); });
    }

    void addAudioTrack()
    {
        core::Track track;
        int count = 0;
        for (const auto& t : session_.project().tracks)
            if (!t.isMidi() && !t.isAux())
                ++count;
        track.name = "Audio " + std::to_string(count + 1);
        auto command = std::make_unique<core::AddTrack>(track);
        const auto id = command->trackId();
        session_.execute(std::move(command));
        tracks_.setSelectedTrack(id);
        selectionChanged(id);
    }

    void addInstrumentTrack()
    {
        const auto id = session_.addMidiTrack();
        tracks_.setSelectedTrack(id);
        selectionChanged(id);
    }

    void addBus()
    {
        core::Bus bus;
        bus.name = "Bus " + std::to_string(session_.project().buses.size() + 1);
        session_.execute(std::make_unique<core::AddBus>(bus));
    }

    // --remove-bus: the last bus goes the way the mixer strip's menu removes it.
    void removeLastBus()
    {
        const auto& buses = session_.project().buses;
        if (!buses.empty() && mixer_.onCommand)
            mixer_.onCommand(std::make_unique<core::RemoveBus>(buses.back().id));
    }

    void newProject()
    {
        if (session_.isRecording())
            return;
        session_.stop();
        mixer_.closeEditors();
        session_.newProject();
        refresh();
    }

    void openProject()
    {
        if (session_.isRecording())
            return;
        chooser_ = std::make_unique<juce::FileChooser>("Open project", projectFolder(), "*.aural");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc)
                              {
                                  if (const auto file = fc.getResult(); file != juce::File())
                                      openFile(file);
                              });
    }

    void openFile(const juce::File& file)
    {
        session_.stop();
        mixer_.closeEditors();
        const auto error = session_.open(file);
        if (!error.empty())
            toast(ui::ToastStack::Kind::error,
                  "Cannot open " + file.getFileName() + ": " + juce::String::fromUTF8(error.c_str()));
        else
            rememberRecent(file);
        refresh();
        grabKeyboardFocus();
        owner_.offerRecovery();
    }

    void saveProject(bool saveAs)
    {
        if (!saveAs && session_.projectFile().getFullPathName().isNotEmpty())
        {
            saveTo(session_.projectFile());
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save project", projectFolder().getChildFile("Untitled.aural"), "*.aural");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension("aural"))
                                      file = file.withFileExtension("aural");
                                  saveTo(file);
                                  grabKeyboardFocus();
                              });
    }

    void saveTo(const juce::File& file)
    {
        const auto error = session_.save(file);
        if (!error.empty())
        {
            quitAfterSave_ = false;
            toast(ui::ToastStack::Kind::error, "Cannot save: " + juce::String::fromUTF8(error.c_str()));
        }
        else
        {
            status_.showMessage("Saved " + file.getFileName());
            rememberRecent(file);
            if (quitAfterSave_)
                juce::JUCEApplication::getInstance()->quit();
        }
        refresh();
    }

    static constexpr int recentCount = 20; // the list's capacity; the menu shows the preferred count

    std::vector<juce::String> recentFiles() const
    {
        std::vector<juce::String> files;
        for (int i = 0; i < recentCount; ++i)
            if (const auto path = settings_.value("recent." + std::to_string(i)); !path.empty())
                files.push_back(juce::String::fromUTF8(path.c_str()));
        return files;
    }

    void rememberRecent(const juce::File& file)
    {
        auto files = recentFiles();
        files.erase(std::remove(files.begin(), files.end(), file.getFullPathName()), files.end());
        files.insert(files.begin(), file.getFullPathName());
        for (int i = 0; i < recentCount; ++i)
            settings_.setValue("recent." + std::to_string(i),
                               static_cast<size_t>(i) < files.size()
                                   ? files[static_cast<size_t>(i)].toStdString()
                                   : std::string{});
    }

    void importAudio()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Import audio",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            session_.mediaLoader().supportedExtensions());
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems,
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
        for (const auto& file : files)
        {
            const auto error = session_.importAudioFile(file, at);
            if (!error.empty())
                toast(ui::ToastStack::Kind::error,
                      file.getFileName() + ": " + juce::String::fromUTF8(error.c_str()));
        }
        refresh();
        grabKeyboardFocus();
    }

    void addExportItems(juce::PopupMenu& menu, int base)
    {
        menu.addItem(base + 1, "Mix to WAV...");
        menu.addItem(base + 2, "Loop region to WAV...", session_.regionEnd() > session_.regionStart());
        menu.addItem(base + 3, "Stems to folder...");
    }

    void exportMenu()
    {
        juce::PopupMenu menu;
        addExportItems(menu, 0);
        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_),
                           [safe](int choice)
                           {
                               if (safe != nullptr && choice > 0)
                                   safe->startExport(choice);
                           });
    }

    // 1 = the mix, 2 = the loop region, 3 = stems.
    void startExport(int choice)
    {
        const auto documents = projectFolder();
        const auto base = projectName();
        juce::Component::SafePointer<Content> safe(this);
        const auto report = [safe](const std::string& error)
        {
            if (safe == nullptr)
                return;
            if (!error.empty())
                safe->toast(ui::ToastStack::Kind::error, "Export: " + juce::String::fromUTF8(error.c_str()));
            safe->refresh();
        };
        if (choice == 3)
        {
            chooser_ =
                std::make_unique<juce::FileChooser>("Stems folder", documents.getChildFile(base + " Stems"));
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [this, report](const juce::FileChooser& fc)
                                  {
                                      const auto folder = fc.getResult();
                                      if (folder == juce::File())
                                          return;
                                      BounceJob::Options options;
                                      options.output = folder;
                                      options.stems = true;
                                      report(session_.startBounce(options));
                                  });
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export mix", documents.getChildFile(base + (choice == 2 ? " loop" : "") + ".wav"), "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, choice, report](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension("wav"))
                                      file = file.withFileExtension("wav");
                                  BounceJob::Options options;
                                  options.output = file;
                                  if (choice == 2)
                                  {
                                      options.start = session_.regionStart();
                                      options.end = session_.regionEnd();
                                  }
                                  report(session_.startBounce(options));
                              });
    }

    // ----- menus ---------------------------------------------------------------------------
    void appMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "About Aural");
        menu.addSeparator();
        menu.addItem(2, "Audio Setup...");
        menu.addItem(3, "Plugins...");
        menu.addItem(6, "Preferences...");
        juce::PopupMenu layout;
        layout.addItem(10, "Edit");
        layout.addItem(11, "Mix");
        layout.addSeparator();
        layout.addItem(12, "Reset layout");
        menu.addSubMenu("Layout", layout);
        menu.addSeparator();
        menu.addItem(4, "Quit");
        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_).withMinimumWidth(200),
                           [safe](int choice)
                           {
                               if (safe == nullptr)
                                   return;
                               switch (choice)
                               {
                                   case 1:
                                       safe->about();
                                       break;
                                   case 2:
                                       safe->owner_.showAudioSetup();
                                       break;
                                   case 3:
                                       safe->owner_.showPluginBrowser();
                                       break;
                                   case 6:
                                       safe->owner_.showPreferences();
                                       break;
                                   case 10:
                                       safe->panels_.applyPreset("Edit");
                                       break;
                                   case 11:
                                       safe->panels_.applyPreset("Mix");
                                       break;
                                   case 12:
                                       safe->panels_.resetLayout();
                                       break;
                                   case 4:
                                       juce::JUCEApplication::getInstance()->systemRequestedQuit();
                                       break;
                                   default:
                                       break;
                               }
                           });
    }

    void about()
    {
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Aural " + juce::JUCEApplication::getInstance()->getApplicationVersion(),
            "A native Windows DAW: ASIO, VST3 and CLAP hosting, multitrack recording and MIDI.",
            {{"Close", true, false}},
            [this](int) { sheet_ = nullptr; });
    }

    void projectMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "New");
        menu.addItem(2, "Open...");
        juce::PopupMenu recent;
        auto files = recentFiles();
        const auto shown = settings_.value(ui::PreferencesView::Keys::recentCount);
        if (const int count = shown.empty() ? 8 : juce::String(shown).getIntValue();
            static_cast<size_t>(std::max(0, count)) < files.size())
            files.resize(static_cast<size_t>(std::max(0, count)));
        for (size_t i = 0; i < files.size(); ++i)
            recent.addItem(100 + static_cast<int>(i), juce::File(files[i]).getFileNameWithoutExtension());
        menu.addSubMenu("Recent", recent, !files.empty());
        menu.addSeparator();
        menu.addItem(3, "Save");
        menu.addItem(4, "Save As...");
        menu.addSeparator();
        menu.addItem(5, "Import audio...");
        menu.addItem(6, "Add audio track");
        menu.addItem(7, "Add instrument track");
        menu.addItem(8, "Add bus");
        menu.addSeparator();
        juce::PopupMenu exports;
        addExportItems(exports, 20);
        menu.addSubMenu("Export", exports);
        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_).withMinimumWidth(220),
                           [safe, files](int choice)
                           {
                               if (safe == nullptr)
                                   return;
                               if (choice >= 100 && static_cast<size_t>(choice - 100) < files.size())
                               {
                                   safe->openFile(juce::File(files[static_cast<size_t>(choice - 100)]));
                                   return;
                               }
                               switch (choice)
                               {
                                   case 1:
                                       safe->newProject();
                                       break;
                                   case 2:
                                       safe->openProject();
                                       break;
                                   case 3:
                                       safe->saveProject(false);
                                       break;
                                   case 4:
                                       safe->saveProject(true);
                                       break;
                                   case 5:
                                       safe->importAudio();
                                       break;
                                   case 6:
                                       safe->addAudioTrack();
                                       break;
                                   case 7:
                                       safe->addInstrumentTrack();
                                       break;
                                   case 8:
                                       safe->addBus();
                                       break;
                                   case 21:
                                   case 22:
                                   case 23:
                                       safe->startExport(choice - 20);
                                       break;
                                   default:
                                       break;
                               }
                           });
    }

    void snapMenu()
    {
        static constexpr int divisions[] = {0, 1, 4, 8, 16, 32};
        static const char* const names[] = {"Adaptive", "Bar", "Beat", "1/8", "1/16", "1/32"};
        juce::PopupMenu menu;
        for (int i = 0; i < 6; ++i)
            menu.addItem(i + 1, names[i], true, tracks_.snapEnabled() && tracks_.snapGrid() == divisions[i]);
        menu.addSeparator();
        menu.addItem(7, "Off", true, !tracks_.snapEnabled());
        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_),
                           [safe](int choice)
                           {
                               if (safe == nullptr || choice == 0)
                                   return;
                               if (choice >= 1 && choice <= 6)
                               {
                                   safe->tracks_.setSnapGrid(divisions[choice - 1]);
                                   safe->tracks_.setSnapEnabled(true);
                               }
                               else
                                   safe->tracks_.setSnapEnabled(false);
                               safe->refreshHeader();
                           });
    }

    // ----- first run -------------------------------------------------------------------------
    void showFirstRun()
    {
        auto content = std::make_unique<FirstRunContent>(theme_, audio_);
        auto* raw = content.get();
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Choose your audio device",
            "Aural works best with an ASIO device. You can change this any time from the status bar.",
            {{"Skip", false, false}, {"Use this device", true, false}},
            [this, raw](int result)
            {
                sheet_ = nullptr;
                if (result == 1)
                    if (const auto error = raw->apply())
                        toast(ui::ToastStack::Kind::warning,
                              "The device could not be opened: " + juce::String::fromUTF8(error->c_str()),
                              "Audio Setup",
                              [this] { owner_.showAudioSetup(); });
                juce::Component::SafePointer<Content> safe(this);
                juce::MessageManager::callAsync(
                    [safe]
                    {
                        if (safe != nullptr)
                            safe->offerScan();
                    });
            },
            std::move(content),
            FirstRunContent::preferredHeight(theme_));
    }

    void offerScan()
    {
        sheet_ = ui::Sheet::show(*this,
                                 theme_,
                                 "Scan for plugins now?",
                                 "Aural looks in the VST3 and CLAP folders. Scanning runs in the background; "
                                 "you can keep working.",
                                 {{"Later", false, false}, {"Scan", true, false}},
                                 [this](int result)
                                 {
                                     sheet_ = nullptr;
                                     if (result == 1)
                                         session_.scanPlugins(true);
                                 });
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    const ui::Theme& theme_ = ui::Theme::dark();
    ui::HeaderBar header_;
    ui::ArrangeView tracks_;
    ui::PanelHost dock_, side_;
    ui::StatusBar status_;
    ui::ToastStack toasts_;
    Splitter dockSplitter_, sideSplitter_;
    ui::MixerView mixer_;
    ui::EditorPanel editor_;
    ui::PianoRoll pianoRoll_;
    ui::DiagnosticsPanel diagnostics_;
    ui::PluginBrowser browser_;
    ui::InspectorPanel inspector_;
    ui::PanelManager panels_; // after the panels: its floating windows go first
    ui::Sheet* sheet_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::string lastRecordingMessage_, lastBounceMessage_;
    bool lastRecording_ = false;
    int ticks_ = 0;
    int dropoutBaseline_ = 0;
    std::int64_t overrunBaseline_ = 0;
    double lastTickMs_ = 0.0;
    std::deque<double> frames_;
    bool lastDeviceOpen_ = false;
    std::string lastDeviceName_;
    int dropoutsToasted_ = 0;
    double lastDropoutToastMs_ = 0.0;
    bool quitAfterSave_ = false;
    double benchmarkEndMs_ = 0.0;
    juce::Component::SafePointer<juce::Component> hoverComponent_;
    double hoverSinceMs_ = 0.0;
};

// ----- secondary windows -------------------------------------------------------------------

class MainWindow::AudioSetupWindow final : public juce::DocumentWindow
{
public:
    AudioSetupWindow(MainWindow& owner, audio::AudioDeviceLayer& audio)
        : juce::DocumentWindow("Audio Setup",
                               ui::Theme::dark().colours.surface0,
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner_(owner)
    {
        setUsingNativeTitleBar(true);
        view_ = std::make_unique<ui::AudioSetupView>(audio);
        view_->onWarning = [&owner](const juce::String& message) { owner.notify(message, true); };
        auto* viewport = new juce::Viewport();
        viewport->setScrollBarsShown(true, false);
        viewport->setScrollBarThickness(ui::Theme::dark().sizes.scrollbar);
        viewport->setViewedComponent(view_.get(), false);
        viewport->setSize(ui::AudioSetupView::preferredWidth, ui::AudioSetupView::preferredHeight);
        setContentOwned(viewport, true);
        setResizable(true, false);
        setResizeLimits(ui::AudioSetupView::minimumWidth, ui::AudioSetupView::minimumHeight, 4000, 4000);
        centreAroundComponent(&owner, getWidth(), getHeight());
        resized();
        setVisible(true);
    }

    ~AudioSetupWindow() override
    {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(getContentComponent()))
            viewport->setViewedComponent(nullptr, false);
        view_.reset();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        if (auto* viewport = dynamic_cast<juce::Viewport*>(getContentComponent());
            viewport != nullptr && view_ != nullptr)
            view_->setSize(viewport->getWidth() - ui::Theme::dark().sizes.scrollbar,
                           std::max(viewport->getHeight(), ui::AudioSetupView::contentHeight));
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
    std::unique_ptr<ui::AudioSetupView> view_;
};

// The Preferences window: the PreferencesView, closed with Escape.
class MainWindow::PreferencesWindow final : public juce::DocumentWindow
{
public:
    PreferencesWindow(MainWindow& owner, std::unique_ptr<ui::PreferencesView> view)
        : juce::DocumentWindow("Preferences",
                               ui::Theme::dark().colours.surface0,
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          owner_(owner), view_(view.get())
    {
        setUsingNativeTitleBar(true);
        setContentOwned(view.release(), true);
        setResizable(true, false);
        setResizeLimits(640, 420, 4000, 4000);
        centreAroundComponent(&owner, getWidth(), getHeight());
        setVisible(true);
    }
    ui::PreferencesView& view() noexcept { return *view_; }
    void closeButtonPressed() override
    {
        juce::Component::SafePointer<MainWindow> owner(&owner_);
        juce::MessageManager::callAsync(
            [owner]
            {
                if (owner != nullptr)
                    owner->closePreferences();
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
    ui::PreferencesView* view_;
};

// ----- the window --------------------------------------------------------------------------

MainWindow::MainWindow(const juce::String& name,
                       audio::AudioDeviceLayer& audio,
                       ProjectSession& session,
                       UserSettings& settings)
    : juce::DocumentWindow(name, ui::Theme::dark().colours.canvas, juce::DocumentWindow::allButtons),
      audio_(audio), session_(session), settings_(settings)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new Content(*this, audio_, session_, settings_), true);
    setResizable(true, false);
    setResizeLimits(1280, 720, 10000, 10000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

MainWindow::~MainWindow()
{
    preferencesWindow_.reset();
    audioSetupWindow_.reset();
}

void MainWindow::closeButtonPressed()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->confirmClose();
    else
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

void MainWindow::showPreferences(const juce::String& page)
{
    if (preferencesWindow_ == nullptr)
    {
        auto view =
            std::make_unique<ui::PreferencesView>(ui::Theme::dark(), audio_, session_.pluginRegistry());
        view->loadSetting = [this](const std::string& key)
        {
            if (key == "__settingsFolder")
                return settings_.file().getParentDirectory().getFullPathName().toStdString();
            return settings_.value(key);
        };
        view->saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };
        juce::Component::SafePointer<MainWindow> safe(this);
        view->onChanged = [safe]
        {
            if (safe != nullptr)
                if (auto* content = dynamic_cast<Content*>(safe->getContentComponent()))
                    content->applyPreferences(false);
        };
        view->onClearBlocklist = [this] { session_.clearPluginBlocklist(); };
        view->onScan = [this] { session_.scanPlugins(false); };
        view->onNotice = [this](const juce::String& message) { notify(message, false); };
        view->onWarning = [this](const juce::String& message) { notify(message, true); };
        view->systemReport = [this]
        {
            if (auto* content = dynamic_cast<Content*>(getContentComponent()))
                return content->diagnostics_.report();
            return juce::String();
        };
        preferencesWindow_ = std::make_unique<PreferencesWindow>(*this, std::move(view));
    }
    if (page.isNotEmpty())
        preferencesWindow_->view().showPage(page);
    preferencesWindow_->toFront(true);
}

void MainWindow::closePreferences()
{
    preferencesWindow_.reset();
}

void MainWindow::showMixer()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->panels_.show(ui::PanelId::mixer);
}

void MainWindow::openInsertEditor(core::Id channelId, core::Id insertId)
{
    showMixer();
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->mixer_.openInsertEditor(channelId, insertId);
}

void MainWindow::showPluginBrowser()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        content->panels_.moveTo(ui::PanelId::browser, ui::HostKind::floating);
        content->panels_.show(ui::PanelId::browser);
    }
}

void MainWindow::openPianoRoll(core::Id trackId, core::Id clipId)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        content->pianoRoll_.setClip(&session_.project(), trackId, clipId);
        content->panels_.show(ui::PanelId::pianoRoll);
    }
}

void MainWindow::showPanel(const juce::String& key)
{
    auto* content = dynamic_cast<Content*>(getContentComponent());
    if (content == nullptr)
        return;
    for (const auto id : {ui::PanelId::mixer,
                          ui::PanelId::editor,
                          ui::PanelId::pianoRoll,
                          ui::PanelId::diagnostics,
                          ui::PanelId::browser,
                          ui::PanelId::inspector})
        if (key == ui::panelKey(id))
            content->panels_.show(id);
}

void MainWindow::applyLayoutPreset(const juce::String& name)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->panels_.applyPreset(name);
}

void MainWindow::notify(const juce::String& message, bool warning)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        if (warning)
            content->toast(ui::ToastStack::Kind::warning, message);
        else
            content->status_.showMessage(message);
    }
}

void MainWindow::addBus()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->addBus();
}

void MainWindow::removeLastBus()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->removeLastBus();
}

void MainWindow::runBenchmark(int seconds)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->runBenchmark(std::max(1, seconds));
}

void MainWindow::showFirstRun()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->showFirstRun();
}

void MainWindow::offerRecovery()
{
    const int count = session_.countPartialRecordings();
    if (count <= 0)
        return;
    auto* content = dynamic_cast<Content*>(getContentComponent());
    if (content == nullptr)
        return;
    juce::Component::SafePointer<MainWindow> safe(this);
    content->toast(
        ui::ToastStack::Kind::warning,
        juce::String(count) + (count == 1 ? " recording was" : " recordings were") +
            " not finished: the app closed while recording.",
        "Recover",
        [safe]
        {
            if (safe != nullptr)
                safe->session_.recoverPartialRecordings();
        },
        "Set aside",
        [safe]
        {
            if (safe != nullptr)
                safe->session_.discardPartialRecordings();
        });
}

} // namespace aural
