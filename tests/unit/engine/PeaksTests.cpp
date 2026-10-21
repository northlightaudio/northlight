#include "engine/Peaks.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

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
