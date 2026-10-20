#pragma once

#include "audio/JuceMediaLoader.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/DiskThread.h"
#include "engine/pitch/VocalService.h"
#include "hosting/HostFactory.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural
{

// Offline rendering (bounce): the master mix, or one file per track (stems),
// over a range of the timeline, to 32-bit float stereo WAV at the project rate.
//
// Each render runs its own engine over the project, built on the message thread
// (plugin instances must be created there) with media loaded fully in memory
// (streaming would race the disk thread offline). The blocks are rendered on a
// background thread; the first outputLatencySamples() of output are dropped and
// that much is rendered past the end, so the file starts and ends exactly on the
// range. Progress is polled; onFinished fires on the message thread.
class BounceJob final : private juce::Thread
{
public:
    struct Options
    {
        juce::File output; // the mix file, or the folder for stems
        core::SamplePosition start = 0;
        core::SamplePosition end = 0; // exclusive; end <= start means the whole project
        bool stems = false;
        int blockSize = 1024;
    };

    struct Result
    {
        std::vector<juce::File> files;
        float integratedLufs = -100.0f; // of the mix (stems: the last one rendered)
        core::SamplePosition length = 0;
        std::string error; // empty on success
        bool cancelled = false;
    };

    // Builds the engines (message thread). `project` should carry the live insert
    // states (ProjectSession captures them before handing it over).
    BounceJob(const core::Project& project, const hosting::HostFactory& inserts, Options options);
    ~BounceJob() override;

    void start();
    // Renders everything on the calling thread (tests, scripts).
    Result runSynchronously();
    void cancel();
    bool isRunning() const noexcept { return isThreadRunning(); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    const Result& result() const noexcept { return result_; }

    std::function<void(const Result&)> onFinished; // message thread

private:
    struct Render
    {
        std::string name;
        juce::File file;
        core::Project project;
        std::unique_ptr<engine::DiskThread> disk;
        std::unique_ptr<audio::JuceMediaLoader> loader;
        std::unique_ptr<engine::VocalService> vocal; // renders vocal edits inside the compile
        std::unique_ptr<engine::AudioEngine> engine;
        // Lanes on hosted plugins (they own their parameters, so the engine does
        // not play them): set from the render thread before each block.
        struct HostedLane
        {
            core::AutomationLane lane;
            std::shared_ptr<engine::InsertProcessor> instance;
        };
        std::vector<HostedLane> hostedLanes;
    };

    void run() override;
    void renderAll();
    bool renderOne(Render& render, int index, int count);

    const hosting::HostFactory& inserts_;
    Options options_;
    std::vector<std::unique_ptr<Render>> renders_;
    std::atomic<float> progress_{0.0f};
    std::atomic<bool> cancel_{false};
    Result result_;
};

} // namespace aural
