#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Delay.h"
#include "engine/Insert.h"
#include "engine/Snapshot.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
// Every file is a single impulse at sample 0.
class ImpulseLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        std::vector<float> impulse(10000, 0.0f);
        impulse[0] = 1.0f;
        return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{impulse}, rate);
    }
};

// "test.delay<N>": delays by N samples and reports N of latency, like a lookahead
// processor would. "test.phantom<N>": reports N but delays nothing (a plugin that
// lies), to show the compensation follows the report.
class LatentInsert final : public engine::InsertProcessor
{
public:
    LatentInsert(std::string type, int latency, bool really)
        : type_(std::move(type)), latency_(latency), really_(really)
    {
    }
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return none_; }
    void prepare(double, int) override { line_.setDelay(really_ ? latency_ : 0); }
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    void process(float* l, float* r, int n) noexcept override
    {
        if (isBypassed())
            return;
        std::vector<float>& sl = scratchL_;
        std::vector<float>& sr = scratchR_;
        line_.process(l, r, sl.data(), sr.data(), n);
        std::copy(sl.begin(), sl.begin() + n, l);
        std::copy(sr.begin(), sr.begin() + n, r);
    }
    int latencySamples() const noexcept override { return latency_; }
    void setLatency(int latency) noexcept { latency_ = latency; }

private:
    std::string type_;
    int latency_;
    bool really_;
    std::vector<engine::ParameterInfo> none_;
    engine::DelayLine line_;
    std::vector<float> scratchL_ = std::vector<float>(8192), scratchR_ = std::vector<float>(8192);
};

class LatentFactory final : public engine::InsertFactory
{
public:
    std::vector<std::string> typeNames() const override { return {}; }
    std::unique_ptr<engine::InsertProcessor> create(const std::string& type) const override
    {
        if (type.starts_with("test.delay"))
            return std::make_unique<LatentInsert>(type, std::stoi(type.substr(10)), true);
        if (type.starts_with("test.phantom"))
            return std::make_unique<LatentInsert>(type, std::stoi(type.substr(12)), false);
        return nullptr;
    }
};

const LatentFactory latentFactory;
constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f);

struct Harness
{
    ImpulseLoader loader;
    engine::AudioEngine engine{loader, latentFactory, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    void play(const core::Project& p)
    {
        engine.setProject(p);
        engine.play();
        engine.process(outputs, 2, blockSize);
    }
    int peakIndex() const
    {
        int best = 0;
        for (int i = 1; i < blockSize; ++i)
            if (std::abs(left[static_cast<size_t>(i)]) > std::abs(left[static_cast<size_t>(best)]))
                best = i;
        return best;
    }
};

core::Track impulseTrack(core::Id id, const char* insertType = nullptr)
{
    core::Track t;
    t.id = id;
    t.clips.push_back({id * 100, "c", "x.wav", 0, 10000, 0, 1.0f});
    if (insertType != nullptr)
        t.inserts.push_back({id * 100 + 1, insertType, false, {}, {}});
    return t;
}
} // namespace

TEST_CASE("A delay line delays by its length and a zero length passes through", "[engine][delay]")
{
    engine::DelayLine line;
    std::vector<float> in(8, 0.0f), out(8, 9.0f);
    in[0] = 1.0f;
    line.process(in.data(), in.data(), out.data(), out.data(), 8); // zero delay copies
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 0.0f);

    line.setDelay(3);
    std::vector<float> l(8, 0.0f), r(8, 0.0f);
    line.process(in.data(), in.data(), l.data(), r.data(), 8);
    CHECK(l[2] == 0.0f);
    CHECK(l[3] == 1.0f);
    CHECK(r[3] == 1.0f);
    CHECK(l[4] == 0.0f);
    // Across blocks: the tail of one block comes out at the start of the next.
    std::vector<float> tail(8, 0.0f);
    tail[7] = 0.5f;
    line.process(tail.data(), tail.data(), l.data(), r.data(), 8);
    std::vector<float> zeros(8, 0.0f);
    line.process(zeros.data(), zeros.data(), l.data(), r.data(), 8);
    CHECK(l[2] == 0.5f);
    line.setDelay(3); // same length: nothing resets
    line.setDelay(1);
    CHECK(line.delay() == 1);
}

TEST_CASE("Tracks with different insert latency arrive at the master together", "[engine][delay]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(impulseTrack(1, "test.delay10"));
    p.tracks.push_back(impulseTrack(2));
    h.play(p);
    CHECK(h.engine.outputLatencySamples() == 10);
    CHECK(h.peakIndex() == 10);
    CHECK(h.left[10] == Approx(2.0f * centre)); // both impulses, mono at centre
    CHECK(h.left[0] == Approx(0.0f));
}

TEST_CASE("Compensation follows the reported latency, not the actual delay", "[engine][delay]")
{
    // A plugin that reports 10 but delays nothing is shifted like one that does:
    // the host can only trust the report. (Its own impulse lands early, as it should.)
    Harness h;
    core::Project p;
    p.tracks.push_back(impulseTrack(1, "test.phantom10"));
    p.tracks.push_back(impulseTrack(2));
    h.play(p);
    CHECK(h.engine.outputLatencySamples() == 10);
    CHECK(h.left[0] == Approx(centre));  // the phantom's impulse, undelayed
    CHECK(h.left[10] == Approx(centre)); // the other track, held back 10
}

TEST_CASE("Bus chains add to the paths through them and direct paths wait", "[engine][delay]")
{
    Harness h;
    core::Project p;
    auto a = impulseTrack(1, "test.delay10");
    a.outputBus = 50;
    p.tracks.push_back(a);
    p.tracks.push_back(impulseTrack(2));
    core::Bus bus;
    bus.id = 50;
    bus.inserts.push_back({51, "test.delay5", false, {}, {}});
    p.buses.push_back(bus);
    h.play(p);
    CHECK(h.engine.outputLatencySamples() == 15);
    CHECK(h.left[15] == Approx(2.0f * centre));
    for (int i = 0; i < 15; ++i)
        CHECK(h.left[static_cast<size_t>(i)] == Approx(0.0f));
}

TEST_CASE("A send and the direct output of one track stay aligned at the master", "[engine][delay]")
{
    Harness h;
    core::Project p;
    auto a = impulseTrack(1);
    a.sends.push_back({50, 1.0f, true}); // pre-fader, full level
    p.tracks.push_back(a);
    core::Bus bus;
    bus.id = 50;
    bus.inserts.push_back({51, "test.delay7", false, {}, {}});
    p.buses.push_back(bus);
    h.play(p);
    CHECK(h.engine.outputLatencySamples() == 7);
    CHECK(h.left[7] == Approx(2.0f * centre)); // direct (held 7) plus via the bus
    CHECK(h.left[0] == Approx(0.0f));

    // Bypassing the bus insert removes its latency and the compensation with it.
    p.buses[0].inserts[0].bypass = true;
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.engine.outputLatencySamples() == 0);
    CHECK(h.left[0] == Approx(2.0f * centre));
}

TEST_CASE("Compensation delay lines survive a recompile", "[engine][delay]")
{
    // The impulse is in flight inside track 2's compensation delay when a fader
    // move recompiles the snapshot; it must still come out on time.
    Harness h;
    core::Project p;
    p.tracks.push_back(impulseTrack(1, "test.delay40"));
    p.tracks.push_back(impulseTrack(2));
    h.engine.setProject(p);
    h.engine.play();
    std::vector<float> l(16), r(16);
    float* out[2] = {l.data(), r.data()};
    h.engine.process(out, 2, 16); // impulse enters the delay line
    p.tracks[1].gain = 0.5f;
    h.engine.setProject(p); // recompile with the same graph
    h.engine.process(out, 2, 16);
    h.engine.process(h.outputs, 2, blockSize);          // samples 32..95: expect it at 40 -> index 8
    CHECK(h.left[8] == Approx(0.5f * centre + centre)); // track 2 at its new gain, track 1 through its insert
    CHECK(h.left[0] == Approx(0.0f));
}

TEST_CASE("A changed insert latency is noticed so the session can recompile", "[engine][delay]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(impulseTrack(1, "test.delay10"));
    h.engine.setProject(p);
    CHECK_FALSE(h.engine.insertLatencyChanged());
    auto live = std::dynamic_pointer_cast<LatentInsert>(h.engine.insertInstance(101));
    REQUIRE(live != nullptr);
    live->setLatency(20);
    CHECK(h.engine.insertLatencyChanged());
    h.engine.setProject(p);
    CHECK_FALSE(h.engine.insertLatencyChanged());
    CHECK(h.engine.outputLatencySamples() == 20);
}

TEST_CASE("A partly dry insert keeps its dry half aligned with the wet half", "[engine][delay]")
{
    // mix 0.5 through a 10-sample delay insert: the dry copy is held back 10 too,
    // so one impulse of full height comes out at 10, nothing at 0.
    Harness h;
    core::Project p;
    auto a = impulseTrack(1, "test.delay10");
    a.inserts[0].mix = 0.5f;
    p.tracks.push_back(a);
    h.play(p);
    CHECK(h.left[0] == Approx(0.0f));
    CHECK(h.left[10] == Approx(centre));

    // Fully dry: the insert's latency still counts (it is still in the chain) but
    // the signal is the input, delayed to match.
    p.tracks[0].inserts[0].mix = 0.0f;
    h.engine.setProject(p);
    h.engine.locate(0);
    h.engine.process(h.outputs, 2, blockSize);
    CHECK(h.left[10] == Approx(centre));
    CHECK(h.engine.outputLatencySamples() == 10);
}
