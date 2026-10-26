#pragma once

#include "app/Notices.h"
#include "ui/components/Toast.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace northlight
{
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
    Host host_;
    int dropoutBaseline_ = 0;
    std::int64_t overrunBaseline_ = 0;
    std::uint64_t diskBaseline_ = 0;
    int dropoutsToasted_ = 0;
    int diskGapsToasted_ = 0;
    double lastDropoutToastMs_ = 0.0;
};

} // namespace northlight
