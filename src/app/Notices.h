#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <utility>
#include <vector>

// What the session has to tell the user, once each: a recording's outcome, a
// plugin that crashed, an export or a freeze finishing. The session pushes a
// Notice with its kind decided where it is made; the window drains the queue on
// its next refresh and shows each as a toast (a `status` one in the status bar).
// So the same sentence twice is told twice, and rewording one changes nothing
// but its text. Message thread.

namespace northlight
{

struct Notice
{
    enum class Kind
    {
        status, // in progress: the status bar, not a toast
        info,
        success,
        warning,
        error,
    };
    Kind kind = Kind::info;
    std::string text;
    juce::File reveal; // a file the toast offers to show in its folder (an export)
};

class NoticeQueue
{
public:
    void push(Notice::Kind kind, std::string text, juce::File reveal = {})
    {
        notices_.push_back({kind, std::move(text), std::move(reveal)});
    }
    // Every notice not yet taken, oldest first.
    std::vector<Notice> take() { return std::exchange(notices_, {}); }
    bool empty() const noexcept { return notices_.empty(); }

private:
    std::vector<Notice> notices_;
};

} // namespace northlight
