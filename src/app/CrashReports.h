#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Crash reports (PLAN.md Phase 9), the local half: when the process dies of an
// unhandled exception (an access violation in a plugin, say) a minidump and a
// short note are written into a folder before it goes, and the next launch can
// say so and show them. Nothing leaves the computer; sending reports is opt in
// and comes with the release work. Windows only; elsewhere the functions do
// nothing. No JUCE, so the handler has nothing to call into but the OS.

namespace aural::crash
{

// Installs the handler. `folder` is created; `version` goes into the note.
void install(const std::filesystem::path& folder, const std::string& version);

// Reports written since they were last acknowledged, newest first.
std::vector<std::filesystem::path> pending(const std::filesystem::path& folder);

// Moves the pending reports into the folder's "Seen" subfolder.
void acknowledge(const std::filesystem::path& folder);

// Writes a report of the running process now (no exception), for tests.
// Returns the dump's path, empty on failure.
std::filesystem::path writeReportNow(const std::filesystem::path& folder);

} // namespace aural::crash
