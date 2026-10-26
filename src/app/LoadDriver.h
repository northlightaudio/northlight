#pragma once

#include "app/ProjectLoad.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <string>

namespace northlight
{
class ProjectSession;
namespace ui
{
class ProgressRow;
class Sheet;
struct Theme;
} // namespace ui

// Steps a project load the session has begun (screens/shell.md, "Opening a
// project"; architecture.md): shows the loading sheet over the window, takes a
// step on a timer and calls back with the outcome. The sheet appears only once the
// load has run for motion.progressDelayMs, but holds the window's input from the
// start; before a step that makes a third-party plugin (which holds the message
// thread) the sheet comes in at once and no step runs until it is on screen.
class LoadDriver final : private juce::Timer
{
public:
    struct Host
    {
        juce::Component& sheetHost; // the window the sheet covers
        const ui::Theme& theme;
        ProjectSession& session;
        // The new project is the session's (past the commit point): the window shows
        // it behind the sheet.
        std::function<void()> projectSwapped;
        // Cancelled while the old project was still the session's (a status line).
        std::function<void(const juce::String& title)> cancelled;
    };
    explicit LoadDriver(Host host);
    ~LoadDriver() override;

    // Watches the load the session has begun; `onFinished` gets the outcome (an
    // error, or empty). `holdAt` (--show-loading) stops stepping once the load
    // reaches that stage and shows the sheet at once, so a capture finds it.
    void watch(const juce::String& title,
               std::function<void(const std::string&)> onFinished,
               std::optional<LoadProgress::Stage> holdAt = std::nullopt);

private:
    static constexpr int stepMs = 15;
    void timerCallback() override { step(); }
    void step();
    LoadProgress showProgress();
    void finish(const std::string& error);

    Host host_;
    juce::Component::SafePointer<ui::Sheet> sheet_; // the sheet and its row delete themselves
    juce::Component::SafePointer<ui::ProgressRow> row_;
    std::function<void(const std::string&)> finished_;
    std::optional<LoadProgress::Stage> holdAt_;
    bool held_ = false;
};

} // namespace northlight
