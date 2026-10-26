#include "MainWindow.h"

#include "CrashReports.h"
#include "ProjectSession.h"
#include "UserSettings.h"
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
#include "ui/ExportView.h"
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

// The first-run sheet's content (screens/setup.md 8.3): driver, device, buffer size
// and a live input meter so the user sees signal before the project opens.
class FirstRunContent final : public juce::Component, private juce::Timer
{
public:
    FirstRunContent(const ui::Theme& theme, audio::AudioDeviceLayer& audio)
        : theme_(theme), audio_(audio), meter_(theme, 1, false)
    {
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
        driver_.onChange = [this]
        {
            fillDevices();
            repaint(); // the driver's hint
        };
        fillDevices();
        // The sizes the device offers, its own among them (Windows Audio often runs
        // at 480), each with what it costs in latency.
        const double rate = status.sampleRate > 0.0 ? status.sampleRate : 48000.0;
        for (const int size : audio_.bufferSizeChoices())
            buffer_.addItem(juce::String(size) + " samples  " +
                                juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + "  " +
                                juce::String(1000.0 * size / rate, 1) + " ms",
                            size);
        buffer_.setSelectedId(status.bufferSize > 0 ? status.bufferSize : 128, juce::dontSendNotification);
        startTimerHz(20);
    }
    ~FirstRunContent() override { stopTimer(); }

    // Opens the chosen device; the error, if any.
    std::optional<std::string> apply()
    {
        audio::AudioSettings settings = audio_.settings();
        const int type = driver_.getSelectedId() - 1;
        const int device = device_.getSelectedId() - 1;
        if (type >= 0 && static_cast<size_t>(type) < types_.size() && device >= 0 &&
            static_cast<size_t>(device) < names_.outputs.size())
            settings = audio::AudioDeviceLayer::withDevice(settings,
                                                           types_[static_cast<size_t>(type)],
                                                           names_,
                                                           names_.outputs[static_cast<size_t>(device)]);
        settings.bufferSize = buffer_.getSelectedId();
        return audio_.apply(settings);
    }

    static constexpr int rowHeight = 56;
    static int preferredHeight(const ui::Theme&) { return 4 * rowHeight; }

    // A card of four rows (screens/setup.md 8.3): a label with a one-line hint on the
    // left, the control on the right, hairlines between the rows.
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto card = getLocalBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(card, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(card.reduced(0.5f), theme_.radius.md, 1.0f);
        const bool asio = driver_.getText() == "ASIO";
        const std::array<std::pair<const char*, juce::String>, 4> rows{{
            {"Driver",
             asio ? juce::String("The lowest latency, for recording and monitoring")
                  : juce::String("ASIO, if your interface has it, is lowest latency")},
            {"Device", "Where Northlight plays and records"},
            {"Buffer size", "Smaller is more responsive; larger avoids clicks"},
            {"Input", "Play or sing: the meter should move"},
        }};
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto row = rowBounds(static_cast<int>(i));
            if (i > 0)
            {
                g.setColour(c.lineSubtle);
                g.fillRect(
                    row.getX() + theme_.spacing.lg, row.getY(), row.getWidth() - 2 * theme_.spacing.lg, 1);
            }
            auto text = row.reduced(theme_.spacing.lg, 0).withTrimmedRight(controlWidth + theme_.spacing.lg);
            const int labelH = juce::roundToInt(theme_.fonts.size.label * theme_.fonts.lineHeight);
            const int hintH = juce::roundToInt(theme_.fonts.size.caption * theme_.fonts.lineHeight);
            auto block = text.withSizeKeepingCentre(text.getWidth(), labelH + theme_.spacing.xxs + hintH);
            g.setFont(theme_.label());
            g.setColour(c.textPrimary);
            g.drawText(rows[i].first, block.removeFromTop(labelH), juce::Justification::centredLeft);
            block.removeFromTop(theme_.spacing.xxs);
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary);
            g.drawText(rows[i].second, block, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        const auto place = [&](juce::Component& control, int row, int height)
        {
            const auto r = rowBounds(row).reduced(theme_.spacing.lg, 0);
            control.setBounds(
                r.withLeft(r.getRight() - controlWidth).withSizeKeepingCentre(controlWidth, height));
        };
        place(driver_, 0, theme_.sizes.control);
        place(device_, 1, theme_.sizes.control);
        place(buffer_, 2, theme_.sizes.control);
        place(meter_, 3, theme_.sizes.meterWidth * 2 + theme_.spacing.xs);
    }

private:
    static constexpr int controlWidth = 260;
    juce::Rectangle<int> rowBounds(int row) const { return {0, row * rowHeight, getWidth(), rowHeight}; }

    void fillDevices()
    {
        const int type = driver_.getSelectedId() - 1;
        device_.clear(juce::dontSendNotification);
        if (type < 0 || static_cast<size_t>(type) >= types_.size())
            return;
        names_ = audio_.deviceNames(types_[static_cast<size_t>(type)].name);
        for (size_t i = 0; i < names_.outputs.size(); ++i)
            device_.addItem(names_.outputs[i], static_cast<int>(i) + 1);
        if (const int current =
                audio::AudioDeviceLayer::indexOf(names_.outputs, audio_.status().outputDevice);
            current >= 0)
            device_.setSelectedId(current + 1, juce::dontSendNotification);
        if (device_.getSelectedId() == 0 && names_.defaultOutput >= 0)
            device_.setSelectedId(names_.defaultOutput + 1, juce::dontSendNotification);
    }
    void timerCallback() override
    {
        const float peak = peaks_.take(audio_.callback().inputPeaks());
        meter_.push(&peak, 1, 50.0);
    }

    const ui::Theme& theme_;
    audio::AudioDeviceLayer& audio_;
    juce::ComboBox driver_, device_, buffer_;
    ui::Meter meter_;
    rt::PeakHistory::Reader peaks_; // the sheet's own place in the input peaks
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
            dropoutBaseline_ = std::max(0, audio_.status().driverXruns);
            overrunBaseline_ = static_cast<std::int64_t>(audio_.callback().stats().summary().overruns);
            diskBaseline_ = session_.diskStarvations();
            dropoutsToasted_ = 0;
            diskGapsToasted_ = 0;
            refreshStatus();
        };
        audio_.addChangeListener(this);
        session_.addChangeListener(this);
        applyPreferences(true);
        refresh();
        startTimerHz(30);
    }

    // The preferences with an immediate effect (screens/setup.md 8.2); `atLaunch` also
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
        session_.setMetronomeLevel(ui::db::toGain(levelDb));
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

    // --benchmark-panels: with the Editor in the dock, opens and closes the
    // inspector in the side panel `cycles` times and prints the intervals between
    // the frames of the slides that reached the screen (16.7 ms is 60 fps), then
    // quits.
    void runPanelBenchmark(int cycles)
    {
        if (!session_.project().tracks.empty())
            select(session_.project().tracks.front().id);
        panels_.show(ui::PanelId::editor);
        panels_.hide(ui::PanelId::browser);
        panels_.hide(ui::PanelId::inspector);
        const double start = juce::Time::getMillisecondCounterHiRes() + 1500.0; // let it settle first
        auto toggles = std::make_shared<int>(0);
        dragBenchmark_ = std::make_unique<juce::TimedCallback>(
            [this, start, cycles, toggles]
            {
                if (juce::Time::getMillisecondCounterHiRes() < start)
                    return;
                if (*toggles == 0)
                {
                    slidePaints_.clear();
                    sideSplitter_.paintTimes = &slidePaints_;
                }
                if (*toggles < 2 * cycles)
                {
                    panels_.toggle(ui::PanelId::inspector);
                    ++*toggles;
                    return;
                }
                dragBenchmark_->stopTimer();
                sideSplitter_.paintTimes = nullptr;
                // Intervals within a slide (a gap of over 150 ms is the pause
                // between one slide and the next).
                std::vector<double> intervals;
                for (size_t k = 1; k < slidePaints_.size(); ++k)
                    if (const double d = slidePaints_[k] - slidePaints_[k - 1]; d < 150.0)
                        intervals.push_back(d);
                std::sort(intervals.begin(), intervals.end());
                const auto at = [&](double q)
                {
                    return intervals.empty() ? 0.0
                                             : intervals[std::min(intervals.size() - 1,
                                                                  static_cast<size_t>(intervals.size() * q))];
                };
                juce::String report;
                report << "benchmark-panels: " << cycles << " open/close cycles, " << intervals.size()
                       << " frames shown, " << getWidth() << "x" << getHeight() << "; ms between frames: p50 "
                       << juce::String(at(0.5), 1) << " p95 " << juce::String(at(0.95), 1) << " max "
                       << juce::String(intervals.empty() ? 0.0 : intervals.back(), 1) << "\n";
                std::printf("%s", report.toRawUTF8());
                std::fflush(stdout);
                juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
            });
        dragBenchmark_->startTimer(450);
    }

    // --benchmark-strip-drag: a knob drag on the first track's strip EQ (a
    // parameter command every 8 ms, like a fast mouse) while the strip shows;
    // prints the intervals between the strip's display ticks, then quits.
    void runStripDragBenchmark(int seconds)
    {
        // The first track whose strip has an EQ; with none, say so and quit (a
        // benchmark that never quits hangs the script that ran it).
        core::Id trackId = core::invalidId, eq = core::invalidId;
        for (const auto& track : session_.project().tracks)
        {
            for (const auto& m : track.strip)
                if (m.type == plugins::strip::types::eq)
                    eq = m.id;
            if (eq != core::invalidId)
            {
                trackId = track.id;
                break;
            }
        }
        if (eq == core::invalidId)
        {
            std::printf("benchmark-strip-drag: no track has a strip EQ\n");
            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
            return;
        }
        select(trackId);
        panels_.show(ui::PanelId::editor);
        const double start = juce::Time::getMillisecondCounterHiRes() + 1000.0; // let it settle first
        const double end = start + seconds * 1000.0;
        dragBenchmark_ = std::make_unique<juce::TimedCallback>(
            [this, trackId, eq, start, end]
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (now < start)
                    return;
                if (!dragging_)
                {
                    dragging_ = true;
                    ui::strip::ChannelStripView::startMeasuring();
                    session_.beginGesture("Band 2 gain");
                }
                const int gain =
                    plugins::strip::eq::paramIndex(plugins::strip::eq::band2, plugins::strip::eq::gain);
                session_.execute(std::make_unique<core::SetInsertParameter>(
                    trackId, eq, gain, static_cast<float>(12.0 * std::sin(now * 0.004))));
                if (now < end)
                    return;
                session_.endGesture();
                dragBenchmark_->stopTimer();
                auto intervals = ui::strip::ChannelStripView::takeTickIntervals();
                std::sort(intervals.begin(), intervals.end());
                const auto at = [&](double q)
                {
                    return intervals.empty() ? 0.0
                                             : intervals[std::min(intervals.size() - 1,
                                                                  static_cast<size_t>(intervals.size() * q))];
                };
                juce::String report;
                report << "benchmark-strip-drag: " << intervals.size() << " strip ticks in "
                       << juce::String((end - start) / 1000.0, 1) << " s ("
                       << juce::String(intervals.size() * 1000.0 / (end - start), 1)
                       << " per second); interval ms p50 " << juce::String(at(0.5), 1) << " p95 "
                       << juce::String(at(0.95), 1) << " max "
                       << juce::String(intervals.empty() ? 0.0 : intervals.back(), 1) << "\n";
                std::printf("%s", report.toRawUTF8());
                std::fflush(stdout);
                juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
            });
        dragBenchmark_->startTimer(8);
    }

    // --benchmark-hover: moves the pointer from knob to knob over the open editor
    // windows, a knob a frame, for `seconds`, and times every paint of those
    // windows meanwhile (the body and its children, by the window's own renderer);
    // prints the percentiles, then quits. The hover goes through the knob's own
    // handlers, so nothing reaches the desktop.
    void runHoverBenchmark(int seconds)
    {
        const double start = juce::Time::getMillisecondCounterHiRes() + 1500.0; // let it settle first
        const double end = start + seconds * 1000.0;
        auto state = std::make_shared<HoverRun>();
        dragBenchmark_ = std::make_unique<juce::TimedCallback>(
            [this, start, end, state]
            {
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (now < start)
                    return;
                if (state->knobs.empty())
                {
                    state->knobs = hoverTargets();
                    ui::InsertEditor::paintTimes = &state->ms;
                    if (state->knobs.empty())
                    {
                        std::printf("benchmark-hover: no knobs to hover\n");
                        std::fflush(stdout);
                        juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
                        dragBenchmark_->stopTimer();
                        return;
                    }
                }
                if (now < end)
                {
                    auto& knobs = state->knobs;
                    const auto event = [](juce::Component& c)
                    {
                        const auto at = c.getLocalBounds().getCentre().toFloat();
                        const auto time = juce::Time::getCurrentTime();
                        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                                at,
                                                juce::ModifierKeys(),
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                0.0f,
                                                &c,
                                                &c,
                                                time,
                                                at,
                                                time,
                                                0,
                                                false);
                    };
                    if (auto* last = state->previous.getComponent())
                        last->mouseExit(event(*last));
                    auto* knob = knobs[state->next++ % knobs.size()].getComponent();
                    if (knob == nullptr)
                        return;
                    knob->mouseEnter(event(*knob));
                    state->previous = knob;
                    return;
                }
                dragBenchmark_->stopTimer();
                ui::InsertEditor::paintTimes = nullptr;
                auto& ms = state->ms;
                std::sort(ms.begin(), ms.end());
                const auto at = [&](double q)
                {
                    return ms.empty() ? 0.0 : ms[std::min(ms.size() - 1, static_cast<size_t>(ms.size() * q))];
                };
                juce::String report;
                report << "benchmark-hover: " << ms.size() << " hovers over " << state->knobs.size()
                       << " knobs; editor paint ms p50 " << juce::String(at(0.5), 2) << " p95 "
                       << juce::String(at(0.95), 2) << " max "
                       << juce::String(ms.empty() ? 0.0 : ms.back(), 2) << "\n";
                std::printf("%s", report.toRawUTF8());
                std::fflush(stdout);
                juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
            });
        dragBenchmark_->startTimer(16);
    }

    struct HoverRun
    {
        std::vector<juce::Component::SafePointer<ui::Knob>> knobs;
        juce::Component::SafePointer<ui::Knob> previous;
        size_t next = 0;
        std::vector<double> ms;
    };

    // The knobs on the open editor windows.
    std::vector<juce::Component::SafePointer<ui::Knob>> hoverTargets()
    {
        std::vector<juce::Component::SafePointer<ui::Knob>> out;
        std::function<void(juce::Component&)> collect = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (!child->isVisible())
                    continue;
                if (auto* k = dynamic_cast<ui::Knob*>(child))
                    out.emplace_back(k);
                collect(*child);
            }
        };
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* top = desktop.getComponent(i);
                top != nullptr && top != getTopLevelComponent() && top->isVisible())
                collect(*top);
        return out;
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
        using M = juce::ModifierKeys;
        const int mods = key.getModifiers().withoutMouseButtons().getRawFlags();
        const auto is = [&](int code, int with = 0) { return key.getKeyCode() == code && mods == with; };
        const auto chr = [&](char c, int with = 0)
        { return key.getTextCharacter() == static_cast<juce::juce_wchar>(c) && mods == with; };
        if (key.getKeyCode() == juce::KeyPress::tabKey)
            ui::NorthlightLookAndFeel::setKeyboardFocusVisible(true);
        // F2 renames the selected track while the arrange view has the focus
        // (10.4); elsewhere it is the mixer's panel key.
        if (key.getKeyCode() == juce::KeyPress::F2Key && mods == 0 && tracks_.hasKeyboardFocus(true) &&
            selection_.channel() != core::invalidId)
        {
            tracks_.renameSelectedTrack();
            return true;
        }
        if (is('F', M::ctrlModifier))
        {
            panels_.show(ui::PanelId::browser);
            browserPanel_.focusSearch();
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
            showExportSheet();
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
            addMidiTrack({});
            return true;
        }
        if (is('B', M::ctrlModifier))
        {
            addBus();
            return true;
        }
        return tracks_.handleEditKey(key);
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
        header_.onPlayStop = [this] { session_.togglePlay(); };
        header_.onStop = [this] { session_.stop(); };
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
        { importFiles(files, at); };
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
        tracks_.onImportRequested = [this] { importAudio(); };
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
                                        key(juce::KeyPress::F3Key),
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
                                        key(juce::KeyPress::F4Key)},
                              browserPanel_);
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
        panels_.registerPanel(PanelInfo{PanelId::audioEditor,
                                        "Audio Editor",
                                        Icon::fileAudio,
                                        HostKind::dock,
                                        480,
                                        260,
                                        true,
                                        false,
                                        key(juce::KeyPress::F9Key)},
                              audioEditor_);
        panels_.registerPanel(PanelInfo{PanelId::instruments,
                                        "Instruments",
                                        Icon::instrument,
                                        HostKind::dock,
                                        480,
                                        200,
                                        true,
                                        false,
                                        key(juce::KeyPress::F8Key)},
                              instruments_);
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
        if (const auto& crash = session_.lastPluginMessage(); crash != lastPluginMessage_)
        {
            lastPluginMessage_ = crash;
            if (!crash.empty())
                toast(ui::ToastStack::Kind::error, juce::String::fromUTF8(crash.c_str()));
        }
        const auto bounce = session_.lastBounceMessage();
        if (bounce != lastBounceMessage_)
        {
            lastBounceMessage_ = bounce;
            showBounceOutcome(bounce);
        }
        if (const auto& freeze = session_.lastFreezeMessage(); freeze != lastFreezeMessage_)
        {
            lastFreezeMessage_ = freeze;
            if (!freeze.empty())
                toast(freeze.rfind("Froze", 0) == 0 ? ui::ToastStack::Kind::success
                                                    : ui::ToastStack::Kind::warning,
                      juce::String::fromUTF8(freeze.c_str()));
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
                       projectName() + (session_.isModified() ? "*" : "") + " - Northlight");
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
        // Disk gaps count as dropouts too: they are just as audible.
        const auto diskGaps = static_cast<int>(session_.diskStarvations() - diskBaseline_);
        state.dropouts = std::max(0, std::max(0, s.driverXruns) - dropoutBaseline_) +
                         static_cast<int>(std::max<std::int64_t>(
                             0, static_cast<std::int64_t>(stats.overruns) - overrunBaseline_)) +
                         diskGaps;
        // Dropouts over the threshold: a toast with the fix, at most once a minute.
        // The fix depends on the cause; most of the new ones coming from the disk
        // means a bigger buffer would not help.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (state.dropouts - dropoutsToasted_ >= 10 && now - lastDropoutToastMs_ > 60000.0)
        {
            const bool mostlyDisk = 2 * (diskGaps - diskGapsToasted_) > state.dropouts - dropoutsToasted_;
            dropoutsToasted_ = state.dropouts;
            diskGapsToasted_ = diskGaps;
            lastDropoutToastMs_ = now;
            if (mostlyDisk)
                toast(ui::ToastStack::Kind::warning,
                      "Audio dropouts. The disk is not keeping up with the streamed clips. Close programs "
                      "that are using the disk, or move the project to a faster drive.");
            else
                toast(ui::ToastStack::Kind::warning,
                      "Audio dropouts. The buffer is too small for this project on this device. Raise the "
                      "buffer size in Audio Setup.",
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
                            "Save changes to " + projectName() + "?",
                            "Unsaved changes will be lost if you close without saving.",
                            {{"Cancel", false, false}, {"Don't save", false, true}, {"Save", true, false}},
                            [this](int result)
                            {
                                sheet_ = nullptr;
                                if (result == 1)
                                    juce::JUCEApplication::getInstance()->quit();
                                else if (result == 2)
                                    saveProject(false, AfterSave::quit);
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
        files_.onImport = [this](const juce::File& file) { importFiles({file}, session_.position()); };
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

    void newProject()
    {
        if (session_.isRecording())
            return;
        session_.stop();
        mixer_.closeEditors();
        session_.newProject();
        refresh();
    }

    void newFromTemplate(const juce::File& file)
    {
        if (session_.isRecording() || session_.isLoading())
            return;
        session_.stop();
        mixer_.closeEditors();
        if (!session_.beginTemplate(file))
            return;
        watchLoad("New project from " + file.getFileNameWithoutExtension(),
                  [this, file](const std::string& error)
                  {
                      if (!error.empty())
                          toast(ui::ToastStack::Kind::error,
                                "Cannot use the template: " + juce::String::fromUTF8(error.c_str()));
                      else
                          status_.showMessage("New project from " + file.getFileNameWithoutExtension());
                      refresh();
                      grabKeyboardFocus();
                  });
    }

    // Restores a crashed session's autosave (the toast's Restore), with progress.
    void restoreAutosave(const ProjectSession::Autosave& autosave)
    {
        if (session_.isRecording() || session_.isLoading())
            return;
        session_.stop();
        mixer_.closeEditors();
        if (!session_.beginRestore(autosave))
            return;
        watchLoad("Restoring " + juce::String::fromUTF8(autosave.name.c_str()),
                  [this](const std::string& error)
                  {
                      if (!error.empty())
                          toast(ui::ToastStack::Kind::error,
                                "Cannot restore: " + juce::String::fromUTF8(error.c_str()));
                      refresh();
                      grabKeyboardFocus();
                  });
    }

    // ----- loading a project in steps (screens/shell.md, "Opening a project") ---------
    // Shows the loading sheet over the window while the session loads what the
    // caller began, steps the load from a timer and calls `onFinished` with the
    // outcome (an error, or empty). The sheet appears only once the load has run
    // for motion.progressDelayMs, but holds the window's input from the start.
    // `holdAt` (--show-loading) stops stepping once the load reaches that stage
    // and shows the sheet at once, so a capture finds it.
    void watchLoad(const juce::String& title,
                   std::function<void(const std::string&)> onFinished,
                   std::optional<LoadProgress::Stage> holdAt = std::nullopt)
    {
        loadFinished_ = std::move(onFinished);
        loadHold_ = holdAt;
        loadHeld_ = false;
        auto row = std::make_unique<ui::ProgressRow>(theme_);
        loadRow_ = row.get();
        ui::SheetOptions options;
        options.blocking = true;
        options.appearAfterMs = holdAt.has_value() ? 0 : theme_.motion.progressDelayMs;
        const auto name = title;
        loadSheet_ = ui::Sheet::show(
            *this,
            theme_,
            title,
            {},
            {{"Cancel", false, false, true}},
            [this, name](int)
            {
                // Cancel (or Esc) while the current project is still the old one.
                if (!session_.canCancelLoad())
                    return;
                session_.cancelLoad();
                loadDriver_.stopTimer();
                loadFinished_ = nullptr;
                status_.showMessage("Stopped: " + name);
                grabKeyboardFocus();
            },
            std::move(row),
            ui::ProgressRow::preferredHeight(theme_),
            options);
        loadDriver_.tick = [this] { stepLoading(); };
        stepLoading();
        if (session_.isLoading() && !loadHeld_)
            loadDriver_.startTimer(loadStepMs);
    }

    void stepLoading()
    {
        // Slow steps ahead (third-party plugins hold the message thread, so the
        // sheet's delay and entrance could not run between them): bring the sheet
        // in now and take no step until it is fully on screen.
        if (loadSheet_ != nullptr && !loadSheet_->hasAppeared() && session_.loadHasSlowStepsAhead())
        {
            loadSheet_->appearNow();
            showLoadProgress();
            return;
        }
        const bool wasCancellable = session_.canCancelLoad();
        const auto outcome = session_.stepLoad();
        if (outcome.has_value())
        {
            finishLoading(*outcome);
            return;
        }
        if (wasCancellable && !session_.canCancelLoad())
        {
            // The new project is the session's now: the window shows it behind the
            // sheet, and the load can no longer be cancelled.
            refresh();
            if (loadSheet_ != nullptr)
                loadSheet_->setButtonEnabled(0, false);
        }
        const auto progress = showLoadProgress();
        if (loadHold_.has_value() && progress.stage >= *loadHold_)
        {
            loadHeld_ = true;
            loadDriver_.stopTimer();
        }
        // Paint now: the next step may make a plugin that holds the thread a while.
        if (auto* peer = getPeer())
            peer->performAnyPendingRepaintsNow();
    }

    LoadProgress showLoadProgress()
    {
        const auto progress = session_.loadProgress();
        if (loadRow_ != nullptr)
            loadRow_->setProgress(progress.fraction, juce::String::fromUTF8(describe(progress).c_str()));
        return progress;
    }

    void finishLoading(const std::string& error)
    {
        loadDriver_.stopTimer();
        if (loadSheet_ != nullptr)
            loadSheet_->dismiss();
        loadSheet_ = nullptr;
        auto finished = std::move(loadFinished_);
        loadFinished_ = nullptr;
        if (finished)
            finished(error);
    }

    void saveTemplate()
    {
        auto field = std::make_unique<juce::TextEditor>();
        field->setText(juce::String::fromUTF8(session_.project().name.c_str()) == "Untitled"
                           ? juce::String("My template")
                           : juce::String::fromUTF8(session_.project().name.c_str()));
        field->selectAll();
        auto* raw = field.get();
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Save as template",
            "The tracks, buses, routing, inserts and channel strips, without clips or markers. "
            "New from template in the Project menu starts a project from it.",
            {{"Cancel", false, false}, {"Save", true, false}},
            [this, raw](int result)
            {
                const auto name = raw->getText();
                sheet_ = nullptr;
                if (result != 1)
                    return;
                const auto error = session_.saveAsTemplate(name);
                if (!error.empty())
                    toast(ui::ToastStack::Kind::error,
                          "Cannot save the template: " + juce::String::fromUTF8(error.c_str()));
                else
                    toast(ui::ToastStack::Kind::success, "Saved the template \"" + name.trim() + "\"");
            },
            std::move(field),
            theme_.sizes.control);
    }

    void gatherMedia()
    {
        int copied = 0;
        const auto error = session_.consolidateMedia(copied);
        if (!error.empty())
            toast(ui::ToastStack::Kind::error,
                  "Cannot gather media: " + juce::String::fromUTF8(error.c_str()));
        else if (copied == 0)
            toast(ui::ToastStack::Kind::info, "All media is already in the project folder.");
        else
            toast(ui::ToastStack::Kind::success,
                  "Copied " + juce::String(copied) + (copied == 1 ? " file" : " files") +
                      " into the project folder. Save to keep the new paths.");
    }

    void removeUnusedMedia()
    {
        const auto report = session_.mediaReport();
        if (report.unused.empty())
        {
            toast(ui::ToastStack::Kind::info, "No unused media in the project folder.");
            return;
        }
        juce::int64 bytes = 0;
        for (const auto& f : report.unused)
            bytes += f.getSize();
        sheet_ =
            ui::Sheet::show(*this,
                            theme_,
                            "Remove unused media",
                            juce::String(static_cast<int>(report.unused.size())) +
                                (report.unused.size() == 1 ? " file" : " files") + " (" +
                                juce::File::descriptionOfSizeInBytes(bytes) +
                                ") in the project folder are not used by any clip. They go to the "
                                "Recycle Bin, so an undo that brings a clip back can still find them there.",
                            {{"Cancel", false, false}, {"Move to Recycle Bin", true, true}},
                            [this](int result)
                            {
                                sheet_ = nullptr;
                                if (result != 1)
                                    return;
                                const int moved = session_.removeUnusedMedia();
                                toast(ui::ToastStack::Kind::success,
                                      "Moved " + juce::String(moved) + (moved == 1 ? " file" : " files") +
                                          " to the Recycle Bin.");
                            });
    }

    void openProject()
    {
        if (session_.isRecording())
            return;
        chooser_ =
            std::make_unique<juce::FileChooser>("Open project", projectFolder(), "*.northlight;*.aural");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& fc)
                              {
                                  if (const auto file = fc.getResult(); file != juce::File())
                                      openFile(file);
                              });
    }

    void openFile(const juce::File& file, std::optional<LoadProgress::Stage> holdAt = std::nullopt)
    {
        if (session_.isLoading())
            return;
        session_.stop();
        mixer_.closeEditors();
        if (!session_.beginOpen(file))
            return;
        watchLoad(
            "Opening " + file.getFileNameWithoutExtension(),
            [this, file](const std::string& error)
            {
                if (!error.empty())
                    toast(ui::ToastStack::Kind::error,
                          "Cannot open " + file.getFileName() + ": " + juce::String::fromUTF8(error.c_str()));
                else
                    rememberRecent(file);
                refresh();
                grabKeyboardFocus();
                owner_.offerRecovery();
            },
            holdAt);
    }

    // What happens once a save succeeds. Passed along rather than kept in a member,
    // so a cancelled or failed save leaves nothing behind for the next one.
    enum class AfterSave
    {
        stay,
        quit,
    };

    void saveProject(bool saveAs, AfterSave after = AfterSave::stay)
    {
        if (!saveAs && session_.projectFile().getFullPathName().isNotEmpty())
        {
            saveTo(session_.projectFile(), after);
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save project", projectFolder().getChildFile("Untitled.northlight"), "*.northlight");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, after](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension("northlight"))
                                      file = file.withFileExtension("northlight");
                                  saveTo(file, after);
                                  grabKeyboardFocus();
                              });
    }

    void saveTo(const juce::File& file, AfterSave after = AfterSave::stay)
    {
        const auto error = session_.save(file);
        if (!error.empty())
        {
            toast(ui::ToastStack::Kind::error, "Cannot save: " + juce::String::fromUTF8(error.c_str()));
        }
        else
        {
            status_.showMessage("Saved " + file.getFileName());
            rememberRecent(file);
            if (after == AfterSave::quit)
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
            // A MIDI file plays the selected instrument (or the one the selected
            // MIDI track plays), else a new one.
            const bool midi = file.hasFileExtension("mid;midi");
            const auto error = midi ? session_.importMidiFile(file, at, selection_.channel())
                                    : session_.importAudioFile(file, at);
            if (!error.empty())
                toast(ui::ToastStack::Kind::error,
                      file.getFileName() + ": " + juce::String::fromUTF8(error.c_str()));
        }
        refresh();
        grabKeyboardFocus();
    }

    void importMidi()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Import MIDI file",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.mid;*.midi");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& fc)
                              {
                                  std::vector<juce::File> files;
                                  for (const auto& f : fc.getResults())
                                      files.push_back(f);
                                  importFiles(files, session_.position());
                              });
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

    void exportMidi()
    {
        const auto clips = midiClipsToExport();
        if (clips.empty())
        {
            toast(ui::ToastStack::Kind::error, "Export MIDI: there are no MIDI clips");
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export MIDI", projectFolder().getChildFile(projectName() + ".mid"), "*.mid");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, clips](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension("mid;midi"))
                                      file = file.withFileExtension("mid");
                                  const auto error = session_.exportMidiFile(file, clips);
                                  if (!error.empty())
                                      toast(ui::ToastStack::Kind::error,
                                            "Export MIDI: " + juce::String::fromUTF8(error.c_str()));
                                  else
                                      toast(ui::ToastStack::Kind::success, "Exported " + file.getFileName());
                              });
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

    // The last export's settings, from the user settings ("export.*").
    engine::ExportSettings exportSettings() const
    {
        std::map<std::string, std::string> values;
        for (const auto& [key, fallback] : engine::ExportSettings{}.toKeyValues())
            if (auto value = settings_.value(key); !value.empty())
                values[key] = value;
        return engine::ExportSettings::fromKeyValues(values);
    }

    void showExportSheet()
    {
        if (sheet_ != nullptr || session_.isBouncing())
            return;
        ui::ExportView::Context context;
        context.hasLoop = session_.regionEnd() > session_.regionStart();
        std::vector<core::Marker> regions;
        for (const auto& marker : session_.project().markers)
            if (marker.isRegion())
            {
                regions.push_back(marker);
                context.regions.push_back(juce::String::fromUTF8(marker.name.c_str()));
            }
        context.selectedTracks = static_cast<int>(selectedTrackIds().size());
        context.projectRate = session_.project().sampleRate;
        context.oggQualities = juce::OggVorbisAudioFormat().getQualityOptions();
        auto view = std::make_unique<ui::ExportView>(theme_, exportSettings(), std::move(context));
        auto* raw = view.get();
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Export",
            "Renders offline, faster than real time, with the plugins as they are now.",
            {{"Cancel", false, false}, {"Export...", true, false}},
            [this, raw, regions](int result)
            {
                sheet_ = nullptr;
                if (result != 1)
                    return;
                const auto chosen = raw->settings();
                for (const auto& [key, value] : chosen.toKeyValues())
                    settings_.setValue(key, value);
                std::optional<core::Marker> region;
                if (chosen.range == engine::ExportRange::region &&
                    static_cast<size_t>(raw->region()) < regions.size())
                    region = regions[static_cast<size_t>(raw->region())];
                startExport(chosen, region);
            },
            std::move(view),
            ui::ExportView::preferredHeight(theme_));
    }

    void startExport(const engine::ExportSettings& chosen, std::optional<core::Marker> region)
    {
        juce::Component::SafePointer<Content> safe(this);
        const auto report = [safe](const std::string& error)
        {
            if (safe == nullptr)
                return;
            if (!error.empty())
                safe->toast(ui::ToastStack::Kind::error, "Export: " + juce::String::fromUTF8(error.c_str()));
            safe->refresh();
        };
        BounceJob::Options options;
        options.format = chosen;
        options.stems = chosen.source != engine::ExportSource::mix;
        if (chosen.source == engine::ExportSource::selectedStems)
        {
            options.stemTracks = selectedTrackIds();
            if (options.stemTracks.empty())
            {
                report("no tracks are selected");
                return;
            }
        }
        juce::String suffix;
        if (chosen.range == engine::ExportRange::loop && session_.regionEnd() > session_.regionStart())
        {
            options.start = session_.regionStart();
            options.end = session_.regionEnd();
            suffix = " loop";
        }
        else if (region.has_value())
        {
            options.start = region->position;
            options.end = region->end();
            suffix =
                " " + juce::File::createLegalFileName(juce::String::fromUTF8(region->name.c_str())).trim();
        }
        const auto documents = projectFolder();
        const auto base = projectName() + suffix;
        const auto extension = juce::String(engine::extensionFor(chosen.format));
        if (options.stems)
        {
            chooser_ =
                std::make_unique<juce::FileChooser>("Stems folder", documents.getChildFile(base + " Stems"));
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [this, report, options](const juce::FileChooser& fc) mutable
                                  {
                                      const auto folder = fc.getResult();
                                      if (folder == juce::File())
                                          return;
                                      options.output = folder;
                                      report(session_.startBounce(options));
                                  });
            return;
        }
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export mix", documents.getChildFile(base + "." + extension), "*." + extension);
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles |
                                  juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, report, options, extension](const juce::FileChooser& fc) mutable
                              {
                                  auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (!file.hasFileExtension(extension))
                                      file = file.withFileExtension(extension);
                                  options.output = file;
                                  report(session_.startBounce(options));
                              });
    }

    // While a bounce runs its progress is in the status bar; when it ends a toast
    // gives the outcome (the measured loudness and any gain), with the file to hand.
    void showBounceOutcome(const std::string& message)
    {
        if (message.empty())
            return;
        const auto text = juce::String::fromUTF8(message.c_str());
        if (session_.isBouncing())
        {
            status_.showMessage(text);
            return;
        }
        const auto& result = session_.lastBounceResult();
        if (!result.error.empty())
        {
            toast(ui::ToastStack::Kind::error, text);
            return;
        }
        if (result.cancelled || result.files.empty())
        {
            toast(ui::ToastStack::Kind::info, text);
            return;
        }
        const auto file = result.files.front();
        toast(result.limitedByCeiling ? ui::ToastStack::Kind::warning : ui::ToastStack::Kind::success,
              text,
              "Show in folder",
              [file] { file.revealToUser(); });
    }

    // ----- menus ---------------------------------------------------------------------------
    void appMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "About Northlight");
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
        ui::SplashScreen::showAbout(
            theme_, juce::String::fromUTF8(version::display().c_str()), getTopLevelComponent());
    }

    void projectMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "New");
        juce::PopupMenu fromTemplate;
        const auto templateFiles = session_.templates();
        for (size_t i = 0; i < templateFiles.size(); ++i)
            fromTemplate.addItem(200 + static_cast<int>(i), templateFiles[i].getFileNameWithoutExtension());
        menu.addSubMenu("New from template", fromTemplate, !templateFiles.empty());
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
        menu.addItem(9, "Save as template...");
        menu.addSeparator();
        juce::PopupMenu media;
        media.addItem(10, "Gather media into the project folder", session_.projectFile() != juce::File());
        media.addItem(11, "Remove unused media...", session_.projectFile() != juce::File());
        menu.addSubMenu("Media", media);
        menu.addSeparator();
        menu.addItem(5, "Import audio...");
        menu.addItem(12, "Import MIDI file...");
        menu.addItem(6, "Add audio track");
        menu.addItem(7, "Add MIDI track");
        juce::PopupMenu fxTracks;
        const auto fxChoices = plugins::fxTrackEffects();
        for (size_t i = 0; i < fxChoices.size(); ++i)
            fxTracks.addItem(300 + static_cast<int>(i), juce::String(fxChoices[i].name));
        menu.addSubMenu("Add FX track", fxTracks);
        menu.addItem(8, "Add bus");
        menu.addSeparator();
        menu.addItem(21, "Export...");
        menu.addItem(24, "Export MIDI clips...");
        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&header_).withMinimumWidth(220),
            [safe, files, templateFiles](int choice)
            {
                if (safe == nullptr)
                    return;
                if (choice >= 300 && choice < 320)
                {
                    const auto fx = plugins::fxTrackEffects();
                    const auto i = static_cast<size_t>(choice - 300);
                    if (i < fx.size())
                        safe->session_.executeGroup(
                            "Add FX track",
                            plugins::addFxTrackCommands(safe->session_.project(), fx[i].type, fx[i].name));
                    return;
                }
                if (choice >= 200 && static_cast<size_t>(choice - 200) < templateFiles.size())
                {
                    safe->newFromTemplate(templateFiles[static_cast<size_t>(choice - 200)]);
                    return;
                }
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
                        safe->addMidiTrack({});
                        break;
                    case 8:
                        safe->addBus();
                        break;
                    case 9:
                        safe->saveTemplate();
                        break;
                    case 10:
                        safe->gatherMedia();
                        break;
                    case 11:
                        safe->removeUnusedMedia();
                        break;
                    case 12:
                        safe->importMidi();
                        break;
                    case 24:
                        safe->exportMidi();
                        break;
                    case 21:
                        safe->showExportSheet();
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&header_.snapButton()),
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

    // ----- first run -------------------------------------------------------------------------
    void showFirstRun()
    {
        auto content = std::make_unique<FirstRunContent>(theme_, audio_);
        auto* raw = content.get();
        ui::SheetOptions options;
        options.hero = true;
        options.eyebrow = "Step 1 of 2";
        options.footnote = "You can change this any time in Audio Setup.";
        options.width = 600;
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Choose your audio device",
            "Pick the interface you record and listen through. Northlight checks it before you start.",
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
            FirstRunContent::preferredHeight(theme_),
            options);
    }

    void offerScan()
    {
        ui::SheetOptions options;
        options.hero = true;
        options.eyebrow = "Step 2 of 2";
        options.footnote = "Scan again any time from the Browser.";
        options.width = 600;
        sheet_ = ui::Sheet::show(
            *this,
            theme_,
            "Find your plugins",
            "Northlight looks in the standard VST3 and CLAP folders. The scan runs in the background, so you "
            "can start working straight away.",
            {{"Later", false, false}, {"Scan now", true, false}},
            [this](int result)
            {
                sheet_ = nullptr;
                if (result == 1)
                    session_.scanPlugins(true);
            },
            nullptr,
            0,
            options);
    }

    MainWindow& owner_;
    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    const ui::Theme& theme_ = ui::Theme::dark();
    double lastValuesRefreshMs_ = 0.0;
    bool pendingParametersOnly_ = true; // the values refresh due covers parameters only
    std::unique_ptr<juce::TimedCallback> dragBenchmark_;
    bool dragging_ = false;
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
    // The loading sheet and its progress row (they delete themselves), and the
    // timer that steps the load (watchLoad).
    static constexpr int loadStepMs = 15;
    juce::Component::SafePointer<ui::Sheet> loadSheet_;
    juce::Component::SafePointer<ui::ProgressRow> loadRow_;
    std::function<void(const std::string&)> loadFinished_;
    std::optional<LoadProgress::Stage> loadHold_;
    bool loadHeld_ = false;
    struct LoadDriver final : juce::Timer
    {
        std::function<void()> tick;
        void timerCallback() override
        {
            if (tick)
                tick();
        }
    } loadDriver_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::string lastRecordingMessage_, lastBounceMessage_, lastPluginMessage_, lastFreezeMessage_;
    bool lastRecording_ = false;
    int ticks_ = 0;
    std::vector<bool> monitoring_; // which tracks monitor their input, for the headers
    int dropoutBaseline_ = 0;
    std::int64_t overrunBaseline_ = 0;
    double lastTickMs_ = 0.0;
    std::deque<double> frames_;
    bool lastDeviceOpen_ = false;
    std::string lastDeviceName_;
    int dropoutsToasted_ = 0;
    int diskGapsToasted_ = 0;
    std::uint64_t diskBaseline_ = 0;
    double lastDropoutToastMs_ = 0.0;
    double benchmarkEndMs_ = 0.0;
    bool slideCached_ = false;        // the views are drawn from cached images during a panel slide
    std::vector<double> slidePaints_; // --benchmark-panels
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
    setContentOwned(new Content(*this, audio_, session_, settings_), true);
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
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
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
    ui::bringToFront(*preferencesWindow_);
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

void MainWindow::showTempoTrack()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->tracks_.setTempoTrackShown(true);
}

void MainWindow::openInsertEditor(core::Id channelId, core::Id insertId)
{
    showMixer();
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->mixer_.openInsertEditor(channelId, insertId);
}

void MainWindow::showFilesBrowser(const juce::File& folder)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        content->browserPanel_.showFiles();
        if (folder.isDirectory())
            content->files_.showFolder(folder);
        content->panels_.show(ui::PanelId::browser);
    }
}

void MainWindow::showPluginBrowser()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        content->browserPanel_.showPlugins();
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

void MainWindow::setPianoRollLane(const juce::String& key)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->pianoRoll_.setLane(ui::PianoRoll::laneForKey(key));
}

void MainWindow::openAudioEditor(core::Id trackId, core::Id clipId, bool pitch)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
    {
        content->audioEditor_.setClip(&session_.project(), trackId, clipId);
        if (pitch)
            content->audioEditor_.setMode(ui::AudioEditor::Mode::pitch);
        content->panels_.show(ui::PanelId::audioEditor);
    }
}

void MainWindow::zoomAudioEditor(double samplesPerPixel, double secondsIntoClip)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->audioEditor_.zoomTo(samplesPerPixel, secondsIntoClip);
}

void MainWindow::selectClip(core::Id trackId, core::Id clipId)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->selection_.select(trackId, clipId);
}

void MainWindow::correctPitchInEditor()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->audioEditor_.correctToKey({});
}

void MainWindow::muteBreathsInEditor()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->audioEditor_.setBreaths(0.0f, true);
}

void MainWindow::runStripDragBenchmark(int seconds)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->runStripDragBenchmark(seconds);
}

void MainWindow::runHoverBenchmark(int seconds)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->runHoverBenchmark(seconds);
}

void MainWindow::showAutomation(core::Id trackId)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->tracks_.showAutomation(trackId, true);
}

void MainWindow::showPanel(const juce::String& key)
{
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->tracks_.pointerAt(content->tracks_.pointOnRow(row, bar), mods);
}

void MainWindow::showEditorGap(int n)
{
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->panels_.applyPreset(name);
}

void MainWindow::openWithProgress(const juce::File& file, const juce::String& holdAt)
{
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
    content->openFile(file, hold);
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

void MainWindow::runPanelBenchmark(int cycles)
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->runPanelBenchmark(std::max(1, cycles));
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

void MainWindow::showExport()
{
    if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        content->showExportSheet();
}

void MainWindow::offerRecovery()
{
    auto* content = dynamic_cast<Content*>(getContentComponent());
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
                if (auto* c = dynamic_cast<Content*>(safe->getContentComponent()))
                    c->restoreAutosave(autosave);
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
