#pragma once

#include "core/Model.h"
#include "engine/AudioSource.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace northlight
{

// Where opening a project has got to, for the loading sheet (screens/shell.md,
// "Opening a project").
struct LoadProgress
{
    enum class Stage
    {
        reading, // the file and the project in it
        media,   // decoding its audio files
        plugins, // making its instruments and effects
        engine,  // the first snapshot of the new project
        done,
    };
    Stage stage = Stage::reading;
    int done = 0;          // items finished in this stage
    int total = 0;         // items in the stage (plugins: the hosted ones only)
    std::string item;      // what is loading now: a file name or a plugin's name
    float fraction = 0.0f; // of the whole load, 0..1, never going back
};

// The progress line under the bar: "Reading audio  ·  3 of 12  ·  kick.wav".
std::string describe(const LoadProgress& progress);

// How long each stage of the last load took, in milliseconds (--time-load).
struct LoadTimings
{
    double readMs = 0.0, mediaMs = 0.0, pluginsMs = 0.0, engineMs = 0.0;
    int mediaFiles = 0, inserts = 0, hostedPlugins = 0;
    double totalMs() const noexcept { return readMs + mediaMs + pluginsMs + engineMs; }
};

// The part of opening a project that needs no message thread, run on a thread of
// its own: reading the file, parsing the project, and decoding
// its media at the project's rate. The current project is untouched until the
// session takes the result, so the load can be cancelled until then. Progress is
// readable from any thread; the work is weighted so the fraction moves evenly
// through the stages that follow (ProjectSession reports those).
class ProjectLoad
{
public:
    // Starts at once. `loader` must outlive the load (the destructor waits).
    ProjectLoad(juce::File file, engine::MediaLoader& loader, std::string readError);
    ~ProjectLoad();
    ProjectLoad(const ProjectLoad&) = delete;
    ProjectLoad& operator=(const ProjectLoad&) = delete;

    const juce::File& file() const noexcept { return file_; }
    // True once the background part has finished (or failed, or was cancelled).
    bool ready() const noexcept { return ready_.load(std::memory_order_acquire); }
    // Blocks until ready().
    void wait();
    // Stops between files; ready() follows soon after.
    void cancel() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
    bool cancelled() const noexcept { return cancelled_.load(std::memory_order_relaxed); }

    LoadProgress progress() const;
    // The session reports the later stages through these; `done`/`total` count
    // the stage's items and `units` the work finished in it (see weights below).
    void report(LoadProgress::Stage stage, int done, int total, std::string item, double units);

    // After ready(): the project (nullopt on failure, with error()), the decoded
    // media by path, and the work weights.
    std::optional<core::Project>& project() noexcept { return project_; }
    const std::optional<core::Project>& project() const noexcept { return project_; }
    const std::string& error() const noexcept { return error_; }
    std::map<std::string, std::shared_ptr<const engine::AudioSource>>& media() noexcept { return media_; }
    LoadTimings& timings() noexcept { return timings_; }
    const LoadTimings& timings() const noexcept { return timings_; }

    // Work weights: a decoded file is one unit, a hosted plugin two (they load
    // DLLs and restore state), a built-in a twentieth; reading and the final
    // snapshot one each.
    static constexpr double hostedUnits = 2.0, builtInUnits = 0.05;

private:
    void run();
    void setProgress(LoadProgress::Stage stage, int done, int total, std::string item, double unitsDone);

    juce::File file_;
    engine::MediaLoader& loader_;
    std::string readError_;
    std::optional<core::Project> project_;
    std::string error_;
    std::map<std::string, std::shared_ptr<const engine::AudioSource>> media_;
    LoadTimings timings_;

    mutable std::mutex mutex_;
    LoadProgress progress_;
    double totalUnits_ = 0.0;  // known once the project is parsed
    double mediaUnits_ = 0.0;  // where the media stage starts
    double pluginUnits_ = 0.0; // ... and the plugin stage
    double engineUnits_ = 0.0; // ... and the final snapshot

    std::atomic<bool> ready_{false};
    std::atomic<bool> cancelled_{false};
    std::thread thread_; // last: started once everything above exists
};

} // namespace northlight
