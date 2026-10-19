#include "Bounce.h"

#include "engine/WavFile.h"

#include <algorithm>

namespace aural
{

BounceJob::BounceJob(const core::Project& project, const hosting::HostFactory& inserts, Options options)
    : juce::Thread("Bounce"), inserts_(inserts), options_(std::move(options))
{
    if (options_.end <= options_.start)
    {
        options_.start = 0;
        options_.end = project.length();
    }
    const auto make = [&](const std::string& name, const juce::File& file, core::Project copy)
    {
        auto render = std::make_unique<Render>();
        render->name = name;
        render->file = file;
        render->project = std::move(copy);
        render->disk = std::make_unique<engine::DiskThread>();
        render->loader = std::make_unique<audio::JuceMediaLoader>(*render->disk, true);
        render->engine = std::make_unique<engine::AudioEngine>(*render->loader, inserts_, 0);
        render->engine->prepare(render->project.sampleRate, options_.blockSize);
        render->engine->setProject(render->project); // plugin instances: message thread
        renders_.push_back(std::move(render));
    };
    if (!options_.stems)
    {
        make("mix", options_.output, project);
        return;
    }
    // One file per track, the others muted; buses and the master stay as they are.
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        core::Project copy = project;
        for (size_t t = 0; t < copy.tracks.size(); ++t)
            copy.tracks[t].mute = t != i;
        const auto safe =
            juce::File::createLegalFileName(juce::String::fromUTF8(project.tracks[i].name.c_str())).trim();
        const auto file =
            options_.output.getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " " +
                                         (safe.isEmpty() ? juce::String("Track") : safe) + ".wav");
        make(project.tracks[i].name, file, std::move(copy));
    }
}

BounceJob::~BounceJob()
{
    cancel();
    stopThread(10000);
}

void BounceJob::start()
{
    if (!isThreadRunning())
        startThread();
}

void BounceJob::cancel()
{
    cancel_.store(true, std::memory_order_release);
}

BounceJob::Result BounceJob::runSynchronously()
{
    renderAll();
    return result_;
}

void BounceJob::run()
{
    renderAll();
    juce::MessageManager::callAsync(
        [this]
        {
            if (onFinished)
                onFinished(result_);
        });
}

void BounceJob::renderAll()
{
    result_ = Result{};
    if (options_.stems)
        options_.output.createDirectory();
    const int count = static_cast<int>(renders_.size());
    for (int i = 0; i < count; ++i)
    {
        if (cancel_.load(std::memory_order_acquire))
        {
            result_.cancelled = true;
            break;
        }
        if (!renderOne(*renders_[static_cast<size_t>(i)], i, count))
            break;
    }
    progress_.store(1.0f, std::memory_order_relaxed);
}

bool BounceJob::renderOne(Render& render, int index, int count)
{
    auto& engine = *render.engine;
    const auto length = std::max<core::SamplePosition>(0, options_.end - options_.start);
    const int latency = engine.outputLatencySamples();
    engine::WavWriter writer;
    if (!writer.open(render.file.getFullPathName().toStdString(), 2, render.project.sampleRate))
    {
        result_.error = "could not write " + render.file.getFullPathName().toStdString();
        return false;
    }

    engine.locate(options_.start);
    engine.play();
    const int block = options_.blockSize;
    std::vector<float> left(static_cast<size_t>(block)), right(static_cast<size_t>(block));
    std::vector<float> interleaved(static_cast<size_t>(block) * 2);
    float* outputs[2] = {left.data(), right.data()};
    // The engine's output lags the timeline by the compensation latency: drop that
    // much at the start and render that much extra at the end.
    core::SamplePosition produced = 0; // engine samples rendered
    core::SamplePosition written = 0;  // file samples
    const core::SamplePosition total = length + latency;
    while (produced < total)
    {
        if (cancel_.load(std::memory_order_acquire))
        {
            result_.cancelled = true;
            writer.close();
            return false;
        }
        const int n = static_cast<int>(std::min<core::SamplePosition>(block, total - produced));
        engine.process(nullptr, 0, outputs, 2, n);
        int from = 0;
        if (produced < latency)
            from = static_cast<int>(std::min<core::SamplePosition>(n, latency - produced));
        const int keep = n - from;
        for (int i = 0; i < keep; ++i)
        {
            interleaved[static_cast<size_t>(2 * i)] = left[static_cast<size_t>(from + i)];
            interleaved[static_cast<size_t>(2 * i + 1)] = right[static_cast<size_t>(from + i)];
        }
        if (keep > 0 && !writer.writeInterleaved(interleaved.data(), keep))
        {
            result_.error = "write failed: " + render.file.getFullPathName().toStdString();
            writer.close();
            return false;
        }
        produced += n;
        written += keep;
        progress_.store(
            (static_cast<float>(index) +
             static_cast<float>(produced) / static_cast<float>(std::max<core::SamplePosition>(1, total))) /
                static_cast<float>(count),
            std::memory_order_relaxed);
    }
    engine.stop();
    if (!writer.close())
    {
        result_.error = "could not finish " + render.file.getFullPathName().toStdString();
        return false;
    }
    result_.files.push_back(render.file);
    result_.integratedLufs = engine.loudness().integratedLufs;
    result_.length = written;
    return true;
}

} // namespace aural
