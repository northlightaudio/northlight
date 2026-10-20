#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// Notifications (02-design-system.md, "Toast"): stacked at the bottom-left of
// the window above the status bar, surface1 with a 3 px stripe in the semantic
// colour, a body message and an optional action link in accent. Info and
// success dismiss after 6 s; warning and error stay until dismissed. Up to
// three are shown; older ones leave first. The owner gives the stack its bounds
// (it lays its toasts out from the bottom up) and calls show().
class ToastStack final : public juce::Component, private juce::Timer
{
public:
    enum class Kind
    {
        info,
        success,
        warning,
        error,
    };
    explicit ToastStack(const Theme& theme);
    ~ToastStack() override;

    void show(Kind kind,
              const juce::String& message,
              const juce::String& action = {},
              std::function<void()> onAction = {},
              const juce::String& secondAction = {},
              std::function<void()> onSecondAction = {});
    void dismissAll();
    int count() const noexcept { return static_cast<int>(toasts_.size()); }
    static constexpr int maxShown = 3;
    static constexpr int autoDismissMs = 6000;

    void resized() override;

private:
    class Toast;
    void timerCallback() override;
    void dismiss(Toast* toast);
    // Places the toasts bottom up; animated, a new one slides up 8 px and fades
    // in and the others glide to their places (02-design-system.md 2.6).
    void layout(bool animated = false);
    // A toast on its way out: it fades and drops 8 px, then goes.
    void retire(std::unique_ptr<Toast> toast);

    const Theme& theme_;
    std::vector<std::unique_ptr<Toast>> toasts_;
    std::vector<std::unique_ptr<Toast>> leaving_;
};

} // namespace aural::ui
