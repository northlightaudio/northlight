#pragma once

#include "engine/Insert.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// The chord tools: Chord (one key plays a chord) and Chord Pads (the keys play
// the chords of a key, voice-led) (docs/plans/midi-inserts.md). JUCE-free.

namespace northlight::plugins
{

// Up to this many notes in one chord.
inline constexpr int maxChordNotes = 8;
struct ChordNotes
{
    std::array<int, maxChordNotes> notes{};
    int count = 0;
};

// A chord's name from its root's pitch class: "C", "Dm", "Bdim", "G7", "Cmaj7",
// and "/E" after it when its lowest note is not the root. Roots are spelled with
// flats (Bb, not A#), as pop charts write them. A set it does not know is named
// by its root alone.
std::string chordName(int rootPitchClass, const ChordNotes& chord);

namespace chord
{
inline constexpr const char* type = "northlight.chord";
enum Param
{
    mode,      // Shape, In key
    shape,     // the shapes below (Shape mode)
    size,      // Triad, Seventh, Ninth (In key mode)
    follow,    // Own key, Project key (In key mode)
    root,      // C..B
    kind,      // the scale (MidiTools scaleKinds order)
    inversion, // 0..3
    voicing,   // Close, Open, Drop 2, Spread
    strum,     // Off, Up, Down, Alternate
    strumTime, // ms between notes
    count
};
enum Shape
{
    major,
    minor,
    dominant7,
    major7,
    minor7,
    sus2,
    sus4,
    diminished,
    augmented,
    add9,
    minor9,
    power,
    octaves,
    shapeCount,
};
enum Voicing
{
    close,
    open,
    drop2,
    spread,
};
// The chord a key plays by shape, inverted and voiced (sorted low to high).
ChordNotes byShape(int note, int shape, int inversion, int voicing) noexcept;
// The chord a key plays in a key: stacked thirds of the scale from that key
// (`size` 0 triad, 1 seventh, 2 ninth). A key outside the scale builds on the
// scale note below and keeps its distance.
ChordNotes inKey(int note, int size, int root, std::uint16_t mask, int inversion, int voicing) noexcept;
std::unique_ptr<engine::InsertProcessor> create();
} // namespace chord

namespace chordpads
{
inline constexpr const char* type = "northlight.chordpads";
enum Param
{
    follow,  // Own key, Project key
    root,    // C..B
    kind,    // the scale
    size,    // Triad, Seventh
    leading, // Off, On: each chord takes the inversion nearest the last one
    bass,    // Off, On: the chord's root an octave below
    octave,  // octaves from the middle, -2..2
    count
};
// The chord a pad plays, before voicing: C D E F G A B of any octave are the
// key's degrees I to vii; C# D# F# G# A# are the borrowed bII, bIII, iv, bVI
// and bVII. Pitch classes are relative to nothing: the notes are absolute,
// rooted in the octave from `centre` up.
ChordNotes padChord(int note, int size, int root, std::uint16_t mask, int centre) noexcept;
// The voicing of `chord` (its pitch classes) nearest `previous` (count 0: none),
// kept within an octave either way of `centre`. Sorted low to high.
ChordNotes leadFrom(const ChordNotes& chord, const ChordNotes& previous, int centre) noexcept;
// The pad's numeral in the key, for its triad: I ii iii IV V vi vii° (cased and marked by the
// chord's quality), and the borrowed bII bIII iv bVI bVII on the black keys.
std::string padNumeral(int note, int root, std::uint16_t mask);
// The pad's chord name ("Dm"): the root is the chord's first note.
std::string padName(int note, int size, int root, std::uint16_t mask);
std::unique_ptr<engine::InsertProcessor> create();
} // namespace chordpads

} // namespace northlight::plugins
