#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Snapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
// Every file is a mono constant of 0.5 (so sums are easy to reason about).
class ConstantLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{std::vector<float>(100000, 0.5f)}, rate);
    }
};

constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f); // constant-power centre pan

struct Harness
{
    ConstantLoader loader;
    engine::AudioEngine engine{loader, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    void play(const core::Project& p)
    {
        engine.setProject(p);
        engine.play();
        engine.process(outputs, 2, blockSize);
    }
};

core::Track trackWithClip(core::Id id)
{
    core::Track t;
    t.id = id;
    t.clips.push_back({id * 100, "c", "x.wav", 0, 100000, 0, 1.0f});
    return t;
}
} // namespace

TEST_CASE("A track routed to a bus goes through the bus fader and the master", "[engine][mixer]")
{
    core::Project p;
    core::Bus bus;
    bus.id = 10;
    bus.gain = 0.5f;
    p.buses.push_back(bus);
    auto t = trackWithClip(1);
    t.gain = 0.8f;
    t.outputBus = 10;
    p.tracks.push_back(t);
    p.master.gain = 0.25f;

    Harness h;
    h.play(p);
    CHECK(h.left[10] == Approx(0.5f * centre * 0.8f * 0.5f * 0.25f).margin(1e-6f));
    CHECK(h.right[10] == Approx(h.left[10]).margin(1e-6f));
}

TEST_CASE("Pre- and post-fader sends tap before and after the channel gain", "[engine][mixer]")
{
    core::Project p;
    core::Bus fx;
    fx.id = 10;
    p.buses.push_back(fx);
    auto t = trackWithClip(1);
    t.gain = 0.5f;
    t.outputBus = core::invalidId; // master directly
    p.tracks.push_back(t);

    SECTION("post-fader")
    {
        p.tracks[0].sends.push_back({10, 0.5f, false});
        Harness h;
        h.play(p);
        // master gets track post (0.5c * 0.5) + bus (0.5c * 0.5 * 0.5)
        CHECK(h.left[5] == Approx(0.5f * centre * 0.5f + 0.5f * centre * 0.5f * 0.5f).margin(1e-6f));
    }
    SECTION("pre-fader")
    {
        p.tracks[0].sends.push_back({10, 0.5f, true});
        Harness h;
        h.play(p);
        CHECK(h.left[5] == Approx(0.5f * centre * 0.5f + 0.5f * centre * 0.5f).margin(1e-6f));
    }
}

TEST_CASE("Solo in place keeps the soloed track's bus path and silences the rest", "[engine][mixer]")
{
    core::Project p;
    core::Bus drums;
    drums.id = 10;
    p.buses.push_back(drums);
    core::Bus other;
    other.id = 11;
    p.buses.push_back(other);
    auto kick = trackWithClip(1);
    kick.outputBus = 10;
    auto snare = trackWithClip(2);
    snare.outputBus = 10;
    auto vocal = trackWithClip(3);
    vocal.outputBus = 11;
    p.tracks = {kick, snare, vocal};

    SECTION("nothing soloed: everything is heard")
    {
        Harness h;
        h.play(p);
        CHECK(h.left[0] == Approx(3.0f * 0.5f * centre).margin(1e-5f));
    }
    SECTION("solo the kick: kick and its bus only")
    {
        p.tracks[0].solo = true;
        Harness h;
        h.play(p);
        CHECK(h.left[0] == Approx(0.5f * centre).margin(1e-6f));
    }
    SECTION("solo the drum bus: both its tracks are heard")
    {
        p.buses[0].solo = true;
        Harness h;
        h.play(p);
        CHECK(h.left[0] == Approx(2.0f * 0.5f * centre).margin(1e-6f));
    }
    SECTION("mute beats solo")
    {
        p.tracks[0].solo = true;
        p.tracks[0].mute = true;
        Harness h;
        h.play(p);
        CHECK(h.left[0] == 0.0f);
    }
}

TEST_CASE("Bus cycles are broken and bus-to-bus routing renders feeders first", "[engine][mixer]")
{
    core::Project p;
    core::Bus a;
    a.id = 10;
    a.outputBus = 11; // a -> b
    a.gain = 0.5f;
    core::Bus b;
    b.id = 11;
    b.outputBus = 10; // b -> a: a cycle. The earlier bus (a) loses its edge and goes to the master.
    b.gain = 0.25f;
    p.buses = {a, b};
    auto t = trackWithClip(1);
    t.outputBus = 11; // track -> b -> a -> master
    p.tracks.push_back(t);

    Harness h;
    h.play(p);
    CHECK(h.left[0] == Approx(0.5f * centre * 0.25f * 0.5f).margin(1e-6f));
    // b must have rendered before a for the signal to arrive at all.
}

TEST_CASE("Phase invert flips a channel", "[engine][mixer]")
{
    core::Project p;
    auto t = trackWithClip(1);
    t.phaseInvert = true;
    p.tracks.push_back(t);
    Harness h;
    h.play(p);
    CHECK(h.left[0] == Approx(-0.5f * centre).margin(1e-6f));
}

TEST_CASE("Meters report post-fader peak and RMS per channel and for the master", "[engine][mixer][meters]")
{
    core::Project p;
    core::Bus bus;
    bus.id = 10;
    bus.gain = 0.5f;
    p.buses.push_back(bus);
    auto t = trackWithClip(1);
    t.gain = 0.5f;
    t.outputBus = 10;
    p.tracks.push_back(t);
    auto muted = trackWithClip(2);
    muted.mute = true;
    p.tracks.push_back(muted);

    Harness h;
    h.play(p);
    auto& meters = h.engine.meters();
    const auto track = meters.take(engine::trackMeterSlot(0));
    CHECK(track.peakLeft == Approx(0.5f * centre * 0.5f).margin(1e-6f));
    CHECK(track.rmsLeft == Approx(0.5f * centre * 0.5f).margin(1e-5f)); // a constant: rms == peak
    const auto silent = meters.take(engine::trackMeterSlot(1));
    CHECK(silent.peakLeft == 0.0f);
    const auto busReading = meters.take(engine::busMeterSlot(p, 0));
    CHECK(busReading.peakRight == Approx(0.5f * centre * 0.5f * 0.5f).margin(1e-6f));
    const auto master = meters.take(engine::masterMeterSlot(p));
    CHECK(master.peakLeft == Approx(0.5f * centre * 0.5f * 0.5f).margin(1e-6f));
    // Peaks hold until taken, then reset.
    CHECK(meters.take(engine::masterMeterSlot(p)).peakLeft == 0.0f);
}
