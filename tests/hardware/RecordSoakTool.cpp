// Recording soak: drives the engine's record path offline with N synthetic inputs
// for M minutes of audio at a chosen speed (the writer must keep up with real time
// times the speed), then verifies every file sample-exactly and reports dropouts.
// No audio device, no JUCE: the Volt has four inputs, so the 16-input milestone is
// checked here.
//
//   northlight_record_soak [--inputs 16] [--minutes 30] [--speed 8] [--folder <dir>] [--keep]

#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
const northlight::plugins::BuiltInFactory builtIns;
} // namespace

namespace
{
class NoMediaLoader final : public northlight::engine::MediaLoader
{
public:
    std::shared_ptr<northlight::engine::AudioSource>
    load(const std::string&, double, std::string& error) override
    {
        error = "no media";
        return nullptr;
    }
};

// Input k, sample s: a deterministic pattern that is cheap to generate and check.
float pattern(int input, std::int64_t sample) noexcept
{
    const auto v = static_cast<std::uint32_t>(
        (sample * 2654435761ull + static_cast<std::uint64_t>(input) * 40503ull) & 0xffffu);
    return static_cast<float>(v) / 65536.0f - 0.5f;
}
} // namespace

int main(int argc, char* argv[])
{
    int inputs = 16;
    double minutes = 30.0;
    double speed = 8.0;
    bool keep = false;
    std::filesystem::path folder = std::filesystem::temp_directory_path() / "northlight-soak";
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--inputs")
            inputs = std::stoi(next());
        else if (arg == "--minutes")
            minutes = std::stod(next());
        else if (arg == "--speed")
            speed = std::stod(next());
        else if (arg == "--folder")
            folder = next();
        else if (arg == "--keep")
            keep = true;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    const auto totalSamples = static_cast<std::int64_t>(minutes * 60.0 * sampleRate);
    std::filesystem::create_directories(folder);
    std::printf("Soak: %d inputs, %.1f minutes of audio at %.0fx real time, %s\n",
                inputs,
                minutes,
                speed,
                folder.string().c_str());

    NoMediaLoader loader;
    northlight::engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(sampleRate, blockSize);
    northlight::core::Project project;
    for (int k = 0; k < inputs; ++k)
    {
        northlight::core::Track t;
        t.id = static_cast<northlight::core::Id>(k + 1);
        t.inputChannel = k;
        t.armed = true;
        project.tracks.push_back(t);
    }
    engine.setProject(project);

    auto session = std::make_unique<northlight::engine::RecordSession>(sampleRate);
    std::vector<std::string> paths;
    for (int k = 0; k < inputs; ++k)
    {
        paths.push_back((folder / ("soak-" + std::to_string(k + 1) + ".partial.wav")).string());
        std::remove(paths.back().c_str());
        session->addInput(static_cast<northlight::core::Id>(k + 1), k, paths.back());
    }
    std::string error;
    if (!engine.startRecording(std::move(session), error))
    {
        std::printf("startRecording failed: %s\n", error.c_str());
        return 1;
    }

    std::vector<std::vector<float>> inputBuffers(static_cast<size_t>(inputs), std::vector<float>(blockSize));
    std::vector<const float*> inputPointers;
    for (auto& b : inputBuffers)
        inputPointers.push_back(b.data());
    std::vector<float> left(blockSize), right(blockSize);
    float* outputs[] = {left.data(), right.data()};

    const auto blockDuration = std::chrono::duration<double>(blockSize / sampleRate / speed);
    const auto started = std::chrono::steady_clock::now();
    double maxBlockSeconds = 0.0;
    std::int64_t position = 0;
    std::int64_t blocks = 0;
    while (position < totalSamples)
    {
        for (int k = 0; k < inputs; ++k)
            for (int i = 0; i < blockSize; ++i)
                inputBuffers[static_cast<size_t>(k)][static_cast<size_t>(i)] = pattern(k, position + i);

        const auto t0 = std::chrono::steady_clock::now();
        {
            northlight::rt::ScopedRealtimeSection realtime;
            engine.process(inputPointers.data(), inputs, outputs, 2, blockSize);
        }
        maxBlockSeconds = std::max(
            maxBlockSeconds, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        position += blockSize;
        ++blocks;
        std::this_thread::sleep_until(started +
                                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                          blockDuration * static_cast<double>(blocks)));
        if (blocks % 20000 == 0)
            std::printf("  %.1f min of audio captured\n", static_cast<double>(position) / sampleRate / 60.0);
    }
    engine.stop();
    {
        northlight::rt::ScopedRealtimeSection realtime;
        engine.process(inputPointers.data(), inputs, outputs, 2, blockSize);
    }
    while (!engine.hasRecordingToCollect())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto finished = engine.collectRecording();
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("Captured %lld samples per input in %.1f s; max process() %.3f ms (block deadline %.3f ms)\n",
                static_cast<long long>(position),
                elapsed,
                maxBlockSeconds * 1000.0,
                blockSize / sampleRate * 1000.0);

    int failures = 0;
    std::uint64_t dropped = 0;
    for (const auto& r : finished->results)
    {
        dropped += r.dropped;
        if (!r.ok || r.samplesWritten != position)
        {
            std::printf("  %s: ok=%d samples=%lld (expected %lld)\n",
                        r.path.c_str(),
                        r.ok ? 1 : 0,
                        static_cast<long long>(r.samplesWritten),
                        static_cast<long long>(position));
            ++failures;
        }
    }
    std::printf("Dropped samples (ring overruns): %llu\n", static_cast<unsigned long long>(dropped));

    // Verify every file sample-exactly, streaming through it in chunks.
    std::printf("Verifying %d files...\n", inputs);
    for (int k = 0; k < inputs; ++k)
    {
        const auto info = northlight::engine::readWavInfo(paths[static_cast<size_t>(k)]);
        if (!info.has_value() || info->lengthSamples != position)
        {
            std::printf("  input %d: bad header\n", k + 1);
            ++failures;
            continue;
        }
        std::FILE* f = std::fopen(paths[static_cast<size_t>(k)].c_str(), "rb");
        if (f == nullptr)
        {
            ++failures;
            continue;
        }
        std::fseek(f, 44, SEEK_SET);
        std::vector<float> chunk(1 << 16);
        std::int64_t s = 0;
        bool exact = true;
        while (s < position && exact)
        {
            const auto n = std::fread(chunk.data(), sizeof(float), chunk.size(), f);
            if (n == 0)
                break;
            for (size_t i = 0; i < n && exact; ++i)
                exact = chunk[i] == pattern(k, s + static_cast<std::int64_t>(i));
            s += static_cast<std::int64_t>(n);
        }
        std::fclose(f);
        if (!exact || s != position)
        {
            std::printf("  input %d: content mismatch at sample %lld\n", k + 1, static_cast<long long>(s));
            ++failures;
        }
    }

    if (!keep)
        for (const auto& p : paths)
            std::remove(p.c_str());

    std::printf(failures == 0 && dropped == 0 ? "PASS\n" : "FAIL\n");
    return failures == 0 && dropped == 0 ? 0 : 2;
}
