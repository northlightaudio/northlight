#include "core/Model.h"

#include <catch2/catch_approx.hpp>
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

TEST_CASE("A fixed grid snaps to the nearest line", "[core][grid]")
{
    CHECK(snapToGrid(1499, 1000) == 1000);
    CHECK(snapToGrid(1500, 1000) == 2000);
    CHECK(snapToGrid(2999, 1000) == 3000);
    CHECK(snapToGrid(123, 1) == 123);
    CHECK(snapToGrid(123, 0) == 123);
}

TEST_CASE("A fade's shape runs from silence to full level, linear at curve 0, bowed up when fast",
          "[core][fades]")
{
    for (const float curve : {-1.0f, -0.4f, 0.0f, 0.4f, 1.0f})
    {
        INFO(curve);
        const float k = fadeShapeK(curve);
        CHECK(fadeShape(0.0f, k) == 0.0f);
        CHECK(fadeShape(1.0f, k) == 1.0f);
        float last = 0.0f;
        for (int i = 1; i <= 100; ++i)
        {
            const float g = fadeShape(static_cast<float>(i) / 100.0f, k);
            CHECK(g >= last); // never dips
            last = g;
        }
    }
    CHECK(fadeShapeK(0.0f) == 1.0f);
    for (int i = 0; i <= 10; ++i)
        CHECK(fadeShape(static_cast<float>(i) / 10.0f, 1.0f) ==
              static_cast<float>(i) / 10.0f);         // exactly linear
    CHECK(fadeShape(0.5f, fadeShapeK(0.5f)) > 0.6f);  // fast: well up by half way
    CHECK(fadeShape(0.5f, fadeShapeK(-0.5f)) < 0.4f); // slow: still low
    CHECK(fadeShape(0.5f, fadeShapeK(1.0f)) == Catch::Approx(8.0f / 9.0f));
    CHECK(fadeShape(2.0f, 1.0f) == 1.0f); // clamped
}
