#include "engine/Peaks.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace northlight::engine;

namespace
{
// Channel 0: a sine with amplitude rising linearly over the file. Channel 1: a
// single spike of +0.9 at sample 100000, -0.6 at 100001, silence elsewhere.
class TestReader final : public StreamReader
{
public:
    explicit TestReader(std::int64_t length) : length_(length) {}
    int numChannels() const noexcept override { return 2; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return 48000.0; }
    bool read(std::int64_t start, int numSamples, float* const* dest) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = start + i;
            const double amp = static_cast<double>(s) / static_cast<double>(length_);
            dest[0][i] = s < length_ ? static_cast<float>(amp * std::sin(s * 0.05)) : 0.0f;
            dest[1][i] = s == 100000 ? 0.9f : (s == 100001 ? -0.6f : 0.0f);
        }
        return true;
    }

private:
    std::int64_t length_;
};

// Decoded blocks [first, last) of `reader`, as the peak store would read them.
std::vector<std::shared_ptr<const SampleBlock>>
decode(TestReader& reader, std::int64_t first, std::int64_t last)
{
    std::vector<std::shared_ptr<const SampleBlock>> blocks;
    for (auto i = first; i < last; ++i)
    {
        auto block = std::make_shared<SampleBlock>();
        block->index = i;
        const auto start = i * SampleBlock::samplesPerBlock;
        const auto count = std::min(SampleBlock::samplesPerBlock, reader.lengthSamples() - start);
        block->channels.assign(2, std::vector<float>(static_cast<size_t>(count)));
        float* dest[] = {block->channels[0].data(), block->channels[1].data()};
        reader.read(start, static_cast<int>(count), dest);
        blocks.push_back(std::move(block));
    }
    return blocks;
}
} // namespace

TEST_CASE("Peaks build every level with the expected bin counts", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = PeakData::build(reader);
    CHECK(peaks.numChannels() == 2);
    CHECK(peaks.lengthSamples() == 300000);
    CHECK(peaks.levelCount() == PeakData::defaultLevelCount);
    CHECK(peaks.level(0, 0).samplesPerBin == 256);
    CHECK(peaks.level(0, 0).minimum.size() == (300000 + 255) / 256);
    CHECK(peaks.level(0, 1).samplesPerBin == 1024);
    CHECK(peaks.level(0, 5).samplesPerBin == 256 * 1024);
    CHECK(peaks.level(0, 5).minimum.size() == 2);
}

TEST_CASE("A lookup finds a spike at every zoom level", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = PeakData::build(reader);

    // Fine span around the spike.
    auto r = peaks.lookup(1, 99900, 100200);
    CHECK(r.maximum == 0.9f);
    CHECK(r.minimum == -0.6f);

    // Whole file at the coarsest level.
    r = peaks.lookup(1, 0, 300000);
    CHECK(r.maximum == 0.9f);
    CHECK(r.minimum == -0.6f);

    // A span that does not include the spike is silent.
    r = peaks.lookup(1, 0, 90000);
    CHECK(r.maximum == 0.0f);
    CHECK(r.minimum == 0.0f);

    // Out of range reads as silence.
    r = peaks.lookup(1, 300000, 400000);
    CHECK(r.maximum == 0.0f);
    r = peaks.lookup(1, -100, 0);
    CHECK(r.maximum == 0.0f);
}

TEST_CASE("Coarse levels bound the fine levels", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = PeakData::build(reader);
    for (int l = 1; l < peaks.levelCount(); ++l)
    {
        const auto& fine = peaks.level(0, l - 1);
        const auto& coarse = peaks.level(0, l);
        for (size_t b = 0; b < coarse.minimum.size(); ++b)
            for (size_t f = b * PeakData::levelRatio;
                 f < std::min(fine.minimum.size(), (b + 1) * PeakData::levelRatio);
                 ++f)
            {
                CHECK(coarse.minimum[b] <= fine.minimum[f]);
                CHECK(coarse.maximum[b] >= fine.maximum[f]);
            }
    }
    // Amplitude grows along the file, so the last coarse bin is louder than the first.
    const auto& top = peaks.level(0, peaks.levelCount() - 1);
    CHECK(top.maximum.back() > top.maximum.front());
}

TEST_CASE("Peaks round-trip through the cache format and reject garbage", "[engine][peaks]")
{
    TestReader reader(50000);
    const auto peaks = PeakData::build(reader, 64, 3);
    const auto bytes = peaks.serialise();

    PeakData restored;
    REQUIRE(PeakData::deserialise(bytes, restored));
    CHECK(restored.numChannels() == 2);
    CHECK(restored.lengthSamples() == 50000);
    CHECK(restored.levelCount() == 3);
    CHECK(restored.level(1, 0).samplesPerBin == 64);
    CHECK(restored.level(0, 2).maximum == peaks.level(0, 2).maximum);
    CHECK(restored.level(1, 0).minimum == peaks.level(1, 0).minimum);

    PeakData bad;
    CHECK_FALSE(PeakData::deserialise("not peaks", bad));
    CHECK_FALSE(PeakData::deserialise(bytes.substr(0, bytes.size() / 2), bad));
}

TEST_CASE("Peak building can be aborted through the progress callback", "[engine][peaks]")
{
    TestReader reader(300000);
    int calls = 0;
    const auto peaks = PeakData::build(reader, 256, 3, [&](double) { return ++calls < 2; });
    CHECK(peaks.empty());
    CHECK(calls == 2);
}

TEST_CASE("A waveform view reads decoded samples for spans finer than the peaks", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = std::make_shared<const PeakData>(PeakData::build(reader));

    // Without samples, a one-sample span shows the whole 256-sample bin around it.
    const WaveformView coarse(peaks, 0, {});
    auto r = coarse.lookup(1, 100010, 100011);
    CHECK(r.maximum == 0.9f);
    CHECK(r.minimum == -0.6f);

    // With samples it shows just that sample (and the one before, to join columns).
    const WaveformView fine(peaks, 0, decode(reader, 0, 5));
    r = fine.lookup(1, 100010, 100011);
    CHECK(r.maximum == 0.0f);
    CHECK(r.minimum == 0.0f);
    r = fine.lookup(1, 100000, 100001);
    CHECK(r.maximum == 0.9f);
    CHECK(r.minimum == 0.0f);
    r = fine.lookup(1, 100001, 100002);
    CHECK(r.maximum == 0.9f);
    CHECK(r.minimum == -0.6f);

    // Spans of a whole peak bin or more still come from the peaks.
    r = fine.lookup(1, 99000, 99000 + PeakData::defaultBaseSamplesPerBin);
    CHECK(r.maximum == peaks->lookup(1, 99000, 99000 + PeakData::defaultBaseSamplesPerBin).maximum);
}

TEST_CASE("A waveform view lookup spans block boundaries exactly", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = std::make_shared<const PeakData>(PeakData::build(reader));
    const WaveformView view(peaks, 0, decode(reader, 0, 5));

    const auto boundary = SampleBlock::samplesPerBlock;
    std::vector<float> a(1), b(1);
    float* pa[] = {a.data(), b.data()};
    float expectedMin = 1.0f, expectedMax = -1.0f;
    for (auto s = boundary - 3; s < boundary + 2; ++s)
    {
        reader.read(s, 1, pa);
        expectedMin = std::min(expectedMin, a[0]);
        expectedMax = std::max(expectedMax, a[0]);
    }
    const auto r = view.lookup(0, boundary - 2, boundary + 2);
    CHECK(r.minimum == expectedMin);
    CHECK(r.maximum == expectedMax);
}

TEST_CASE("A waveform view falls back to peaks where a block is missing", "[engine][peaks]")
{
    TestReader reader(300000);
    const auto peaks = std::make_shared<const PeakData>(PeakData::build(reader));
    auto blocks = decode(reader, 0, 5);
    blocks[1] = nullptr; // the spike at 100000 lives in block 1
    const WaveformView view(peaks, 0, std::move(blocks));
    CHECK_FALSE(view.hasSamples(100010, 100011));
    CHECK(view.hasSamples(10, 11));
    const auto r = view.lookup(1, 100010, 100011);
    CHECK(r.maximum == 0.9f); // the coarse bin
}

TEST_CASE("Block spans cover the range, one sample early, clamped to the file", "[engine][peaks]")
{
    const auto n = SampleBlock::samplesPerBlock;
    auto span = blocksFor(0, 10, 300000);
    CHECK(span.first == 0);
    CHECK(span.last == 1);
    span = blocksFor(n, n + 10, 300000); // needs sample n - 1 too
    CHECK(span.first == 0);
    CHECK(span.last == 2);
    span = blocksFor(-500, 1000000, 300000);
    CHECK(span.first == 0);
    CHECK(span.last == (300000 - 1) / n + 1);
    span = blocksFor(400000, 500000, 300000);
    CHECK(span.first == span.last);
}

TEST_CASE("A waveform view with no peaks is empty and reads silence", "[engine][peaks]")
{
    const WaveformView view;
    CHECK(view.empty());
    CHECK(view.numChannels() == 0);
    const auto r = view.lookup(0, 0, 10);
    CHECK(r.maximum == 0.0f);
    CHECK(r.minimum == 0.0f);
}
