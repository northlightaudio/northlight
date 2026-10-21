#include "core/Model.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight::core;

TEST_CASE("Project length is the end of the furthest clip", "[core][model]")
{
    Project p;
    CHECK(p.length() == 0);
    Track t;
    t.clips.push_back({1, "a", "a.wav", 100, 50});
    t.clips.push_back({2, "b", "b.wav", 1000, 10});
    p.tracks.push_back(t);
    CHECK(p.length() == 1010);
}

TEST_CASE("Tracks and clips are found by id", "[core][model]")
{
    Project p;
    Track t;
    t.id = 7;
    t.clips.push_back({3, "c", "c.wav", 0, 10});
    p.tracks.push_back(t);
    REQUIRE(p.findTrack(7) != nullptr);
    CHECK(p.findTrack(8) == nullptr);
    CHECK(p.trackIndex(7) == 0);
    CHECK(p.trackIndex(8) == -1);
    REQUIRE(p.findTrack(7)->findClip(3) != nullptr);
    CHECK(p.findTrack(7)->findClip(4) == nullptr);
}

TEST_CASE("Clips sort by start position", "[core][model]")
{
    Track t;
    t.clips.push_back({1, "", "", 500, 10});
    t.clips.push_back({2, "", "", 100, 10});
    t.clips.push_back({3, "", "", 300, 10});
    t.sortClips();
    CHECK(t.clips[0].id == 2);
    CHECK(t.clips[1].id == 3);
    CHECK(t.clips[2].id == 1);
}

TEST_CASE("Samples convert to bars and beats at 120 bpm 4/4", "[core][model]")
{
    const double sr = 48000.0;
    const TimeSignature sig{4, 4};
    CHECK(samplesPerBeat(sr, 120.0, sig) == 24000.0);

    CHECK(samplesToBarsBeats(0, sr, 120.0, sig) == BarsBeats{1, 1, 0});
    CHECK(samplesToBarsBeats(24000, sr, 120.0, sig) == BarsBeats{1, 2, 0});
    CHECK(samplesToBarsBeats(96000, sr, 120.0, sig) == BarsBeats{2, 1, 0});
    CHECK(samplesToBarsBeats(12000, sr, 120.0, sig) == BarsBeats{1, 1, 480});

    CHECK(barsBeatsToSamples({2, 1, 0}, sr, 120.0, sig) == 96000);
    CHECK(barsBeatsToSamples({1, 1, 480}, sr, 120.0, sig) == 12000);
}

TEST_CASE("Compound time signatures count the denominator note value", "[core][model]")
{
    const double sr = 48000.0;
    const TimeSignature sig{6, 8};
    // An eighth note at 120 bpm (quarter notes) is 0.25 s.
    CHECK(samplesPerBeat(sr, 120.0, sig) == 12000.0);
    CHECK(samplesToBarsBeats(6 * 12000, sr, 120.0, sig) == BarsBeats{2, 1, 0});
}

TEST_CASE("The editing grid picks the finest division that fits and snaps to the nearest line",
          "[core][grid]")
{
    const TimeSignature four{4, 4};
    // 120 bpm at 48 kHz: a beat is 24000 samples. At 100 samples per pixel an
    // eighth of a beat (3000 samples) is 30 px: fine enough for a 16 px minimum.
    CHECK(gridSamples(48000.0, 120.0, four, 100.0, 16) == 3000);
    // At 1000 samples per pixel an eighth is 3 px, a quarter 6, a half 12, a beat 24.
    CHECK(gridSamples(48000.0, 120.0, four, 1000.0, 16) == 24000);
    // Zoomed far out, whole bars (96000) are 9.6 px at 10000/px: two bars.
    CHECK(gridSamples(48000.0, 120.0, four, 10000.0, 16) == 192000);
    CHECK(snapToGrid(1499, 1000) == 1000);
    CHECK(snapToGrid(1500, 1000) == 2000);
    CHECK(snapToGrid(2999, 1000) == 3000);
    CHECK(snapToGrid(123, 1) == 123);
    CHECK(snapToGrid(123, 0) == 123);
}
