#include "DemoSong.h"

#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/Cirrus.h"
#include "plugins/EffectPresets.h"
#include "plugins/FxTracks.h"
#include "plugins/Glue.h"
#include "plugins/Lumen.h"
#include "plugins/Nebula.h"
#include "plugins/Orbit.h"
#include "plugins/Patina.h"
#include "plugins/Pulsar.h"
#include "plugins/TapeEcho.h"
#include "plugins/Widener.h"
#include "plugins/midi/Utilities.h"

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
constexpr core::SamplePosition beatLength = demoSongBarLength / 4; // 75 bpm at 48 kHz

core::SamplePosition at(double beats)
{
    return static_cast<core::SamplePosition>(std::llround(beats * static_cast<double>(beatLength)));
}

// The form, in bars: the keys alone, the loop, the second progression with the
// melody, a breakdown with no kick, the loop with the melody, the outro.
constexpr int intro = 0, loop = 4, changes = 12, breakdown = 20, reprise = 24, outro = 32, ending = 36;

// ----- the harmony -------------------------------------------------------------------------------

// A chord: the keys' voicing (low to high, rootless, as a pianist's left hand
// plays them) and the bass note.
struct Chord
{
    std::array<int, 4> keys;
    int bass;
};
constexpr Chord bbMaj9{{62, 65, 69, 72}, 46};
constexpr Chord aM9{{60, 64, 67, 71}, 45};
constexpr Chord gM9{{58, 62, 65, 69}, 43};
constexpr Chord c13{{58, 62, 64, 69}, 36};
constexpr Chord c7Flat9{{58, 61, 64, 68}, 36};
constexpr Chord fMaj9{{57, 60, 64, 67}, 41};
constexpr Chord dM9{{60, 64, 65, 69}, 38};
constexpr Chord d7Flat9{{60, 63, 66, 69}, 38};

// A bar's chords: the first half's and the second's (the same for most bars).
struct BarChords
{
    Chord first, second;
};
// The loop: IV, iii, ii, V (Bbmaj9, Am9, Gm9, C13 to C7b9).
constexpr std::array<BarChords, 4> loopChords{{{bbMaj9, bbMaj9}, {aM9, aM9}, {gM9, gM9}, {c13, c7Flat9}}};
// The changes: ii, V, I, vi with a D7b9 turnaround, then ii, V, iii and VI, ii and V.
constexpr std::array<BarChords, 8> changeChords{{{gM9, gM9},
                                                 {c13, c7Flat9},
                                                 {fMaj9, fMaj9},
                                                 {dM9, d7Flat9},
                                                 {gM9, gM9},
                                                 {c13, c7Flat9},
                                                 {aM9, d7Flat9},
                                                 {gM9, c7Flat9}}};

BarChords chordsAt(int bar)
{
    if (bar >= ending)
        return {fMaj9, fMaj9};
    if (bar >= changes && bar < breakdown)
        return changeChords[static_cast<size_t>(bar - changes)];
    if (bar >= outro) // the changes' second half, home to F
        return changeChords[static_cast<size_t>(4 + bar - outro)];
    return loopChords[static_cast<size_t>(bar % 4)];
}
Chord chordAt(int bar, double beat)
{
    const auto chords = chordsAt(bar);
    return beat < 2.0 ? chords.first : chords.second;
}
bool twoChords(int bar)
{
    const auto c = chordsAt(bar);
    return c.first.keys != c.second.keys;
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
    // channel named for the instrument ("Keys" over "Keys: Lumen").
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

    // Humanise on a MIDI track: up to `lateMs` behind the beat and `velocity` either way.
    void humanise(core::Track& midiTrack, float lateMs, float velocity, float seed)
    {
        namespace hu = plugins::humanise;
        auto h = insert(hu::type);
        h.params[hu::timing] = lateMs;
        h.params[hu::velocity] = velocity;
        h.params[hu::chance] = 100.0f;
        h.params[hu::seed] = seed;
        midiTrack.midiInserts.push_back(std::move(h));
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
// well late: the lazy swing of a sampler's 62%.
double step16(int step)
{
    return step * 0.25 + (step % 2 == 1 ? 0.08 : 0.0);
}

// The drums: a slow boom-bap, on the grid (unswung, so the beat stays steady
// under the keys' swing). The kick on one and the "and" of three (and the "a"
// of one in the second bar), the snare on two and four, soft hats and ghost
// notes.
std::vector<Note> drumBar(int bar, Wobble& w)
{
    namespace pu = plugins::pulsar;
    const int kick = pu::padNote(pu::kick), snare = pu::padNote(pu::snare), hat = pu::padNote(pu::closedHat),
              open = pu::padNote(pu::openHat), rim = pu::padNote(pu::rim), tomLow = pu::padNote(pu::lowTom);
    std::vector<Note> n;
    const double b = bar * 4.0; // the clip starts at the song's start
    const auto hit = [&](int pitch, int velocity, int step)
    { n.push_back({pitch, w.velocity(velocity, 7), b + step * 0.25, 0.15}); };
    const bool second = bar % 2 == 1;
    const bool phraseEnd = bar % 4 == 3;
    const bool changesSection = bar >= changes && bar < breakdown;

    if (bar < loop || bar >= ending)
        return n;
    const auto hats = [&](int loud, int soft)
    {
        for (int e = 0; e < 8; ++e)
            hit(phraseEnd && e == 7 ? open : hat, e % 2 == 0 ? loud : soft, e * 2);
    };
    if (bar >= breakdown && bar < reprise)
    {
        // No kick: the rim, the hats, and the kick's return on the last beat.
        hit(rim, 74, 4);
        hit(rim, 74, 12);
        hats(56, 38);
        if (bar == reprise - 1)
        {
            hit(kick, 96, 14);
            hit(snare, 40, 15);
        }
        return n;
    }
    if (bar >= outro + 2) // the outro thins to the rim and the hats
    {
        hit(rim, 70, 4);
        hit(rim, 66, 12);
        hats(50, 32);
        return n;
    }

    for (const int s : second ? std::vector<int>{0, 3, 10} : std::vector<int>{0, 10})
        hit(kick, s == 0 ? 116 : 98, s);
    for (const int s : {4, 12})
        hit(snare, 108, s);
    if (second)
        hit(snare, 30, 7); // a ghost
    if (changesSection)
        hit(snare, 26, 15);
    hats(changesSection ? 74 : 68, changesSection ? 50 : 44);
    for (const int s : {7, 15})
        hit(hat, 26, s);
    // A small fill into the next section: the last beat goes to the snare and a tom.
    if (bar == changes - 1 || bar == breakdown - 1 || bar == outro - 1)
    {
        std::erase_if(n, [&](const Note& x) { return x.start >= b + 3.0; });
        hit(snare, 54, 13);
        hit(tomLow, 88, 14);
        hit(snare, 72, 15);
    }
    return n;
}

// Whether a pitch is one of a chord's notes (its bass or a voicing's), in any octave.
bool inChord(const Chord& c, int pitch)
{
    const auto same = [&](int other) { return (pitch - other) % 12 == 0; };
    return same(c.bass) || std::any_of(c.keys.begin(), c.keys.end(), same);
}

// The note the bass leads into `target` from while `held` still sounds: the
// nearest of the held chord's notes, from above on a tie (a step down into the
// root). A chromatic step would rub a semitone against the chord the keys hold.
int approachFrom(const Chord& held, int target)
{
    for (int distance = 1; distance <= 6; ++distance)
        for (const int pitch : {target + distance, target - distance})
            if (inChord(held, pitch))
                return pitch;
    return held.bass;
}

// The bass: the root on one, the fifth on the "and" of three and a step from
// a note of the chord into the next bar's root, or the second chord's root on
// three.
std::vector<Note> bassBar(int bar, Wobble& w)
{
    const double b = bar * 4.0;
    const int root = chordAt(bar, 0.0).bass;
    const int later = chordAt(bar, 2.0).bass;
    if (bar >= ending)
        return {{fMaj9.bass, 108, b, 6.0}};
    if (bar >= breakdown && bar < reprise) // long notes while the filter closes
        return {{root, 96, b, 3.8}};
    if (bar >= outro + 2)
        return {{root, 100, b, 1.9}, {later, 96, b + 2.0, 1.9}};
    std::vector<Note> n;
    n.push_back({root, w.velocity(110, 5), b, 1.6});
    if (later != root)
    {
        n.push_back({later, w.velocity(100, 5), b + 2.0, 1.4});
        n.push_back({later + 12, w.velocity(72, 5), b + step16(14), 0.35});
        return n;
    }
    // The fifth, or the root an octave up where the keys' voicing leaves the
    // fifth out (C7b9's flat thirteenth is a semitone above it).
    const int up = root + 7 <= 48 ? root + 7 : root - 5;
    const int fifth = inChord(chordAt(bar, 2.0), up) ? up : root + 12;
    const int next = chordAt(bar + 1, 0.0).bass;
    n.push_back({fifth, w.velocity(90, 5), b + step16(10), 0.7});
    n.push_back({approachFrom(chordAt(bar, 2.0), next), w.velocity(80, 5), b + step16(14), 0.4});
    return n;
}

// The keys: each chord rolled slowly. Held in the intro, the breakdown and the
// last bars; elsewhere struck on one with the top three again on the "a" of
// three, or with the second chord anticipated on the "a" of two.
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
        roll(fMaj9, b, 7.5, 72, 0.09);
        n.push_back({74, 58, b + 0.45, 7.0}); // the thirteenth on top
        return n;
    }
    const bool held = bar < loop || (bar >= breakdown && bar < reprise) || bar >= outro + 2;
    if (held)
    {
        if (!twoChords(bar))
            roll(first, b, 3.9, 68, 0.06);
        else
        {
            roll(first, b, 1.95, 68, 0.06);
            roll(second, b + 2.0, 1.95, 64, 0.06);
        }
        return n;
    }
    if (twoChords(bar))
    {
        roll(first, b, 1.75, 80, 0.035);
        roll(second, b + step16(7), 2.1, 72, 0.035);
        return n;
    }
    roll(first, b, 2.6, 80, 0.035);
    roll(first, b + step16(11), 1.0, 56, 0.025, 1);
    return n;
}

// The melody's lines (beats from their section's start): one over the changes
// and one over the loop.
const std::vector<Note>& changesLine()
{
    static const std::vector<Note> line{
        // Gm9
        {74, 88, 1.5, 0.5},
        {77, 96, 2.0, 1.0},
        {76, 84, 3.0, 0.5},
        {74, 84, 3.5, 0.5},
        // C13 C7b9
        {72, 92, 4.0, 1.5},
        {69, 80, 5.5, 0.5},
        {68, 84, 6.0, 0.5},
        {67, 88, 6.5, 1.5},
        // Fmaj9
        {69, 80, 8.5, 0.5},
        {72, 86, 9.0, 0.5},
        {76, 96, 9.5, 1.5},
        {79, 90, 11.0, 0.5},
        {76, 84, 11.5, 0.5},
        // Dm9 D7b9
        {77, 94, 12.0, 1.0},
        {76, 84, 13.0, 0.5},
        {74, 84, 13.5, 0.5},
        {78, 90, 14.0, 0.5},
        {75, 88, 14.5, 1.25},
        // Gm9
        {74, 86, 16.5, 0.5},
        {70, 80, 17.0, 0.5},
        {69, 80, 17.5, 0.5},
        {65, 88, 18.0, 1.5},
        // C13 C7b9
        {67, 80, 20.5, 0.5},
        {69, 84, 21.0, 0.5},
        {76, 94, 21.5, 1.0},
        {74, 84, 22.5, 0.5},
        {73, 88, 23.0, 0.75},
        // Am9 D7b9
        {72, 90, 24.0, 1.0},
        {76, 86, 25.0, 0.5},
        {78, 94, 26.0, 1.0},
        {75, 84, 27.0, 0.5},
        {72, 82, 27.5, 0.5},
        // Gm9 C7b9
        {74, 92, 28.0, 1.5},
        {70, 80, 29.5, 0.5},
        {73, 84, 30.0, 0.5},
        {70, 80, 30.5, 0.5},
        {67, 86, 31.0, 0.75},
    };
    return line;
}
const std::vector<Note>& loopLine()
{
    static const std::vector<Note> line{
        // Bbmaj9
        {77, 92, 0.5, 0.5},
        {74, 84, 1.0, 0.5},
        {72, 90, 1.5, 1.5},
        {69, 80, 3.0, 0.5},
        {72, 84, 3.5, 0.5},
        // Am9
        {76, 94, 4.0, 1.5},
        {74, 80, 5.5, 0.25},
        {72, 80, 5.75, 0.25},
        {71, 88, 6.0, 1.0},
        {67, 80, 7.0, 0.5},
        // Gm9
        {70, 82, 8.5, 0.5},
        {74, 86, 9.0, 0.5},
        {77, 92, 9.5, 1.0},
        {81, 98, 10.5, 1.5},
        // C13 C7b9
        {79, 90, 12.0, 1.0},
        {76, 84, 13.0, 0.5},
        {74, 82, 13.5, 0.5},
        {73, 86, 14.0, 0.5},
        {70, 80, 14.5, 0.5},
        {67, 84, 15.0, 0.75},
        // Bbmaj9 Am9 again, then an answer that leaves room for the echo
        {77, 92, 16.5, 0.5},
        {74, 84, 17.0, 0.5},
        {72, 90, 17.5, 1.5},
        {69, 80, 19.0, 0.5},
        {72, 84, 19.5, 0.5},
        {76, 94, 20.0, 2.5},
        {81, 96, 26.0, 2.0},
        {79, 86, 28.5, 0.5},
        {77, 84, 29.0, 0.5},
        {76, 90, 29.5, 2.0},
    };
    return line;
}

void append(std::vector<Note>& to, const std::vector<Note>& line, double offset, double from = 0.0)
{
    for (const auto& x : line)
        if (x.start >= from)
            to.push_back({x.pitch, x.velocity, offset + x.start - from, x.length});
}
} // namespace

core::Project makeDemoSong()
{
    namespace au = plugins::aurora;
    namespace ci = plugins::cirrus;
    namespace lu = plugins::lumen;
    namespace ne = plugins::nebula;
    namespace pu = plugins::pulsar;
    Builder s;
    auto& p = s.project;
    p.name = "Window Seat";
    p.sampleRate = sampleRate;
    p.tempoBpm = demoSongBpm;
    p.timeSignature = {4, 4};
    p.key.root = 5; // F
    p.key.scale = core::ScaleKind::major;
    p.master.gain = 0.8f;

    // ----- tracks (the references are taken after every track is in) ---------------------------
    s.instrumentTrack("Drums", pu::type, presetValues(pu::kits(), "Dusty"));
    s.instrumentTrack("Bass", au::type, presetValues(au::presets(), "Polar Bass"));
    s.instrumentTrack("Keys", lu::type, presetValues(lu::presets(), "Lo-fi Keys"));
    s.instrumentTrack("Melody", ci::type, presetValues(ci::presets(), "Evening Bells"));
    s.instrumentTrack("Pad", ne::type, presetValues(ne::presets(), "Drifting Veil"));
    // Each part: its MIDI track (the notes) and its instrument channel (the sound).
    auto& drumsMidi = p.tracks[0];
    auto& drums = p.tracks[1];
    auto& bassMidi = p.tracks[2];
    auto& bass = p.tracks[3];
    auto& keysMidi = p.tracks[4];
    auto& keys = p.tracks[5];
    auto& melodyMidi = p.tracks[6];
    auto& melody = p.tracks[7];
    auto& padMidi = p.tracks[8];
    auto& pad = p.tracks[9];

    // A few touches on the presets.
    drums.instrument.params[pu::output] = -3.0f;
    {
        // A slower, duller kit: the snare tuned down and longer, the hats softer.
        auto& d = drums.instrument.params;
        d[static_cast<size_t>(pu::paramIndex(pu::snare, pu::tune))] -= 2.0f;
        d[static_cast<size_t>(pu::paramIndex(pu::kick, pu::tune))] -= 1.0f;
        d[static_cast<size_t>(pu::paramIndex(pu::closedHat, pu::level))] -= 3.0f;
    }
    bass.instrument.params[au::level] = -9.5f;
    bass.instrument.params[au::cutoff] = 260.0f; // round, under the keys
    keys.instrument.params[lu::level] = 4.0f;    // the piano leads
    melody.instrument.params[ci::outputLevel] = -5.0f;
    pad.instrument.params[ne::level] = -18.0f;
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
        Wobble w(0x61u);
        std::vector<Note> n;
        for (int bar = loop; bar < ending; ++bar)
            for (const auto& x : drumBar(bar, w))
                n.push_back(x);
        s.clip(drumsMidi, "Boom bap", intro, ending, n);
    }
    {
        Wobble w(0x62u);
        std::vector<Note> n;
        for (int bar = loop; bar <= ending; ++bar)
            for (const auto& x : bassBar(bar, w))
                n.push_back({x.pitch, x.velocity, x.start - loop * 4.0, x.length});
        s.clip(bassMidi, "Bass line", loop, ending + 2, n);
    }
    {
        Wobble w(0x63u);
        std::vector<Note> n;
        for (int bar = intro; bar <= ending; ++bar)
            for (const auto& x : keysBar(bar, w))
                n.push_back(x);
        s.clip(keysMidi, "Chords", intro, ending + 2, n);
    }
    {
        // The melody: the changes, the loop with the reprise twice through,
        // the changes' second half in the outro and a last note.
        std::vector<Note> n;
        append(n, changesLine(), 0.0);
        append(n, loopLine(), (reprise - changes) * 4.0);
        append(n, changesLine(), (outro - changes) * 4.0, 16.0);
        n.push_back({76, 80, (ending - changes) * 4.0 + 0.5, 5.0});
        s.clip(melodyMidi, "Bells", changes, ending + 2, n);
    }
    {
        // The pad holds each chord an octave up, softly, from the changes on
        // (and through the breakdown, where it carries the harmony).
        std::vector<Note> n;
        for (int bar = changes; bar < ending; ++bar)
            for (const double half : {0.0, 2.0})
                for (const int t : chordAt(bar, half).keys)
                    n.push_back({t + 12, 70, (bar - changes) * 4.0 + half, 2.0});
        for (const int t : fMaj9.keys)
            n.push_back({t + 12, 72, (ending - changes) * 4.0, 7.0});
        s.clip(padMidi, "Haze", changes, ending + 2, n);
    }
    // The keys and the melody a touch behind the beat, never quite the same.
    s.humanise(keysMidi, 14.0f, 8.0f, 75.0f);
    s.humanise(melodyMidi, 10.0f, 10.0f, 1975.0f);

    // ----- effects -------------------------------------------------------------------------------
    namespace types = plugins::types;
    // Drums: crushed like an old sampler, then glued.
    drums.inserts.push_back(s.preset(plugins::patina::type, "Lo-Fi Sampler", 0.6f));
    drums.inserts.push_back(s.preset(plugins::glue::type, "Drum Bus Punch"));
    // Bass: ducked under the kick (the key is set once the kick's track exists).
    {
        auto duck = s.insert(types::compressor);
        namespace co = plugins::strip::compressor;
        duck.params[co::threshold] = -26.0f;
        duck.params[co::ratio] = 4.0f;
        duck.params[co::attack] = 3.0f;
        duck.params[co::release] = 180.0f;
        duck.params[co::autoMakeup] = 0.0f;
        bass.inserts.push_back(std::move(duck));
    }
    // Keys: a worn cassette, its top end automated. Its wow and flutter are
    // turned well down: at the preset's they bend the chords about 20 cents
    // against the steady bass, and the bass sounds out of tune.
    keys.inserts.push_back(s.preset(plugins::patina::type, "Worn Cassette"));
    keys.inserts.back().params[plugins::patina::driftWow] = 10.0f;
    keys.inserts.back().params[plugins::patina::driftFlutter] = 8.0f;
    const auto keysPatina = keys.inserts.back().id;
    // Melody: a faded film's wobble, half in.
    melody.inserts.push_back(s.preset(plugins::patina::type, "Faded Film", 0.5f));
    // Pad: thinned and wide.
    pad.inserts.push_back(s.preset(types::eq, "Low Cut 80 Hz"));
    pad.inserts.push_back(s.preset(plugins::widener::type, "Wide Pad"));
    // The master: a record's crackle, a worn reel, glue and a ceiling.
    p.master.inserts.push_back(s.preset(plugins::patina::type, "Dusty Vinyl", 0.45f));
    p.master.inserts.push_back(s.preset(plugins::orbit::type, "Worn Reel"));
    p.master.inserts.push_back(s.preset(plugins::glue::type, "Mastering Touch"));
    p.master.inserts.push_back(s.preset(types::limiter, "Streaming -1 dB"));

    // Three FX tracks: a room for the drums, a plate and a tape echo.
    auto plate = s.fxTrack(types::reverb, "Plate", "Send: Plate");
    auto room = s.fxTrack(types::reverb, "Room", "Send: Room");
    auto echo = s.fxTrack(plugins::tapeecho::type, "Tape Echo", "Send: Dub");
    room.gain = 0.7f;
    echo.gain = 0.6f;
    const auto send = [](core::Track& t, core::Id bus, float level)
    { t.sends.push_back({bus, level, false}); };
    send(keys, plate.id, 0.22f);
    send(keys, echo.id, 0.12f);
    send(melody, plate.id, 0.3f);
    send(melody, echo.id, 0.35f);
    send(pad, plate.id, 0.3f);

    // Panning and levels.
    keys.pan = -0.1f;
    melody.pan = 0.2f;
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
    on(drums, plugins::strip::types::limiter);

    // ----- automation ----------------------------------------------------------------------------
    using T = core::AutomationTarget;
    // The keys come in muffled, open when the drums drop, close for the
    // breakdown and fade back into the tape in the outro.
    s.lane(keys,
           T::insertParameter,
           keysPatina,
           plugins::patina::highCut,
           {{0.0, 1400.0f},
            {3.75, 2600.0f},
            {4.0, 20000.0f},
            {19.9, 20000.0f},
            {20.0, 2400.0f},
            {23.75, 4500.0f},
            {24.0, 20000.0f},
            {34.0, 20000.0f},
            {38.0, 1400.0f}});
    // The tremolo deepens in the breakdown.
    s.lane(keys,
           T::insertParameter,
           keys.instrument.id,
           lu::tremoloDepth,
           {{19.9, 30.0f}, {20.0, 55.0f}, {23.9, 55.0f}, {24.0, 30.0f}});
    // The pad fades in with the changes and out at the end.
    s.lane(pad,
           T::gain,
           core::invalidId,
           0,
           {{12.0, 0.0f}, {14.0, 0.6f}, {20.0, 0.6f}, {21.0, 0.85f}, {24.0, 0.6f}, {38.0, 0.0f}});
    // The bass's filter closes through the breakdown and opens for the reprise.
    s.lane(bass,
           T::insertParameter,
           bass.instrument.id,
           au::cutoff,
           {{19.9, 260.0f}, {20.0, 130.0f}, {23.5, 220.0f}, {24.0, 260.0f}});
    // The tape echo runs away at the end of the breakdown.
    s.lane(echo,
           T::insertParameter,
           echo.inserts.front().id,
           plugins::tapeecho::feedback,
           {{22.0, 55.0f}, {23.75, 88.0f}, {24.0, 55.0f}});
    // The song fades on its last chord.
    s.lane(p.master, T::gain, core::invalidId, 0, {{36.0, 0.8f}, {37.0, 0.8f}, {38.0, 0.0f}});
    p.buses.push_back(std::move(plate));
    p.buses.push_back(std::move(room));
    p.buses.push_back(std::move(echo));

    // ----- markers -------------------------------------------------------------------------------
    const auto marker = [&](const char* name, int from, int to)
    { p.markers.push_back({s.id(), name, from * demoSongBarLength, (to - from) * demoSongBarLength}); };
    marker("Intro", intro, loop);
    marker("Loop", loop, changes);
    marker("Changes", changes, breakdown);
    marker("Breakdown", breakdown, reprise);
    marker("Reprise", reprise, outro);
    marker("Outro", outro, ending);
    marker("End", ending, demoSongBars);
    p.sortMarkers();

    // ----- the drum outputs: a track each, in a closed folder under the drums' channel -------
    // (Last: inserting tracks moves the ones the references above point at.)
    {
        const auto drumsChannel = p.tracks[1];
        const auto roomId = p.buses[1].id;
        const char* names[] = {"", "Kick", "Snares", "Hats"};
        const float gains[] = {1.0f, 1.0f, 0.9f, 0.75f};
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
        outputs[2].sends.push_back({roomId, 0.25f, false}); // the snares in the room
        outputs[3].sends.push_back({roomId, 0.1f, false});
        outputs[3].pan = 0.15f; // the hats a little right
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
