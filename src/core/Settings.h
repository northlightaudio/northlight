#pragma once

#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The user settings Preferences shows (screens/setup.md 8.2), as one schema: each
// setting's key in the settings file, its type and its default. The Preferences
// page binds its controls to these, and the app reads them through Reader, so a
// default is written once. Values are stored as text: a flag as "1" or "0", a
// number in decimal. A setting nothing reads yet is marked comingSoon, and
// Preferences badges it. JUCE-free.

namespace northlight::settings
{

enum class Type
{
    text,
    flag,
    number,
    choice, // one of the values the control lists
};

struct Setting
{
    std::string_view key;
    Type type = Type::text;
    std::string_view fallback; // the default, as stored text
    bool comingSoon = false;   // shown in Preferences, read by nothing yet
};

// General
inline constexpr Setting projectFolder{"general.projectFolder", Type::text, ""};
inline constexpr Setting recentCount{"general.recentCount", Type::number, "8"};
inline constexpr Setting autosaveMinutes{"general.autosaveMinutes", Type::number, "2"};
inline constexpr Setting theme{"general.theme", Type::choice, "dark"};
inline constexpr Setting accent{"general.accent", Type::text, ""};
inline constexpr Setting uiScale{"general.uiScale", Type::choice, "0"}; // percent; 0 follows the system
inline constexpr Setting contrast{"general.contrast", Type::choice, "standard"};
// Recording
inline constexpr Setting recordingFormat{"recording.format", Type::choice, "float", true};
inline constexpr Setting namePattern{"recording.namePattern", Type::text, "{track} {take}", true};
inline constexpr Setting countIn{"recording.countIn", Type::choice, "1"}; // bars
inline constexpr Setting preRoll{"recording.preRollBars", Type::number, "0", true};
inline constexpr Setting metronomeLevel{"recording.metronomeLevelDb", Type::number, "-6"};
// Editing
inline constexpr Setting snap{"editing.snap", Type::flag, "1"};
inline constexpr Setting snapGrid{
    "editing.snapGrid", Type::choice, "0"}; // 0 adaptive, else divisions of a bar
inline constexpr Setting ripple{"editing.ripple", Type::flag, "0"};
inline constexpr Setting fadeMs{"editing.fadeMs", Type::number, "10", true};
inline constexpr Setting followPlayhead{"editing.followPlayhead", Type::flag, "1"};
// Plugins
inline constexpr Setting pluginPaths{"plugins.paths", Type::text, ""}; // tab-separated folders
inline constexpr Setting scanOnLaunch{"plugins.scanOnLaunch", Type::flag, "0"};
inline constexpr Setting pluginsOnTop{"plugins.pin", Type::flag, "0"};
// Diagnostics
inline constexpr Setting crashReports{"diagnostics.crashReports", Type::flag, "0", true};

// Every setting above, in Preferences order (for tests and tools).
inline const std::vector<const Setting*>& all()
{
    static const std::vector<const Setting*> list{
        &projectFolder, &recentCount,     &autosaveMinutes, &theme,   &accent,         &uiScale,
        &contrast,      &recordingFormat, &namePattern,     &countIn, &preRoll,        &metronomeLevel,
        &snap,          &snapGrid,        &ripple,          &fadeMs,  &followPlayhead, &pluginPaths,
        &scanOnLaunch,  &pluginsOnTop,    &crashReports};
    return list;
}

inline std::string flagText(bool on)
{
    return on ? "1" : "0";
}

// Typed reads over any key-to-text lookup that answers "" for a key never set.
class Reader
{
public:
    using Lookup = std::function<std::string(const std::string& key)>;
    explicit Reader(Lookup lookup) : lookup_(std::move(lookup)) {}

    // The stored text, or the default when there is none.
    std::string text(const Setting& setting) const
    {
        auto value = lookup_ ? lookup_(std::string(setting.key)) : std::string{};
        return value.empty() ? std::string(setting.fallback) : value;
    }
    bool flag(const Setting& setting) const { return text(setting) == "1"; }
    double number(const Setting& setting) const { return std::strtod(text(setting).c_str(), nullptr); }
    int integer(const Setting& setting) const { return static_cast<int>(number(setting)); }

private:
    Lookup lookup_;
};

} // namespace northlight::settings
