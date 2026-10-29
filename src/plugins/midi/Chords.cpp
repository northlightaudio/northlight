#include "plugins/midi/Chords.h"

#include "plugins/midi/MidiEffectBase.h"
#include "plugins/midi/MidiTools.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace northlight::plugins
{

using engine::MidiEvent;
using engine::ParameterInfo;

namespace
{
int rounded(float v) noexcept
{
    return static_cast<int>(std::lround(v));
}

ParameterInfo choice(const char* name, std::vector<std::string> choices, int defaultIndex = 0)
{
    ParameterInfo info;
    info.name = name;
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = static_cast<float>(defaultIndex);
    info.choices = std::move(choices);
    return info;
}

ParameterInfo range(const char* name, const char* unit, float minimum, float maximum, float defaultValue)
{
    ParameterInfo info;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    return info;
}

// A whole-number setting (a count, semitones, a velocity).
ParameterInfo whole(const char* name, const char* unit, float minimum, float maximum, float defaultValue)
{
    auto info = range(name, unit, minimum, maximum, defaultValue);
    info.integer = true;
    return info;
}

ChordNotes fromIntervals(int note, std::initializer_list<int> intervals) noexcept
{
    ChordNotes c;
    for (int i : intervals)
        if (c.count < maxChordNotes)
            c.notes[static_cast<size_t>(c.count++)] = note + i;
    return c;
}

void sortChord(ChordNotes& c) noexcept
{
    std::sort(c.notes.begin(), c.notes.begin() + c.count);
}

// Inversions raise the lowest note an octave, then the voicing spreads it.
ChordNotes invertAndVoice(ChordNotes c, int inversion, int voicing) noexcept
{
    sortChord(c);
    for (int i = 0; i < std::clamp(inversion, 0, 3) && c.count > 1; ++i)
    {
        c.notes[0] += 12;
        sortChord(c);
    }
    switch (voicing)
    {
        case chord::open:
            if (c.count >= 3)
                c.notes[1] += 12; // the second from the bottom up an octave
            break;
        case chord::drop2:
            if (c.count >= 3)
                c.notes[static_cast<size_t>(c.count - 2)] -= 12; // the second from the top down
            break;
        case chord::spread:
            for (int i = 1; i < c.count; i += 2)
                c.notes[static_cast<size_t>(i)] += 12;
            break;
        default:
            break;
    }
    sortChord(c);
    return c;
}

std::vector<std::string> keyFollowChoices()
{
    return {"Own", "Project"};
}

double meanOf(const ChordNotes& c) noexcept
{
    double sum = 0.0;
    for (int i = 0; i < c.count; ++i)
        sum += c.notes[static_cast<size_t>(i)];
    return c.count > 0 ? sum / c.count : 0.0;
}
} // namespace

// ----- names -----------------------------------------------------------------------------

std::string chordName(int rootPitchClass, const ChordNotes& chord)
{
    static constexpr const char* roots[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    const int root = ((rootPitchClass % 12) + 12) % 12;
    if (chord.count == 0)
        return {};
    // The pitch classes above the root, as a set.
    unsigned set = 0;
    int lowest = chord.notes[0];
    for (int i = 0; i < chord.count; ++i)
    {
        const int n = chord.notes[static_cast<size_t>(i)];
        lowest = std::min(lowest, n);
        set |= 1u << (((n - root) % 12 + 12) % 12);
    }
    struct Quality
    {
        unsigned set;
        const char* suffix;
    };
    const auto bits = [](std::initializer_list<int> steps)
    {
        unsigned b = 0;
        for (int s : steps)
            b |= 1u << s;
        return b;
    };
    const Quality qualities[] = {
        {bits({0, 4, 7}), ""},
        {bits({0, 3, 7}), "m"},
        {bits({0, 3, 6}), "dim"},
        {bits({0, 4, 8}), "aug"},
        {bits({0, 2, 7}), "sus2"},
        {bits({0, 5, 7}), "sus4"},
        {bits({0, 4, 7, 10}), "7"},
        {bits({0, 4, 7, 11}), "maj7"},
        {bits({0, 3, 7, 10}), "m7"},
        {bits({0, 3, 6, 10}), "m7b5"},
        {bits({0, 3, 6, 9}), "dim7"},
        {bits({0, 3, 7, 11}), "m(maj7)"},
        {bits({0, 2, 4, 7}), "add9"},
        {bits({0, 2, 4, 7, 10}), "9"},
        {bits({0, 2, 4, 7, 11}), "maj9"},
        {bits({0, 2, 3, 7, 10}), "m9"},
        {bits({0, 7}), "5"},
        {bits({0}), ""},
    };
    std::string name = roots[root];
    for (const auto& q : qualities)
        if (q.set == set)
        {
            name += q.suffix;
            break;
        }
    if (const int bass = ((lowest % 12) + 12) % 12; bass != root)
        name += std::string("/") + roots[bass];
    return name;
}

// ----- Chord -----------------------------------------------------------------------------

namespace chord
{
ChordNotes byShape(int note, int shape, int inversion, int voicing) noexcept
{
    ChordNotes c;
    switch (shape)
    {
        case minor:
            c = fromIntervals(note, {0, 3, 7});
            break;
        case dominant7:
            c = fromIntervals(note, {0, 4, 7, 10});
            break;
        case major7:
            c = fromIntervals(note, {0, 4, 7, 11});
            break;
        case minor7:
            c = fromIntervals(note, {0, 3, 7, 10});
            break;
        case sus2:
            c = fromIntervals(note, {0, 2, 7});
            break;
        case sus4:
            c = fromIntervals(note, {0, 5, 7});
            break;
        case diminished:
            c = fromIntervals(note, {0, 3, 6});
            break;
        case augmented:
            c = fromIntervals(note, {0, 4, 8});
            break;
        case add9:
            c = fromIntervals(note, {0, 4, 7, 14});
            break;
        case minor9:
            c = fromIntervals(note, {0, 3, 7, 10, 14});
            break;
        case power:
            c = fromIntervals(note, {0, 7, 12});
            break;
        case octaves:
            c = fromIntervals(note, {0, 12});
            break;
        default:
            c = fromIntervals(note, {0, 4, 7});
            break;
    }
    return invertAndVoice(c, inversion, voicing);
}

ChordNotes inKey(int note, int size, int root, std::uint16_t mask, int inversion, int voicing) noexcept
{
    const midi::Key key{root, mask};
    if ((mask & 0x0FFFu) == 0)
        return byShape(note, size == 0 ? major : major7, inversion, voicing);
    // The scale note at or below, and how far the key is above it.
    int base = note;
    while (!key.contains(base))
        --base;
    const int offset = note - base;
    const auto degrees = midi::degreesOf(key);
    // The base's degree, counted from the root of its octave.
    const int rootNote = base - ((base - root) % 12 + 12) % 12;
    int degree = 0;
    for (int i = 0; i < degrees.count; ++i)
        if (rootNote + degrees.steps[static_cast<size_t>(i)] == base)
            degree = i;
    ChordNotes c;
    const int tones = 3 + std::clamp(size, 0, 2);
    for (int t = 0; t < tones; ++t)
        c.notes[static_cast<size_t>(c.count++)] =
            midi::degreeNote(degrees, rootNote, degree + 2 * t) + offset;
    return invertAndVoice(c, inversion, voicing);
}

namespace
{
class Chord final : public midi::MidiEffectBase
{
public:
    Chord()
        : MidiEffectBase(type,
                         {choice("Mode", {"Shape", "In key"}),
                          choice("Shape",
                                 {"Major",
                                  "Minor",
                                  "7",
                                  "Maj7",
                                  "Min7",
                                  "Sus2",
                                  "Sus4",
                                  "Diminished",
                                  "Augmented",
                                  "Add9",
                                  "Min9",
                                  "Power",
                                  "Octaves"}),
                          choice("Size", {"Triad", "Seventh", "Ninth"}),
                          choice("Key", keyFollowChoices(), 1),
                          choice("Root", midi::rootChoices()),
                          choice("Scale", midi::scaleChoices()),
                          whole("Inversion", "", 0.0f, 3.0f, 0.0f),
                          choice("Voicing", {"Close", "Open", "Drop 2", "Spread"}),
                          choice("Strum", {"Off", "Up", "Down", "Alternate"}),
                          whole("Strum time", "ms", 1.0f, 200.0f, 30.0f)}),
          strums_(16 * 128)
    {
    }
    int midiExpansion() const noexcept override { return maxChordNotes; }

private:
    // A strummed chord: its notes and when each starts, so its release ends
    // them all, even ones not started yet.
    struct Strummed
    {
        ChordNotes chord;
        std::array<std::int64_t, maxChordNotes> starts{};
        bool active = false;
    };

    void beginBlock() noexcept override
    {
        inKey_ = rounded(value(mode)) == 1;
        shape_ = rounded(value(shape));
        size_ = rounded(value(size));
        key_ = midi::keyFor(playHead(), rounded(value(follow)) == 1, value(root), value(kind));
        inversion_ = rounded(value(inversion));
        voicing_ = rounded(value(voicing));
        strum_ = rounded(value(strum));
        strumSamples_ = static_cast<std::int64_t>(value(strumTime) * 0.001 * sampleRate());
    }
    void reset() noexcept override
    {
        std::fill(strums_.begin(), strums_.end(), Strummed{});
        downNext_ = false;
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        ChordNotes c = inKey_ ? inKey(e.note, size_, key_.root, key_.mask, inversion_, voicing_)
                              : byShape(e.note, shape_, inversion_, voicing_);
        if (strum_ == 0)
        {
            take(e); // a chord whose notes all fall out of range still takes the key
            for (int i = 0; i < c.count; ++i)
                startNote(e, c.notes[static_cast<size_t>(i)], e.velocity, e.channel);
            return;
        }
        // Strummed: low to high, high to low, or each way in turn.
        bool downward = strum_ == 2;
        if (strum_ == 3)
        {
            downward = downNext_;
            downNext_ = !downNext_;
        }
        if (downward)
            std::reverse(c.notes.begin(), c.notes.begin() + c.count);
        take(e);
        auto& s = strums_[slot(e)];
        s = {};
        s.active = true;
        const std::int64_t at = blockClock() + e.sampleOffset;
        for (int i = 0; i < c.count; ++i)
        {
            const int note = c.notes[static_cast<size_t>(i)];
            if (note < 0 || note > 127)
                continue;
            const std::int64_t start = at + i * strumSamples_;
            MidiEvent on{0, MidiEvent::Type::noteOn, e.channel, static_cast<std::uint8_t>(note), e.velocity};
            if (schedule(start, on))
            {
                s.chord.notes[static_cast<size_t>(s.chord.count)] = note;
                s.starts[static_cast<size_t>(s.chord.count++)] = start;
                willPlay(note);
            }
        }
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        auto& s = strums_[slot(e)];
        if (!s.active)
        {
            releaseNote(e);
            return;
        }
        const std::int64_t at = blockClock() + e.sampleOffset;
        for (int i = 0; i < s.chord.count; ++i)
        {
            MidiEvent off{0,
                          MidiEvent::Type::noteOff,
                          e.channel,
                          static_cast<std::uint8_t>(s.chord.notes[static_cast<size_t>(i)]),
                          0};
            // A note the strum has not reached yet still sounds for a moment.
            schedule(std::max(at, s.starts[static_cast<size_t>(i)] + 1), off);
        }
        s = {};
        releaseNote(e);
    }
    static size_t slot(const MidiEvent& e) noexcept
    {
        return static_cast<size_t>((e.channel & 15) * 128 + (e.note & 127));
    }

    bool inKey_ = false;
    int shape_ = major, size_ = 0, inversion_ = 0, voicing_ = close, strum_ = 0;
    std::int64_t strumSamples_ = 0;
    midi::Key key_;
    bool downNext_ = false;
    std::vector<Strummed> strums_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Chord>();
}
} // namespace chord

// ----- Chord Pads ------------------------------------------------------------------------

namespace chordpads
{
ChordNotes padChord(int note, int size, int root, std::uint16_t mask, int centre) noexcept
{
    const int pc = ((note % 12) + 12) % 12;
    // The chord's root in the octave from `centre` up.
    const auto placed = [&](int semitonesAboveKeyRoot)
    {
        const int pitchClass = (root + semitonesAboveKeyRoot) % 12;
        return centre + ((pitchClass - centre) % 12 + 12) % 12;
    };
    const bool seventh = size >= 1;
    switch (pc)
    {
        // The borrowed chords, on the black keys.
        case 1:
            return seventh ? fromIntervals(placed(1), {0, 4, 7, 11}) : fromIntervals(placed(1), {0, 4, 7});
        case 3:
            return seventh ? fromIntervals(placed(3), {0, 4, 7, 11}) : fromIntervals(placed(3), {0, 4, 7});
        case 6:
            return seventh ? fromIntervals(placed(5), {0, 3, 7, 10}) : fromIntervals(placed(5), {0, 3, 7});
        case 8:
            return seventh ? fromIntervals(placed(8), {0, 4, 7, 11}) : fromIntervals(placed(8), {0, 4, 7});
        case 10:
            return seventh ? fromIntervals(placed(10), {0, 4, 7, 10}) : fromIntervals(placed(10), {0, 4, 7});
        default:
            break;
    }
    static constexpr int whiteIndex[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    const midi::Key key{root, (mask & 0x0FFFu) != 0 ? mask : std::uint16_t{0b1010'1011'0101}};
    const auto degrees = midi::degreesOf(key);
    const int degree = whiteIndex[pc];
    const int base = placed(0);
    ChordNotes c;
    const int tones = seventh ? 4 : 3;
    const int first = midi::degreeNote(degrees, base, degree);
    const int shift = placed(first - base) - first; // the chord's root in the octave from centre up
    for (int t = 0; t < tones; ++t)
        c.notes[static_cast<size_t>(c.count++)] = midi::degreeNote(degrees, base, degree + 2 * t) + shift;
    return c;
}

ChordNotes leadFrom(const ChordNotes& chord, const ChordNotes& previous, int centre) noexcept
{
    if (chord.count == 0)
        return chord;
    ChordNotes best;
    double bestCost = 1e9;
    const double target = previous.count > 0 ? meanOf(previous) : static_cast<double>(centre + 6);
    for (int inversion = 0; inversion < chord.count; ++inversion)
    {
        ChordNotes c = invertAndVoice(chord, inversion, chord::close);
        // The octave that puts it nearest where the last chord was.
        const double shiftOctaves = std::round((target - meanOf(c)) / 12.0);
        for (int i = 0; i < c.count; ++i)
            c.notes[static_cast<size_t>(i)] += static_cast<int>(shiftOctaves) * 12;
        double cost = 0.0;
        if (previous.count == c.count)
            for (int i = 0; i < c.count; ++i)
                cost += std::abs(c.notes[static_cast<size_t>(i)] - previous.notes[static_cast<size_t>(i)]);
        else
            cost = std::abs(meanOf(c) - target) * c.count;
        // Stay near the middle: drifting further than an octave costs.
        const double drift = std::abs(meanOf(c) - (centre + 6));
        if (drift > 12.0)
            cost += (drift - 12.0) * 4.0;
        if (cost < bestCost)
        {
            bestCost = cost;
            best = c;
        }
    }
    return best;
}

std::string padNumeral(int note, int root, std::uint16_t mask)
{
    const int pc = ((note % 12) + 12) % 12;
    switch (pc)
    {
        case 1:
            return "bII";
        case 3:
            return "bIII";
        case 6:
            return "iv";
        case 8:
            return "bVI";
        case 10:
            return "bVII";
        default:
            break;
    }
    static constexpr const char* upper[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static constexpr const char* lower[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    static constexpr int whiteIndex[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    const int degree = whiteIndex[pc];
    // The quality from the triad's thirds.
    const auto c = padChord(note, 0, root, mask, 48);
    const int third = c.count >= 2 ? c.notes[1] - c.notes[0] : 4;
    const int fifth = c.count >= 3 ? c.notes[2] - c.notes[0] : 7;
    if (third == 3 && fifth == 6)
        return std::string(lower[degree]) + "\xc2\xb0"; // vii°
    if (third == 3)
        return lower[degree];
    if (fifth == 8)
        return std::string(upper[degree]) + "+";
    return upper[degree];
}

std::string padName(int note, int size, int root, std::uint16_t mask)
{
    const auto c = padChord(note, size, root, mask, 48);
    return c.count > 0 ? chordName(c.notes[0], c) : std::string{};
}

namespace
{
class ChordPads final : public midi::MidiEffectBase
{
public:
    ChordPads()
        : MidiEffectBase(type,
                         {choice("Key", keyFollowChoices(), 1),
                          choice("Root", midi::rootChoices()),
                          choice("Scale", midi::scaleChoices()),
                          choice("Size", {"Triad", "Seventh"}),
                          choice("Voice leading", {"Off", "On"}, 1),
                          choice("Bass", {"Off", "On"}),
                          whole("Octave", "", -2.0f, 2.0f, 0.0f)})
    {
    }
    int midiExpansion() const noexcept override { return 6; }

private:
    void beginBlock() noexcept override
    {
        key_ = midi::keyFor(playHead(), rounded(value(follow)) == 1, value(root), value(kind));
        size_ = rounded(value(size));
        leading_ = rounded(value(leading)) == 1;
        bass_ = rounded(value(bass)) == 1;
        centre_ = 54 + 12 * std::clamp(rounded(value(octave)), -2, 2); // F#3: chords sit around C4
    }
    void reset() noexcept override { previous_ = {}; }
    void noteOn(const MidiEvent& e) noexcept override
    {
        take(e);
        ChordNotes c = padChord(e.note, size_, key_.root, key_.mask, centre_);
        if (leading_)
        {
            c = leadFrom(c, previous_, centre_);
            previous_ = c;
        }
        if (bass_ && c.count < maxChordNotes)
        {
            // The chord's root, an octave below the chord.
            const int rootPc = ((padChord(e.note, 0, key_.root, key_.mask, centre_).notes[0] % 12) + 12) % 12;
            const int bassNote = centre_ - 12 + ((rootPc - (centre_ - 12)) % 12 + 12) % 12;
            startNote(e, bassNote, e.velocity, e.channel);
        }
        for (int i = 0; i < c.count; ++i)
            startNote(e, c.notes[static_cast<size_t>(i)], e.velocity, e.channel);
    }

    midi::Key key_;
    int size_ = 0, centre_ = 54;
    bool leading_ = true, bass_ = false;
    ChordNotes previous_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<ChordPads>();
}
} // namespace chordpads

} // namespace northlight::plugins
