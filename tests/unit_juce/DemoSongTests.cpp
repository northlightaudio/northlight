#include "app/DemoSong.h"
#include "engine/WavFile.h"
#include "engine/pitch/PitchAnalysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

using namespace northlight;

TEST_CASE("The demo voice is a singer the pitch analysis follows", "[app][demo][pitch]")
{
    const auto voice = demoVoice(48000.0);
    REQUIRE(static_cast<std::int64_t>(voice.size()) == demoVoiceLength);
    float peak = 0.0f;
    for (const float v : voice)
        peak = std::max(peak, std::abs(v));
    CHECK(peak == Catch::Approx(0.5f).margin(0.01f));

    const auto a = engine::pitch::analyse({voice}, 48000.0);
    // Most of its 29 notes are found, as separate notes, in its range (A2 to C4).
    CHECK(a.notes.size() >= 20);
    for (const auto& n : a.notes)
    {
        CHECK(n.centre > 5600.0f);
        CHECK(n.centre < 7300.0f);
    }
    // The first note is the E sung in tune.
    REQUIRE(!a.notes.empty());
    CHECK(std::abs(a.notes.front().centre - 6400.0f) < 40.0f);
}

TEST_CASE("The demo song has its voice as an audio clip and the drum outputs in a closed folder",
          "[app][demo]")
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("demo-voice", "", false);
    folder.createDirectory();
    const auto song = makeDemoSong(folder.getFullPathName().toStdString());
    const auto voice = std::find_if(
        song.tracks.begin(), song.tracks.end(), [](const core::Track& t) { return t.name == "Voice"; });
    REQUIRE(voice != song.tracks.end());
    REQUIRE(voice->clips.size() == 1);
    const auto info = engine::readWavInfo(voice->clips[0].mediaPath);
    REQUIRE(info.has_value());
    CHECK(info->lengthSamples == demoVoiceLength);
    CHECK(voice->clips[0].length == demoVoiceLength);

    const auto drumFolder = std::find_if(
        song.tracks.begin(), song.tracks.end(), [](const core::Track& t) { return t.isFolder(); });
    REQUIRE(drumFolder != song.tracks.end());
    CHECK_FALSE(drumFolder->folderOpen);
    int inFolder = 0;
    for (const auto& t : song.tracks)
        if (t.folder == drumFolder->id)
        {
            ++inFolder;
            CHECK(t.isAux());
            CHECK(song.hiddenByFolder(t));
        }
    CHECK(inFolder == 3);

    // Without a folder there is no voice to write: no voice track.
    const auto plain = makeDemoSong();
    CHECK(std::none_of(
        plain.tracks.begin(), plain.tracks.end(), [](const core::Track& t) { return t.name == "Voice"; }));
    folder.deleteRecursively();
}
