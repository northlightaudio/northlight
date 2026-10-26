#pragma once

#include "audio/JuceMediaLoader.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/DiskThread.h"
#include "engine/ExportSettings.h"
#include "engine/Stretch.h"
#include "engine/pitch/VocalService.h"
#include "hosting/HostFactory.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight
{

// Offline rendering (bounce, export): the master mix, or one file per track
// (stems), over a range of the timeline, to WAV, AIFF, FLAC or Ogg Vorbis at the
// project rate or another (engine/ExportSettings.h).
//
// Each render runs its own engine over the project, built on the message thread
// (plugin instances must be created there) with media loaded fully in memory
// (streaming would race the disk thread offline). The blocks are rendered on a
// background thread into memory; the first outputLatencySamples() of output are
// dropped and that much is rendered past the end, so the file starts and ends
// exactly on the range. Then, two-pass: the render is converted to the output
// rate (windowed sinc), measured (integrated loudness, sample or true peak),
// given the normalisation gain if asked for, quantised to the bit depth with
// dither, and written. Stems are normalised together: a render of the mix is
// measured first and its gain applied to every stem, so they still sum to the
// normalised mix. Progress is polled; onFinished fires on the message thread.
// cancel() is seen within one render block and within a few thousand samples of
// the conversion, measurement and write, so destroying a running job (which
// cancels it and waits for the thread) holds the message thread only that long.
class BounceJob final : private juce::Thread
{
public:
    struct Options
    {
        juce::File output; // the mix file, or the folder for stems
        core::SamplePosition start = 0;
        core::SamplePosition end = 0; // exclusive; end <= start means the whole project
        bool stems = false;
        std::vector<core::Id> stemTracks; // stems: the tracks to write (empty = every track)
        // Format, bit depth, rate, dither and normalisation (its source and range
        // are the caller's: they are given by `stems` and the range above). The
        // default is the original bounce: 32-bit float WAV at the project rate.
        engine::ExportSettings format = floatWav();
        int blockSize = 1024;

        static engine::ExportSettings floatWav()
        {
            engine::ExportSettings s;
            s.depth = engine::ExportDepth::float32;
            s.dither = engine::DitherMode::off;
            return s;
        }
    };

    struct Result
    {
        std::vector<juce::File> files;
        // Integrated loudness as rendered, before the normalisation gain: of the mix
        // (stems: of the mix when normalised, else of the last stem written).
        float integratedLufs = -100.0f;
        double gainDb = 0.0;             // the normalisation gain applied
        bool limitedByCeiling = false;   // the gain stopped short of the target at the peak ceiling
        double peakDb = -200.0;          // after the gain: true peak or sample peak, per the settings
        bool truePeak = false;           // peakDb is a true peak (dBTP)
        core::SamplePosition length = 0; // samples per file, at the output rate
        std::string error;               // empty on success
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

    // The outcome as a sentence for the completion toast: the file(s), the length,
    // the measured loudness and, when normalised, the gain and the resulting peak.
    static std::string describe(const Result& result, const Options& options, double outputRate);

private:
    struct Render
    {
        std::string name;
        bool measureOnly = false; // the mix, rendered to find the stems' normalisation gain
        juce::File file;
        core::Project project;
        std::unique_ptr<engine::DiskThread> disk;
        std::unique_ptr<audio::JuceMediaLoader> loader;
        std::unique_ptr<engine::VocalService> vocal;     // renders vocal edits inside the compile
        std::unique_ptr<engine::StretchService> stretch; // and stretched clips
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
    // Renders the range into `channels` (project rate); false on cancel.
    bool renderToMemory(Render& render, int index, int count, std::vector<std::vector<float>>& channels);
    bool writeFile(const juce::File& file, const std::vector<std::vector<float>>& channels, double rate);
    void setProgress(int index, int count, float fraction) noexcept;

    const hosting::HostFactory& inserts_;
    Options options_;
    std::vector<std::unique_ptr<Render>> renders_;
    std::atomic<float> progress_{0.0f};
    std::atomic<bool> cancel_{false};
    // False once the job is destroyed: guards the completion queued by run().
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    Result result_;
    double sharedGainDb_ = 0.0; // stems: the gain the mix render found
};

} // namespace northlight
