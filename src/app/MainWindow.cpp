#include "MainWindow.h"

#include "CrashReports.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "app/AppMenus.h"
#include "app/Benchmarks.h"
#include "app/ExportController.h"
#include "app/FileActions.h"
#include "app/FirstRun.h"
#include "app/LoadDriver.h"
#include "app/SessionNotices.h"
#include "app/WindowPlacement.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/Midi.h"
#include "engine/Snapshot.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ArrangeView.h"
#include "ui/AudioEditor.h"
#include "ui/AudioSetupView.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/MediaBrowser.h"
#include "ui/MixerView.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PianoRoll.h"
#include "ui/PluginBrowser.h"
#include "ui/PreferencesView.h"
#include "ui/Selection.h"
#include "ui/Shortcuts.h"
#include "ui/SplashScreen.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/Meter.h"
#include "ui/components/ProgressRow.h"
#include "ui/components/Sheet.h"
#include "ui/components/Toast.h"
#include "ui/components/Tooltips.h"
#include "ui/panels/DiagnosticsPanel.h"
#include "ui/panels/EditorPanel.h"
#include "ui/panels/InspectorPanel.h"
#include "ui/panels/InstrumentRack.h"
#include "ui/shell/HeaderBar.h"
#include "ui/shell/Panels.h"
#include "ui/shell/StatusBar.h"
#include "ui/strip/ChannelStripView.h"
#include "version/Version.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <optional>

namespace northlight
{

namespace
{
juce::String barsText(const core::TempoMap& map, core::SamplePosition position)
{
    const auto bb = map.barsBeatsAt(std::abs(position));
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
core::SamplePosition parseBars(const core::TempoMap& map, const juce::String& text)
{
    const auto parts = juce::StringArray::fromTokens(text.trim(), ".", "");
    if (parts.isEmpty() || parts[0].getIntValue() < 1)
        return -1;
    core::BarsBeats bb;
    bb.bar = parts[0].getIntValue();
    bb.beat = parts.size() > 1 ? std::max(1, parts[1].getIntValue()) : 1;
    bb.tick = parts.size() > 2 ? std::max(0, parts[2].getIntValue()) : 0;
    return map.positionAt(bb);
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

// A splitter between the arrange view and a host (design-system.md): a 5 px
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
    // --benchmark-panels: when set, each paint's time is appended. The side
    // splitter moves on every frame of a slide and is never cached, so its paints
    // are the frames that reach the screen.
    std::vector<double>* paintTimes = nullptr;

    void paint(juce::Graphics& g) override
    {
        if (paintTimes != nullptr)
            paintTimes->push_back(juce::Time::getMillisecondCounterHiRes());
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
          tracks_(session.peakStore(), theme_), dock_(theme_, ui::HostKind::dock),
          side_(theme_, ui::HostKind::side), status_(theme_), toasts_(theme_), tooltips_(theme_),
          dockSplitter_(theme_, true), sideSplitter_(theme_, false), meters_(session.engine().meters()),
          mixer_(
              meters_,
              [&session] { return session.engine().loudness(); },
              [&session] { return session.engine().masterMeterSlot(); }),
          editor_(theme_), instruments_(theme_), pianoRoll_(theme_),
          audioEditor_(session.peakStore(), session.vocal(), theme_), diagnostics_(theme_),
          browser_(session.pluginRegistry()), inspector_(theme_), panels_(theme_, dock_, side_)
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
            notices_.clearDropouts();
            refreshStatus();
        };
        audio_.addChangeListener(this);
        session_.addChangeListener(this);
        applyPreferences(true);
        refresh();
        startTimerHz(30);
    }

    // The user's settings, typed and defaulted by the schema (core/Settings.h).
    settings::Reader preferences() const
    {
        return settings::Reader([this](const std::string& key) { return settings_.value(key); });
    }

    // The preferences with an immediate effect (screens/setup.md 8.2); `atLaunch` also
    // applies the defaults a project starts with.
    void applyPreferences(bool atLaunch)
    {
        const auto read = preferences();
        const double scale = read.number(settings::uiScale);
        juce::Desktop::getInstance().setGlobalScaleFactor(
            static_cast<float>(scale > 0.0 ? scale / 100.0 : 1.0));
        session_.setMetronomeLevel(ui::db::toGain(read.number(settings::metronomeLevel)));
        std::vector<std::string> paths;
        for (const auto& item : juce::StringArray::fromTokens(
                 juce::String::fromUTF8(read.text(settings::pluginPaths).c_str()), "\t", ""))
            if (item.isNotEmpty())
                paths.push_back(item.toStdString());
        session_.setPluginSearchPaths(std::move(paths));
        tracks_.setFollowPlayhead(read.flag(settings::followPlayhead));
        session_.setReturnOnStop(read.flag(settings::returnOnStop));
        // The keys: loaded when the setting differs from the registry (the Shortcuts
        // page changes the registry itself), applied whenever the registry changed.
        if (const auto bindings = read.text(settings::shortcuts); bindings != ui::Shortcuts::save())
            ui::Shortcuts::load(bindings);
        if (atLaunch || ui::Shortcuts::generation() != shortcutsApplied_)
        {
            applyShortcuts();
            shortcutsApplied_ = ui::Shortcuts::generation();
        }
        // Accent and contrast apply at once: the colours load again into the theme
        // every view holds, and every window repaints.
        const auto appearance = read.text(settings::accent) + "|" + read.text(settings::contrast);
        if (!atLaunch && appearance != appearance_)
            applyAppearance();
        appearance_ = appearance;
        if (atLaunch)
        {
            session_.setCountInBars(read.integer(settings::countIn));
            tracks_.setSnapEnabled(read.flag(settings::snap));
            tracks_.setSnapGrid(read.integer(settings::snapGrid));
            tracks_.setRippleEnabled(read.flag(settings::ripple));
            if (read.flag(settings::scanOnLaunch))
                session_.scanPlugins(false);
        }
        refreshHeader();
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
        //
        // While a panel slides it moves over the arrange view (and the side panel
        // over the dock) at its open size, instead of resizing everything every
        // frame: the views are laid out once (at the start of a close, at the end
        // of an open) and drawn from cached images meanwhile, so a frame of the
        // slide is mostly a copy.
        const bool sliding = panels_.sliding();
        if (sliding != slideCached_)
        {
            slideCached_ = sliding;
            for (auto* view : std::initializer_list<juce::Component*>{&tracks_, &dock_, &side_})
                view->setBufferedToImage(sliding);
        }
        const int maxSide = std::max(0, area.getWidth() - 480 - splitter);
        const int maxDock = std::max(0, area.getHeight() - 240 - splitter);
        const int sideShown = std::min(panels_.sideWidth(), maxSide);
        const int sideLayout = std::min(panels_.sideLayoutWidth(), maxSide);
        const int sideWidth =
            sliding ? std::max(sideShown, std::min(panels_.sideOpenWidth(), maxSide)) : sideShown;
        side_.setVisible(sideShown > 0);
        sideSplitter_.setVisible(sideShown > 0);
        if (sideShown > 0)
        {
            side_.setBounds(area.getRight() - sideShown, area.getY(), sideWidth, area.getHeight());
            sideSplitter_.setBounds(side_.getX() - splitter, area.getY(), splitter, area.getHeight());
        }
        if (sideLayout > 0)
            area.removeFromRight(sideLayout + splitter);
        const int dockShown = std::min(panels_.dockHeight(), maxDock);
        const int dockLayout = std::min(panels_.dockLayoutHeight(), maxDock);
        const int dockHeight =
            sliding ? std::max(dockShown, std::min(panels_.dockOpenHeight(), maxDock)) : dockShown;
        dock_.setVisible(dockShown > 0);
        dockSplitter_.setVisible(dockShown > 0);
        if (dockShown > 0)
        {
            dock_.setBounds(area.getX(), area.getBottom() - dockShown, area.getWidth(), dockHeight);
            dockSplitter_.setBounds(area.getX(), dock_.getY() - splitter, area.getWidth(), splitter);
        }
        if (dockLayout > 0)
            area.removeFromBottom(dockLayout + splitter);
        tracks_.setBounds(area);
        const int toastW = std::min(420, getWidth() - 2 * theme_.spacing.md);
        toasts_.setBounds(theme_.spacing.md,
                          header_.getBottom() + theme_.spacing.md,
                          toastW,
                          status_.getY() - header_.getBottom() - 2 * theme_.spacing.md);
        if (sheet_ != nullptr)
            sheet_->setBounds(getLocalBounds());
    }

    // ----- the keyboard map (interaction.md 10.4) ---------------------------------------
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (session_.isLoading())
            return true; // the loading sheet holds the window
        if (key.getKeyCode() == juce::KeyPress::tabKey)
            ui::NorthlightLookAndFeel::setKeyboardFocusVisible(true);
        // The keyboard map (ui/Shortcuts.h): renaming while the arrange view has the
        // focus (F2 there), the window's commands, the panels' keys, then the
        // arrange view's. The piano roll and audio editor took theirs first, while
        // they had the focus.
        if (ui::Shortcuts::matches(ui::Command::renameTrack, key) && perform(ui::Command::renameTrack))
            return true;
        if (ui::Shortcuts::dispatch(key, [this](ui::Command command) { return perform(command); }))
            return true;
        if (panels_.handleKey(key))
            return true;
        if (!tracks_.handleEditKey(key))
            return false;
        // Esc clears the selection and gives the arrange view the focus back (10.4).
        if (ui::Shortcuts::matches(ui::Command::clearSelection, key) && !tracks_.hasKeyboardFocus(true))
            tracks_.grabKeyboardFocus();
        return true;
    }

    void applyAppearance()
    {
        const auto before = theme_.colours;
        ui::Theme::reloadColours(settings_.file().getSiblingFile(ui::Theme::userOverrideName),
                                 settings_.file().getSiblingFile(ui::Theme::preferencesOverrideName));
        if (auto* lnf = dynamic_cast<ui::NorthlightLookAndFeel*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
            lnf->refreshColours();
        auto& desktop = juce::Desktop::getInstance();
        std::vector<juce::Component*> windows{&owner_};
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = desktop.getComponent(i); window != &owner_)
                windows.push_back(window);
        for (auto* window : windows)
        {
            ui::NorthlightLookAndFeel::remapColours(*window, before, theme_.colours);
            window->sendLookAndFeelChange(); // repaints, and drops cached images
        }
    }

    // The keys the user rebound reach what reads them outside the table: the
    // panels' keys and the tooltips set once.
    void applyShortcuts()
    {
        using C = ui::Command;
        const std::pair<ui::PanelId, C> panelKeys[]{{ui::PanelId::mixer, C::showMixer},
                                                    {ui::PanelId::editor, C::showEditor},
                                                    {ui::PanelId::browser, C::showBrowser},
                                                    {ui::PanelId::inspector, C::showInspector},
                                                    {ui::PanelId::pianoRoll, C::showPianoRoll},
                                                    {ui::PanelId::audioEditor, C::showAudioEditor},
                                                    {ui::PanelId::instruments, C::showInstruments},
                                                    {ui::PanelId::diagnostics, C::showDiagnostics}};
        for (const auto& [panel, command] : panelKeys)
        {
            const auto& keys = ui::Shortcuts::of(command).keys;
            panels_.setShortcut(panel, keys.empty() ? juce::KeyPress() : keys.front().keyPress());
        }
        header_.refreshTooltips();
        ui::Shortcuts::retag(owner_);
        for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
            if (auto* window = juce::Desktop::getInstance().getComponent(i); window != &owner_)
                ui::Shortcuts::retag(*window);
        refreshHeader();
    }

    // Runs one of the window's keyboard commands; false when it does not apply now
    // (the key then goes on to the panels and the arrange view).
    bool perform(ui::Command command)
    {
        using C = ui::Command;
        switch (command)
        {
            case C::renameTrack:
                // F2 renames the selected track while the arrange view has the focus
                // (10.4); elsewhere it is the mixer's panel key.
                if (!tracks_.hasKeyboardFocus(true) || selection_.channel() == core::invalidId)
                    return false;
                tracks_.renameSelectedTrack();
                return true;
            case C::findPlugin:
                panels_.show(ui::PanelId::browser);
                browserPanel_.focusSearch();
                return true;
            case C::playStop:
                session_.togglePlay();
                return true;
            case C::pause:
                session_.togglePause();
                return true;
            case C::record:
                toggleRecord();
                return true;
            case C::armSelected:
                armSelected();
                return true;
            case C::returnToStart:
                session_.returnToStart();
                return true;
            case C::goToEnd:
                session_.locate(session_.project().length());
                return true;
            case C::loop:
                session_.setLoopEnabled(!session_.loopEnabled());
                return true;
            case C::punch:
                session_.setPunchEnabled(!session_.punchEnabled());
                return true;
            case C::click:
                session_.setMetronome(!session_.metronomeEnabled());
                return true;
            case C::zoomIn:
                tracks_.zoomBy(0.75);
                return true;
            case C::zoomOut:
                tracks_.zoomBy(1.0 / 0.75);
                return true;
            case C::zoomToFit:
                tracks_.zoomToFit();
                return true;
            case C::zoomToSelection:
                tracks_.zoomToSelection();
                return true;
            case C::snap:
                tracks_.setSnapEnabled(!tracks_.snapEnabled());
                refreshHeader();
                return true;
            case C::ripple:
                tracks_.setRippleEnabled(!tracks_.rippleEnabled());
                refreshHeader();
                return true;
            case C::muteTrack:
                if (const auto* t = selectedTrack())
                    session_.execute(std::make_unique<core::SetTrackMute>(t->id, !t->mute));
                return true;
            case C::soloTrack:
                if (const auto* t = selectedTrack())
                    session_.execute(std::make_unique<core::SetTrackSolo>(t->id, !t->solo));
                return true;
            case C::newProject:
                fileActions_.newProject();
                return true;
            case C::open:
                fileActions_.openProject();
                return true;
            case C::save:
                fileActions_.saveProject(false);
                return true;
            case C::saveAs:
                fileActions_.saveProject(true);
                return true;
            case C::importAudio:
                fileActions_.importAudio();
                return true;
            case C::exportMix:
                exporter_.showSheet();
                return true;
            case C::undo:
                undo();
                return true;
            case C::redo:
                redo();
                return true;
            case C::audioSetup:
                owner_.showAudioSetup();
                return true;
            case C::preferences:
                owner_.showPreferences();
                return true;
            case C::plugins:
                owner_.showPluginBrowser();
                return true;
            case C::addAudioTrack:
                addAudioTrack();
                return true;
            case C::addMidiTrack:
                addMidiTrack({});
                return true;
            case C::addBus:
                addBus();
                return true;
            default:
                return false; // a panel's or the arrange view's
        }
    }

    // The custom chrome: what a point in the content is to the window. Only the
    // header bar is title bar; everything else is client. Geometry only.
    juce::Component::WindowControlKind windowControlAt(juce::Point<float> point) const
    {
        if (header_.getBounds().toFloat().contains(point))
            return header_.windowControlAt(point - header_.getPosition().toFloat());
        return juce::Component::WindowControlKind::client;
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
        // Play turns into Pause while playing (transport.md); Stop returns to where
        // playback started when that preference is on.
        header_.onPlayStop = [this] { session_.togglePause(); };
        header_.onStop = [this] { session_.stopTransport(); };
        header_.onRecord = [this] { toggleRecord(); };
        header_.onDeviceWarning = [this] { owner_.showAudioSetup(); };
        header_.onMinimise = [this] { owner_.minimiseButtonPressed(); };
        header_.onMaximise = [this] { owner_.maximiseButtonPressed(); };
        header_.onCloseWindow = [this] { owner_.closeButtonPressed(); };
        header_.onToggleLoop = [this] { session_.setLoopEnabled(!session_.loopEnabled()); };
        header_.onTogglePunch = [this] { session_.setPunchEnabled(!session_.punchEnabled()); };
        header_.onToggleClick = [this] { session_.setMetronome(!session_.metronomeEnabled()); };
        header_.onCountInBars = [this](int bars) { session_.setCountInBars(bars); };
        header_.onBarsTyped = [this](const juce::String& text)
        {
            if (const auto at = parseBars(session_.tempoMap(), text); at >= 0)
                session_.locate(at);
        };
        header_.onTimeTyped = [this](const juce::String& text)
        {
            if (const auto at = parseTime(session_.project(), text); at >= 0)
                session_.locate(at);
        };
        header_.onBarsNudged = [this](int segment, int steps)
        {
            // Bars, beats or ticks on the tempo track at the position.
            const auto map = session_.tempoMap(); // a copy: the handler may change the tempo
            const auto at = session_.position();
            const double beat = core::TempoMap::quartersPerBeat(map.signatureAt(at));
            const core::GridStep step = segment == 0 ? core::GridStep{1, 0.0}
                                        : segment == 1
                                            ? core::GridStep{0, beat}
                                            : core::GridStep{0, beat / core::BarsBeats::ticksPerBeat};
            nudge(static_cast<double>(map.move(at, step, steps) - at));
        };
        header_.onTimeNudged = [this](int segment, int steps)
        {
            const double rate = session_.project().sampleRate;
            const double unit = segment == 0 ? 60.0 * rate : segment == 1 ? rate : rate / 1000.0;
            nudge(unit * steps);
        };
        // The header edits the tempo and signature in effect at the playhead: the
        // song's start values, or the tempo track's change that governs there.
        header_.onTempo = [this](double bpm)
        {
            const auto& p = session_.project();
            const auto map = session_.tempoMap(); // a copy: the handler may change the tempo
            const double q = map.quartersAt(static_cast<double>(heardPosition()));
            int index = -1;
            for (int i = 0; i < static_cast<int>(p.tempoChanges.size()); ++i)
                if (p.tempoChanges[static_cast<size_t>(i)].quarter <= q + 1.0e-9)
                    index = i;
            if (index < 0)
            {
                session_.execute(std::make_unique<core::SetTempo>(bpm));
                return;
            }
            auto state = core::TempoState::of(p);
            state.tempoChanges[static_cast<size_t>(index)].bpm = bpm;
            session_.execute(std::make_unique<core::SetTempoMap>(std::move(state), "Set tempo"));
        };
        header_.onSignature = [this](int n, int d)
        {
            const auto& p = session_.project();
            const auto map = session_.tempoMap(); // a copy: the handler may change the tempo
            const int bar = map.barAtQuarter(map.quartersAt(static_cast<double>(heardPosition())));
            int index = -1;
            for (int i = 0; i < static_cast<int>(p.meterChanges.size()); ++i)
                if (p.meterChanges[static_cast<size_t>(i)].bar <= bar)
                    index = i;
            if (index < 0)
            {
                session_.execute(std::make_unique<core::SetTimeSignature>(n, d));
                return;
            }
            auto state = core::TempoState::of(p);
            state.meterChanges[static_cast<size_t>(index)].signature = {n, d};
            session_.execute(std::make_unique<core::SetTempoMap>(std::move(state), "Set time signature"));
        };
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
        tracks_.onTrackInputChanged = [this](core::Id id, int channel, const std::string& bus)
        { session_.execute(std::make_unique<core::SetTrackInput>(id, channel, bus)); };
        tracks_.onTrackMidiInputChanged = [this](core::Id id, const std::string& name)
        { session_.execute(std::make_unique<core::SetTrackMidiInput>(id, name)); };
        tracks_.onTrackArmChanged = [this](core::Id id, bool armed)
        { session_.execute(std::make_unique<core::SetTrackArmed>(id, armed)); };
        tracks_.onTrackMonitorChanged = [this](core::Id id, core::MonitorMode mode)
        { session_.execute(std::make_unique<core::SetTrackMonitor>(id, mode)); };
        tracks_.onImportFiles = [this](const std::vector<juce::File>& files, core::SamplePosition at)
        { fileActions_.importFiles(files, at); };
        tracks_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        { session_.executeGroup(name, std::move(commands)); };
        tracks_.onOpenMidiClip = [this](core::Id trackId, core::Id clipId)
        { owner_.openPianoRoll(trackId, clipId); };
        tracks_.onOpenAudioClip = [this](core::Id trackId, core::Id clipId)
        { owner_.openAudioEditor(trackId, clipId); };
        tracks_.onNormalise = [this](core::Id trackId, core::Id clipId)
        {
            const auto error = session_.normaliseClip(trackId, clipId);
            if (!error.empty())
                toast(ui::ToastStack::Kind::error,
                      "Normalise failed: " + juce::String::fromUTF8(error.c_str()));
        };
        tracks_.onConformClip = [this](core::Id trackId, core::Id clipId) { conformClip(trackId, clipId); };
        tracks_.onFreezeTrack = [this](core::Id trackId, bool freeze)
        {
            if (!freeze)
            {
                session_.execute(std::make_unique<core::SetTrackFrozen>(trackId, std::string{}));
                return;
            }
            if (const auto error = session_.freezeTrack(trackId); !error.empty())
                toast(ui::ToastStack::Kind::warning,
                      "Cannot freeze: " + juce::String::fromUTF8(error.c_str()));
        };
        // The tempo track: its edits, and whether it shows (kept per user).
        tracks_.onTempoEdit = [this](core::TempoState state, const std::string& name)
        { session_.execute(std::make_unique<core::SetTempoMap>(std::move(state), name)); };
        tracks_.setTempoTrackShown(settings_.value("arrange.tempoTrack", "0") == "1");
        tracks_.onTempoTrackShownChanged = [this](bool shown)
        { settings_.setValue("arrange.tempoTrack", shown ? "1" : "0"); };
        tracks_.stretchProgress = [this](const core::AudioClip& clip)
        { return session_.stretch().progress(clip, session_.project().sampleRate); };
        tracks_.onOpenInSampler = [this](core::Id trackId, core::Id clipId)
        { openInSampler(trackId, clipId); };
        tracks_.playheadForEdits = [this] { return session_.position(); };
        // Audio files and Standard MIDI Files.
        tracks_.setAcceptedExtensions(session_.mediaLoader().supportedExtensions() + ";*.mid;*.midi");
        tracks_.setSelection(&selection_);
        tracks_.onEditModesChanged = [this]
        {
            arrangeModes_ = {tracks_.snapEnabled(), tracks_.rippleEnabled(), tracks_.snapGridName()};
            refreshHeader();
        };
        arrangeModes_ = {tracks_.snapEnabled(), tracks_.rippleEnabled(), tracks_.snapGridName()};
        tracks_.onTrackSelected = [this](core::Id id) { select(id); };
        tracks_.onSelectionChanged = [this]
        {
            select(tracks_.selectedTrack());
            followMidiSelection();
        };
        selection_.onChanged = [this] { applySelection(); };
        tracks_.onAddAudioTrack = [this] { addAudioTrack(); };
        tracks_.onAddMidiTrack = [this](const std::string& type) { addMidiTrack(type); };
        tracks_.onAddMidiTrackPlaying = [this](core::Id channel)
        {
            const auto id = session_.addMidiTrackPlaying(channel);
            select(id);
        };
        tracks_.onAddBus = [this] { addBus(); };
        tracks_.onImportRequested = [this] { fileActions_.importAudio(); };
        tracks_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        tracks_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        tracks_.onGestureEnd = [this] { session_.endGesture(); };
        tracks_.trackMeter = [this](int index) { return meters_.reading(engine::trackMeterSlot(index)); };
        tracks_.busMeter = [this](int index)
        { return meters_.reading(engine::busMeterSlot(session_.project(), index)); };
        tracks_.masterMeter = [this] { return meters_.reading(session_.engine().masterMeterSlot()); };
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
        // --capture saves these by name when they are showing (MainWindow::capture).
        mixer_.setComponentID("capture:mixer");
        pianoRoll_.setComponentID("capture:piano-roll");
        audioEditor_.setComponentID("capture:audio-editor");
        mixer_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        mixer_.onGestureEnd = [this] { session_.endGesture(); };
        mixer_.setHost(&session_.insertFactory());
        mixer_.liveInsert = [this](core::Id insertId) { return session_.liveInsert(insertId); };
        mixer_.onScanPlugins = [this] { session_.scanPlugins(true); };
        mixer_.onSelectChannel = [this](core::Id id) { select(id); };
        mixer_.onOpenPluginBrowser = [this] { owner_.showPluginBrowser(); };
        mixer_.automatedValues = [this](core::Id id)
        {
            const auto v = session_.engine().automatedValues(id);
            return ui::MixerView::Automated{v.gain, v.pan, v.gainValue, v.panValue};
        };
        mixer_.onShowStrip = [this](core::Id id)
        {
            select(id); // a bus too: the editor shows its strip
            panels_.show(ui::PanelId::editor);
        };
        mixer_.loadSetting = [this](const std::string& key) { return settings_.value(key); };
        mixer_.saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };

        editor_.setRegistry(&session_.pluginRegistry());
        editor_.liveInsert = [this](core::Id insertId) { return session_.liveInsert(insertId); };
        editor_.onMinimumHeightChanged = [this] { panels_.enforceDockMinimum(); };
        editor_.displayName = [this](const std::string& type) { return mixer_.insertDisplayName(type); };
        editor_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        editor_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        editor_.onGestureEnd = [this] { session_.endGesture(); };
        editor_.onOpenEditor = [this](core::Id channelId, core::Id insertId)
        { owner_.openInsertEditor(channelId, insertId); };
        editor_.onPlayNewInstrument = [this](core::Id midiTrack, const std::string& type)
        { playNewInstrument(midiTrack, type); };

        instruments_.setRegistry(&session_.pluginRegistry());
        instruments_.liveInsert = [this](core::Id insertId) { return session_.liveInsert(insertId); };
        instruments_.displayName = [this](const std::string& type) { return mixer_.insertDisplayName(type); };
        instruments_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        instruments_.onOpenEditor = [this](core::Id channelId, core::Id insertId)
        { owner_.openInsertEditor(channelId, insertId); };
        instruments_.onSelectTrack = [this](core::Id trackId) { select(trackId); };
        instruments_.onInserted = [this](const std::string& type) { browser_.rememberRecent(type); };
        instruments_.onAddMidiTrack = [this](const std::string& type) { addMidiTrack(type); };

        inspector_.onCommand = [this](std::unique_ptr<core::Command> c) { session_.execute(std::move(c)); };
        inspector_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        inspector_.onGestureEnd = [this] { session_.endGesture(); };

        pianoRoll_.onEdit =
            [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        { session_.executeGroup(name, std::move(commands)); };

        audioEditor_.onEdit =
            [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        { session_.executeGroup(name, std::move(commands)); };
        audioEditor_.onGestureBegin = [this](const std::string& name) { session_.beginGesture(name); };
        audioEditor_.onGestureEnd = [this] { session_.endGesture(); };
        // A linked editor's own zoom and scroll move the arrange view, which the
        // tick then feeds back to every linked editor.
        audioEditor_.onTimeViewChanged = [this](double samplesPerPixel, core::SamplePosition first)
        { tracks_.setTimeView(samplesPerPixel, first); };
        pianoRoll_.onTimeViewChanged = [this](double samplesPerPixel, core::SamplePosition first)
        { tracks_.setTimeView(samplesPerPixel, first); };
        audioEditor_.onUndo = [this] { session_.undo(); };
        audioEditor_.onAudition = [this](core::SamplePosition from, core::SamplePosition to)
        {
            // Plays the stretch, then stops (the timer watches for its end).
            session_.locate(from);
            session_.play();
            auditionEnd_ = to;
        };

        pianoRoll_.onPreviewNote = [this](core::Id trackId, int note, bool on)
        {
            engine::MidiEvent event;
            event.type = on ? engine::MidiEvent::Type::noteOn : engine::MidiEvent::Type::noteOff;
            event.note = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
            event.velocity = on ? 100 : 0;
            session_.engine().pushMidiPreview(event, trackId);
        };
        browser_.onScan = [this](bool all) { session_.scanPlugins(all); };
        browser_.onCancelScan = [this] { session_.cancelPluginScan(); };
        browser_.onClearBlocklist = [this] { session_.clearPluginBlocklist(); };
        browser_.onRetry = [this](const std::string& path) { session_.retryBlockedPlugin(path); };
        browser_.scanProgress = [this] { return session_.pluginScanProgress(); };
        browser_.loadSetting = [this](const std::string& key) { return settings_.value(key); };
        browser_.saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };
        browser_.onInsert = [this](const std::string& type) { insertPlugin(selection_.channel(), type); };
        browser_.setStatus(session_.pluginScanStatus());
        wireFiles();
        tracks_.setDisplayName([this](const std::string& type) { return mixer_.insertDisplayName(type); });
        tracks_.midiOutputName = [this] { return audio_.midi().outputName(); };
        tracks_.setHostedParameters(
            [this](core::Id insertId) -> const std::vector<engine::ParameterInfo>*
            {
                const auto live = session_.liveInsert(insertId);
                return live != nullptr ? &live->parameters() : nullptr;
            });
        tracks_.onInsertPlugin = [this](core::Id trackId, const std::string& type)
        { insertPlugin(trackId, type); };
        tracks_.onPlayNewInstrument = [this](core::Id midiTrack, const std::string& type)
        { playNewInstrument(midiTrack, type); };
        editor_.isEditorOpen = [this](core::Id insertId) { return mixer_.isEditorOpen(insertId); };
        mixer_.onEditorWindowsChanged = [this] { editor_.editorWindowsChanged(); };
        pluginSources_.registry = &session_.pluginRegistry();
        pluginSources_.listSetting = [this](const std::string& key)
        {
            std::vector<std::string> values;
            for (const auto& item : juce::StringArray::fromTokens(
                     juce::String::fromUTF8(settings_.value(key).c_str()), "\t", ""))
                if (item.isNotEmpty())
                    values.push_back(item.toStdString());
            return values;
        };
        tracks_.setPluginSources(&pluginSources_);
        mixer_.setPluginSources(&pluginSources_);
        editor_.setPluginSources(&pluginSources_);
        instruments_.setPluginSources(&pluginSources_);
        editor_.onInserted = [this](const std::string& type) { browser_.rememberRecent(type); };
    }

    void wirePanels()
    {
        using ui::HostKind;
        using ui::Icon;
        using ui::PanelId;
        using ui::PanelInfo;
        // Each panel's key comes from the shortcut table (applyShortcuts keeps it current).
        const auto key = [](ui::Command command)
        {
            const auto& keys = ui::Shortcuts::of(command).keys;
            return keys.empty() ? juce::KeyPress() : keys.front().keyPress();
        };
        panels_.registerPanel(PanelInfo{PanelId::mixer,
                                        "Mixer",
                                        Icon::mixer,
                                        HostKind::dock,
                                        480,
                                        240,
                                        true,
                                        false,
                                        key(ui::Command::showMixer)},
                              mixer_);
        panels_.registerPanel(PanelInfo{PanelId::editor,
                                        "Editor",
                                        Icon::plugin,
                                        HostKind::dock,
                                        320,
                                        200,
                                        true,
                                        true,
                                        key(ui::Command::showEditor),
                                        [this] { return editor_.minimumHeight(); }},
                              editor_);
        panels_.registerPanel(PanelInfo{PanelId::browser,
                                        "Browser",
                                        Icon::browser,
                                        HostKind::side,
                                        240,
                                        200,
                                        true,
                                        true,
                                        key(ui::Command::showBrowser)},
                              browserPanel_);
        panels_.registerPanel(PanelInfo{PanelId::inspector,
                                        "Inspector",
                                        Icon::inspector,
                                        HostKind::side,
                                        240,
                                        200,
                                        false,
                                        true,
                                        key(ui::Command::showInspector)},
                              inspector_);
        panels_.registerPanel(PanelInfo{PanelId::pianoRoll,
                                        "Piano Roll",
                                        Icon::pianoRoll,
                                        HostKind::dock,
                                        480,
                                        240,
                                        true,
                                        false,
                                        key(ui::Command::showPianoRoll)},
                              pianoRoll_);
        panels_.registerPanel(PanelInfo{PanelId::audioEditor,
                                        "Audio Editor",
                                        Icon::fileAudio,
                                        HostKind::dock,
                                        480,
                                        260,
                                        true,
                                        false,
                                        key(ui::Command::showAudioEditor)},
                              audioEditor_);
        panels_.registerPanel(PanelInfo{PanelId::instruments,
                                        "Instruments",
                                        Icon::instrument,
                                        HostKind::dock,
                                        480,
                                        200,
                                        true,
                                        false,
                                        key(ui::Command::showInstruments)},
                              instruments_);
        panels_.registerPanel(PanelInfo{PanelId::diagnostics,
                                        "Diagnostics",
                                        Icon::diagnostics,
                                        HostKind::dock,
                                        320,
                                        200,
                                        true,
                                        false,
                                        key(ui::Command::showDiagnostics)},
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
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        // A knob or fader drag sends a command per mouse move. Those only move
        // values, so the views refresh their values (no device queries, no
        // relayout, no full repaints), no more than 20 times a second, and a
        // parameter (a strip knob) refreshes only the editor windows that show
        // parameters: the strip cards and insert cards follow the processors
        // themselves. That leaves the message thread free for the analysers.
        const auto change = source == &session_ ? session_.takeChange() : ProjectSession::Change::structure;
        if (change == ProjectSession::Change::progress)
        {
            // A plugin scanned: the browser's list and status and the status bar.
            browser_.setStatus(session_.pluginScanStatus());
            browser_.refresh();
            refreshStatus();
            return;
        }
        if (change != ProjectSession::Change::structure)
        {
            pendingParametersOnly_ = pendingParametersOnly_ && change == ProjectSession::Change::parameters;
            const double now = juce::Time::getMillisecondCounterHiRes();
            const double wait = 50.0 - (now - lastValuesRefreshMs_);
            if (wait <= 0.0)
                refreshValues();
            else if (!valuesRefreshPending_)
            {
                valuesRefreshPending_ = true;
                juce::Component::SafePointer<Content> safe(this);
                juce::Timer::callAfterDelay(static_cast<int>(wait) + 1,
                                            [safe]
                                            {
                                                if (safe == nullptr)
                                                    return;
                                                safe->valuesRefreshPending_ = false;
                                                safe->refreshValues();
                                            });
            }
            return;
        }
        refresh();
    }

    void refreshValues()
    {
        lastValuesRefreshMs_ = juce::Time::getMillisecondCounterHiRes();
        const bool parametersOnly = pendingParametersOnly_;
        pendingParametersOnly_ = true;
        if (parametersOnly)
        {
            mixer_.refreshEditors();
            instruments_.setProject(&session_.project()); // an instrument's outputs follow its parameters
            return;
        }
        tracks_.setProject(&session_.project(), true);
        mixer_.setProject(&session_.project(), true);
        editor_.setProject(&session_.project());
        instruments_.setProject(&session_.project());
        inspector_.setProject(&session_.project());
    }

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
            // The input menus: the stereo buses this device can play, then every
            // enabled channel, both named by their device channel numbers.
            std::vector<ui::InputChoice> choices;
            if (s.open)
            {
                const auto in = [](int deviceChannel) { return "In " + std::to_string(deviceChannel + 1); };
                const auto resolved = audio::resolveInputBuses(audio_.inputBuses(), s.activeInputs);
                for (const auto& bus : audio_.inputBuses())
                    if (resolved.contains(bus.name))
                        choices.push_back({bus.name, in(bus.left) + " + " + in(bus.right), -1, bus.name});
                for (size_t i = 0; i < s.activeInputs.size(); ++i)
                    choices.push_back({in(s.activeInputs[i]), {}, static_cast<int>(i), {}});
            }
            mixer_.setInputChoices(choices, audio_.midi().openInputNames());
            tracks_.setInputChoices(std::move(choices));
            tracks_.setMidiInputNames(audio_.midi().openInputNames());
        }
        tracks_.setProject(&session_.project());
        // There is always a selected track while the project has one (the editor
        // and inspector follow it); a removed one falls back to the first.
        selection_.validate(session_.project());
        mixer_.setProject(&session_.project());
        editor_.setProject(&session_.project());
        instruments_.setProject(&session_.project());
        inspector_.setProject(&session_.project());
        pianoRoll_.setClip(&session_.project(), pianoRoll_.trackId(), pianoRoll_.clipId());
        audioEditor_.setClip(&session_.project(), audioEditor_.trackId(), audioEditor_.clipId());
        browser_.setStatus(session_.pluginScanStatus());
        browser_.refresh();
        tracks_.setRegion(
            session_.regionStart(), session_.regionEnd(), session_.loopEnabled(), session_.punchEnabled());
        refreshHeader();
        refreshStatus();
        refreshTitle();
        // What the session has to tell the user, each once.
        notices_.drain();
    }

    void refreshTitle()
    {
        owner_.setName((session_.isRecording() ? juce::String::fromUTF8("\xe2\x97\x8f ") : juce::String()) +
                       fileActions_.projectName() + (session_.isModified() ? "*" : "") + " - Northlight");
    }

    void refreshHeader()
    {
        ui::HeaderBar::State state;
        const auto& p = session_.project();
        state.projectName = fileActions_.projectName();
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
        state.barsText = barsText(session_.tempoMap(), heardPosition());
        state.timeText = timeText(p, heardPosition());
        {
            // The tempo and signature at the heard position.
            const auto& map = session_.tempoMap();
            const auto at = std::max<core::SamplePosition>(0, heardPosition());
            const auto signature = map.signatureAt(at);
            state.tempo = map.bpmAt(at);
            state.signatureNumerator = signature.numerator;
            state.signatureDenominator = signature.denominator;
        }
        state.masterClipped = mixer_.masterClipped();
        state.snap = arrangeModes_.snap;
        state.snapName = arrangeModes_.snapName;
        state.ripple = arrangeModes_.ripple;
        state.mixerVisible = panels_.isVisible(ui::PanelId::mixer);
        state.editorVisible = panels_.isVisible(ui::PanelId::editor);
        state.browserVisible = panels_.isVisible(ui::PanelId::browser);
        state.windowControls = !owner_.isUsingNativeTitleBar();
        state.maximised = owner_.isFullScreen();
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
        state.devicePreferredUnavailable = s.open && s.preferredUnavailable;
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
        state.dropouts = notices_.dropouts();
        state.recording = session_.isRecording();
        if (state.recording)
            state.diskFreeGb = static_cast<double>(session_.mediaFolder().getBytesFreeOnVolume()) / 1.0e9;
        state.warning = juce::String::fromUTF8(session_.sampleRateWarning().c_str());
        if (session_.isScanningPlugins())
            state.warning = juce::String::fromUTF8(session_.pluginScanStatus().c_str());
        if (session_.isBouncing())
            state.warning =
                "Bouncing " + juce::String(static_cast<int>(session_.bounceProgress() * 100.0f)) + "%";
        if (session_.isFreezing())
            state.warning =
                "Freezing " + juce::String(static_cast<int>(session_.freezeProgress() * 100.0f)) + "%";
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
        // The takes being recorded, drawn growing (screens/arrange.md "Recording clip").
        session_.updateLiveRecording(tracks_.liveRecording());
        tracks_.liveRecordingChanged();
        meters_.tick();
        mixer_.pushMeters();
        tracks_.pushMeters();
        if (auditionEnd_ >= 0 && (!session_.isPlaying() || session_.position() >= auditionEnd_))
        {
            if (session_.isPlaying())
                session_.stop();
            auditionEnd_ = -1;
        }
        if (panels_.isVisible(ui::PanelId::audioEditor))
        {
            audioEditor_.setPlayhead(session_.position());
            if (audioEditor_.zoomLinked())
                audioEditor_.setTimeView(tracks_.samplesPerPixel(), tracks_.scrollPosition());
        }
        if (panels_.isVisible(ui::PanelId::pianoRoll))
        {
            pianoRoll_.setPlayhead(session_.position());
            if (pianoRoll_.zoomLinked())
                pianoRoll_.setTimeView(tracks_.samplesPerPixel(), tracks_.scrollPosition());
        }
        refreshHeader();
        status_.setSelectedClips(static_cast<int>(tracks_.selection().size()));
        if (ticks_ % 5 == 0)
        {
            monitoring_.clear(); // kept: no allocation per tick
            for (const auto& track : session_.project().tracks)
                monitoring_.push_back(session_.isTrackMonitoring(track));
            tracks_.setMonitoringStates(monitoring_, recording);
        }
        if (ticks_ % 15 == 0)
            refreshStatus();
        updateHoverHint();
        benchmarks_.tick(ticks_);
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
        sample.lateCallbacks = static_cast<int>(stats.late);
        sample.maxLateness = stats.maxLateness;
        sample.driverDropouts = s.open ? s.driverXruns : -1;
        if (const auto* tap = audio_.callback().outputTap())
        {
            const auto t = tap->totals();
            sample.tapActive = true;
            sample.tapJumps = static_cast<int>(t.jumps);
            sample.tapGaps = static_cast<int>(t.gaps);
        }
        sample.diskGaps = static_cast<int>(session_.diskStarvations());
        sample.device = s.open ? juce::String::fromUTF8(s.deviceType.c_str()) + " " +
                                     juce::String::fromUTF8(s.outputDevice.c_str())
                               : juce::String();
        sample.sampleRate = s.sampleRate;
        sample.bufferSize = s.bufferSize;
        sample.workerThreads = session_.engine().workerThreads();
        sample.mmcssWorkerThreads = session_.engine().mmcssWorkerThreads();
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
            for (const auto* insert : track.processingChain())
                if (const auto live = session_.liveInsert(insert->id))
                    latency += live->latencySamples();
            if (latency > 0)
                sample.trackLatencies.emplace_back(juce::String::fromUTF8(track.name.c_str()), latency);
        }
        diagnostics_.push(sample);
    }

    // ----- selection -----------------------------------------------------------------------
    const core::Track* selectedTrack() const { return session_.project().findTrack(selection_.channel()); }

    // Everything that selects comes here; selection_ notifies applySelection.
    void select(core::Id channel) { selection_.select(channel, focusedClipOn(channel)); }

    // The piano roll shows the most recently selected MIDI clip (a selection with
    // none keeps the clip it shows).
    void followMidiSelection()
    {
        const auto& clips = tracks_.selection();
        for (auto it = clips.rbegin(); it != clips.rend(); ++it)
            if (it->midi)
            {
                if (it->clip != pianoRoll_.clipId() || it->track != pianoRoll_.trackId())
                    pianoRoll_.setClip(&session_.project(), it->track, it->clip);
                return;
            }
    }

    // The audio clip on the channel the inspector shows: the arrange view's most
    // recently selected one there, if any.
    core::Id focusedClipOn(core::Id channel) const
    {
        core::Id clipId = core::invalidId;
        for (const auto& clip : tracks_.selection())
            if (!clip.midi && clip.track == channel)
                clipId = clip.clip;
        return clipId;
    }

    // The one place the views learn the selection.
    void applySelection()
    {
        const auto channel = selection_.channel();
        tracks_.setSelectedTrack(channel);
        editor_.setChannel(channel);
        inspector_.setSelection(channel, selection_.clip());
        mixer_.setSelectedChannel(channel);
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
                            "Save changes to " + fileActions_.projectName() + "?",
                            "Unsaved changes will be lost if you close without saving.",
                            {{"Cancel", false, false}, {"Don't save", false, true}, {"Save", true, false}},
                            [this](int result)
                            {
                                sheet_ = nullptr;
                                if (result == 1)
                                    juce::JUCEApplication::getInstance()->quit();
                                else if (result == 2)
                                    fileActions_.saveProject(false, FileActions::AfterSave::quit);
                            });
    }

    // Inserts a plugin type on a track (from the browser, a drop or the chain).
    // Makes an instrument channel playing `type` just under a MIDI track and aims
    // the track at it (one undo step).
    void playNewInstrument(core::Id midiTrack, const std::string& type)
    {
        if (session_.playNewInstrument(midiTrack, type) == core::invalidId)
            return;
        browser_.rememberRecent(type);
        select(midiTrack);
    }

    void insertPlugin(core::Id trackId, const std::string& type)
    {
        // Where it goes is the session's rule; the shell remembers it and selects.
        using Placement = ProjectSession::PluginPlacement;
        const auto result = session_.insertPlugin(trackId, type);
        if (result.placement == Placement::noChannel)
        {
            toast(ui::ToastStack::Kind::info, "Select a track to insert the plugin on.");
            return;
        }
        browser_.rememberRecent(type);
        // A new instrument selects the MIDI track that plays it.
        select(result.placement == Placement::newInstrument ? result.midiTrack : trackId);
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
            session_.stopTransport();
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
            if (t.kind == core::TrackKind::audio)
                ++count;
        track.name = "Audio " + std::to_string(count + 1);
        auto command = std::make_unique<core::AddTrack>(track);
        const auto* added = command.get(); // the undo history keeps it alive
        session_.execute(std::move(command));
        const auto id = added->trackId(); // allocated on apply
        select(id);
    }

    // The browser's Files tab: places from the settings and the project, previews
    // through the session, a file onto a new track at the playhead.
    void wireFiles()
    {
        files_.loadSetting = [this](const std::string& key) { return settings_.value(key); };
        files_.saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };
        files_.projectFolder = [this]
        {
            const auto& file = session_.projectFile();
            return file.existsAsFile() ? file.getParentDirectory() : juce::File();
        };
        files_.onPreview = [this](const juce::File& file) { return session_.previewFile(file); };
        files_.onStopPreview = [this] { session_.stopPreview(); };
        files_.isPreviewing = [this] { return session_.isPreviewing(); };
        files_.onPump = [this] { session_.pumpPreview(); };
        files_.onPreviewGain = [this](float gain) { session_.setPreviewGain(gain); };
        files_.onImport = [this](const juce::File& file)
        { fileActions_.importFiles({file}, session_.position()); };
        browserPanel_.onTabChanged = [this](bool files)
        { settings_.setValue("browser.tab", files ? "files" : "plugins"); };
        if (settings_.value("browser.tab") == "files")
            browserPanel_.showFiles();
    }

    // An audio clip played by a new Sampler: an instrument channel and a MIDI
    // track, the sampler's start and end at the part of the file the clip uses;
    // its editor opens.
    void openInSampler(core::Id trackId, core::Id clipId)
    {
        const auto* track = session_.project().findTrack(trackId);
        const auto* clip = track != nullptr ? track->findClip(clipId) : nullptr;
        if (clip == nullptr)
            return;
        const juce::File file(juce::String::fromUTF8(clip->mediaPath.c_str()));
        std::string error;
        const auto info = session_.mediaLoader().probe(clip->mediaPath, error);
        if (!info.has_value() || info->lengthSamples <= 0)
        {
            toast(ui::ToastStack::Kind::error,
                  "Cannot open in Sampler: " + juce::String::fromUTF8(error.c_str()));
            return;
        }
        const double total = static_cast<double>(info->lengthSamples);
        const double used = static_cast<double>(clip->length) * info->sampleRate /
                            std::max(1.0, session_.project().sampleRate);
        const double from = static_cast<double>(clip->sourceOffset) / total;
        const double to = (static_cast<double>(clip->sourceOffset) + used) / total;
        const auto midi = session_.addSamplerTrack(file, from, std::min(1.0, to));
        const auto* added = session_.project().findTrack(midi);
        if (added == nullptr)
            return;
        browser_.rememberRecent(plugins::sampler::type);
        select(midi);
        if (const auto* channel = session_.project().findTrack(added->midiTarget))
            owner_.openInsertEditor(channel->id, channel->instrument.id);
    }

    // A MIDI track with a new instrument channel (the default instrument when
    // `type` is empty).
    void addMidiTrack(const std::string& type)
    {
        const auto id = session_.addMidiTrack(type);
        if (!type.empty())
            browser_.rememberRecent(type);
        select(id);
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

    // The MIDI clips an export takes: those selected in the arrange view, else
    // every clip of the selected MIDI track, else every MIDI clip.
    std::vector<std::pair<core::Id, core::Id>> midiClipsToExport() const
    {
        std::vector<std::pair<core::Id, core::Id>> clips;
        for (const auto& ref : tracks_.selection())
            if (ref.midi)
                clips.emplace_back(ref.track, ref.clip);
        if (!clips.empty())
            return clips;
        const auto& project = session_.project();
        if (const auto* track = project.findTrack(selection_.channel()); track != nullptr && track->isMidi())
            for (const auto& c : track->midiClips)
                clips.emplace_back(track->id, c.id);
        if (!clips.empty())
            return clips;
        for (const auto& track : project.tracks)
            for (const auto& c : track.midiClips)
                clips.emplace_back(track.id, c.id);
        return clips;
    }

    // ----- export ----------------------------------------------------------------------------
    // The tracks "selected tracks" exports: the selected channel when it is a
    // track, and every track holding a selected clip.
    std::vector<core::Id> selectedTrackIds() const
    {
        std::vector<core::Id> ids;
        const auto add = [&](core::Id id)
        {
            if (session_.project().findTrack(id) != nullptr &&
                std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        };
        add(selection_.channel());
        for (const auto& clip : tracks_.selection())
            add(clip.track);
        return ids;
    }

    // ----- menus (AppMenus.h: every item carries its action) -------------------------------
    // An action that runs on this content if it still exists when the item is clicked.
    std::function<void()> whenAlive(std::function<void(Content&)> f)
    {
        juce::Component::SafePointer<Content> safe(this);
        return [safe, f = std::move(f)]
        {
            if (safe != nullptr)
                f(*safe);
        };
    }

    void appMenu()
    {
        juce::Component::SafePointer<Content> safe(this);
        menus::AppMenu actions;
        actions.about = whenAlive([](Content& c) { c.about(); });
        actions.audioSetup = whenAlive([](Content& c) { c.owner_.showAudioSetup(); });
        actions.plugins = whenAlive([](Content& c) { c.owner_.showPluginBrowser(); });
        actions.preferences = whenAlive([](Content& c) { c.owner_.showPreferences(); });
        actions.layout = [safe](const juce::String& preset)
        {
            if (safe != nullptr)
                safe->panels_.applyPreset(preset);
        };
        actions.resetLayout = whenAlive([](Content& c) { c.panels_.resetLayout(); });
        actions.quit = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        menus::appMenu(actions).showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&header_).withMinimumWidth(200));
    }

    void about()
    {
        ui::SplashScreen::showAbout(
            theme_, juce::String::fromUTF8(version::display().c_str()), getTopLevelComponent());
    }

    void projectMenu()
    {
        juce::Component::SafePointer<Content> safe(this);
        menus::ProjectMenu m;
        m.templates = session_.templates();
        auto files = fileActions_.recentFiles();
        if (const int count = preferences().integer(settings::recentCount);
            static_cast<size_t>(std::max(0, count)) < files.size())
            files.resize(static_cast<size_t>(std::max(0, count)));
        for (const auto& file : files)
            m.recent.emplace_back(file);
        for (const auto& choice : plugins::fxTrackEffects())
            m.fxTracks.push_back(choice.name);
        m.saved = session_.projectFile() != juce::File();
        m.newProject = whenAlive([](Content& c) { c.fileActions_.newProject(); });
        m.open = whenAlive([](Content& c) { c.fileActions_.openProject(); });
        m.save = whenAlive([](Content& c) { c.fileActions_.saveProject(false); });
        m.saveAs = whenAlive([](Content& c) { c.fileActions_.saveProject(true); });
        m.saveTemplate = whenAlive([](Content& c) { c.fileActions_.saveTemplate(); });
        m.gatherMedia = whenAlive([](Content& c) { c.fileActions_.gatherMedia(); });
        m.removeUnusedMedia = whenAlive([](Content& c) { c.fileActions_.removeUnusedMedia(); });
        m.importAudio = whenAlive([](Content& c) { c.fileActions_.importAudio(); });
        m.importMidi = whenAlive([](Content& c) { c.fileActions_.importMidi(); });
        m.addAudioTrack = whenAlive([](Content& c) { c.addAudioTrack(); });
        m.addMidiTrack = whenAlive([](Content& c) { c.addMidiTrack({}); });
        m.addBus = whenAlive([](Content& c) { c.addBus(); });
        m.exportMix = whenAlive([](Content& c) { c.exporter_.showSheet(); });
        m.exportMidi = whenAlive([](Content& c) { c.fileActions_.exportMidi(); });
        m.newFromTemplate = [safe](const juce::File& file)
        {
            if (safe != nullptr)
                safe->fileActions_.newFromTemplate(file);
        };
        m.openRecent = [safe](const juce::File& file)
        {
            if (safe != nullptr)
                safe->fileActions_.openFile(file);
        };
        m.addFxTrack = [safe](size_t i)
        {
            const auto fx = plugins::fxTrackEffects();
            if (safe != nullptr && i < fx.size())
                safe->session_.executeGroup(
                    "Add FX track",
                    plugins::addFxTrackCommands(safe->session_.project(), fx[i].type, fx[i].name));
        };
        menus::projectMenu(m).showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&header_).withMinimumWidth(220));
    }

    void snapMenu()
    {
        juce::Component::SafePointer<Content> safe(this);
        menus::snapMenu(tracks_.snapEnabled(),
                        tracks_.snapGrid(),
                        [safe](int divisions)
                        {
                            if (safe == nullptr)
                                return;
                            if (divisions >= 0)
                            {
                                safe->tracks_.setSnapGrid(divisions);
                                safe->tracks_.setSnapEnabled(true);
                            }
                            else
                                safe->tracks_.setSnapEnabled(false);
                            safe->refreshHeader();
                        })
            .showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_.snapButton()));
    }

    // ----- clip tempo --------------------------------------------------------------------------
    // Asks for the tempo a clip was recorded at (offering a guess from its name,
    // beats and length), then stretches it to the project's tempo.
    void conformClip(core::Id trackId, core::Id clipId)
    {
        const auto guess = session_.guessClipTempo(trackId, clipId);
        auto field = std::make_unique<juce::TextEditor>();
        field->setInputRestrictions(7, "0123456789.");
        field->setJustification(juce::Justification::centredLeft);
        field->setText(guess.bpm > 0.0 ? juce::String(guess.bpm, 2) : juce::String(), false);
        field->selectAll();
        auto* raw = field.get();
        juce::String body = "The clip will play at the project's tempo and follow tempo changes. ";
        switch (guess.source)
        {
            case engine::TempoGuess::Source::name:
                body << "The tempo below is from the file's name.";
                break;
            case engine::TempoGuess::Source::length:
                body << "The tempo below fits the clip's beats and a whole number of bars.";
                break;
            case engine::TempoGuess::Source::beats:
                body << "The tempo below is a guess from the clip's beats; check it.";
                break;
            case engine::TempoGuess::Source::none:
                body << "Enter the tempo the clip was recorded at.";
                break;
        }
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Clip tempo (BPM)",
            body,
            {{"Cancel", false, false}, {"Stretch", true, false}},
            [this, raw, trackId, clipId](int result)
            {
                sheet_ = nullptr;
                const double bpm = raw->getText().getDoubleValue();
                if (result != 1)
                    return;
                if (bpm < 20.0 || bpm > 400.0)
                {
                    toast(ui::ToastStack::Kind::warning, "A clip tempo is between 20 and 400 BPM.");
                    return;
                }
                session_.conformClipTempo(trackId, clipId, bpm);
            },
            std::move(field),
            theme_.sizes.controlLarge);
        juce::Component::SafePointer<juce::TextEditor> focus(raw);
        juce::MessageManager::callAsync(
            [focus]
            {
                if (focus != nullptr)
                    focus->grabKeyboardFocus();
            });
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    const ui::Theme& theme_ = ui::Theme::dark();
    double lastValuesRefreshMs_ = 0.0;
    bool pendingParametersOnly_ = true; // the values refresh due covers parameters only
    bool valuesRefreshPending_ = false;
    ui::HeaderBar header_;
    ui::Selection selection_;         // before the views: they are told about it
    ui::PluginSources pluginSources_; // every view's plugin menus read it
    ui::ArrangeView tracks_;
    ui::PanelHost dock_, side_;
    ui::StatusBar status_;
    ui::ToastStack toasts_;
    ui::Tooltips tooltips_; // the floating tooltip for every control
    Splitter dockSplitter_, sideSplitter_;
    ui::MeterReadings meters_; // every view's meters, read once a tick
    // The arrange view's snap, grid and ripple as it last reported them (the header shows them).
    struct ArrangeModes
    {
        bool snap = true, ripple = false;
        juce::String snapName;
    } arrangeModes_;
    ui::MixerView mixer_;
    ui::EditorPanel editor_;
    ui::InstrumentRack instruments_;
    ui::PianoRoll pianoRoll_;
    ui::AudioEditor audioEditor_;
    core::SamplePosition auditionEnd_ = -1; // an auditioned note stops the transport here
    ui::DiagnosticsPanel diagnostics_;
    ui::PluginBrowser browser_;
    ui::MediaBrowser files_;
    ui::BrowserPanel browserPanel_{browser_, files_};
    ui::InspectorPanel inspector_;
    ui::PanelManager panels_; // after the panels: its floating windows go first
    ui::Sheet* sheet_ = nullptr;
    // The export sheet and the bounce it starts (ExportController.h).
    ExportController exporter_{{*this,
                                theme_,
                                session_,
                                settings_,
                                sheet_,
                                [this] { return selectedTrackIds(); },
                                [this] { return fileActions_.projectFolder(); },
                                [this] { return fileActions_.projectName(); },
                                [this](const juce::String& message)
                                { toast(ui::ToastStack::Kind::error, message); },
                                [this] { refresh(); }}};
    // Steps a project load with the loading sheet over the window (LoadDriver.h).
    LoadDriver loader_{{*this,
                        theme_,
                        session_,
                        [this] { refresh(); },
                        [this](const juce::String& title)
                        {
                            status_.showMessage("Stopped: " + title);
                            grabKeyboardFocus();
                        }}};
    // The session's notices and the dropout warnings (SessionNotices.h).
    SessionNotices notices_{{session_,
                             audio_,
                             [this](ui::ToastStack::Kind kind,
                                    const juce::String& text,
                                    const juce::String& action,
                                    std::function<void()> onAction)
                             { toast(kind, text, action, std::move(onAction)); },
                             [this](const juce::String& text) { status_.showMessage(text); },
                             [this] { owner_.showAudioSetup(); }}};
    // The first-run sheets: the audio device, then the plugin scan (FirstRun.h).
    FirstRun firstRun_{{*this,
                        theme_,
                        audio_,
                        session_,
                        sheet_,
                        [this](ui::ToastStack::Kind kind,
                               const juce::String& text,
                               const juce::String& action,
                               std::function<void()> onAction)
                        { toast(kind, text, action, std::move(onAction)); },
                        [this] { owner_.showAudioSetup(); }}};
    // New, open, save, templates, media, imports and MIDI export (FileActions.h).
    FileActions fileActions_{{*this,
                              theme_,
                              session_,
                              settings_,
                              loader_,
                              sheet_,
                              [this] { mixer_.closeEditors(); },
                              [this] { refresh(); },
                              [this](ui::ToastStack::Kind kind, const juce::String& text)
                              { toast(kind, text); },
                              [this](const juce::String& text) { status_.showMessage(text); },
                              [this] { owner_.offerRecovery(); },
                              [this] { return selection_.channel(); },
                              [this] { return midiClipsToExport(); }}};
    bool lastRecording_ = false;
    int ticks_ = 0;
    std::vector<bool> monitoring_; // which tracks monitor their input, for the headers
    double lastTickMs_ = 0.0;
    std::deque<double> frames_;
    bool lastDeviceOpen_ = false;
    std::string lastDeviceName_;
    std::string appearance_;    // the accent and contrast the colours were last loaded with
    int shortcutsApplied_ = -1; // the ui::Shortcuts generation applyShortcuts last saw
    bool slideCached_ = false;  // the views are drawn from cached images during a panel slide
    juce::Component::SafePointer<juce::Component> hoverComponent_;
    double hoverSinceMs_ = 0.0;
    // The developer benchmarks (Benchmarks.h); last, since they refer to the parts above.
    Benchmarks benchmarks_{{*this,
                            session_,
                            audio_,
                            tracks_,
                            panels_,
                            sideSplitter_.paintTimes,
                            frames_,
                            [this](core::Id channel) { select(channel); }}};
};

// ----- secondary windows -------------------------------------------------------------------

class MainWindow::AudioSetupWindow final : public juce::DocumentWindow
{
public:
    AudioSetupWindow(MainWindow& owner, audio::AudioDeviceLayer& audio)
        : juce::DocumentWindow("Audio Setup",
                               ui::Theme::dark().colours.surface0,
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton,
                               !ui::offscreenMode()),
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
        ui::showWindow(*this);
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
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton,
                               !ui::offscreenMode()),
          owner_(owner), view_(view.get())
    {
        setUsingNativeTitleBar(true);
        setContentOwned(view.release(), true);
        setResizable(true, false);
        setResizeLimits(640, 420, 4000, 4000);
        centreAroundComponent(&owner, getWidth(), getHeight());
        ui::showWindow(*this);
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
    : juce::DocumentWindow(name,
                           ui::Theme::dark().colours.canvas,
                           juce::DocumentWindow::allButtons,
                           !ui::offscreenMode()), // off-screen: never a native window
      audio_(audio), session_(session), settings_(settings)
{
    // Custom chrome (screens/shell.md): no Windows title bar; the header bar is the
    // title bar and draws the window buttons. findControlAtPoint keeps snapping,
    // edge resizing and the Snap Layouts flyout native.
    setUsingNativeTitleBar(false);
    setTitleBarHeight(0);
    auto content = std::make_unique<Content>(*this, audio_, session_, settings_);
    content_ = content.get();
    setContentOwned(content.release(), true);
    setResizable(true, false);
    setResizeLimits(1280, 720, 10000, 10000);
    centreWithSize(getWidth(), getHeight());
    restorePlacement();
    ui::showWindow(*this); // --offscreen: no native window at all
    if (ui::offscreenMode())
    {
        // Nothing paints a window that is not on screen, and some parts load what
        // they draw on their first paint (a clip's waveform peaks): paint into a
        // throwaway image a few times a second, as the screen would.
        offscreenPainter_ = std::make_unique<juce::TimedCallback>(
            [this]
            {
                if (auto* content = getContentComponent())
                    content->createComponentSnapshot(content->getLocalBounds(), true, 1.0f);
            });
        offscreenPainter_->startTimer(250);
    }
}

MainWindow::~MainWindow()
{
    savePlacement();
    offscreenPainter_.reset();
    preferencesWindow_.reset();
    audioSetupWindow_.reset();
}

void MainWindow::restorePlacement()
{
    if (ui::offscreenMode())
        return;
    const auto state = juce::String::fromUTF8(settings_.value(placementKey).c_str());
    const auto saved = parsePlacement(state);
    if (!saved)
        return; // the first launch, or nothing usable: centred at the default size
    std::vector<juce::Rectangle<int>> workAreas;
    for (const auto& display : juce::Desktop::getInstance().getDisplays().displays)
        workAreas.push_back(display.userBounds.toNearestInt());
    if (!reachable(saved->bounds, workAreas))
        return; // its screen is gone (or it was dragged off): centred instead
    restoreWindowStateFromString(state);
}

void MainWindow::savePlacement()
{
    if (ui::offscreenMode() || !isOnDesktop())
        return;
    settings_.setValue(placementKey, getWindowStateAsString().toStdString());
    settings_.flush();
}

void MainWindow::closeButtonPressed()
{
    if (auto* content = content_)
        content->confirmClose();
    else
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

auto MainWindow::findControlAtPoint(juce::Point<float> point) const -> WindowControlKind
{
    if (isUsingNativeTitleBar())
        return DocumentWindow::findControlAtPoint(point);
    // Windows 11 expects the top resize edge inside the window (the other edges
    // are outside it, in the invisible frame).
    constexpr float edge = 4.0f;
    const auto bounds = getLocalBounds().toFloat();
    if (isResizable() && !isFullScreen() && point.y < edge)
        return point.x < edge                       ? WindowControlKind::sizeTopLeft
               : point.x > bounds.getRight() - edge ? WindowControlKind::sizeTopRight
                                                    : WindowControlKind::sizeTop;
    if (auto* content = dynamic_cast<const Content*>(getContentComponent()))
        return content->windowControlAt(point - content->getPosition().toFloat());
    return WindowControlKind::client;
}

void MainWindow::showAbout()
{
    ui::SplashScreen::showAbout(ui::Theme::dark(), juce::String::fromUTF8(version::display().c_str()), this);
}

void MainWindow::showAudioSetup()
{
    if (audioSetupWindow_ == nullptr)
        audioSetupWindow_ = std::make_unique<AudioSetupWindow>(*this, audio_);
    ui::bringToFront(*audioSetupWindow_);
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
        view->settingsFolder = settings_.file().getParentDirectory();
        view->loadSetting = [this](const std::string& key) { return settings_.value(key); };
        view->saveSetting = [this](const std::string& key, const std::string& value)
        { settings_.setValue(key, value); };
        juce::Component::SafePointer<MainWindow> safe(this);
        view->onChanged = [safe]
        {
            if (safe != nullptr)
                if (auto* content = safe->content_)
                    content->applyPreferences(false);
        };
        view->onClearBlocklist = [this] { session_.clearPluginBlocklist(); };
        view->onScan = [this] { session_.scanPlugins(false); };
        view->onNotice = [this](const juce::String& message) { notify(message, false); };
        view->onWarning = [this](const juce::String& message) { notify(message, true); };
        view->systemReport = [this]
        {
            if (auto* content = content_)
                return content->diagnostics_.report();
            return juce::String();
        };
        preferencesWindow_ = std::make_unique<PreferencesWindow>(*this, std::move(view));
    }
    if (page.isNotEmpty())
        preferencesWindow_->view().showPage(page);
    ui::bringToFront(*preferencesWindow_);
}

void MainWindow::closePreferences()
{
    preferencesWindow_.reset();
}

void MainWindow::showMixer()
{
    if (auto* content = content_)
        content->panels_.show(ui::PanelId::mixer);
}

void MainWindow::showTempoTrack()
{
    if (auto* content = content_)
        content->tracks_.setTempoTrackShown(true);
}

// The editor opens in its own window; the dock stays on whatever panel it shows
// (opening a plugin from the Editor panel keeps the Editor panel up).
void MainWindow::openInsertEditor(core::Id channelId, core::Id insertId)
{
    if (auto* content = content_)
        content->mixer_.openInsertEditor(channelId, insertId);
}

void MainWindow::showFilesBrowser(const juce::File& folder)
{
    if (auto* content = content_)
    {
        content->browserPanel_.showFiles();
        if (folder.isDirectory())
            content->files_.showFolder(folder);
        content->panels_.show(ui::PanelId::browser);
    }
}

void MainWindow::showPluginBrowser()
{
    if (auto* content = content_)
    {
        content->browserPanel_.showPlugins();
        content->panels_.moveTo(ui::PanelId::browser, ui::HostKind::floating);
        content->panels_.show(ui::PanelId::browser);
    }
}

void MainWindow::openPianoRoll(core::Id trackId, core::Id clipId)
{
    if (auto* content = content_)
    {
        content->pianoRoll_.setClip(&session_.project(), trackId, clipId);
        content->panels_.show(ui::PanelId::pianoRoll);
    }
}

void MainWindow::setPianoRollLane(const juce::String& key)
{
    if (auto* content = content_)
        content->pianoRoll_.setLane(ui::PianoRoll::laneForKey(key));
}

void MainWindow::openAudioEditor(core::Id trackId, core::Id clipId, bool pitch)
{
    if (auto* content = content_)
    {
        content->audioEditor_.setClip(&session_.project(), trackId, clipId);
        if (pitch)
            content->audioEditor_.setMode(ui::AudioEditor::Mode::pitch);
        content->panels_.show(ui::PanelId::audioEditor);
    }
}

void MainWindow::zoomAudioEditor(double samplesPerPixel, double secondsIntoClip)
{
    if (auto* content = content_)
        content->audioEditor_.zoomTo(samplesPerPixel, secondsIntoClip);
}

void MainWindow::selectClip(core::Id trackId, core::Id clipId)
{
    if (auto* content = content_)
        content->selection_.select(trackId, clipId);
}

void MainWindow::correctPitchInEditor()
{
    if (auto* content = content_)
        content->audioEditor_.correctToKey({});
}

void MainWindow::muteBreathsInEditor()
{
    if (auto* content = content_)
        content->audioEditor_.setBreaths(0.0f, true);
}

void MainWindow::runStripDragBenchmark(int seconds)
{
    if (auto* content = content_)
        content->benchmarks_.runStripDragBenchmark(seconds);
}

void MainWindow::runHoverBenchmark(int seconds)
{
    if (auto* content = content_)
        content->benchmarks_.runHoverBenchmark(seconds);
}

void MainWindow::showAutomation(core::Id trackId)
{
    if (auto* content = content_)
        content->tracks_.showAutomation(trackId, true);
}

void MainWindow::showPanel(const juce::String& key)
{
    auto* content = content_;
    if (content == nullptr)
        return;
    for (const auto id : {ui::PanelId::mixer,
                          ui::PanelId::editor,
                          ui::PanelId::pianoRoll,
                          ui::PanelId::audioEditor,
                          ui::PanelId::diagnostics,
                          ui::PanelId::browser,
                          ui::PanelId::inspector,
                          ui::PanelId::instruments})
        if (key == ui::panelKey(id))
            content->panels_.show(id);
}

void MainWindow::showMenu(const juce::String& key)
{
    auto* content = content_;
    if (content == nullptr)
        return;
    if (key == "project")
        content->projectMenu();
    else if (key == "add-track")
        content->tracks_.openAddTrackMenu();
    else if (key == "clip")
        content->tracks_.openFirstClipMenu();
}

bool MainWindow::setArrangeTool(const juce::String& name)
{
    auto* content = content_;
    if (content == nullptr)
        return false;
    for (int i = 0; i < ui::ArrangeView::toolCount; ++i)
        if (name == ui::ArrangeView::toolName(static_cast<ui::ArrangeView::Tool>(i)))
        {
            content->tracks_.setTool(static_cast<ui::ArrangeView::Tool>(i));
            return true;
        }
    return false;
}

void MainWindow::arrangePointer(int row, double bar, juce::ModifierKeys mods)
{
    if (auto* content = content_)
        content->tracks_.pointerAt(content->tracks_.pointOnRow(row, bar), mods);
}

void MainWindow::showEditorGap(int n)
{
    auto* content = content_;
    if (content == nullptr)
        return;
    const auto& gaps = content->editor_.gapButtons();
    if (n >= 1 && static_cast<size_t>(n) <= gaps.size())
        content->editor_.pointerAt(gaps[static_cast<size_t>(n - 1)]->getBounds().getCentre());
}

juce::StringArray MainWindow::capture(const juce::File& folder, float scale)
{
    juce::StringArray saved;
    auto* content = getContentComponent();
    if (content == nullptr || !folder.createDirectory())
        return saved;
    const auto save = [&](juce::Component& component, const juce::String& name)
    {
        const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, scale);
        const auto file = folder.getChildFile(name + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        if (out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out))
            saved.add(file.getFileName());
    };
    save(*content, "window");
    // Every part tagged "capture:<name>" that is on screen, in full even where a
    // scrolled panel shows only some of it.
    std::function<void(juce::Component&)> visit = [&](juce::Component& component)
    {
        const auto id = component.getComponentID();
        if (id.startsWith("capture:") && ui::onScreen(component) && !component.getLocalBounds().isEmpty())
            save(component, id.fromFirstOccurrenceOf(":", false, false));
        for (auto* child : component.getChildren())
            visit(*child);
    };
    visit(*content);
    // Preferences is a window of its own: save each of its pages, then return to the
    // page it was showing.
    if (preferencesWindow_ != nullptr && preferencesWindow_->isVisible())
    {
        auto& view = preferencesWindow_->view();
        const auto shown = view.currentPage();
        for (const auto& page : ui::PreferencesView::pageKeys())
        {
            view.showPage(page);
            save(view, "preferences-" + page);
        }
        view.showPage(shown);
    }
    // Other windows (an insert's or an instrument's editor) as window-<name>.
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        auto* window = juce::TopLevelWindow::getTopLevelWindow(i);
        if (window == nullptr || window == this || !window->isVisible() ||
            (preferencesWindow_ != nullptr && window == preferencesWindow_.get()))
            continue;
        juce::Component* shown = window;
        if (auto* document = dynamic_cast<juce::DocumentWindow*>(window);
            document != nullptr && document->getContentComponent() != nullptr)
            shown = document->getContentComponent();
        const auto name =
            juce::File::createLegalFileName(window->getName()).toLowerCase().replaceCharacter(' ', '-');
        save(*shown, "window-" + (name.isNotEmpty() ? name : juce::String(i)));
    }
    return saved;
}

void MainWindow::applyLayoutPreset(const juce::String& name)
{
    if (auto* content = content_)
        content->panels_.applyPreset(name);
}

void MainWindow::openWithProgress(const juce::File& file, const juce::String& holdAt)
{
    auto* content = content_;
    if (content == nullptr)
        return;
    std::optional<LoadProgress::Stage> hold;
    if (holdAt == "reading")
        hold = LoadProgress::Stage::reading;
    else if (holdAt == "media")
        hold = LoadProgress::Stage::media;
    else if (holdAt == "plugins")
        hold = LoadProgress::Stage::plugins;
    else if (holdAt == "engine")
        hold = LoadProgress::Stage::engine;
    content->fileActions_.openFile(file, hold);
}

void MainWindow::notify(const juce::String& message, bool warning)
{
    if (auto* content = content_)
    {
        if (warning)
            content->toast(ui::ToastStack::Kind::warning, message);
        else
            content->status_.showMessage(message);
    }
}

void MainWindow::addBus()
{
    if (auto* content = content_)
        content->addBus();
}

void MainWindow::removeLastBus()
{
    if (auto* content = content_)
        content->removeLastBus();
}

void MainWindow::runPanelBenchmark(int cycles)
{
    if (auto* content = content_)
        content->benchmarks_.runPanelBenchmark(std::max(1, cycles));
}

void MainWindow::runBenchmark(int seconds)
{
    if (auto* content = content_)
        content->benchmarks_.runBenchmark(std::max(1, seconds));
}

void MainWindow::showFirstRun()
{
    if (auto* content = content_)
        content->firstRun_.show();
}

void MainWindow::showExport()
{
    if (auto* content = content_)
        content->exporter_.showSheet();
}

void MainWindow::offerRecovery()
{
    auto* content = content_;
    if (content == nullptr)
        return;
    juce::Component::SafePointer<MainWindow> safe(this);
    // A crash report from last time: say so, once.
    const auto crashes = settings_.file().getSiblingFile("Crashes");
    const auto reports = crash::pending(crashes.getFullPathName().toWideCharPointer());
    if (!reports.empty())
    {
        const auto report = juce::File(juce::String(reports.front().wstring().c_str()));
        content->toast(
            ui::ToastStack::Kind::warning,
            "Northlight closed unexpectedly last time. A crash report was saved on this computer.",
            "Show",
            [report, crashes]
            {
                crash::acknowledge(crashes.getFullPathName().toWideCharPointer());
                crashes.getChildFile("Seen").getChildFile(report.getFileName()).revealToUser();
            },
            "Dismiss",
            [crashes] { crash::acknowledge(crashes.getFullPathName().toWideCharPointer()); });
    }
    // A project a crashed session left unsaved: the newest is offered (older
    // ones stay for the next launch).
    if (const auto leftovers = session_.leftoverAutosaves(); !leftovers.empty())
    {
        const auto autosave = leftovers.front();
        content->toast(
            ui::ToastStack::Kind::warning,
            "Northlight closed with unsaved changes to \"" + juce::String::fromUTF8(autosave.name.c_str()) +
                "\" (autosaved " + autosave.time.formatted("%H:%M") + ").",
            "Restore",
            [safe, autosave]
            {
                if (safe == nullptr)
                    return;
                if (auto* c = safe->content_)
                    c->fileActions_.restoreAutosave(autosave);
            },
            "Discard",
            [autosave] { ProjectSession::discardAutosave(autosave); });
    }
    const int count = session_.countPartialRecordings();
    if (count <= 0)
        return;
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

} // namespace northlight
