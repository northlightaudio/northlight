#include "SyntheticSinger.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/VocalListener.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace vocal = plugins::vocal;
using testing::Singer;
using vocal::Sound;

namespace
{
struct Heard
{
    double time = 0.0; // the frame's end, seconds
    vocal::Hearing hearing;
};

std::vector<Heard> listen(const std::vector<float>& audio, double rate, float gapSeconds = 0.25f)
{
    vocal::Listener listener;
    listener.prepare(rate);
    listener.setGapSeconds(gapSeconds);
    std::vector<Heard> heard;
    for (size_t n = 0; n < audio.size(); ++n)
        if (listener.push(audio[n]))
            heard.push_back({static_cast<double>(n + 1) / rate, listener.hearing()});
    return heard;
}

// The share of frames in [from, to) heard as `sound`.
double share(const std::vector<Heard>& heard, double from, double to, Sound sound)
{
    int total = 0, match = 0;
    for (const auto& h : heard)
        if (h.time >= from && h.time < to)
        {
            ++total;
            match += h.hearing.sound == sound ? 1 : 0;
        }
    return total > 0 ? static_cast<double>(match) / total : 0.0;
}

std::vector<double> lineEnds(const std::vector<Heard>& heard)
{
    std::vector<double> ends;
    for (const auto& h : heard)
        if (h.hearing.lineEnd)
            ends.push_back(h.time);
    return ends;
}
} // namespace

TEST_CASE("The Listener hears a sung vowel as voiced, at its pitch", "[plugins][listener]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const float hz : {110.0f, 220.0f, 440.0f, 880.0f})
        {
            Singer singer(rate);
            singer.rest(0.1).vowel(0.6, hz);
            const auto heard = listen(singer.audio(), rate);
            INFO("rate " << rate << ", " << hz << " Hz");
            CHECK(share(heard, 0.2, 0.65, Sound::voiced) > 0.95);
            std::vector<float> pitches;
            for (const auto& h : heard)
                if (h.time >= 0.2 && h.time < 0.65 && h.hearing.pitchHz > 0.0f)
                    pitches.push_back(h.hearing.pitchHz);
            REQUIRE_FALSE(pitches.empty());
            std::nth_element(pitches.begin(), pitches.begin() + pitches.size() / 2, pitches.end());
            const float median = pitches[pitches.size() / 2];
            CHECK(std::abs(1200.0f * std::log2(median / hz)) < 20.0f);
        }
}

TEST_CASE("The Listener hears silence as silence and ends no line in it", "[plugins][listener]")
{
    Singer singer(48000.0);
    singer.rest(2.0);
    const auto heard = listen(singer.audio(), 48000.0);
    CHECK(share(heard, 0.0, 2.0, Sound::silence) == 1.0);
    CHECK(lineEnds(heard).empty());
}

TEST_CASE("The Listener hears an s and a t as consonants, and the s as sibilant", "[plugins][listener]")
{
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.4, 220.0f);
    const double sAt = singer.now();
    singer.s(0.15).vowel(0.4, 247.0f);
    const double tAt = singer.now();
    singer.t().vowel(0.4, 262.0f);
    const auto heard = listen(singer.audio(), 48000.0);

    CHECK(share(heard, sAt + 0.03, sAt + 0.13, Sound::consonant) > 0.9);
    float sibilance = 1.0f;
    for (const auto& h : heard)
        if (h.time >= sAt + 0.03 && h.time < sAt + 0.13)
            sibilance = std::min(sibilance, h.hearing.sibilance);
    CHECK(sibilance > 0.6f);
    CHECK(share(heard, tAt + 0.005, tAt + 0.03, Sound::consonant) > 0.5);
    // The vowels either side stay voiced and not sibilant.
    CHECK(share(heard, 0.25, 0.45, Sound::voiced) > 0.95);
    for (const auto& h : heard)
        if (h.time >= 0.25 && h.time < 0.45)
            CHECK(h.hearing.sibilance < 0.3f);
}

TEST_CASE("The Listener hears a breath before a line as a breath, and it ends no line", "[plugins][listener]")
{
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.8, 220.0f, -18.0f).rest(0.15);
    const double breathAt = singer.now();
    singer.breath(0.35, -38.0f).rest(0.05).vowel(0.5, 247.0f);
    // A gap longer than the pause before the breath: if the breath counted as
    // singing, it would keep the line going and no line would end.
    const auto heard = listen(singer.audio(), 48000.0, 0.45f);
    CHECK(share(heard, breathAt + 0.05, breathAt + 0.33, Sound::breath) > 0.9);
    const auto ends = lineEnds(heard);
    REQUIRE(ends.size() == 1);
    CHECK(ends[0] > breathAt);
    CHECK(ends[0] < breathAt + 0.35);
}

TEST_CASE("The Listener ends a line at a pause as long as the gap, and not at a shorter one",
          "[plugins][listener]")
{
    const auto endsWithPause = [](double pause)
    {
        Singer singer(48000.0);
        singer.rest(0.1).vowel(0.5, 220.0f);
        const double pauseAt = singer.now();
        singer.rest(pause).vowel(0.5, 247.0f);
        const auto ends = lineEnds(listen(singer.audio(), 48000.0, 0.25f));
        int inPause = 0;
        for (const double at : ends)
            if (at > pauseAt && at < pauseAt + pause + 0.01)
                ++inPause;
        return inPause;
    };
    CHECK(endsWithPause(0.3) == 1);
    CHECK(endsWithPause(0.2) == 0);

    // Reported once, on the frame the gap is reached, with the pause so far.
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.5, 220.0f);
    const double pauseAt = singer.now();
    singer.rest(1.0);
    const auto heard = listen(singer.audio(), 48000.0, 0.25f);
    const auto ends = lineEnds(heard);
    REQUIRE(ends.size() == 1);
    CHECK(ends[0] == Approx(pauseAt + 0.25).margin(0.03));
    for (const auto& h : heard)
        if (h.hearing.lineEnd)
            CHECK(h.hearing.pauseSeconds == Approx(0.25).margin(0.005));
}

TEST_CASE("The Listener holds the last pitch through a gap", "[plugins][listener]")
{
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.4, 330.0f).rest(0.5);
    const auto heard = listen(singer.audio(), 48000.0);
    const auto& last = heard.back().hearing;
    CHECK(last.sound == Sound::silence);
    CHECK(last.pitchHz == 0.0f);
    CHECK(std::abs(1200.0f * std::log2(last.heldPitchHz / 330.0f)) < 20.0f);
}

TEST_CASE("The Listener counts syllables a second", "[plugins][listener]")
{
    Singer singer(48000.0);
    singer.rest(0.1);
    // Two syllables a second for four seconds: separate, then joined by level dips.
    for (int i = 0; i < 4; ++i)
        singer.vowel(0.3, 220.0f).rest(0.2);
    for (int i = 0; i < 4; ++i)
        singer.vowel(0.25, 247.0f, -18.0f, true).vowel(0.25, 247.0f, -30.0f, true);
    const auto heard = listen(singer.audio(), 48000.0);
    // Read once three seconds of singing fill the window: across the separate
    // syllables and into the joined ones, and at the end.
    for (const auto& h : heard)
        if (h.time > 3.0 && h.time < 3.09)
            CHECK(h.hearing.syllablesPerSecond == Approx(2.0).margin(0.4));
    CHECK(heard.back().hearing.syllablesPerSecond == Approx(2.0).margin(0.4));
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> listenerViolations{0};
void countListenerViolation(const rt::Violation&) noexcept
{
    listenerViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The Listener runs without allocating on the real-time thread", "[plugins][listener][rt]")
{
    Singer singer(48000.0);
    singer.vowel(0.3, 220.0f).s(0.1).rest(0.4).breath(0.2).t();
    vocal::Listener listener;
    listener.prepare(48000.0);
    const auto previous = rt::setViolationHandler(&countListenerViolation);
    listenerViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (const float x : singer.audio())
            listener.push(x);
        listener.reset();
        listener.setGapSeconds(0.3f);
    }
    rt::setViolationHandler(previous);
    CHECK(listenerViolations == 0);
}
#endif

// A benchmark, hidden from the default run (northlight_unit_tests "[.bench]"): what
// the Listener costs in percent of one core at 48 kHz, on singing (YIN runs every
// other frame whatever is sung).
TEST_CASE("Listener cost", "[.bench][listener]")
{
    Singer singer(48000.0);
    while (singer.now() < 10.0)
        singer.vowel(0.4, 220.0f).s(0.1).vowel(0.3, 262.0f).rest(0.3).breath(0.2, -38.0f);
    vocal::Listener listener;
    listener.prepare(48000.0);
    const auto start = std::chrono::steady_clock::now();
    for (const float x : singer.audio())
        listener.push(x);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double percent = 100.0 * elapsed / singer.now();
    std::printf("Listener: %.3f%% of a core\n", percent);
    CHECK(percent < 5.0); // generous: a debug build is slow
}
