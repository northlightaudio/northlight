#include "DemoSong.h"

#include "engine/WavFile.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "plugins/Nebula.h"
#include "plugins/Pulsar.h"
#include "plugins/Starbit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace northlight
{

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double tempo = 120.0;
constexpr core::SamplePosition beatLength = 24000; // 120 bpm at 48 kHz
constexpr core::SamplePosition barLength = 4 * beatLength;
constexpr int bars = 33; // 32 and a last chord

core::SamplePosition at(double beats)
{
    return static_cast<core::SamplePosition>(std::llround(beats * static_cast<double>(beatLength)));
}

// The form, in bars.
constexpr int intro = 0, verse = 4, chorus = 12, breakdown = 20, finale = 24, ending = 32;

// Am F C G, a bar each: the chord tones (low to high) and the bass root.
struct Chord
{
    std::array<int, 3> tones;
    int root;
};
constexpr std::array<Chord, 4> progression{{
    {{57, 60, 64}, 45}, // Am
    {{53, 57, 60}, 41}, // F
    {{55, 60, 64}, 48}, // C
    {{55, 59, 62}, 43}, // G
}};
const Chord& chordAt(int bar)
{
    return progression[static_cast<size_t>(bar % 4)];
}

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

    // A clip over bars [from, to), the notes given in beats from the clip's start.
    struct Note
    {
        int pitch;
        int velocity;
        double start, length;
    };
    void clip(core::Track& track, const std::string& name, int from, int to, const std::vector<Note>& notes)
    {
        core::MidiClip c;
        c.id = id();
        c.name = name;
        c.start = from * barLength;
        c.length = (to - from) * barLength;
        for (const auto& n : notes)
            c.notes.push_back(
                {id(), n.pitch, n.velocity, at(n.start), std::max<core::SamplePosition>(1, at(n.length))});
        c.sortNotes();
        track.midiClips.push_back(std::move(c));
    }

    void lane(core::Track& track,
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
        track.automation.push_back(std::move(l));
    }
};

template <typename List> std::vector<float> preset(const List& list, const std::string& name)
{
    for (const auto& p : list)
        if (p.name == name)
            return p.values;
    return list.front().values;
}

// ----- the parts ----------------------------------------------------------------------------------

std::vector<Builder::Note> drumBar(int bar)
{
    namespace pu = plugins::pulsar;
    const int kick = pu::padNote(pu::kick), snare = pu::padNote(pu::snare), clap = pu::padNote(pu::clap),
              hat = pu::padNote(pu::closedHat), open = pu::padNote(pu::openHat), rim = pu::padNote(pu::rim),
              tomLow = pu::padNote(pu::lowTom), tomHigh = pu::padNote(pu::highTom);
    std::vector<Builder::Note> n;
    const double b = (bar - verse) * 4.0; // within the drum clip, which starts at the verse
    const bool inVerse = bar >= verse && bar < chorus;
    const bool inChorus = (bar >= chorus && bar < breakdown) || (bar >= finale && bar < ending);
    const bool inBreakdown = bar >= breakdown && bar < finale;
    const bool fill = bar == chorus - 1 || bar == breakdown - 1 || bar == finale - 1 || bar == ending - 1;
    if (inBreakdown)
    {
        // Just the hats, sixteenths, breathing.
        for (int s = 0; s < 16; ++s)
            n.push_back({hat, s % 4 == 0 ? 70 : s % 2 == 0 ? 50 : 34, b + s * 0.25, 0.1});
        if (bar == finale - 1)
            for (int s = 0; s < 8; ++s) // a snare roll into the finale
                n.push_back({snare, 50 + s * 9, b + 2.0 + s * 0.25, 0.1});
        return n;
    }
    if (inVerse)
    {
        for (const double k : {0.0, 2.0, 2.5})
            n.push_back({kick, 118, b + k, 0.1});
        for (int e = 0; e < 8; ++e)
            n.push_back({hat, e % 2 == 0 ? 84 : 56, b + e * 0.5, 0.1});
        if (bar >= verse + 4) // the second half of the verse claps
            for (const double c : {1.0, 3.0})
                n.push_back({clap, 96, b + c, 0.1});
        if (bar % 2 == 1)
            n.push_back({rim, 70, b + 3.5, 0.1});
    }
    if (inChorus)
    {
        for (int k = 0; k < 4; ++k)
            n.push_back({kick, k == 0 ? 124 : 110, b + k, 0.1});
        for (const double s : {1.0, 3.0})
        {
            n.push_back({snare, 112, b + s, 0.1});
            n.push_back({clap, 88, b + s, 0.1});
        }
        for (int e = 0; e < 8; ++e)
            n.push_back(e % 2 == 1 ? Builder::Note{open, 76, b + e * 0.5, 0.1}
                                   : Builder::Note{hat, 90, b + e * 0.5, 0.1});
    }
    if (fill && !inBreakdown)
    {
        // Toms down the last two beats (replacing what was there).
        std::erase_if(n, [&](const Builder::Note& x) { return x.start >= b + 2.0; });
        const int toms[] = {tomHigh, tomHigh, tomLow, tomLow, snare, snare, snare, snare};
        for (int s = 0; s < 8; ++s)
            n.push_back({toms[s], 92 + s * 4, b + 2.0 + s * 0.25, 0.1});
    }
    return n;
}

// The lead: two four-bar phrases over Am F C G (beats from the phrase start).
const std::vector<Builder::Note>& phraseA()
{
    static const std::vector<Builder::Note> p{
        {76, 100, 0.0, 1.0},
        {74, 90, 1.0, 0.5},
        {72, 92, 1.5, 0.5},
        {69, 100, 2.0, 2.0}, // Am
        {72, 96, 4.0, 1.0},
        {77, 104, 5.0, 1.0},
        {76, 92, 6.0, 0.5},
        {74, 90, 6.5, 0.5},
        {72, 96, 7.0, 1.0}, // F
        {76, 100, 8.0, 1.5},
        {79, 104, 9.5, 0.5},
        {76, 94, 10.0, 1.0},
        {72, 92, 11.0, 1.0}, // C
        {74, 100, 12.0, 1.0},
        {71, 92, 13.0, 1.0},
        {74, 96, 14.0, 0.5},
        {76, 100, 14.5, 0.5},
        {74, 96, 15.0, 1.0}, // G
    };
    return p;
}
const std::vector<Builder::Note>& phraseB()
{
    static const std::vector<Builder::Note> p{
        {81, 108, 0.0, 1.5},
        {79, 96, 1.5, 0.5},
        {76, 100, 2.0, 1.0},
        {72, 94, 3.0, 1.0}, // Am
        {77, 104, 4.0, 1.0},
        {76, 96, 5.0, 0.5},
        {74, 94, 5.5, 0.5},
        {72, 100, 6.0, 2.0}, // F
        {76, 98, 8.0, 0.5},
        {79, 100, 8.5, 0.5},
        {84, 112, 9.0, 1.5},
        {79, 98, 10.5, 0.5},
        {76, 96, 11.0, 1.0}, // C
        {74, 100, 12.0, 1.0},
        {71, 94, 13.0, 1.0},
        {74, 104, 14.0, 2.0}, // G
    };
    return p;
}

// The voice's line through the verse (beats from the verse's start): four
// two-bar phrases with a breath before each, and how far off pitch each note is
// sung (cents), so Correct has work to do.
struct SungNote
{
    int pitch;
    double start, length;
    float cents;
    int vowel; // 0 ah, 1 oh, 2 ee, 3 eh
};
const std::vector<SungNote>& voiceLine()
{
    static const std::vector<SungNote> line{
        // Am F
        {64, 0.0, 1.5, 0.0f, 0},
        {62, 1.5, 0.5, -20.0f, 1},
        {60, 2.0, 1.0, 8.0f, 0},
        {57, 3.0, 1.0, -30.0f, 3},
        {60, 4.0, 1.0, 5.0f, 2},
        {62, 5.0, 1.0, 18.0f, 1},
        {60, 6.0, 1.5, -12.0f, 0},
        // C G
        {64, 8.0, 1.0, -6.0f, 3},
        {67, 9.0, 1.0, -35.0f, 0},
        {64, 10.0, 1.5, 10.0f, 1},
        {62, 11.5, 0.5, 0.0f, 2},
        {62, 12.0, 1.0, -18.0f, 0},
        {59, 13.0, 1.0, 22.0f, 3},
        {62, 14.0, 1.5, -8.0f, 1},
        // Am F
        {69, 16.0, 1.5, -25.0f, 0},
        {67, 17.5, 0.5, 6.0f, 2},
        {64, 18.0, 1.0, -10.0f, 1},
        {60, 19.0, 1.0, 15.0f, 0},
        {65, 20.0, 1.0, -28.0f, 3},
        {64, 21.0, 0.5, 4.0f, 0},
        {62, 21.5, 0.5, -14.0f, 2},
        {60, 22.0, 1.5, 9.0f, 1},
        // C G, the last note held
        {64, 24.0, 1.0, 0.0f, 0},
        {67, 25.0, 0.5, 12.0f, 3},
        {72, 25.5, 1.5, -32.0f, 0},
        {67, 27.0, 1.0, 8.0f, 1},
        {62, 28.0, 1.0, -16.0f, 2},
        {59, 29.0, 1.0, 20.0f, 0},
        {62, 30.0, 1.75, -6.0f, 1},
    };
    return line;
}
} // namespace

std::vector<float> demoVoice(double rate)
{
    constexpr double pi = 3.14159265358979323846;
    const double secondsPerBeat = 60.0 / tempo;
    const auto total = static_cast<size_t>(32.0 * secondsPerBeat * rate); // eight bars
    std::vector<float> out(total, 0.0f);
    const auto& line = voiceLine();
    // The vowels' formants (Hz) and the soft resonance they make.
    constexpr double formants[4][3] = {
        {730, 1090, 2440}, {570, 840, 2410}, {300, 2200, 2900}, {530, 1840, 2480}};
    const auto envelope = [&](double hz, double f1, double f2, double f3)
    {
        const auto peak = [hz](double f, double w) { return std::exp(-((hz - f) / w) * ((hz - f) / w)); };
        return 0.25 + peak(f1, 120.0) + 0.6 * peak(f2, 150.0) + 0.3 * peak(f3, 220.0);
    };
    std::uint32_t noise = 0x1234567u;
    const auto random = [&noise]
    {
        noise = noise * 1664525u + 1013904223u;
        return static_cast<double>(noise >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    };
    double phase = 0.0, breath = 0.0, level = 0.0;
    double f1 = formants[0][0], f2 = formants[0][1], f3 = formants[0][2];
    std::array<double, 64> amps{};
    int harmonics = 0;
    for (size_t i = 0; i < total; ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double beat = t / secondsPerBeat;
        // The note sung now (or the one just before, ringing out), and its neighbours.
        int current = -1;
        for (int k = 0; k < static_cast<int>(line.size()); ++k)
            if (beat >= line[static_cast<size_t>(k)].start &&
                beat < line[static_cast<size_t>(k)].start + line[static_cast<size_t>(k)].length)
                current = k;
        double target = 0.0; // amplitude
        double hz = 0.0;
        if (current >= 0)
        {
            const auto& n = line[static_cast<size_t>(current)];
            const double into = (beat - n.start) * secondsPerBeat;
            const double left = (n.start + n.length - beat) * secondsPerBeat;
            double cents = (n.pitch - 69) * 100.0 + n.cents;
            // The way in: a slide from the note before when it is joined (legato),
            // else a scoop up from 60 cents under.
            const bool joined =
                current > 0 && std::abs(line[static_cast<size_t>(current - 1)].start +
                                        line[static_cast<size_t>(current - 1)].length - n.start) < 1.0e-9;
            if (into < 0.04)
            {
                const double s = into / 0.04;
                const double ease = s * s * (3.0 - 2.0 * s);
                const double from = joined ? (line[static_cast<size_t>(current - 1)].pitch - 69) * 100.0 +
                                                 line[static_cast<size_t>(current - 1)].cents
                                           : cents - 60.0;
                cents = from + (cents - from) * ease;
            }
            // Vibrato on held notes, growing in after a quarter of a second.
            const double depth = std::clamp((into - 0.25) / 0.3, 0.0, 1.0) * 32.0;
            cents += depth * std::sin(2.0 * pi * 5.4 * into);
            // A little slow drift.
            cents += 6.0 * std::sin(2.0 * pi * 0.7 * t + current);
            hz = 440.0 * std::pow(2.0, cents / 1200.0);
            target = std::clamp(into / 0.03, 0.0, 1.0) * std::clamp(left / 0.05, 0.0, 1.0);
            const auto& v = formants[n.vowel];
            f1 += (v[0] - f1) * 0.0015;
            f2 += (v[1] - f2) * 0.0015;
            f3 += (v[2] - f3) * 0.0015;
        }
        level += (target - level) * 0.004; // ~5 ms smoothing
        double s = 0.0;
        if (hz > 0.0 || level > 1.0e-4)
        {
            if (hz <= 0.0)
                hz = 220.0;
            phase += 2.0 * pi * hz / rate;
            if (phase > 2.0 * pi * 1000.0)
                phase -= 2.0 * pi * 1000.0;
            // The harmonics' levels through the formants, updated every 32 samples.
            if (i % 32 == 0 || harmonics == 0)
            {
                harmonics = std::min(static_cast<int>(amps.size()), static_cast<int>(5000.0 / hz));
                for (int k = 1; k <= harmonics; ++k)
                    amps[static_cast<size_t>(k - 1)] = envelope(k * hz, f1, f2, f3) / k;
            }
            if (level > 1.0e-4)
                for (int k = 1; k <= harmonics; ++k)
                    s += amps[static_cast<size_t>(k - 1)] * std::sin(k * phase);
        }
        // Breaths: noise for a third of a second before each phrase.
        double air = 0.0;
        for (const double phraseStart : {0.0, 8.0, 16.0, 24.0})
        {
            const double before = (phraseStart - beat) * secondsPerBeat;
            if (before > 0.0 && before < 0.35)
                air = std::sin(pi * before / 0.35);
        }
        breath += 0.2 * (random() - breath);
        out[i] = static_cast<float>(0.22 * level * s + 0.05 * air * breath);
    }
    // To -6 dBFS.
    float peak = 0.0f;
    for (const float v : out)
        peak = std::max(peak, std::abs(v));
    if (peak > 0.0f)
        for (auto& v : out)
            v *= 0.5f / peak;
    return out;
}

core::Project makeDemoSong(const std::string& mediaFolder)
{
    namespace au = plugins::aurora;
    namespace ne = plugins::nebula;
    namespace pu = plugins::pulsar;
    namespace sb = plugins::starbit;
    Builder s;
    auto& p = s.project;
    p.name = "Northern Drift";
    p.sampleRate = sampleRate;
    p.tempoBpm = tempo;
    p.timeSignature = {4, 4};
    p.master.gain = 0.7f; // headroom: the finale peaks near -1 dBFS

    // ----- tracks (the references are taken after every track is in) ---------------------------
    s.instrumentTrack("Drums", pu::type, preset(pu::kits(), "Tight Electro"));
    s.instrumentTrack("Bass", au::type, preset(au::presets(), "Polar Bass"));
    s.instrumentTrack("Pad", ne::type, preset(ne::presets(), "Choir of Dust"));
    s.instrumentTrack("Keys", au::type, preset(au::presets(), "Glacier Keys"));
    s.instrumentTrack("Arp", sb::type, preset(sb::presets(), "Chord Arp"));
    s.instrumentTrack("Lead", sb::type, preset(sb::presets(), "Hero Lead"));
    // The voice through the verse: an audio clip, rendered next to the project.
    bool hasVoice = false;
    if (!mediaFolder.empty())
    {
        const auto path = mediaFolder + "/Northern Drift - Voice.wav";
        const auto samples = demoVoice(sampleRate);
        engine::WavWriter writer;
        const float* channels[] = {samples.data()};
        if (writer.open(path, 1, sampleRate) && writer.write(channels, static_cast<int>(samples.size())) &&
            writer.close())
        {
            core::Track voice;
            voice.id = s.id();
            voice.name = "Voice";
            voice.kind = core::TrackKind::audio;
            core::AudioClip c;
            c.id = s.id();
            c.name = "Voice";
            c.mediaPath = path;
            c.start = verse * barLength;
            c.length = static_cast<core::SamplePosition>(samples.size());
            c.fadeOut = at(0.5);
            voice.clips.push_back(std::move(c));
            p.tracks.push_back(std::move(voice));
            hasVoice = true;
        }
    }
    // Each part: its MIDI track (the notes) and its instrument channel (the sound).
    auto& drumsMidi = p.tracks[0];
    auto& drums = p.tracks[1];
    auto& bassMidi = p.tracks[2];
    auto& bass = p.tracks[3];
    auto& padMidi = p.tracks[4];
    auto& pad = p.tracks[5];
    auto& keysMidi = p.tracks[6];
    auto& keys = p.tracks[7];
    auto& arpMidi = p.tracks[8];
    auto& arp = p.tracks[9];
    auto& leadMidi = p.tracks[10];
    auto& lead = p.tracks[11];

    // A few touches on the presets.
    arp.instrument.params[sb::arpRate] = 16.0f; // sixteenths of a triplet feel at 120
    arp.instrument.params[sb::echo] = 0.0f;     // the Echo track does it
    arp.instrument.params[sb::level] = -12.0f;
    lead.instrument.params[sb::echo] = 0.0f;
    lead.instrument.params[sb::level] = -8.0f;
    lead.instrument.params[sb::duty] = 1.0f; // 25%: a thinner, singing pulse
    keys.instrument.params[au::level] = -10.0f;
    pad.instrument.params[ne::level] = -8.0f;
    bass.instrument.params[au::level] = -6.0f;
    drums.instrument.params[pu::output] = -4.0f;
    // The kit in groups: the kick on the main pair; snares, hats and toms each
    // on an output of its own (their tracks are added at the end).
    {
        constexpr std::array<int, pu::padCount> groups{0, 1, 1, 1, 2, 2, 3, 3};
        for (int padIndex = 0; padIndex < pu::padCount; ++padIndex)
            drums.instrument.params[static_cast<size_t>(pu::paramIndex(padIndex, pu::route))] =
                static_cast<float>(groups[static_cast<size_t>(padIndex)]);
    }

    // ----- parts ---------------------------------------------------------------------------------
    {
        std::vector<Builder::Note> n;
        for (int bar = verse; bar < ending; ++bar)
            for (const auto& x : drumBar(bar))
                n.push_back(x);
        s.clip(drumsMidi, "Beat", verse, ending, n);
    }
    {
        // Eighths on the root (the last one up an octave); whole notes in the breakdown.
        std::vector<Builder::Note> n;
        for (int bar = verse; bar < ending; ++bar)
        {
            const double b = (bar - verse) * 4.0;
            const int root = chordAt(bar).root;
            if (bar >= breakdown && bar < finale)
                n.push_back({root, 96, b, 3.9});
            else
                for (int e = 0; e < 8; ++e)
                    n.push_back({e == 7 ? root + 12 : root, e % 2 == 0 ? 112 : 92, b + e * 0.5, 0.42});
        }
        n.push_back({45, 112, (ending - verse) * 4.0, 3.5}); // the last chord's A
        s.clip(bassMidi, "Bass line", verse, ending + 1, n);
    }
    {
        // The pad holds every chord, a bar each, through the whole song.
        std::vector<Builder::Note> n;
        for (int bar = intro; bar < ending; ++bar)
            for (const int t : chordAt(bar).tones)
                n.push_back({t, 80, bar * 4.0, 4.0});
        for (const int t : {57, 60, 64, 69})
            n.push_back({t, 84, ending * 4.0, 4.0});
        s.clip(padMidi, "Chords", intro, ending + 1, n);
    }
    {
        // Keys: off-beat stabs in the choruses, held chords in the breakdown.
        std::vector<Builder::Note> n;
        for (int bar = chorus; bar < ending; ++bar)
        {
            const double b = (bar - chorus) * 4.0;
            const auto& c = chordAt(bar);
            if (bar >= breakdown && bar < finale)
                for (const int t : c.tones)
                    n.push_back({t + 12, 76, b, 3.8});
            else
                for (const double stab : {0.5, 1.5, 2.5, 3.5})
                    for (const int t : c.tones)
                        n.push_back({t + 12, stab == 0.5 ? 96 : 80, b + stab, 0.35});
        }
        s.clip(keysMidi, "Stabs", chorus, ending, n);
    }
    {
        // The arpeggio: the chord held an octave up, Starbit cycles it.
        std::vector<Builder::Note> n;
        for (int bar = intro; bar < ending; ++bar)
        {
            const bool plays = bar < verse || (bar >= breakdown && bar < finale) || bar >= finale;
            if (!plays)
                continue;
            for (const int t : chordAt(bar).tones)
                n.push_back({t + 24, 90, bar * 4.0, 3.9});
        }
        s.clip(arpMidi, "Sparkle", intro, ending, n);
    }
    {
        // The lead sings in the choruses: phrase A then B, twice; the last note rings.
        std::vector<Builder::Note> n;
        for (const int start : {chorus, finale})
        {
            const double b = (start - chorus) * 4.0;
            for (const auto& x : phraseA())
                n.push_back({x.pitch, x.velocity, b + x.start, x.length});
            for (const auto& x : phraseB())
                n.push_back({x.pitch, x.velocity, b + 16.0 + x.start, x.length});
        }
        n.push_back({81, 104, (ending - chorus) * 4.0, 3.0});
        s.clip(leadMidi, "Melody", chorus, ending + 1, n);
    }

    // ----- effects -------------------------------------------------------------------------------
    drums.inserts.push_back(s.insert(
        plugins::types::eq, {30.0f, 90.0f, 2.5f, 1000.0f, -1.5f, 1.0f, 9000.0f, 2.0f})); // body and air
    drums.inserts.push_back(s.insert(plugins::types::compressor, {-16.0f, 3.0f, 8.0f, 120.0f, 6.0f, 2.0f}));
    bass.inserts.push_back(s.insert(plugins::types::compressor, {-20.0f, 4.0f, 3.0f, 90.0f, 6.0f, 3.0f}));
    pad.inserts.push_back(
        s.insert(plugins::types::eq, {180.0f, 120.0f, 0.0f, 1200.0f, -2.0f, 0.8f, 8000.0f, 1.5f}));
    keys.inserts.push_back(
        s.insert(plugins::types::eq, {120.0f, 120.0f, 0.0f, 2500.0f, 1.5f, 1.0f, 8000.0f, 0.0f}));
    lead.inserts.push_back(s.insert(plugins::types::limiter, {-6.0f, 60.0f, 1.5f}));

    // Two FX tracks: a hall and a dotted-eighth echo (375 ms at 120 bpm).
    auto hall = plugins::makeFxTrack(p, plugins::types::reverb, "Hall");
    hall.id = s.id();
    for (auto& i : hall.inserts)
    {
        i.id = s.id();
        if (i.params.size() > plugins::reverb::mix)
        {
            i.params[plugins::reverb::decay] = 3.2f;
            i.params[plugins::reverb::size] = 80.0f;
            i.params[plugins::reverb::lowCut] = 200.0f;
        }
    }
    auto echo = plugins::makeFxTrack(p, plugins::types::delay, "Echo");
    echo.id = s.id();
    for (auto& i : echo.inserts)
    {
        i.id = s.id();
        if (i.params.size() > plugins::delay::mix)
        {
            i.params[plugins::delay::time] = 375.0f;
            i.params[plugins::delay::feedback] = 40.0f;
            i.params[plugins::delay::pingPong] = 1.0f;
            i.params[plugins::delay::tone] = 4500.0f;
        }
    }
    echo.gain = 0.7f;
    const auto send = [](core::Track& t, core::Id bus, float level)
    { t.sends.push_back({bus, level, false}); };
    send(pad, hall.id, 0.5f);
    send(keys, hall.id, 0.35f);
    send(lead, hall.id, 0.3f);
    send(arp, hall.id, 0.3f);
    send(drums, hall.id, 0.12f);
    send(lead, echo.id, 0.35f);
    send(arp, echo.id, 0.25f);
    if (hasVoice)
    {
        send(p.tracks[12], hall.id, 0.3f);
        send(p.tracks[12], echo.id, 0.15f);
        p.tracks[12].gain = 0.8f;
        p.tracks[12].pan = -0.05f;
    }
    p.buses.push_back(std::move(hall));
    p.buses.push_back(std::move(echo));

    // Panning and levels.
    keys.pan = -0.3f;
    arp.pan = 0.35f;
    lead.pan = 0.05f;
    pad.gain = 0.8f;
    lead.gain = 0.9f;

    // ----- the channel strip: every channel has one; switch some modules on ---------------------
    plugins::strip::ensureChannelStrip(p);
    const auto on = [](core::Channel& c, const std::string& type)
    {
        for (auto& m : c.strip)
            if (m.type == type)
                m.bypass = false;
    };
    on(bass, plugins::strip::types::saturation);
    on(drums, plugins::strip::types::limiter);
    on(lead, plugins::strip::types::compressor);
    on(keys, plugins::strip::types::eq);
    if (hasVoice)
        on(p.tracks[12], plugins::strip::types::compressor);

    // ----- automation ----------------------------------------------------------------------------
    using T = core::AutomationTarget;
    // The pad fades in over the intro and swells into the finale.
    s.lane(pad,
           T::gain,
           core::invalidId,
           0,
           {{0.0, 0.0f}, {3.0, 0.8f}, {20.0, 0.8f}, {24.0, 1.0f}, {33.0, 1.0f}});
    // Nebula brightens through the breakdown.
    s.lane(pad,
           T::insertParameter,
           pad.instrument.id,
           ne::brightness,
           {{20.0, 0.25f}, {24.0, 0.65f}, {32.0, 0.65f}});
    // The bass filter closes in the breakdown and opens back up into the finale.
    s.lane(bass,
           T::insertParameter,
           bass.instrument.id,
           au::cutoff,
           {{19.0, 380.0f}, {20.0, 160.0f}, {23.0, 220.0f}, {24.0, 520.0f}, {32.0, 520.0f}});
    // The arp drifts across the field in the breakdown.
    s.lane(arp, T::pan, core::invalidId, 0, {{20.0, 0.35f}, {22.0, -0.4f}, {24.0, 0.35f}});

    // ----- markers -------------------------------------------------------------------------------
    const auto marker = [&](const char* name, int from, int to)
    { p.markers.push_back({s.id(), name, from * barLength, (to - from) * barLength}); };
    marker("Intro", intro, verse);
    marker("Verse", verse, chorus);
    marker("Chorus", chorus, breakdown);
    marker("Breakdown", breakdown, finale);
    marker("Finale", finale, ending);
    marker("End", ending, bars);
    p.sortMarkers();

    // ----- the drum outputs: a track each, in a closed folder under the drums' channel -------
    // (Last: inserting tracks moves the ones the references above point at.)
    {
        const auto drumsChannel = p.tracks[1];
        const char* names[] = {"", "Snares", "Hats", "Toms"};
        const float gains[] = {1.0f, 0.9f, 0.7f, 0.85f};
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
        outputs[1].sends.push_back({p.buses[0].id, 0.2f, false}); // the snares into the hall
        outputs[2].pan = 0.15f;                                   // the hats a little right
        p.tracks.insert(p.tracks.begin() + 2, outputs.begin(), outputs.end());
        plugins::strip::ensureChannelStrip(p); // their strips (off)
    }
    return p;
}

} // namespace northlight
