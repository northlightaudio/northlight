#pragma once

#include "ui/components/Toast.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight
{
class ProjectSession;
namespace audio
{
class AudioDeviceLayer;
}
namespace ui
{
class Sheet;
struct Theme;
} // namespace ui

// The first-run sheets (screens/setup.md 8.3): choose the audio device (driver,
// device, buffer size and a live input meter), then offer the plugin scan.
class FirstRun
{
public:
    using Toast = std::function<void(ui::ToastStack::Kind kind,
                                     const juce::String& text,
                                     const juce::String& action,
                                     std::function<void()> onAction)>;
    struct Host
    {
        juce::Component& window;
        const ui::Theme& theme;
        audio::AudioDeviceLayer& audio;
        ProjectSession& session;
        ui::Sheet*& openSheet; // the window's one sheet at a time
        Toast toast;
        std::function<void()> showAudioSetup;
    };
    explicit FirstRun(Host host);

    void show();

private:
    void offerScan();

    Host host_;
};

} // namespace northlight
