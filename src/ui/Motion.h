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
// `ms`, ending on exactly 1, then onDone (if any), then deletes itself. Stops
// early (without the last frame or onDone) if the component is deleted. With
// ms <= 0 (reduce motion) the last frame and onDone run at once. Message
// thread only.
class Tween final : private juce::Timer
{
public:
    static void run(juce::Component& component,
                    int ms,
                    std::function<void(float)> onFrame,
                    std::function<void()> onDone = {})
    {
        if (ms <= 0)
        {
            onFrame(1.0f);
            if (onDone)
                onDone();
            return;
        }
        auto* tween = new Tween(component, ms, std::move(onFrame)); // owns itself
        tween->onDone_ = std::move(onDone);
        live().push_back(tween);
        tween->onFrame_(0.0f);
        tween->startTimerHz(60);
    }

    // Stops the component's running tweens where they are (no last frame, no
    // onDone): a new layout or animation takes over.
    static void cancel(juce::Component& component)
    {
        for (size_t i = live().size(); i-- > 0;)
            if (i < live().size() && live()[i]->component_ == &component)
                live()[i]->finish();
    }

    // Tests run without a message loop: bring every running tween to its end.
    static void finishAllForTesting()
    {
        while (!live().empty())
        {
            auto* tween = live().back();
            const bool alive = tween->component_ != nullptr;
            if (alive)
                tween->onFrame_(1.0f);
            auto done = alive ? std::move(tween->onDone_) : std::function<void()>{};
            tween->finish();
            if (done)
                done();
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
        {
            auto done = std::move(onDone_);
            finish(); // deletes this: done runs after
            if (done)
                done();
        }
    }

    juce::Component::SafePointer<juce::Component> component_;
    int ms_;
    std::function<void(float)> onFrame_;
    std::function<void()> onDone_;
    double startMs_;
};

// Moves a component from `from` to its current bounds (and its alpha from
// `alphaFrom` to 1) over `ms`. The usual entrance and rearrangement.
inline void glideIn(juce::Component& component, juce::Rectangle<int> from, float alphaFrom, int ms)
{
    const auto to = component.getBounds();
    Tween::cancel(component);
    if (ms <= 0 || (from == to && alphaFrom >= 1.0f))
    {
        component.setAlpha(1.0f);
        return;
    }
    juce::Component::SafePointer<juce::Component> safe(&component);
    component.setBounds(from);
    component.setAlpha(alphaFrom);
    Tween::run(component,
               ms,
               [safe, from, to, alphaFrom](float t)
               {
                   if (safe == nullptr)
                       return;
                   const auto lerp = [t](int a, int b)
                   { return a + juce::roundToInt(static_cast<float>(b - a) * t); };
                   safe->setBounds(lerp(from.getX(), to.getX()),
                                   lerp(from.getY(), to.getY()),
                                   lerp(from.getWidth(), to.getWidth()),
                                   lerp(from.getHeight(), to.getHeight()));
                   safe->setAlpha(alphaFrom + (1.0f - alphaFrom) * t);
               });
}

} // namespace aural::ui::motion
