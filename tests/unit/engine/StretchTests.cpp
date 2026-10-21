#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/AudioEngine.h"
#include "engine/Click.h"
#include "engine/Stretch.h"
#include "engine/TempoDetect.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

std::vector<float> sine(double hz, double sampleRate, std::int64_t length, float level = 0.5f)
{
    std::vector<float> out(static_cast<size_t>(length));
    for (std::int64_t i = 0; i < length; ++i)
        out[static_cast<size_t>(i)] =
            level *
            static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate));
    return out;
}

// Zero crossings per second over [from, to): twice the frequency of a sine.
double frequencyOf(const std::vector<float>& x, size_t from, size_t to, double sampleRate)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        crossings += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
    return crossings / 2.0 / (static_cast<double>(to - from) / sampleRate);
}

double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

// "sine:<hz>:<seconds>" files.
class SineLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource>
    load(const std::string& path, double sampleRate, std::string& error) override
    {
        if (path.rfind("sine:", 0) != 0)
        {
            error = "no such file";
            return nullptr;
        }
        const auto second = path.find(':', 5);
        const double hz = std::stod(path.substr(5, second - 5));
        const double seconds = std::stod(path.substr(second + 1));
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{
                sine(hz, sampleRate, static_cast<std::int64_t>(seconds * sampleRate))},
            sampleRate);
    }
};
} // namespace

TEST_CASE("Stretching keeps the pitch and changes the length", "[engine][stretch]")
{
    const double rate = 48000.0;
    const auto in = sine(440.0, rate, 48000);
    std::vector<float> out(96000, 0.0f);
    std::int64_t lastProgress = 0;
    engine::stretchAudio({in.data()},
                         48000,
                         {out.data()},
                         96000,
                         rate,
                         0.0,
                         [&](std::int64_t done)
                         {
                             CHECK(done >= lastProgress);
                             lastProgress = done;
                         });
    CHECK(lastProgress == 96000);
    CHECK(frequencyOf(out, 10000, 86000, rate) == Approx(440.0).epsilon(0.01));
    CHECK(rms(out, 10000, 86000) == Approx(rms(in, 5000, 43000)).epsilon(0.15));
}

TEST_CASE("Transposing changes the pitch and keeps the length", "[engine][stretch]")
{
    const double rate = 48000.0;
    const auto in = sine(330.0, rate, 48000);
    std::vector<float> out(48000, 0.0f);
    engine::stretchAudio({in.data()}, 48000, {out.data()}, 48000, rate, 12.0);
    CHECK(frequencyOf(out, 6000, 42000, rate) == Approx(660.0).epsilon(0.015));
    std::vector<float> down(48000, 0.0f);
    engine::stretchAudio({in.data()}, 48000, {down.data()}, 48000, rate, -7.0);
    CHECK(frequencyOf(down, 6000, 42000, rate) == Approx(330.0 * std::pow(2.0, -7.0 / 12.0)).epsilon(0.015));
}

TEST_CASE("A stretched clip plays its render through the engine", "[engine][stretch]")
{
    SineLoader loader;
    engine::StretchService stretch(loader, true);
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.setStretchHook(
        [&stretch](const core::AudioClip& clip, double rate, std::shared_ptr<const engine::AudioSource> base)
        { return stretch.sourceFor(clip, rate, base); });
    engine.prepare(48000.0, 256);

    core::Project p;
    core::Track t;
    t.id = 1;
    core::AudioClip clip{2, "tone", "sine:500:2", 4800, 96000, 24000, 1.0f};
    clip.stretch = 1.5; // 1.5 s of timeline from 1 s of source (from 0.5 s in)
    clip.transpose = 5.0;
    clip.length = 72000;
    t.clips.push_back(clip);
    p.tracks.push_back(t);
    p.nextId = 3;
    engine.setProject(p);
    CHECK(stretch.progress(clip, 48000.0) == Approx(1.0));
    engine.play();

    std::vector<float> left(256), right(256), heard;
    float* outputs[2] = {left.data(), right.data()};
    while (engine.position() < 4800 + 72000 + 4800)
    {
        engine.process(outputs, 2, 256);
        heard.insert(heard.end(), left.begin(), left.end());
    }
    // Silent before the clip, the transposed tone inside it, silent after.
    CHECK(rms(heard, 0, 4700) < 1e-6);
    CHECK(frequencyOf(heard, 4800 + 6000, 4800 + 66000, 48000.0) ==
          Approx(500.0 * std::pow(2.0, 5.0 / 12.0)).epsilon(0.015));
    CHECK(rms(heard, 4800 + 6000, 4800 + 66000) > 0.1);
    CHECK(rms(heard, 4800 + 72000 + 100, heard.size()) < 1e-6);
}

TEST_CASE("Background renders play once they are ready", "[engine][stretch]")
{
    SineLoader loader;
    engine::StretchService stretch(loader, false, 2);
    core::AudioClip clip{2, "tone", "sine:200:3", 0, 48000, 0, 1.0f};
    clip.stretch = 0.5;
    std::string error;
    const auto base = loader.load(clip.mediaPath, 48000.0, error);
    const auto source = stretch.sourceFor(clip, 48000.0, base);
    REQUIRE(source != nullptr);
    CHECK(source->lengthSamples() == 72000);
    stretch.waitUntilIdle();
    CHECK_FALSE(stretch.isBusy());
    CHECK(stretch.progress(clip, 48000.0) == Approx(1.0));
    std::vector<float> out(48000);
    source->read(0, 0, out.data(), 48000);
    CHECK(frequencyOf(out, 4000, 44000, 48000.0) == Approx(200.0).epsilon(0.02));
    // Asking again for the same clip shares the render.
    const auto again = stretch.sourceFor(clip, 48000.0, base);
    CHECK(&static_cast<const engine::StretchedSource&>(*again).render() ==
          &static_cast<const engine::StretchedSource&>(*source).render());
}

TEST_CASE("The metronome follows the tempo map", "[engine][click][tempo]")
{
    // 120 BPM in 2/4 for a bar, then 60 BPM from quarter 2; clicks at 0, 24000,
    // 48000 (bar 2, accented), 96000.
    const core::TempoMap map(48000.0, 120.0, {2, 4}, {{2.0, 60.0, false}});
    engine::ClickGenerator click;
    std::vector<float> left(120000, 0.0f), right(120000, 0.0f);
    for (int done = 0; done < 120000; done += 512)
    {
        const int n = std::min(512, 120000 - done);
        click.render(done, n, map, 1.0f, left.data() + done, right.data() + done);
    }
    const auto loud = [&](size_t at)
    { return std::abs(left[at + 5]) > 0.05f || std::abs(left[at + 20]) > 0.05f; };
    CHECK(loud(0));
    CHECK(loud(24000));
    CHECK(loud(48000));
    CHECK(loud(96000));
    CHECK(rms(left, 2000, 23900) < 1e-6);
    CHECK(rms(left, 50000, 95900) < 1e-6);
}

TEST_CASE("Tempos are read from names, beats and lengths", "[engine][tempo]")
{
    CHECK(engine::tempoFromName("Drums 124bpm.wav") == 124.0);
    CHECK(engine::tempoFromName("bass_BPM_98_Gm.aif") == 98.0);
    CHECK(engine::tempoFromName("loop_90_Am.wav") == 90.0);
    CHECK(engine::tempoFromName("Vocal take 3.wav") == 0.0);
    CHECK(engine::tempoFromName("808 kick.wav") == 0.0);
    CHECK(engine::tempoFromLength(4 * 96000, 48000.0) == Approx(120.0)); // 4 bars of 4/4 at 120
    CHECK(engine::tempoFromLength(48000 * 8, 48000.0, 4.0, 90.0) == Approx(120.0));

    // A click track at 128 BPM, eight bars.
    const double rate = 44100.0;
    const double bpm = 128.0;
    const auto length = static_cast<std::int64_t>(32 * 60.0 / bpm * rate);
    std::vector<float> clicks(static_cast<size_t>(length), 0.0f);
    for (int beat = 0; beat < 32; ++beat)
    {
        const auto at = static_cast<size_t>(beat * 60.0 / bpm * rate);
        for (size_t i = 0; i < 600 && at + i < clicks.size(); ++i)
            clicks[at + i] =
                static_cast<float>(std::sin(i * 0.3) * std::exp(-static_cast<double>(i) / 120.0));
    }
    const auto beats = engine::tempoFromBeats({clicks.data()}, length, rate);
    CHECK(beats.bpm == Approx(128.0).margin(1.5));
    const auto guess = engine::guessTempo("Untitled loop.wav", {clicks.data()}, length, rate);
    CHECK(guess.bpm == Approx(128.0).margin(0.01)); // the whole-bar length agrees and wins
    CHECK(guess.source == engine::TempoGuess::Source::length);
}
