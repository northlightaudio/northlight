#include "Bounce.h"

#include "engine/ExportDsp.h"
#include "engine/WavFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace northlight
{

BounceJob::BounceJob(const core::Project& project, const hosting::HostFactory& inserts, Options options)
    : juce::Thread("Bounce"), inserts_(inserts), options_(std::move(options))
{
    options_.format = options_.format.validated();
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
        render->vocal = std::make_unique<engine::VocalService>(*render->loader, true);
        render->engine->setClipSourceHook(
            [vocal = render->vocal.get()](
                const core::AudioClip& clip, double rate, std::shared_ptr<const engine::AudioSource> base)
            { return vocal->sourceFor(clip, rate, std::move(base)); });
        render->stretch = std::make_unique<engine::StretchService>(*render->loader, true);
        render->engine->setStretchHook(
            [stretch = render->stretch.get()](
                const core::AudioClip& clip, double rate, std::shared_ptr<const engine::AudioSource> base)
            { return stretch->sourceFor(clip, rate, base); });
        render->engine->prepare(render->project.sampleRate, options_.blockSize);
        render->engine->setProject(render->project); // plugin instances: message thread
        const auto collect = [&](const core::Channel& channel)
        {
            if (channel.automationMode == core::AutomationMode::off ||
                channel.automationMode == core::AutomationMode::write)
                return;
            for (const auto& lane : channel.automation)
                if (lane.target == core::AutomationTarget::insertParameter && !lane.points.empty())
                    if (auto instance = render->engine->insertInstance(lane.insert);
                        instance != nullptr && instance->ownsParameters())
                        render->hostedLanes.push_back({lane, std::move(instance)});
        };
        for (const auto& t : render->project.tracks)
            collect(t);
        for (const auto& b : render->project.buses)
            collect(b);
        collect(render->project.master);
        renders_.push_back(std::move(render));
    };
    if (!options_.stems)
    {
        make("mix", options_.output, project);
        return;
    }
    // Stems normalised together: the mix is measured first for their common gain.
    if (options_.format.normalise)
    {
        make("mix", juce::File(), project);
        renders_.back()->measureOnly = true;
    }
    // One file per track, the others muted; buses and the master stay as they are.
    const auto extension = juce::String(".") + engine::extensionFor(options_.format.format);
    for (size_t i = 0; i < project.tracks.size(); ++i)
    {
        const auto& wanted = options_.stemTracks;
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), project.tracks[i].id) == wanted.end())
            continue;
        core::Project copy = project;
        for (size_t t = 0; t < copy.tracks.size(); ++t)
            copy.tracks[t].mute = t != i;
        const auto safe =
            juce::File::createLegalFileName(juce::String::fromUTF8(project.tracks[i].name.c_str())).trim();
        const auto file =
            options_.output.getChildFile(juce::String(i + 1).paddedLeft('0', 2) + " " +
                                         (safe.isEmpty() ? juce::String("Track") : safe) + extension);
        make(project.tracks[i].name, file, std::move(copy));
    }
}

BounceJob::~BounceJob()
{
    // A finished render may still have its completion queued on the message
    // thread; it checks this and does nothing once the job is gone.
    alive_->store(false, std::memory_order_release);
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
        [this, alive = alive_]
        {
            if (alive->load(std::memory_order_acquire) && onFinished)
                onFinished(result_);
        });
}

void BounceJob::renderAll()
{
    result_ = Result{};
    sharedGainDb_ = 0.0;
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

void BounceJob::setProgress(int index, int count, float fraction) noexcept
{
    progress_.store((static_cast<float>(index) + fraction) / static_cast<float>(std::max(1, count)),
                    std::memory_order_relaxed);
}

bool BounceJob::renderOne(Render& render, int index, int count)
{
    std::vector<std::vector<float>> channels;
    if (!renderToMemory(render, index, count, channels))
        return false;

    // Second pass: convert the rate, measure, normalise, then write.
    const auto& settings = options_.format;
    const double fromRate = render.project.sampleRate;
    const double toRate = engine::outputRate(settings, fromRate);
    if (toRate != fromRate)
        for (auto& channel : channels)
            channel = engine::resample(channel.data(), channel.size(), fromRate, toRate);
    if (cancel_.load(std::memory_order_acquire))
    {
        result_.cancelled = true;
        return false;
    }
    setProgress(index, count, 0.9f);
    const bool truePeak = settings.normalise && settings.peakMode == engine::PeakMode::truePeak;
    const auto analysis = engine::analyse(channels, toRate, truePeak);
    const double peakDb = truePeak ? analysis.truePeakDb : analysis.samplePeakDb;
    const auto own =
        engine::normalisationGain(analysis.integratedLufs, peakDb, settings.targetLufs, settings.ceilingDb);
    const auto report = [&](const engine::Normalisation& n)
    {
        result_.integratedLufs = static_cast<float>(analysis.integratedLufs);
        result_.gainDb = n.gainDb;
        result_.limitedByCeiling = n.limitedByCeiling;
        result_.peakDb = peakDb + n.gainDb;
        result_.truePeak = truePeak;
    };
    if (render.measureOnly)
    {
        sharedGainDb_ = own.gainDb;
        report(own);
        return true;
    }

    double gainDb = 0.0;
    if (settings.normalise)
        gainDb = options_.stems ? sharedGainDb_ : own.gainDb;
    engine::applyGain(channels, static_cast<float>(engine::dbToGain(gainDb)));
    if (!writeFile(render.file, channels, toRate))
        return false;
    result_.files.push_back(render.file);
    result_.length = channels.empty() ? 0 : static_cast<core::SamplePosition>(channels[0].size());
    if (!(options_.stems && settings.normalise))
        report(settings.normalise ? own : engine::Normalisation{});
    setProgress(index, count, 1.0f);
    return true;
}

std::string BounceJob::describe(const Result& result, const Options& options, double outputRate)
{
    if (!result.error.empty())
        return "Export failed: " + result.error;
    if (result.cancelled)
        return "Export cancelled";
    const auto seconds = static_cast<int>(static_cast<double>(result.length) / std::max(1.0, outputRate));
    const auto length = juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
    const auto db = [](double value, int places = 1)
    { return (value > 0.0 ? juce::String("+") : juce::String()) + juce::String(value, places); };
    const auto& settings = options.format;
    const auto peakUnit = juce::String(result.truePeak ? " dBTP" : " dBFS");
    juce::String text;
    if (options.stems)
    {
        text = "Exported " + juce::String(static_cast<int>(result.files.size())) +
               (result.files.size() == 1 ? " stem" : " stems") + " (" + length + ")";
        if (settings.normalise)
            text << ": the mix measured " << juce::String(result.integratedLufs, 1) << " LUFS, "
                 << db(result.gainDb) << " dB on every stem";
        return text.toStdString();
    }
    const auto name = result.files.empty() ? juce::String("the mix") : result.files.front().getFileName();
    text = "Exported " + name + " (" + length + "): " + juce::String(result.integratedLufs, 1) + " LUFS";
    if (!settings.normalise)
        return (text + " integrated, peak " + juce::String(result.peakDb, 1) + peakUnit).toStdString();
    text << ", " << db(result.gainDb) << " dB gain to "
         << juce::String(static_cast<double>(result.integratedLufs) + result.gainDb, 1) << " LUFS, peak "
         << juce::String(result.peakDb, 1) << peakUnit;
    if (result.limitedByCeiling)
        text << " (held at the ceiling, short of " << juce::String(settings.targetLufs, 1) << ")";
    return text.toStdString();
}

bool BounceJob::renderToMemory(Render& render,
                               int index,
                               int count,
                               std::vector<std::vector<float>>& channels)
{
    auto& engine = *render.engine;
    const auto length = std::max<core::SamplePosition>(0, options_.end - options_.start);
    const int latency = engine.outputLatencySamples();
    channels.assign(2, {});
    for (auto& channel : channels)
        channel.reserve(static_cast<size_t>(length));

    engine.locate(options_.start);
    engine.play();
    const int block = options_.blockSize;
    std::vector<float> left(static_cast<size_t>(block)), right(static_cast<size_t>(block));
    float* outputs[2] = {left.data(), right.data()};
    // The engine's output lags the timeline by the compensation latency: drop that
    // much at the start and render that much extra at the end.
    core::SamplePosition produced = 0; // engine samples rendered
    const core::SamplePosition total = length + latency;
    while (produced < total)
    {
        if (cancel_.load(std::memory_order_acquire))
        {
            result_.cancelled = true;
            engine.stop();
            return false;
        }
        const int n = static_cast<int>(std::min<core::SamplePosition>(block, total - produced));
        for (const auto& hosted : render.hostedLanes)
            hosted.instance->setParameter(hosted.lane.parameter, hosted.lane.valueAt(engine.position()));
        engine.process(nullptr, 0, outputs, 2, n);
        int from = 0;
        if (produced < latency)
            from = static_cast<int>(std::min<core::SamplePosition>(n, latency - produced));
        channels[0].insert(channels[0].end(), left.begin() + from, left.begin() + n);
        channels[1].insert(channels[1].end(), right.begin() + from, right.begin() + n);
        produced += n;
        // Rendering is most of the work; conversion and writing take the rest.
        setProgress(index,
                    count,
                    0.8f * static_cast<float>(produced) /
                        static_cast<float>(std::max<core::SamplePosition>(1, total)));
    }
    engine.stop();
    return true;
}

bool BounceJob::writeFile(const juce::File& file,
                          const std::vector<std::vector<float>>& channels,
                          double rate)
{
    const auto& settings = options_.format;
    const auto path = file.getFullPathName().toStdString();
    const int numChannels = static_cast<int>(channels.size());
    const auto length = channels.empty() ? size_t{0} : channels[0].size();
    const auto depth = engine::depthFor(settings.format, settings.depth);
    constexpr size_t chunk = 4096;

    // 32-bit float WAV: the engine's writer (the same file the recorder writes).
    if (settings.format == engine::ExportFormat::wav && depth == engine::ExportDepth::float32)
    {
        engine::WavWriter writer;
        if (!writer.open(path, numChannels, rate))
        {
            result_.error = "could not write " + path;
            return false;
        }
        std::vector<float> interleaved(chunk * static_cast<size_t>(numChannels));
        for (size_t at = 0; at < length; at += chunk)
        {
            const auto n = std::min(chunk, length - at);
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < numChannels; ++c)
                    interleaved[i * static_cast<size_t>(numChannels) + static_cast<size_t>(c)] =
                        channels[static_cast<size_t>(c)][at + i];
            if (!writer.writeInterleaved(interleaved.data(), static_cast<int>(n)))
            {
                result_.error = "write failed: " + path;
                writer.close();
                return false;
            }
        }
        if (!writer.close())
        {
            result_.error = "could not finish " + path;
            return false;
        }
        return true;
    }

    std::unique_ptr<juce::AudioFormat> format;
    switch (settings.format)
    {
        case engine::ExportFormat::wav:
            format = std::make_unique<juce::WavAudioFormat>();
            break;
        case engine::ExportFormat::aiff:
            format = std::make_unique<juce::AiffAudioFormat>();
            break;
        case engine::ExportFormat::flac:
            format = std::make_unique<juce::FlacAudioFormat>();
            break;
        case engine::ExportFormat::ogg:
            format = std::make_unique<juce::OggVorbisAudioFormat>();
            break;
    }
    const bool lossy = engine::isLossy(settings.format);
    const int bits = lossy ? 16 : engine::bitsOf(depth);
    file.deleteFile(); // the stream would append to an old file
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
    {
        result_.error = "could not write " + path;
        return false;
    }
    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate(rate)
                                   .withNumChannels(numChannels)
                                   .withBitsPerSample(bits)
                                   .withQualityOptionIndex(settings.oggQuality)
                                   .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = format->createWriterFor(stream, writerOptions);
    if (writer == nullptr)
    {
        result_.error = std::string(engine::formatName(settings.format)) + " cannot be written at " +
                        std::to_string(static_cast<int>(rate)) + " Hz, " + std::to_string(bits) + "-bit";
        file.deleteFile();
        return false;
    }

    bool ok = true;
    if (lossy)
    {
        // The encoder takes floats.
        std::vector<const float*> pointers(static_cast<size_t>(numChannels));
        for (size_t at = 0; at < length && ok; at += chunk)
        {
            for (int c = 0; c < numChannels; ++c)
                pointers[static_cast<size_t>(c)] = channels[static_cast<size_t>(c)].data() + at;
            ok = writer->writeFromFloatArrays(
                pointers.data(), numChannels, static_cast<int>(std::min(chunk, length - at)));
        }
    }
    else
    {
        // Quantised here, with dither, rather than by the writer (which truncates):
        // the writers take 32-bit integers left-justified.
        const auto mode = settings.dither;
        std::vector<engine::Quantiser> quantisers;
        for (int c = 0; c < numChannels; ++c)
            quantisers.emplace_back(
                bits, mode, 0x9e3779b97f4a7c15ull + static_cast<std::uint64_t>(c) * 7919u);
        const int shift = 32 - bits;
        std::vector<std::vector<int>> buffers(static_cast<size_t>(numChannels), std::vector<int>(chunk));
        std::vector<const int*> pointers(static_cast<size_t>(numChannels) + 1, nullptr); // null-terminated
        for (size_t at = 0; at < length && ok; at += chunk)
        {
            const auto n = std::min(chunk, length - at);
            for (int c = 0; c < numChannels; ++c)
            {
                auto& q = quantisers[static_cast<size_t>(c)];
                const auto& in = channels[static_cast<size_t>(c)];
                auto& out = buffers[static_cast<size_t>(c)];
                for (size_t i = 0; i < n; ++i)
                    out[i] = static_cast<int>(q.process(in[at + i])) * (1 << shift);
                pointers[static_cast<size_t>(c)] = out.data();
            }
            ok = writer->write(pointers.data(), static_cast<int>(n));
        }
    }
    writer.reset(); // finishes the file (header sizes, encoder tail)
    if (!ok)
    {
        result_.error = "write failed: " + path;
        file.deleteFile();
        return false;
    }
    return true;
}

} // namespace northlight
