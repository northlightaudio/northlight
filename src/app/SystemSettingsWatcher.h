#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

// Tells the app when Windows' accessibility settings change while it runs: a
// contrast theme turned on or off, or Show animations switched (Windows sends
// WM_SETTINGCHANGE to every top-level window). A hidden window of its own takes the
// message, so nothing has to reach into JUCE's windows. `onChanged` runs on the
// message thread, only when one of the two values changed.

namespace northlight
{

class SystemSettingsWatcher final : private juce::AsyncUpdater
{
public:
    explicit SystemSettingsWatcher(std::function<void()> onChanged);
    ~SystemSettingsWatcher() override;

    SystemSettingsWatcher(const SystemSettingsWatcher&) = delete;
    SystemSettingsWatcher& operator=(const SystemSettingsWatcher&) = delete;

private:
    void handleAsyncUpdate() override;

    struct Window;
    std::function<void()> onChanged_;
    bool highContrast_ = false, reduceMotion_ = false;
    std::unique_ptr<Window> window_;
};

} // namespace northlight
