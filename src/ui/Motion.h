#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <vector>

// Motion helpers for the design system's motion table (02-design-system.md 2.6):
// one easing, and a short tween that drives a component until it is done or the
// component goes away.

namespace aural::ui::motion
{

// The one easing: cubic-bezier(0.2, 0, 0, 1), decelerating. t and the result
// are 0..1.
inline float ease(float t) noexcept
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    // x(s) and y(s) for control points (0.2, 0) and (0, 1); solve x(s) = t.
    const auto bezier = [](float s, float p1, float p2)
    {
        const float u = 1.0f - s;
        return 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s;
    };
    float lo = 0.0f, hi = 1.0f, s = t;
    for (int i = 0; i < 24; ++i) // bisection: x is monotonic
    {
        s = 0.5f * (lo + hi);
        (bezier(s, 0.2f, 0.0f) < t ? lo : hi) = s;
    }
    return bezier(s, 0.0f, 1.0f);
}

// Calls onFrame with the eased progress (0..1) about 60 times a second for
// `ms`, ending on exactly 1, then deletes itself. Stops early (without the
// last frame) if the component is deleted. With ms <= 0 (reduce motion) the
// last frame runs at once. Message thread only.
class Tween final : private juce::Timer
{
public:
    static void run(juce::Component& component, int ms, std::function<void(float)> onFrame)
    {
        if (ms <= 0)
        {
            onFrame(1.0f);
            return;
        }
        auto* tween = new Tween(component, ms, std::move(onFrame)); // owns itself
        live().push_back(tween);
        tween->onFrame_(0.0f);
        tween->startTimerHz(60);
    }

    // Tests run without a message loop: bring every running tween to its end.
    static void finishAllForTesting()
    {
        while (!live().empty())
        {
            auto* tween = live().back();
            if (tween->component_ != nullptr)
                tween->onFrame_(1.0f);
            tween->finish();
        }
    }

private:
    Tween(juce::Component& component, int ms, std::function<void(float)> onFrame)
        : component_(&component), ms_(ms), onFrame_(std::move(onFrame)),
          startMs_(juce::Time::getMillisecondCounterHiRes())
    {
    }

    static std::vector<Tween*>& live()
    {
        static std::vector<Tween*> tweens;
        return tweens;
    }

    void finish()
    {
        stopTimer();
        std::erase(live(), this);
        delete this;
    }

    void timerCallback() override
    {
        if (component_ == nullptr)
        {
            finish();
            return;
        }
        const auto t = static_cast<float>((juce::Time::getMillisecondCounterHiRes() - startMs_) / ms_);
        onFrame_(ease(std::min(1.0f, t)));
        if (t >= 1.0f)
            finish();
    }

    juce::Component::SafePointer<juce::Component> component_;
    int ms_;
    std::function<void(float)> onFrame_;
    double startMs_;
};

} // namespace aural::ui::motion
