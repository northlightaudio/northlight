#include "app/DemoSong.h"
#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
// Every insert in the song: on the tracks, the FX tracks and the master.
std::vector<const core::Insert*> effects(const core::Project& song)
{
    std::vector<const core::Insert*> all;
    const auto add = [&](const core::Channel& c)
    {
        for (const auto& i : c.inserts)
            all.push_back(&i);
    };
    for (const auto& t : song.tracks)
        add(t);
    for (const auto& b : song.buses)
        add(b);
    add(song.master);
    return all;
}
} // namespace

TEST_CASE("The demo song is played by the built-in synths alone, with no audio", "[app][demo]")
{
    const auto song = makeDemoSong();
    const std::set<std::string> synths{
        "northlight.aurora", "northlight.nebula", "northlight.pulsar", "northlight.starbit"};
    int instruments = 0;
    for (const auto& t : song.tracks)
    {
        CHECK(t.clips.empty());
        if (t.kind == core::TrackKind::instrument)
        {
            ++instruments;
            CHECK(synths.contains(t.instrument.type));
        }
    }
    CHECK(instruments == 6);
    for (const auto* i : effects(song))
    {
        INFO(i->type);
        CHECK(plugins::findBuiltIn(i->type) != nullptr);
    }
}

TEST_CASE("The demo song uses a wide range of the built-in effects, each from a factory preset",
          "[app][demo]")
{
    const auto song = makeDemoSong();
    std::set<std::string> types;
    for (const auto* i : effects(song))
    {
        types.insert(i->type);
        if (i->type == plugins::types::compressor)
            continue; // the bass's ducker is set by hand
        // A preset other than the defaults (a misspelt name would leave them).
        const auto& presets = plugins::factoryPresets(i->type);
        INFO(i->type);
        REQUIRE(presets.size() > 1);
        CHECK(std::any_of(presets.begin() + 1,
                          presets.end(),
                          [&](const plugins::EffectPreset& p) { return p.values == i->params; }));
    }
    CHECK(types.size() >= 15);
}

TEST_CASE("The demo song's notes stay inside their clips", "[app][demo]")
{
    const auto song = makeDemoSong();
    for (const auto& t : song.tracks)
        for (const auto& c : t.midiClips)
        {
            INFO(t.name << ": " << c.name);
            CHECK(!c.notes.empty());
            CHECK(c.start + c.length <= demoSongBars * demoSongBarLength);
            for (const auto& n : c.notes)
            {
                CHECK(n.start >= 0);
                CHECK(n.start + n.length <= c.length);
            }
        }
}

TEST_CASE("The demo song's bass is keyed by the kick's output, and the drum outputs are in a closed folder",
          "[app][demo]")
{
    const auto song = makeDemoSong();
    const auto drumFolder = std::find_if(
        song.tracks.begin(), song.tracks.end(), [](const core::Track& t) { return t.isFolder(); });
    REQUIRE(drumFolder != song.tracks.end());
    CHECK_FALSE(drumFolder->folderOpen);
    int inFolder = 0;
    core::Id kick = core::invalidId;
    for (const auto& t : song.tracks)
        if (t.folder == drumFolder->id)
        {
            ++inFolder;
            CHECK(t.isAux());
            CHECK(song.hiddenByFolder(t));
            if (t.auxOutput == 1)
                kick = t.id;
        }
    CHECK(inFolder == 3);
    REQUIRE(kick != core::invalidId);

    int keyed = 0;
    for (const auto* i : effects(song))
        if (i->sidechain != core::invalidId)
        {
            ++keyed;
            CHECK(i->type == plugins::types::compressor);
            CHECK(i->sidechain == kick);
        }
    CHECK(keyed == 1);
}
