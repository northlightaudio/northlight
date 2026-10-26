#pragma once

#include "app/Notices.h"
#include "ui/components/Toast.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <string>

namespace northlight
{

// The dropout count the status bar shows, from the raw counters: driver xruns,
// callback overruns and disk gaps, each counted from a baseline. A device that
// has just started (opened, reopened, its rate or buffer changed) glitches
// while it settles, before anything plays: for `settleMs` after the device
// changes, the baselines follow the counters, so a fresh launch never shows
// "1 dropout" with nothing played. A counter that goes down (the callback's
// statistics reset when the device restarts) counts as a change too.
class DropoutCounter
{
public:
    struct Raw
    {
        std::string device; // what is open, its rate and buffer; empty when nothing is
        int driverXruns = 0;
        std::uint64_t overruns = 0;
        std::uint64_t diskGaps = 0;
    };
    static constexpr double settleMs = 2000.0;

    // The dropouts since the last clear (or since the device settled).
    int count(const Raw& raw, double nowMs);
    // Those of them that were disk gaps.
    int diskGaps(const Raw& raw) const;
    // Start counting again from now.
    void clear(const Raw& raw);

private:
    std::string device_;
    double settleUntilMs_ = 0.0;
    int xrunBaseline_ = 0;
    std::uint64_t overrunBaseline_ = 0, diskBaseline_ = 0;
};

class ProjectSession;
namespace audio
{
class AudioDeviceLayer;
}

// What the window tells the user about the session and the device, once each:
// the session's notices (app/Notices.h) as toasts or a status line, and the
// dropout count the status bar shows (driver xruns, callback overruns and disk
// gaps since the user last cleared it), with a toast naming the likely fix once
// they pile up (at most once a minute).
class SessionNotices
{
public:
    using Toast = std::function<void(ui::ToastStack::Kind kind,
                                     const juce::String& text,
                                     const juce::String& action,
                                     std::function<void()> onAction)>;
    struct Host
    {
        ProjectSession& session;
        audio::AudioDeviceLayer& audio;
        Toast toast;
        std::function<void(const juce::String&)> status; // the status bar's message line
        std::function<void()> showAudioSetup;
    };
    explicit SessionNotices(Host host);

    // Shows every notice the session has queued.
    void drain();
    void show(const Notice& notice);

    // The dropouts since the last clear; warns when they pile up.
    int dropouts();
    // The status bar's count clicked: start counting again from now.
    void clearDropouts();

private:
    DropoutCounter::Raw raw() const;
    Host host_;
    DropoutCounter counter_;
    int dropoutsToasted_ = 0;
    int diskGapsToasted_ = 0;
    double lastDropoutToastMs_ = 0.0;
};

} // namespace northlight
