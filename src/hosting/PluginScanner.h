#pragma once

#include "hosting/PluginRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::hosting
{

// Finds VST3 files in the search paths and inspects each one in a child process
// (aural_plugin_scan), so a plugin that crashes or hangs while loading only costs
// that process. Runs on its own thread; the registry is updated on the message
// thread through update(), and onProgress / onFinished are called there too.
class PluginScanner final : private juce::Thread
{
public:
    explicit PluginScanner(PluginRegistry& registry);
    ~PluginScanner() override;

    // The default VST3 locations plus any extra folders.
    void setExtraSearchPaths(std::vector<std::string> folders) { extraPaths_ = std::move(folders); }

    // Starts a scan: every file whose modification time changed since it was last
    // scanned (all of them when `rescanAll`). No-op while one is running.
    void start(bool rescanAll = false);
    bool isScanning() const noexcept { return isThreadRunning(); }
    void cancel();

    // Message thread: "Scanning 3/12: name" while running.
    std::string status() const;
    int scanned() const noexcept { return done_.load(std::memory_order_relaxed); }
    int total() const noexcept { return total_.load(std::memory_order_relaxed); }

    std::function<void()> onProgress; // message thread, after each file
    std::function<void()> onFinished; // message thread, once, after the last file

    // Where the scanner executable is looked for: next to this executable, or
    // the AURAL_PLUGIN_SCANNER environment variable.
    static juce::File scannerExecutable();

    // Inspects one file in a child process. Blocks up to timeoutMs, or until
    // `shouldStop` says so (checked every 100 ms; the child is killed). Returns
    // false when the process crashed, hung or was stopped (the caller
    // blocklists the file unless it stopped); `found` holds the descriptions
    // otherwise.
    static bool inspect(const juce::File& scanner,
                        const juce::File& file,
                        std::vector<PluginInfo>& found,
                        std::string& error,
                        int timeoutMs = 60000,
                        const std::function<bool()>& shouldStop = {});

    static PluginInfo toInfo(const juce::PluginDescription& description);
    static std::int64_t modificationTime(const juce::File& file);

private:
    void run() override;

    PluginRegistry& registry_;
    // Cleared by the destructor: results the scan thread queued for the message
    // thread that run after the scanner is gone find it false and do nothing.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::vector<std::string> extraPaths_;
    bool rescanAll_ = false;
    std::atomic<int> done_{0}, total_{0};
    juce::CriticalSection statusLock_;
    std::string current_;
};

} // namespace aural::hosting
