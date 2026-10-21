#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// The status bar (screens/shell.md): device, rate and buffer, round trip, load,
// dropouts and (while recording) disk, left in timeSmall mono separated by
// middle dots, each with a tooltip; a transient confirmation on the right that
// fades after four seconds. Anything needing action is a toast, not a message.
class StatusBar final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit StatusBar(const Theme& theme);

    struct State
    {
        bool deviceOpen = false;
        bool devicePreferredUnavailable = false;
        juce::String driver, device;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double roundTripMs = 0.0;
        bool roundTripMeasured = false;
        double loadP50 = 0.0, loadP99 = 0.0, loadMax = 0.0; // fractions of the deadline
        int dropouts = 0;
        juce::String lastDropoutTime;
        bool recording = false;
        double diskMbPerSec = 0.0, diskFreeGb = 0.0;
        juce::String warning; // a persistent warning text (sample-rate mismatch): shown in warning colour
    };
    void setState(const State& state);
    // A transient confirmation ("Saved"), gone after four seconds.
    void showMessage(const juce::String& message);
    // A one-line description of the control under the pointer (10.5).
    void setHint(const juce::String& hint);

    std::function<void()> onDeviceClicked, onDropoutsClicked;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    struct Segment
    {
        juce::String text, tooltip;
        juce::Colour colour;
        juce::Rectangle<int> bounds;
        std::function<void()>* action = nullptr;
        bool warningDot = false;
    };
    void rebuildSegments();
    void timerCallback() override;

    const Theme& theme_;
    State state_;
    std::vector<Segment> segments_;
    juce::String message_, hint_;
    double messageShownAt_ = 0.0;
    int hoverSegment_ = -1;
    static constexpr int messageMs = 4000;
};

} // namespace northlight::ui
