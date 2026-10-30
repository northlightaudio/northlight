#include "app/DemoSong.h"
#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"
#include "plugins/Lumen.h"
#include "plugins/Patina.h"
#include "plugins/midi/Utilities.h"

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
    const std::set<std::string> synths{"northlight.aurora",
                                       "northlight.cirrus",
                                       "northlight.lumen",
                                       "northlight.nebula",
                                       "northlight.pulsar"};
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
    CHECK(instruments == 5);
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
        // A preset other than the defaults (a misspelt name would leave them),
        // with at most two values changed (the keys' tape wobble is turned down).
        const auto& presets = plugins::factoryPresets(i->type);
        INFO(i->type);
        REQUIRE(presets.size() > 1);
        CHECK(std::any_of(presets.begin() + 1,
                          presets.end(),
                          [&](const plugins::EffectPreset& p)
                          {
                              if (p.values.size() != i->params.size())
                                  return false;
                              size_t changed = 0;
                              for (size_t k = 0; k < p.values.size(); ++k)
                                  changed += p.values[k] != i->params[k] ? 1 : 0;
                              return changed <= 2;
                          }));
    }
    CHECK(types.size() >= 9);
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

TEST_CASE("The demo song's keys are Lumen on four-note chords, loosened by Humanise", "[app][demo]")
{
    const auto song = makeDemoSong();
    const core::Track* keysMidi = nullptr;
    for (const auto& t : song.tracks)
        if (t.name == "Keys")
            keysMidi = &t;
    REQUIRE(keysMidi != nullptr);
    const auto* keys = song.findTrack(keysMidi->midiTarget);
    REQUIRE(keys != nullptr);
    CHECK(keys->instrument.type == plugins::lumen::type);
    REQUIRE(keysMidi->midiInserts.size() == 1);
    CHECK(keysMidi->midiInserts.front().type == plugins::humanise::type);

    // Every chord the keys strike on one is a full four-note voicing.
    REQUIRE(keysMidi->midiClips.size() == 1);
    const auto& clip = keysMidi->midiClips.front();
    for (int bar = 0; bar < demoSongBars - 2; ++bar)
    {
        const auto from = bar * demoSongBarLength, to = from + demoSongBarLength / 8;
        const auto struck =
            std::count_if(clip.notes.begin(),
                          clip.notes.end(),
                          [&](const core::MidiNote& n) { return n.start >= from && n.start < to; });
        INFO("bar " << bar);
        CHECK(struck >= 4);
    }

    // The keys' tape wobbles only gently, so the chords stay in tune with the
    // bass (Worn Cassette's own wow and flutter bend them about 20 cents).
    const auto tape = std::find_if(keys->inserts.begin(),
                                   keys->inserts.end(),
                                   [](const core::Insert& i) { return i.type == plugins::patina::type; });
    REQUIRE(tape != keys->inserts.end());
    CHECK(tape->params[plugins::patina::driftWow] <= 12.0f);
    CHECK(tape->params[plugins::patina::driftFlutter] <= 10.0f);

    // Only MIDI tracks carry MIDI FX.
    for (const auto& t : song.tracks)
        if (t.kind != core::TrackKind::midi)
            CHECK(t.midiInserts.empty());
}

TEST_CASE("The demo song's bass plays a chord's root or a note the keys hold, never a semitone against them",
          "[app][demo]")
{
    const auto song = makeDemoSong();
    const auto notesOf = [&](const std::string& name)
    {
        std::vector<core::MidiNote> all;
        for (const auto& t : song.tracks)
            if (t.name == name)
                for (const auto& c : t.midiClips)
                    for (auto n : c.notes)
                    {
                        n.start += c.start;
                        all.push_back(n);
                    }
        return all;
    };
    const auto bass = notesOf("Bass");
    const auto keys = notesOf("Keys");
    REQUIRE(!bass.empty());
    REQUIRE(!keys.empty());
    const auto pitchClass = [](int pitch) { return ((pitch % 12) + 12) % 12; };
    constexpr auto half = demoSongBarLength / 2;
    for (const auto& b : bass)
    {
        // The root: the bass note that began this half bar (or the last one that did).
        int root = -1;
        core::SamplePosition rootStart = -1;
        for (const auto& r : bass)
            if (r.start % half == 0 && r.start <= b.start && r.start > rootStart)
            {
                rootStart = r.start;
                root = pitchClass(r.note);
            }
        std::set<int> held;
        for (const auto& k : keys)
            if (k.start <= b.start && k.start + k.length > b.start)
                held.insert(pitchClass(k.note));
        INFO("bass " << b.note << " at bar " << static_cast<double>(b.start) / demoSongBarLength);
        CHECK((pitchClass(b.note) == root || held.contains(pitchClass(b.note))));
    }
}
