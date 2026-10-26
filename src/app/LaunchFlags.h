#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

// The machinery behind the developer launch flags (the table itself is the app's,
// in LaunchRun.cpp): one entry per flag with its arguments, its help and what
// it does; the help text is generated from the same entries; and one scheduler for
// work that waits for the app to settle, which drops whatever is still pending once
// the app starts shutting down.

namespace northlight
{

// A flag's view of the command line: the tokens after it, consumed as it reads them.
class LaunchArguments
{
public:
    LaunchArguments(const juce::Array<juce::ArgumentList::Argument>& tokens, int& at)
        : tokens_(tokens), at_(at)
    {
    }

    // The next token (the table checked the required ones are there).
    const juce::ArgumentList::Argument& next() { return tokens_.getReference(++at_); }
    juce::String text() { return next().text; }
    juce::File file() { return next().resolveAsFile(); }
    int integer() { return text().getIntValue(); }
    double number() { return text().getDoubleValue(); }
    // An optional value: the next token when it is not another flag (and `accept`
    // takes it, if given), consumed; null otherwise.
    const juce::ArgumentList::Argument* value(const std::function<bool(const juce::String&)>& accept = {})
    {
        if (at_ + 1 >= tokens_.size())
            return nullptr;
        const auto& candidate = tokens_.getReference(at_ + 1);
        if (candidate.text.startsWith("--") || (accept && !accept(candidate.text)))
            return nullptr;
        return &next();
    }
    // An optional word: true (and consumed) when the next token is exactly `word`.
    bool word(const juce::String& word)
    {
        return value([&word](const juce::String& t) { return t == word; });
    }
    int remaining() const noexcept { return tokens_.size() - 1 - at_; }

private:
    const juce::Array<juce::ArgumentList::Argument>& tokens_;
    int& at_;
};

// One launch flag. `run` is null for a flag read at start-up (before the window
// exists), which the table lists so the help covers it.
template <typename Context> struct LaunchFlag
{
    const char* name;      // "--open"
    const char* arguments; // "<file>", "[page]"; "" for none
    int required;          // how many arguments must follow, else the flag is skipped
    const char* help;
    void (*run)(Context&, LaunchArguments&) = nullptr;
};

// Every flag with its arguments and help, wrapped to `width` columns: what --help
// prints and what the comment above the table points to.
template <typename Context>
juce::String launchHelp(const std::vector<LaunchFlag<Context>>& flags, int width = 92)
{
    constexpr int column = 26; // where the help starts
    const auto indent = juce::String::repeatedString(" ", column);
    juce::String out;
    for (const auto& flag : flags)
    {
        auto head = juce::String("  ") + flag.name;
        if (juce::String(flag.arguments).isNotEmpty())
            head << " " << flag.arguments;
        juce::String line;
        if (head.length() + 1 > column)
        {
            out << head << "\n"; // a long name and arguments: the help starts below
            line = indent;
        }
        else
            line = head.paddedRight(' ', column);
        juce::StringArray words;
        words.addTokens(flag.help, " ", "");
        for (const auto& word : words)
        {
            if (line.length() > column && line.length() + 1 + word.length() > width)
            {
                out << line << "\n";
                line = indent;
            }
            if (line.length() > column)
                line << " ";
            line << word;
        }
        out << line.trimEnd() << "\n";
    }
    return out;
}

// Runs the flags on the command line in order. A flag missing its required
// arguments is skipped with a note on stderr; an unknown token is ignored.
// `stop` set by a flag ends the run (--time-load quits).
template <typename Context>
void runLaunchFlags(const std::vector<LaunchFlag<Context>>& flags,
                    Context& context,
                    const juce::String& commandLine,
                    const bool& stop)
{
    const auto tokens = juce::ArgumentList("Northlight", commandLine).arguments;
    for (int i = 0; i < tokens.size() && !stop; ++i)
    {
        const auto& arg = tokens.getReference(i).text;
        for (const auto& flag : flags)
        {
            if (arg != flag.name)
                continue;
            if (flag.run == nullptr)
                break;
            LaunchArguments arguments(tokens, i);
            if (arguments.remaining() < flag.required)
            {
                std::fprintf(stderr, "%s needs %s\n", flag.name, flag.arguments);
                break;
            }
            flag.run(context, arguments);
            break;
        }
    }
}

// Work a launch flag leaves for later: after a delay, on the message thread, and
// never once cancel() has run (the app calls it first thing in shutdown, so no
// scheduled call can reach a window or session that is going away).
class LaunchSchedule
{
public:
    // The moments flags wait for.
    static constexpr int launchedMs = 500; // the launch's own messages are through (--save)
    static constexpr int settledMs = 1500; // the window has laid out, the device has opened

    LaunchSchedule() = default;
    LaunchSchedule(const LaunchSchedule&) = delete;
    LaunchSchedule& operator=(const LaunchSchedule&) = delete;
    ~LaunchSchedule() { cancel(); }

    void after(int milliseconds, std::function<void()> action)
    {
        juce::Timer::callAfterDelay(std::max(0, milliseconds), guard(std::move(action)));
    }
    // `action` as a call that does nothing once cancel() has run.
    std::function<void()> guard(std::function<void()> action) const
    {
        return [alive = alive_, action = std::move(action)]
        {
            if (alive->load(std::memory_order_acquire))
                action();
        };
    }
    void afterSeconds(double seconds, std::function<void()> action)
    {
        after(static_cast<int>(seconds * 1000.0), std::move(action));
    }
    void cancel() noexcept { alive_->store(false, std::memory_order_release); }
    bool cancelled() const noexcept { return !alive_->load(std::memory_order_acquire); }

private:
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

} // namespace northlight
