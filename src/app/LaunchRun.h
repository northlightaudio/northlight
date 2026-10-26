#pragma once

#include "app/LaunchFlags.h"
#include "core/Model.h"

#include <functional>
#include <memory>

namespace northlight
{
class MainWindow;
class ProjectSession;
namespace audio
{
class AudioDeviceLayer;
}
namespace engine
{
class OutputTap;
}

// The developer launch flags for unattended checks (AGENTS.md: "Seeing the app
// without clicking it"): the table in LaunchRun.cpp, one entry per flag with its
// arguments, its help and what it does. `Northlight.exe --help` prints the help
// from the same table. A few flags (--capture, --offscreen, --no-splash,
// --first-run) are read at start-up, before the window exists; the table lists
// them too.
class LaunchRun
{
public:
    LaunchRun(ProjectSession& session, MainWindow& window, audio::AudioDeviceLayer& audio);
    ~LaunchRun();

    // Runs the flags on the command line, in order.
    void apply(const juce::String& commandLine);
    // Drops every call still scheduled: first thing in shutdown.
    void cancel() noexcept { schedule.cancel(); }
    // The --output-tap recorder, for shutdown to stop once the device has stopped.
    std::unique_ptr<engine::OutputTap> takeOutputTap();

    static const std::vector<LaunchFlag<LaunchRun>>& flags();
    static juce::String help() { return launchHelp(flags()); }

    // ----- what the flags act on and keep between them
    ProjectSession& session;
    MainWindow& window;
    audio::AudioDeviceLayer& audio;
    LaunchSchedule schedule;
    int stopAfterSeconds = 0;                     // --stop-after, for a --save after it
    float captureScale = 2.0f;                    // --capture-scale
    bool stopped = false;                         // a flag ended the run (--time-load, --help)
    core::Id lastInsertChannel = core::invalidId; // --insert-param's target: the insert the
    core::Id lastInsertId = core::invalidId;      // last --insert or --master-insert added
    std::unique_ptr<engine::OutputTap> outputTap;

    // Helpers some flags share.
    void whenVocalReady(std::function<void()> action, int tries);
    void showControllerLane(const juce::String& key);
    void applyStripFlag(const juce::String& text);
};

} // namespace northlight
