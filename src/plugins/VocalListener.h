#pragma once

#include "plugins/Biquad.h"

#include <array>
#include <cstdint>
#include <vector>

// The Listener: what a singer is doing, decided every 2.5 ms from a mono vocal, for
// Vesper (docs/plans/vesper.md) to shape its reverb by. It hears:
//
//  - voiced sound and its pitch: YIN on the input decimated to about 11 kHz
//    (a 20 ms window over lags from 65 Hz to 1.1 kHz), every other frame;
//  - consonants and sibilance: sound without pitch above the noise floor, and a
//    5-9 kHz band followed on its own, so an "s" inside a sung word shows too;
//  - breaths: quiet, unpitched, not bright, in a pause (12 dB or more under the
//    line's level, 100 ms or more since the last pitch);
//  - line ends: a pause in the singing (voice or consonants; breaths do not count)
//    as long as the gap, reported once, on the frame it is reached;
//  - the singing rate: syllables a second over the last 3 s (each voiced start,
//    and each 6 dB rise inside a held voice).
//
// It decides from the past alone, so it adds no latency, and its pitch is about
// 20 ms behind the sound (the window's centre). Fixed-size and allocation-free
// after prepare(), so it runs on the audio thread. JUCE-free.

namespace northlight::plugins::vocal
{

enum class Sound : std::uint8_t
{
    silence,
    voiced,
    consonant,
    breath
};

// What the Listener heard in the latest frame.
struct Hearing
{
    Sound sound = Sound::silence;
    float pitchHz = 0.0f;       // while voiced; 0 otherwise
    float heldPitchHz = 0.0f;   // the last voiced pitch, held through the gaps (0 before any)
    float levelDb = -120.0f;    // the input's level (3 ms, above 80 Hz)
    float lineLevelDb = -24.0f; // the singing's recent peak level (falls 2 dB a second)
    float sibilance = 0.0f;     // 0..1: how much of the sound is in the 5-9 kHz band
    bool lineEnd = false;       // this frame ended a line
    float pauseSeconds = 0.0f;  // since the last voice or consonant
    float syllablesPerSecond = 0.0f;
};

inline constexpr double frameSeconds = 0.0025;
inline constexpr float lowestHz = 65.0f, highestHz = 1100.0f;
inline constexpr float minGapSeconds = 0.12f, maxGapSeconds = 0.5f;

class Listener
{
public:
    // Allocates the analysis buffers for this rate. Not real-time.
    void prepare(double sampleRate);
    // Forgets everything heard.
    void reset() noexcept;
    // How long a pause ends a line (minGapSeconds..maxGapSeconds).
    void setGapSeconds(float seconds) noexcept;

    // One sample of the mono input. True when it completes a frame, and
    // hearing() is then the new frame's.
    bool push(float x) noexcept;
    const Hearing& hearing() const noexcept { return hearing_; }

private:
    void analyseFrame() noexcept;
    bool detectPitch(float& hz) noexcept;

    double sampleRate_ = 48000.0, rate_ = 12000.0;
    int decimation_ = 1, phase_ = 0;
    int frameSamples_ = 120, frameCount_ = 0;
    float frameDuration_ = 0.0025f;
    // Decimated input for YIN.
    std::array<Biquad, 2> antiAlias_;
    std::vector<float> ring_, window_, diff_, normalised_;
    int ringMask_ = 0, span_ = 0, tauMin_ = 2, tauMax_ = 2, windowSize_ = 0;
    std::int64_t written_ = 0;
    // Full-rate features.
    Biquad rumble_;                  // 80 Hz high-pass for the level
    std::array<Biquad, 2> high_;     // 4 kHz high-pass: brightness
    std::array<Biquad, 3> sibilant_; // 5-9 kHz band
    float envelopeStep_ = 0.0f;
    float power_ = 0.0f, highPower_ = 0.0f, sibilantPower_ = 0.0f;
    // Decisions.
    Hearing hearing_;
    std::int64_t frame_ = 0;
    std::int64_t lastVoicedFrame_ = -1000000, lastActiveFrame_ = -1000000;
    bool yinVoiced_ = false, active_ = false, wasVoiced_ = false;
    float yinHz_ = 0.0f;
    float gapSeconds_ = 0.25f;
    // Syllables: after one starts, the level must fall from its peak before a
    // rise from the dip after it counts again.
    float syllablePeakDb_ = 0.0f, syllableDipDb_ = 0.0f;
    bool syllableArmed_ = false;
    std::array<std::int64_t, 64> onsets_{};
    int onsetWrite_ = 0;
};

} // namespace northlight::plugins::vocal
