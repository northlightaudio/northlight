#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Insert.h"
#include "engine/Snapshot.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
// "loud.wav" is a constant 0.8, "quiet.wav" a constant 0.05, anything else an
// impulse at sample 0.
class LevelLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string& path, double rate, std::string&) override
    {
        std::vector<float> data(20000, 0.0f);
        if (path == "loud.wav")
            std::fill(data.begin(), data.end(), 0.8f);
        else if (path == "quiet.wav")
            std::fill(data.begin(), data.end(), 0.05f);
        else
            data[0] = 1.0f;
        return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{data}, rate);
    }
};

// "test.key": replaces its audio with the key signal (so a test can see what the
// slot was fed), or silence without one. Built-ins come from the real factory.
class KeyInsert final : public engine::InsertProcessor
{
public:
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return none_; }
    void prepare(double, int) override {}
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    bool acceptsSidechain() const noexcept override { return true; }
    void process(float* l, float* r, int n) noexcept override
    {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
    }
    void
    processWithSidechain(float* l, float* r, const float* keyL, const float* keyR, int n) noexcept override
    {
        std::copy(keyL, keyL + n, l);
        std::copy(keyR, keyR + n, r);
    }

private:
    std::string type_ = "test.key";
    std::vector<engine::ParameterInfo> none_;
};

// "test.latent": a pure delay of `latency` samples that reports it, so the
// compiler compensates for it.
class LatentInsert final : public engine::InsertProcessor
{
public:
    static constexpr int latency = 32;
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return none_; }
    void prepare(double, int) override {}
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    int latencySamples() const noexcept override { return latency; }
    void process(float* l, float* r, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            std::swap(l[i], left_[static_cast<size_t>(position_)]);
            std::swap(r[i], right_[static_cast<size_t>(position_)]);
            position_ = (position_ + 1) % latency;
        }
    }

private:
    std::string type_ = "test.latent";
    std::vector<engine::ParameterInfo> none_;
    std::vector<float> left_ = std::vector<float>(latency), right_ = std::vector<float>(latency);
    int position_ = 0;
};

class KeyFactory final : public engine::InsertFactory
{
public:
    std::vector<std::string> typeNames() const override { return {}; }
    std::unique_ptr<engine::InsertProcessor> create(const std::string& type) const override
    {
        if (type == "test.key")
            return std::make_unique<KeyInsert>();
        if (type == "test.latent")
            return std::make_unique<LatentInsert>();
        return builtIns_.create(type);
    }

private:
    plugins::BuiltInFactory builtIns_;
};

const KeyFactory keyFactory;
constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f);

struct Harness
{
    LevelLoader loader;
    engine::AudioEngine engine{loader, keyFactory, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    void play(const core::Project& p, int blocks = 1)
    {
        engine.setProject(p);
        engine.play();
        for (int i = 0; i < blocks; ++i)
            engine.process(outputs, 2, blockSize);
    }
};

core::Track
track(core::Id id, const char* file, const char* insertType = nullptr, core::Id key = core::invalidId)
{
    core::Track t;
    t.id = id;
    if (file != nullptr)
        t.clips.push_back({id * 100, "c", file, 0, 20000, 0, 1.0f});
    if (insertType != nullptr)
    {
        core::Insert i;
        i.id = id * 100 + 1;
        i.type = insertType;
        i.params = plugins::defaultParameters(insertType);
        i.sidechain = key;
        t.inserts.push_back(i);
    }
    return t;
}
} // namespace

TEST_CASE("A keyed track renders after its source even when it comes first in the project",
          "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, nullptr, "test.key", 2)); // silent, keyed from track 2
    p.tracks.push_back(track(2, "impulse.wav"));
    h.play(p);
    // Track 1 outputs track 2's pre-fader signal: both impulses reach the master.
    CHECK(h.left[0] == Approx(2.0f * centre));
    REQUIRE(h.engine.snapshotForTest() != nullptr);
    const auto& levels = h.engine.snapshotForTest()->trackLevels;
    REQUIRE(levels.size() == 2);
    CHECK(levels[0] == std::vector<int>{1});
    CHECK(levels[1] == std::vector<int>{0});
}

TEST_CASE("The key is the source's pre-fader signal; its fader does not matter but mute wins",
          "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, nullptr, "test.key", 2));
    auto source = track(2, "impulse.wav");
    source.gain = 0.0f; // fader down: the key still carries the signal
    p.tracks.push_back(source);
    h.play(p);
    CHECK(h.left[0] == Approx(centre)); // only track 1's copy of the key (track 2 itself is at -inf)

    p.tracks[1].mute = true; // muted: silent everywhere, the key included
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[0] == Approx(0.0f));
}

TEST_CASE("Keys that form a cycle are dropped and both tracks still render", "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, "impulse.wav", "test.key", 2));
    p.tracks.push_back(track(2, "impulse.wav", "test.key", 1));
    h.play(p);
    CHECK(h.engine.snapshotForTest()->trackLevels.size() == 1);
    CHECK(h.left[0] == Approx(0.0f)); // unkeyed, the test insert outputs silence
}

TEST_CASE("A bus insert can key from a track or from a bus that renders before it", "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, "impulse.wav"));
    auto quiet = track(2, nullptr);
    quiet.outputBus = 50; // feeds bus X (silent)
    p.tracks.push_back(quiet);
    core::Bus x;
    x.id = 50;
    core::Insert keyed;
    keyed.id = 51;
    keyed.type = "test.key";
    keyed.sidechain = 1; // from track 1
    x.inserts.push_back(keyed);
    p.buses.push_back(x);
    h.play(p);
    CHECK(h.left[0] == Approx(2.0f * centre)); // track 1 direct + bus X carrying the key

    // Bus Y (fed by track 1) keys bus X: X must come after Y.
    core::Bus y;
    y.id = 60;
    p.buses.insert(p.buses.begin(), y); // Y before X in project order
    p.tracks[0].outputBus = 60;
    p.buses[1].inserts[0].sidechain = 60;
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    const auto& order = h.engine.snapshotForTest()->busOrder;
    REQUIRE(order.size() == 2);
    CHECK(order[0] == 0);                      // Y (index 0) first
    CHECK(h.left[0] == Approx(2.0f * centre)); // Y to master + X carrying Y's key

    // X feeding Y while keying from Y is a cycle: the key is dropped, no crash.
    p.buses[1].outputBus = 60;
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[0] == Approx(centre)); // only track 1 through Y; X is silent unkeyed
}

TEST_CASE("A compressor keyed from a loud track ducks a quiet one", "[engine][sidechain][compressor]")
{
    const auto run = [](bool keyed)
    {
        Harness h;
        core::Project p;
        p.tracks.push_back(track(1, "loud.wav"));
        auto quiet = track(2, "quiet.wav", plugins::types::compressor.c_str(), keyed ? 1 : core::invalidId);
        quiet.inserts[0].params[0] = -20.0f; // threshold: above the quiet track's -26 dB
        quiet.inserts[0].params[1] = 20.0f;  // ratio
        quiet.inserts[0].params[2] = 0.1f;   // attack ms
        quiet.inserts[0].params[5] = 0.0f;   // no makeup
        p.tracks.push_back(quiet);
        h.play(p, 40);                                // ~53 ms, well past the attack
        return h.left[blockSize - 1] - 0.8f * centre; // the quiet track's share
    };
    const float unkeyed = run(false);
    const float keyed = run(true);
    CHECK(unkeyed == Approx(0.05f * centre).margin(0.003f)); // under the threshold on its own: untouched
    CHECK(keyed < unkeyed * 0.5f);                           // the loud key drives heavy reduction
}

TEST_CASE("A post-fader key follows the source's fader, and mute still wins", "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, nullptr, "test.key", 2));
    p.tracks[0].inserts[0].sidechainPostFader = true;
    auto source = track(2, "impulse.wav");
    source.gain = 0.5f;
    p.tracks.push_back(source);
    h.play(p);
    // Track 2 at half gain, plus track 1's copy of the key, also at half.
    CHECK(h.left[0] == Approx(0.5f * centre + 0.5f * centre));

    p.tracks[1].gain = 0.0f; // fader down: the post-fader key goes with it
    h.engine.setProject(p);
    h.engine.locate(0);
    for (int i = 0; i < 4; ++i) // let the fader ramp settle
        h.engine.process(h.outputs, 2, blockSize);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[0] == Approx(0.0f).margin(1e-6f));

    p.tracks[1].gain = 1.0f;
    p.tracks[1].mute = true;
    h.engine.setProject(p);
    h.engine.locate(0);
    for (int i = 0; i < 4; ++i)
        h.engine.process(h.outputs, 2, blockSize);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[0] == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("A bus insert can key from a bus after its fader", "[engine][sidechain]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, "impulse.wav"));
    p.tracks[0].outputBus = 60; // into bus Y
    core::Bus y;
    y.id = 60;
    y.gain = 0.25f;
    p.buses.push_back(y);
    core::Bus x;
    x.id = 50;
    core::Insert keyed;
    keyed.id = 51;
    keyed.type = "test.key";
    keyed.sidechain = 60;
    keyed.sidechainPostFader = true;
    x.inserts.push_back(keyed);
    p.buses.push_back(x);
    h.play(p);
    // Y to the master at a quarter, plus X carrying Y's post-fader key.
    CHECK(h.left[0] == Approx(0.25f * centre + 0.25f * centre));
    REQUIRE(h.engine.snapshotForTest() != nullptr);
    CHECK(h.engine.snapshotForTest()->buses[0].keyPostFader);
    CHECK_FALSE(h.engine.snapshotForTest()->buses[1].keyPostFader);
}

TEST_CASE("An early key is delayed to line up with the audio at its slot", "[engine][sidechain][latency]")
{
    Harness h;
    core::Project p;
    // Track 1: a latent insert, then the key slot. Its audio reaches the key slot
    // 32 samples late, so the key from track 2 (no latency) must wait as long.
    auto keyed = track(1, nullptr, "test.latent");
    core::Insert key;
    key.id = 102;
    key.type = "test.key";
    key.sidechain = 2;
    keyed.inserts.push_back(key);
    p.tracks.push_back(keyed);
    p.tracks.push_back(track(2, "impulse.wav"));
    h.play(p);
    const auto* snapshot = h.engine.snapshotForTest();
    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->tracks[0].insertKeyDelay.size() == 2);
    CHECK(snapshot->tracks[0].insertKeyDelay[0] == nullptr);
    REQUIRE(snapshot->tracks[0].insertKeyDelay[1] != nullptr);
    CHECK(snapshot->tracks[0].insertKeyDelay[1]->delay() == LatentInsert::latency);
    // Track 2's own output is compensated to the master by the same amount, so
    // both impulses land together at sample 32.
    CHECK(h.left[0] == Approx(0.0f).margin(1e-6f));
    CHECK(h.left[LatentInsert::latency] == Approx(2.0f * centre));

    // The key slot first in the chain: nothing to wait for.
    std::swap(p.tracks[0].inserts[0], p.tracks[0].inserts[1]);
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.engine.snapshotForTest()->tracks[0].insertKeyDelay[0] == nullptr);
    // Now the key copy runs through the latent insert after the key slot.
    CHECK(h.left[LatentInsert::latency] == Approx(2.0f * centre));
}

TEST_CASE("A key from a source with more latency is not delayed", "[engine][sidechain][latency]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(track(1, nullptr, "test.key", 2));
    p.tracks.push_back(track(2, "impulse.wav", "test.latent"));
    h.play(p);
    const auto* snapshot = h.engine.snapshotForTest();
    REQUIRE(snapshot != nullptr);
    CHECK(snapshot->tracks[0].insertKeyDelay[0] == nullptr);
    // The key is used as it arrives: track 1's copy is 32 samples late in its
    // own buffer and then delayed another 32 to meet track 2 at the master.
    CHECK(h.left[LatentInsert::latency] == Approx(centre));
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[2 * LatentInsert::latency - blockSize] == Approx(centre));
}
