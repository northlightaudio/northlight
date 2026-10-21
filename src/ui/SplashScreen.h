#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// The splash screen (screens/setup.md): the brand photograph with the mark, the wordmark, the
// version, what is loading, and the copyright, licence and photo credit. It shows
// while the app starts and again as the About box. It owns itself: it closes and
// deletes itself after the start (finish()) or when clicked as the About box.
class SplashScreen final : public juce::Component, private juce::Timer
{
public:
    // A startup splash, on top of everything, centred on the main display.
    static juce::Component::SafePointer<SplashScreen> showStartup(const Theme& theme,
                                                                  const juce::String& version);
    // The About box, centred over the given window; a click or Escape closes it.
    static void showAbout(const Theme& theme, const juce::String& version, juce::Component* over);

    // What is loading now and how far the start has got (0..1). Painted at once:
    // the start runs on the message thread, so nothing else would paint it.
    void setStatus(const juce::String& text, float progress);
    // The app is up: close after the splash has shown for a moment, fading out.
    void finish();

    static constexpr int width = 600, height = 400;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusLost(FocusChangeType cause) override;

private:
    enum class Mode
    {
        startup,
        about,
    };
    SplashScreen(const Theme& theme, const juce::String& version, Mode mode);
    void timerCallback() override;
    void close();

    const Theme& theme_;
    const juce::String version_;
    const Mode mode_;
    juce::Image photo_;
    std::unique_ptr<juce::Drawable> mark_, wordmark_;
    juce::String status_;
    float progress_ = 0.0f;
    double shownAtMs_ = 0.0;
    bool finishing_ = false, closing_ = false;
};

} // namespace northlight::ui
