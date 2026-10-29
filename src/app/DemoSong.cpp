#include "DemoSong.h"

#include "plugins/Amp.h"
#include "plugins/Aurora.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/EffectPresets.h"
#include "plugins/Exciter.h"
#include "plugins/FxTracks.h"
#include "plugins/Glue.h"
#include "plugins/Nebula.h"
#include "plugins/Orbit.h"
#include "plugins/Patina.h"
#include "plugins/Pedals.h"
#include "plugins/Pulsar.h"
#include "plugins/Starbit.h"
#include "plugins/TapeEcho.h"
#include "plugins/Widener.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace northlight
{

namespace
{
constexpr double sampleRate = 48000.0;
constexpr core::SamplePosition beatLength = demoSongBarLength / 4; // 90 bpm at 48 kHz

core::SamplePosition at(double beats)
{
    return static_cast<core::SamplePosition>(std::llround(beats * static_cast<double>(beatLength)));
}

// The form, in bars.
constexpr int intro = 0, verse = 4, chorus = 12, bridge = 20, finale = 24, outro = 32, ending = 36;

bool inChorus(int bar)
{
    return (bar >= chorus && bar < bridge) || (bar >= finale && bar < outro);
}

// ----- the harmony -------------------------------------------------------------------------------

// A chord: the keys' voicing (low to high) and the bass note.
struct Chord
{
    std::array<int, 4> keys;
    int bass;
};
constexpr Chord gMaj9{{59, 62, 66, 69}, 43};
constexpr Chord fSharpM7{{57, 61, 64, 69}, 42};
constexpr Chord eM9{{55, 59, 62, 66}, 40};
constexpr Chord a13Sus{{55, 59, 62, 66}, 45};
constexpr Chord a13{{55, 59, 61, 66}, 45};
constexpr Chord bM9{{57, 61, 62, 66}, 47};
constexpr Chord gMaj9Open{{57, 59, 62, 66}, 43};
constexpr Chord dOverFSharp{{57, 61, 62, 64}, 42};
constexpr Chord dMaj9{{54, 57, 61, 64}, 38};

// A bar's chords: the first half's and the second's (the same for most bars).
struct BarChords
{
    Chord first, second;
};
BarChords chordsAt(int bar)
{
    const int step = bar % 4;
    if (bar >= ending)
        return {dMaj9, dMaj9};
    if (inChorus(bar))
    {
        constexpr std::array<Chord, 3> cycle{bM9, gMaj9Open, dOverFSharp};
        return step < 3 ? BarChords{cycle[static_cast<size_t>(step)], cycle[static_cast<size_t>(step)]}
                        : BarChords{eM9, a13};
    }
    if (bar >= bridge && bar < finale) // rising into the last chorus
    {
        constexpr std::array<Chord, 3> cycle{eM9, fSharpM7, gMaj9};
        return step < 3 ? BarChords{cycle[static_cast<size_t>(step)], cycle[static_cast<size_t>(step)]}
                        : BarChords{a13Sus, a13};
    }
    // The intro, the verse and the outro.
    constexpr std::array<Chord, 3> cycle{gMaj9, fSharpM7, eM9};
    return step < 3 ? BarChords{cycle[static_cast<size_t>(step)], cycle[static_cast<size_t>(step)]}
                    : BarChords{a13Sus, a13};
}
Chord chordAt(int bar, double beat)
{
    const auto chords = chordsAt(bar);
    return beat < 2.0 ? chords.first : chords.second;
}

// ----- the builder -------------------------------------------------------------------------------

struct Note
{
    int pitch;
    int velocity;
    double start, length; // beats
};

// Small deterministic variations, so the parts sound played.
class Wobble
{
public:
    explicit Wobble(std::uint32_t seed) : state_(seed) {}
    double next() // -1..1
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }
    int velocity(int v, int by) { return std::clamp(v + static_cast<int>(std::lround(next() * by)), 1, 127); }

private:
    std::uint32_t state_;
};

struct Builder
{
    core::Project project;

    core::Id id() { return project.allocateId(); }

    core::Insert insert(const std::string& type, std::vector<float> params = {})
    {
        core::Insert i;
        i.id = id();
        i.type = type;
        i.params = params.empty() ? plugins::defaultParameters(type) : std::move(params);
        return i;
    }

    // An insert set to one of its type's factory presets.
    core::Insert preset(const std::string& type, const std::string& name, float mix = 1.0f)
    {
        std::vector<float> values;
        for (const auto& p : plugins::factoryPresets(type))
            if (p.name == name)
                values = p.values;
        auto i = insert(type, std::move(values));
        i.mix = mix;
        return i;
    }

    // An instrument track: a MIDI track named for the part over an instrument
    // channel named for the instrument ("Drums" over "Drums: Pulsar").
    void instrumentTrack(const std::string& name, const std::string& instrument, std::vector<float> params)
    {
        core::Track midi;
        midi.id = id();
        midi.kind = core::TrackKind::midi;
        midi.name = name;
        core::Track channel;
        channel.id = id();
        channel.kind = core::TrackKind::instrument;
        channel.name = name + ": " + plugins::BuiltInFactory::displayName(instrument);
        channel.instrument = insert(instrument, std::move(params));
        midi.midiTarget = channel.id;
        project.tracks.push_back(std::move(midi));
        project.tracks.push_back(std::move(channel));
    }

    // A clip over bars [from, to), the notes in beats from the clip's start.
    void clip(core::Track& track, const std::string& name, int from, int to, const std::vector<Note>& notes)
    {
        core::MidiClip c;
        c.id = id();
        c.name = name;
        c.start = from * demoSongBarLength;
        c.length = (to - from) * demoSongBarLength;
        for (const auto& n : notes)
            c.notes.push_back(
                {id(), n.pitch, n.velocity, at(n.start), std::max<core::SamplePosition>(1, at(n.length))});
        c.sortNotes();
        track.midiClips.push_back(std::move(c));
    }

    void lane(core::Channel& channel,
              core::AutomationTarget target,
              core::Id insertId,
              int parameter,
              std::vector<std::pair<double, float>> points) // (bar, value)
    {
        core::AutomationLane l;
        l.id = id();
        l.target = target;
        l.insert = insertId;
        l.parameter = parameter;
        for (const auto& [bar, value] : points)
            l.points.push_back({at(bar * 4.0), value});
        channel.automation.push_back(std::move(l));
    }

    // An FX track whose effect is set to one of its presets.
    core::Bus fxTrack(const std::string& type, const std::string& name, const std::string& presetName)
    {
        auto bus = plugins::makeFxTrack(project, type, name);
        bus.id = id();
        bus.inserts.clear();
        bus.inserts.push_back(preset(type, presetName));
        return bus;
    }
};

template <typename List> std::vector<float> presetValues(const List& list, const std::string& name)
{
    for (const auto& p : list)
        if (p.name == name)
            return p.values;
    return list.front().values;
}

// ----- the parts ---------------------------------------------------------------------------------

// A sixteenth step's place in the bar (beats), the off-beat sixteenths pushed
// late for a lazy swing.
double step16(int step)
{
    return step * 0.25 + (step % 2 == 1 ? 0.05 : 0.0);
}

std::vector<Note> drumBar(int bar, Wobble& w)
{
    namespace pu = plugins::pulsar;
    const int kick = pu::padNote(pu::kick), snare = pu::padNote(pu::snare), clap = pu::padNote(pu::clap),
              hat = pu::padNote(pu::closedHat), open = pu::padNote(pu::openHat), rim = pu::padNote(pu::rim),
              tomLow = pu::padNote(pu::lowTom), tomHigh = pu::padNote(pu::highTom);
    std::vector<Note> n;
    const double b = bar * 4.0; // the clip starts at the song's start
    const auto hit = [&](int pitch, int velocity, int step, double late = 0.0)
    { n.push_back({pitch, w.velocity(velocity, 6), b + step16(step) + late + w.next() * 0.008, 0.1}); };
    const bool second = bar % 2 == 1;
    const bool phraseEnd = bar % 4 == 3;

    if (bar < verse - 1)
        return n;
    if (bar == verse - 1) // the hats count in
    {
        for (int e = 0; e < 8; ++e)
            hit(hat, 34 + e * 6, e * 2);
        hit(kick, 90, 14);
        return n;
    }
    if (bar >= ending)
    {
        hit(kick, 110, 0);
        hit(open, 70, 0);
        return n;
    }
    if (bar >= outro) // just the time: a kick, a rim, soft hats, thinning out
    {
        if (bar < outro + 3)
        {
            hit(kick, 96, 0);
            hit(rim, 70, 12, 0.02);
            for (int e = 0; e < 8; ++e)
                hit(hat, e % 2 == 0 ? 54 : 36, e * 2);
        }
        return n;
    }
    if (bar >= bridge && bar < finale)
    {
        // The rim and the hats; the kick comes back for the build, and a snare
        // roll into the last chorus.
        hit(rim, 76, 4, 0.02);
        hit(rim, 76, 12, 0.02);
        for (int s = 0; s < 16; ++s)
            hit(hat, s % 4 == 0 ? 62 : s % 2 == 0 ? 46 : 30, s);
        if (bar >= bridge + 2)
            hit(kick, 100, 0);
        if (bar == finale - 1)
            for (int s = 8; s < 16; ++s)
                hit(snare, 50 + (s - 8) * 9, s);
        return n;
    }

    // The groove: the kick on one, the "a" of two and the "and" of three (and of
    // four in the second bar), the backbeat a little late, hats on the eighths
    // with swung ghosts between.
    const bool loud = inChorus(bar);
    for (const int s : second ? std::vector<int>{0, 3, 10, 14} : std::vector<int>{0, 7, 10})
        hit(kick, s == 0 ? 118 : 100, s);
    const int backbeat = bar < verse + 4 ? rim : snare; // the verse's first half on the rim
    for (const int s : {4, 12})
    {
        hit(backbeat, loud ? 112 : 100, s, 0.02);
        if (loud)
            hit(clap, 84, s, 0.025);
    }
    if (second)
        hit(snare, 34, 9); // a ghost
    for (int e = 0; e < 8; ++e)
    {
        const bool lift = phraseEnd && e == 7;
        hit(lift ? open : hat, (e % 2 == 0 ? 82 : 60) + (loud ? 8 : 0), e * 2);
    }
    for (const int s : {3, 7, 11, 15})
        hit(hat, 32, s);
    // Fills into the next section.
    if (bar == chorus - 1 || bar == bridge - 1 || bar == outro - 1)
    {
        std::erase_if(n, [&](const Note& x) { return x.start >= b + 3.0; });
        const int toms[] = {tomHigh, tomHigh, tomLow, tomLow};
        for (int s = 0; s < 4; ++s)
            hit(toms[s], 96 + s * 6, 12 + s);
    }
    return n;
}

// The bass: the root on one, a pickup, the fifth (or the root again) on the
// "and" of three, and a chromatic step into the next bar's root.
std::vector<Note> bassBar(int bar, Wobble& w)
{
    std::vector<Note> n;
    const double b = bar * 4.0;
    const int root = chordAt(bar, 0.0).bass;
    const int later = chordAt(bar, 2.0).bass;
    if (bar >= ending)
        return {{dMaj9.bass, 112, b, 6.0}};
    if (bar >= bridge && bar < finale) // long notes while the filter closes
        return {{root, 100, b, 3.8}};
    if (bar >= outro)
        return {{root, 104, b, 3.5}};
    const int next = chordAt(bar + 1, 0.0).bass;
    const int fifth = later + 7 <= 52 ? later + 7 : later - 5;
    n.push_back({root, w.velocity(112, 5), b, 1.2});
    n.push_back({root, w.velocity(78, 5), b + step16(7), 0.22});
    n.push_back({inChorus(bar) ? fifth : later, w.velocity(98, 5), b + 2.5, 0.8});
    if (next != later)
        n.push_back({next > later ? next - 1 : next + 1, w.velocity(86, 5), b + 3.5, 0.45});
    else
        n.push_back({later + 12, w.velocity(80, 5), b + 3.5, 0.3});
    return n;
}

// The keys: whole chords, rolled, in the intro, the bridge and the outro;
// elsewhere a chord on one, the top two notes again on the "a" of two, the
// chord on the "and" of three, and a push of the next chord at the bar's end.
std::vector<Note> keysBar(int bar, Wobble& w)
{
    std::vector<Note> n;
    const double b = bar * 4.0;
    const auto roll =
        [&](const Chord& c, double start, double length, int velocity, double spread, int from = 0)
    {
        for (int k = from; k < 4; ++k)
            n.push_back({c.keys[static_cast<size_t>(k)],
                         w.velocity(velocity, 5),
                         start + (k - from) * spread,
                         length - (k - from) * spread});
    };
    const auto first = chordAt(bar, 0.0);
    const auto second = chordAt(bar, 2.0);
    if (bar >= ending)
    {
        roll(dMaj9, b, 7.5, 74, 0.08);
        n.push_back({dMaj9.keys[3] + 12, 60, b + 0.4, 7.0}); // the ninth on top
        return n;
    }
    const bool held = bar < verse || bar >= outro || (bar >= bridge && bar < finale);
    if (held)
    {
        if (first.keys == second.keys)
            roll(first, b, 3.9, 70, 0.03);
        else
        {
            roll(first, b, 1.95, 70, 0.03);
            roll(second, b + 2.0, 1.95, 66, 0.03);
        }
        return n;
    }
    roll(first, b, 1.4, 82, 0.015);
    roll(first, b + step16(7), 0.4, 58, 0.01, 2);
    roll(second, b + 2.5, 0.9, 74, 0.015);
    roll(chordAt(bar + 1, 0.0), b + step16(15), 0.2, 52, 0.01, 2);
    return n;
}

// The lead's line (beats from its section's start): a verse and a chorus.
const std::vector<Note>& verseLine()
{
    static const std::vector<Note> line{
        // Gmaj9 F#m7
        {66, 92, 0.5, 0.5},
        {69, 100, 1.0, 1.0},
        {71, 96, 2.0, 0.5},
        {69, 90, 2.5, 0.5},
        {66, 92, 3.0, 0.5},
        {64, 88, 3.5, 0.5},
        {64, 98, 4.0, 1.5},
        {61, 86, 5.5, 0.5},
        {64, 90, 6.0, 0.5},
        {66, 96, 6.5, 1.25},
        // Em9 A13
        {67, 90, 8.5, 0.5},
        {66, 88, 9.0, 0.5},
        {64, 88, 9.5, 0.5},
        {71, 102, 10.0, 1.0},
        {69, 92, 11.0, 0.5},
        {67, 88, 11.5, 0.5},
        {66, 96, 12.0, 1.0},
        {64, 88, 13.0, 0.5},
        {61, 92, 13.5, 1.5},
        // Gmaj9 F#m7, higher
        {66, 92, 16.5, 0.5},
        {69, 96, 17.0, 0.5},
        {74, 106, 17.5, 1.0},
        {73, 96, 18.5, 0.5},
        {71, 94, 19.0, 0.5},
        {69, 92, 19.5, 0.5},
        {69, 98, 20.0, 1.0},
        {64, 88, 21.0, 0.5},
        {66, 90, 21.5, 0.5},
        {69, 100, 22.0, 1.5},
        // Em9 A13
        {71, 94, 24.5, 0.5},
        {69, 92, 25.0, 0.5},
        {67, 90, 25.5, 0.5},
        {66, 92, 26.0, 0.5},
        {64, 96, 26.5, 1.0},
        {64, 90, 28.0, 0.5},
        {66, 92, 28.5, 0.5},
        {64, 88, 29.0, 0.5},
        {62, 88, 29.5, 0.5},
        {61, 94, 30.0, 0.5},
        {64, 98, 30.5, 1.25},
    };
    return line;
}
const std::vector<Note>& chorusLine()
{
    static const std::vector<Note> line{
        // Bm9 Gmaj9
        {74, 110, 0.0, 1.0},
        {73, 98, 1.0, 0.5},
        {71, 96, 1.5, 0.5},
        {69, 100, 2.0, 1.0},
        {66, 92, 3.0, 0.5},
        {69, 96, 3.5, 0.5},
        {71, 104, 4.0, 1.5},
        {69, 94, 5.5, 0.5},
        {66, 96, 6.0, 1.0},
        // D/F# Em9 A13
        {64, 90, 8.0, 0.5},
        {66, 94, 8.5, 0.5},
        {69, 98, 9.0, 1.0},
        {73, 100, 10.0, 0.5},
        {74, 108, 10.5, 1.0},
        {73, 96, 11.5, 0.5},
        {71, 100, 12.0, 1.0},
        {69, 92, 13.0, 0.5},
        {67, 90, 13.5, 0.5},
        {64, 90, 14.0, 0.5},
        {66, 98, 14.5, 1.25},
        // Bm9 Gmaj9, up to the top
        {74, 110, 16.0, 1.0},
        {73, 98, 17.0, 0.5},
        {71, 96, 17.5, 0.5},
        {69, 100, 18.0, 1.0},
        {66, 92, 19.0, 0.5},
        {69, 96, 19.5, 0.5},
        {71, 100, 20.0, 1.0},
        {74, 106, 21.0, 1.0},
        {76, 114, 22.0, 1.5},
        // D/F# Em9 A13
        {74, 106, 24.0, 1.0},
        {73, 98, 25.0, 0.5},
        {69, 94, 25.5, 0.5},
        {66, 92, 26.0, 1.0},
        {69, 96, 27.0, 1.0},
        {67, 90, 28.0, 0.5},
        {66, 90, 28.5, 0.5},
        {64, 94, 29.0, 1.0},
        {69, 102, 30.0, 1.75},
    };
    return line;
}
// The bridge's solo and the outro's last words (beats from their start).
const std::vector<Note>& bridgeLine()
{
    static const std::vector<Note> line{
        // Em9
        {71, 104, 0.0, 1.5},
        {74, 96, 1.5, 0.5},
        {76, 108, 2.0, 1.5},
        {74, 92, 3.5, 0.5},
        // F#m7
        {73, 100, 4.0, 1.0},
        {76, 98, 5.0, 0.5},
        {78, 110, 5.5, 1.5},
        {76, 94, 7.0, 0.5},
        {73, 92, 7.5, 0.5},
        // Gmaj9
        {74, 100, 8.0, 0.5},
        {78, 104, 8.5, 0.5},
        {81, 116, 9.0, 2.0},
        {78, 98, 11.0, 0.5},
        {76, 94, 11.5, 0.5},
        // A13sus A13
        {74, 100, 12.0, 1.0},
        {76, 96, 13.0, 0.5},
        {78, 100, 13.5, 0.5},
        {79, 104, 14.0, 0.5},
        {76, 96, 14.5, 0.5},
        {73, 98, 15.0, 0.5},
        {78, 110, 15.5, 0.5},
    };
    return line;
}
const std::vector<Note>& outroLine()
{
    static const std::vector<Note> line{
        {78, 100, 0.0, 1.0},
        {76, 92, 1.0, 0.5},
        {74, 92, 1.5, 0.5},
        {71, 96, 2.0, 2.0},
        {69, 88, 4.0, 0.5},
        {73, 92, 4.5, 0.5},
        {76, 100, 5.0, 2.0},
        {73, 90, 7.0, 1.0},
        {71, 92, 8.0, 1.0},
        {74, 92, 9.0, 0.5},
        {71, 90, 9.5, 0.5},
        {76, 100, 10.0, 1.5},
        {74, 90, 11.5, 0.5},
        {71, 94, 12.0, 2.0},
        {73, 96, 14.0, 2.0},
        {74, 100, 16.0, 7.0},
    };
    return line;
}

void append(std::vector<Note>& to, const std::vector<Note>& line, double offset)
{
    for (const auto& x : line)
        to.push_back({x.pitch, x.velocity, offset + x.start, x.length});
}
} // namespace

core::Project makeDemoSong()
{
    namespace au = plugins::aurora;
    namespace ne = plugins::nebula;
    namespace pu = plugins::pulsar;
    namespace sb = plugins::starbit;
    Builder s;
    auto& p = s.project;
    p.name = "Harbour Lights";
    p.sampleRate = sampleRate;
    p.tempoBpm = demoSongBpm;
    p.timeSignature = {4, 4};
    p.key.root = 2; // D
    p.key.scale = core::ScaleKind::major;
    p.master.gain = 0.8f;

    // ----- tracks (the references are taken after every track is in) ---------------------------
    s.instrumentTrack("Drums", pu::type, presetValues(pu::kits(), "Dusty"));
    s.instrumentTrack("Bass", au::type, presetValues(au::presets(), "Polar Bass"));
    s.instrumentTrack("Keys", au::type, presetValues(au::presets(), "Glacier Keys"));
    s.instrumentTrack("Pad", ne::type, presetValues(ne::presets(), "Glass Halo"));
    s.instrumentTrack("Arp", sb::type, presetValues(sb::presets(), "Chord Arp"));
    s.instrumentTrack("Lead", au::type, presetValues(au::presets(), "Comet Lead"));
    // Each part: its MIDI track (the notes) and its instrument channel (the sound).
    auto& drumsMidi = p.tracks[0];
    auto& drums = p.tracks[1];
    auto& bassMidi = p.tracks[2];
    auto& bass = p.tracks[3];
    auto& keysMidi = p.tracks[4];
    auto& keys = p.tracks[5];
    auto& padMidi = p.tracks[6];
    auto& pad = p.tracks[7];
    auto& arpMidi = p.tracks[8];
    auto& arp = p.tracks[9];
    auto& leadMidi = p.tracks[10];
    auto& lead = p.tracks[11];

    // A few touches on the presets.
    drums.instrument.params[pu::output] = -3.0f;
    bass.instrument.params[au::level] = -6.0f;
    bass.instrument.params[au::cutoff] = 320.0f; // rounder
    keys.instrument.params[au::level] = -4.0f;
    keys.instrument.params[au::cutoff] = 2400.0f; // a softer, electric-piano strike
    pad.instrument.params[ne::level] = -14.0f;
    arp.instrument.params[sb::arpSync] = 5.0f; // sixteenths at the song's tempo
    arp.instrument.params[sb::echo] = 0.0f;    // the Tape Echo track does it
    arp.instrument.params[sb::level] = -7.0f;
    lead.instrument.params[au::level] = -16.0f; // into the pedal at about a guitar's level
    lead.instrument.params[au::glide] = 40.0f;
    // The kit in groups: the kick on the first extra pair (it keys the bass's
    // compressor), the rim, snare and clap on the second, the hats on the third;
    // the toms stay on the main pair.
    {
        constexpr std::array<int, pu::padCount> groups{1, 2, 2, 2, 3, 3, 0, 0};
        for (int padIndex = 0; padIndex < pu::padCount; ++padIndex)
            drums.instrument.params[static_cast<size_t>(pu::paramIndex(padIndex, pu::route))] =
                static_cast<float>(groups[static_cast<size_t>(padIndex)]);
    }

    // ----- parts ---------------------------------------------------------------------------------
    {
        Wobble w(0x51u);
        std::vector<Note> n;
        for (int bar = verse - 1; bar <= ending; ++bar)
            for (const auto& x : drumBar(bar, w))
                n.push_back(x);
        s.clip(drumsMidi, "Groove", intro, ending + 1, n);
    }
    {
        Wobble w(0x52u);
        std::vector<Note> n;
        for (int bar = verse; bar <= ending; ++bar)
            for (const auto& x : bassBar(bar, w))
                n.push_back({x.pitch, x.velocity, x.start - verse * 4.0, x.length});
        s.clip(bassMidi, "Bass line", verse, ending + 2, n);
    }
    {
        Wobble w(0x53u);
        std::vector<Note> n;
        for (int bar = intro; bar <= ending; ++bar)
            for (const auto& x : keysBar(bar, w))
                n.push_back(x);
        s.clip(keysMidi, "Chords", intro, ending + 2, n);
    }
    {
        // The pad holds each chord an octave up, softly, from the second bar.
        std::vector<Note> n;
        for (int bar = 1; bar < ending; ++bar)
        {
            const auto first = chordAt(bar, 0.0);
            const auto second = chordAt(bar, 2.0);
            if (first.keys == second.keys)
                for (const int t : first.keys)
                    n.push_back({t + 12, 72, bar * 4.0, 4.0});
            else
                for (int k = 0; k < 4; ++k)
                {
                    n.push_back({first.keys[static_cast<size_t>(k)] + 12, 72, bar * 4.0, 2.0});
                    n.push_back({second.keys[static_cast<size_t>(k)] + 12, 72, bar * 4.0 + 2.0, 2.0});
                }
        }
        for (const int t : dMaj9.keys)
            n.push_back({t + 12, 76, ending * 4.0, 8.0});
        s.clip(padMidi, "Wash", intro, ending + 2, n);
    }
    {
        // The arpeggio: the chord held two octaves up through the bridge and the
        // last chorus; Starbit cycles it in sixteenths.
        std::vector<Note> n;
        for (int bar = bridge; bar < outro; ++bar)
            for (const double half : {0.0, 2.0})
                for (const int t : chordAt(bar, half).keys)
                    n.push_back({t + 24, bar < finale ? 84 : 72, (bar - bridge) * 4.0 + half, 1.95});
        s.clip(arpMidi, "Sparkle", bridge, outro, n);
    }
    {
        // The lead: the verse, the chorus, the bridge's solo, the chorus again and
        // the outro.
        std::vector<Note> n;
        append(n, verseLine(), 0.0);
        append(n, chorusLine(), (chorus - verse) * 4.0);
        append(n, bridgeLine(), (bridge - verse) * 4.0);
        append(n, chorusLine(), (finale - verse) * 4.0);
        append(n, outroLine(), (outro - verse) * 4.0);
        s.clip(leadMidi, "Melody", verse, ending + 2, n);
    }

    // ----- effects -------------------------------------------------------------------------------
    namespace types = plugins::types;
    // Drums: glued, and a little sampler grit underneath.
    drums.inserts.push_back(s.preset(plugins::glue::type, "Drum Bus Punch"));
    drums.inserts.push_back(s.preset(plugins::patina::type, "Lo-Fi Sampler", 0.35f));
    // Bass: ducked under the kick (the key is set once the kick's track exists).
    {
        auto duck = s.insert(types::compressor);
        namespace co = plugins::strip::compressor;
        duck.params[co::threshold] = -28.0f;
        duck.params[co::ratio] = 4.0f;
        duck.params[co::attack] = 2.0f;
        duck.params[co::release] = 140.0f;
        duck.params[co::autoMakeup] = 0.0f;
        bass.inserts.push_back(std::move(duck));
    }
    // Keys: a chorus pedal and a dusty record, its top end automated.
    keys.inserts.push_back(s.preset(plugins::tide::type, "Lush"));
    keys.inserts.push_back(s.preset(plugins::patina::type, "Dusty Vinyl"));
    const auto keysPatina = keys.inserts.back().id;
    // Pad: wide.
    pad.inserts.push_back(s.preset(plugins::widener::type, "Wide Pad"));
    // Lead: a drive pedal into the tube amp at the edge of break-up, then a
    // pedal delay: a synth played like a guitar.
    lead.inserts.push_back(s.preset(plugins::flare::type, "Light Crunch"));
    lead.inserts.push_back(s.preset(plugins::amp::type, "Edge of Breakup"));
    lead.inserts.push_back(s.preset(plugins::comet::type, "Lead Trail", 0.6f));
    // Arp: an EQ to thin it.
    arp.inserts.push_back(s.preset(types::eq, "Low Cut 80 Hz"));
    // The master: glue, tape, a little sparkle and a ceiling.
    p.master.inserts.push_back(s.preset(plugins::glue::type, "Mastering Touch"));
    p.master.inserts.push_back(s.preset(plugins::orbit::type, "Master Polish"));
    p.master.inserts.push_back(s.preset(plugins::exciter::type, "Mix Sparkle", 0.5f));
    p.master.inserts.push_back(s.preset(types::limiter, "Streaming -1 dB"));

    // Three FX tracks: a plate, a shimmer and a dub tape echo.
    auto plate = s.fxTrack(types::reverb, "Plate", "Send: Plate");
    auto shimmer = s.fxTrack(plugins::bloom::type, "Shimmer", "Send: Shimmer");
    auto echo = s.fxTrack(plugins::tapeecho::type, "Dub Echo", "Send: Dub");
    shimmer.gain = 0.6f;
    echo.gain = 0.6f;
    const auto send = [](core::Track& t, core::Id bus, float level)
    { t.sends.push_back({bus, level, false}); };
    send(keys, plate.id, 0.3f);
    send(pad, shimmer.id, 0.35f);
    send(lead, plate.id, 0.25f);
    send(lead, echo.id, 0.3f);
    send(arp, echo.id, 0.4f);
    send(arp, shimmer.id, 0.3f);

    // Panning and levels.
    keys.pan = -0.2f;
    arp.pan = 0.35f;
    lead.pan = 0.1f;
    pad.gain = 0.7f;

    // ----- the channel strip: every channel has one; switch some modules on ---------------------
    plugins::strip::ensureChannelStrip(p);
    const auto on = [](core::Channel& c, const std::string& type)
    {
        for (auto& m : c.strip)
            if (m.type == type)
                m.bypass = false;
    };
    on(bass, plugins::strip::types::saturation);
    on(keys, plugins::strip::types::eq);
    on(lead, plugins::strip::types::compressor);
    on(drums, plugins::strip::types::limiter);

    // ----- automation ----------------------------------------------------------------------------
    using T = core::AutomationTarget;
    // The keys come in as if through an old radio and open at the verse; the
    // outro closes them again.
    s.lane(keys,
           T::insertParameter,
           keysPatina,
           plugins::patina::highCut,
           {{0.0, 1600.0f},
            {3.75, 3000.0f},
            {4.0, 20000.0f},
            {32.0, 20000.0f},
            {35.5, 3000.0f},
            {38.0, 1600.0f}});
    // The choruses lift: the keys, the lead and the pad come up from the verse.
    s.lane(keys, T::gain, core::invalidId, 0, {{4.0, 0.8f}, {11.9, 0.8f}, {12.0, 1.0f}});
    s.lane(lead,
           T::gain,
           core::invalidId,
           0,
           {{4.0, 0.65f}, {11.9, 0.65f}, {12.0, 0.8f}, {32.0, 0.8f}, {32.5, 0.7f}});
    // The pad swells into the bridge and fades at the end.
    s.lane(pad,
           T::gain,
           core::invalidId,
           0,
           {{0.0, 0.0f},
            {3.0, 0.5f},
            {11.9, 0.55f},
            {12.0, 0.8f},
            {20.0, 0.8f},
            {24.0, 0.9f},
            {32.0, 0.7f},
            {38.0, 0.0f}});
    // The bass's filter closes through the bridge and opens into the last chorus.
    s.lane(bass,
           T::insertParameter,
           bass.instrument.id,
           au::cutoff,
           {{19.9, 320.0f}, {20.0, 140.0f}, {23.5, 260.0f}, {24.0, 320.0f}});
    // The arp drifts across the field in the bridge.
    s.lane(arp, T::pan, core::invalidId, 0, {{20.0, 0.35f}, {22.0, -0.45f}, {24.0, 0.35f}});
    // The dub echo runs away at the end of the bridge.
    s.lane(echo,
           T::insertParameter,
           echo.inserts.front().id,
           plugins::tapeecho::feedback,
           {{22.0, 60.0f}, {23.75, 92.0f}, {24.0, 60.0f}});
    // The song fades on its last chord.
    s.lane(p.master, T::gain, core::invalidId, 0, {{36.0, 0.8f}, {37.0, 0.8f}, {38.0, 0.0f}});
    p.buses.push_back(std::move(plate));
    p.buses.push_back(std::move(shimmer));
    p.buses.push_back(std::move(echo));

    // ----- markers -------------------------------------------------------------------------------
    const auto marker = [&](const char* name, int from, int to)
    { p.markers.push_back({s.id(), name, from * demoSongBarLength, (to - from) * demoSongBarLength}); };
    marker("Intro", intro, verse);
    marker("Verse", verse, chorus);
    marker("Chorus", chorus, bridge);
    marker("Bridge", bridge, finale);
    marker("Last chorus", finale, outro);
    marker("Outro", outro, ending);
    marker("End", ending, demoSongBars);
    p.sortMarkers();

    // ----- the drum outputs: a track each, in a closed folder under the drums' channel -------
    // (Last: inserting tracks moves the ones the references above point at.)
    {
        const auto drumsChannel = p.tracks[1];
        const char* names[] = {"", "Kick", "Snares", "Hats"};
        const float gains[] = {1.0f, 1.0f, 0.9f, 0.7f};
        std::vector<core::Track> outputs;
        core::Track folder;
        folder.id = s.id();
        folder.kind = core::TrackKind::folder;
        folder.name = "Drum outputs";
        folder.colour = drumsChannel.colour;
        folder.folderOpen = false; // the song reads as its parts; open it for the kit's pairs
        outputs.push_back(std::move(folder));
        for (int o = 1; o <= 3; ++o)
        {
            core::Track out;
            out.id = s.id();
            out.kind = core::TrackKind::aux;
            out.name = names[o];
            out.colour = drumsChannel.colour;
            out.auxTrack = drumsChannel.id;
            out.auxInsert = drumsChannel.instrument.id;
            out.auxOutput = o;
            out.gain = gains[o];
            out.folder = outputs.front().id;
            outputs.push_back(std::move(out));
        }
        outputs[2].sends.push_back({p.buses[0].id, 0.15f, false}); // the snares on the plate
        outputs[3].pan = 0.15f;                                    // the hats a little right
        // The bass's compressor listens to the kick.
        const auto kick = outputs[1].id;
        for (auto& t : p.tracks)
            if (t.name == "Bass: " + plugins::BuiltInFactory::displayName(au::type))
                for (auto& i : t.inserts)
                    if (i.type == types::compressor)
                        i.sidechain = kick;
        p.tracks.insert(p.tracks.begin() + 2, outputs.begin(), outputs.end());
        plugins::strip::ensureChannelStrip(p); // their strips (off)
    }
    return p;
}

} // namespace northlight
