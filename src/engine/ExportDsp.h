#pragma once

#include "engine/ExportSettings.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The signal processing of an export, offline and JUCE-free: sample-rate
// conversion, peak and loudness measurement, the normalisation gain, and the
// quantiser that reduces floats to 16 or 24-bit integers with dither.

namespace northlight::engine
{

// The export's figures, floored at measurementFloorDb (engine/Gain.h).
double gainToDb(double gain) noexcept;
double dbToGain(double db) noexcept;

// ----- sample-rate conversion ------------------------------------------------------------

// Output samples for `length` input samples converted between the rates.
std::size_t resampledLength(std::size_t length, double fromRate, double toRate) noexcept;

// Converts a whole signal with a Kaiser-windowed sinc (48 zero crossings each side,
// about 90 dB stop band, the cut-off at 92% of the lower Nyquist frequency so a
// 44.1 kHz result is flat to about 19 kHz). Output sample j sits at input position
// j * fromRate / toRate exactly: no delay, any ratio. Samples outside the input
// read as silence. Equal rates return a copy.
std::vector<float> resample(const float* input, std::size_t length, double fromRate, double toRate);

// ----- measurement -----------------------------------------------------------------------

// The largest absolute sample value (linear).
float samplePeak(const float* signal, std::size_t length) noexcept;
// The true peak (linear): the largest absolute value of the signal 4x oversampled
// by windowed-sinc interpolation, the method of ITU-R BS.1770-4 Annex 2.
float truePeak(const float* signal, std::size_t length) noexcept;

// Integrated loudness (LUFS, BS.1770-4 gated) of one or two channels; -100 when
// nothing passes the gates. A mono signal is measured as one channel.
double integratedLoudness(const std::vector<std::vector<float>>& channels, double sampleRate);

struct ExportAnalysis
{
    double integratedLufs = -100.0;
    double samplePeakDb = -200.0;
    double truePeakDb = -200.0; // only measured when asked for (it costs an oversampling pass)
};
ExportAnalysis analyse(const std::vector<std::vector<float>>& channels, double sampleRate, bool withTruePeak);

// ----- normalisation ---------------------------------------------------------------------

struct Normalisation
{
    double gainDb = 0.0;
    bool limitedByCeiling = false; // the gain stops short of the target so the peak stays under the ceiling
};

// The gain that brings `measuredLufs` to `targetLufs`, reduced if it would lift
// `peakDb` (sample or true peak, whichever the export uses) above `ceilingDb`.
// Silence (under the -70 LUFS gate) gets no gain.
Normalisation
normalisationGain(double measuredLufs, double peakDb, double targetLufs, double ceilingDb) noexcept;

void applyGain(std::vector<std::vector<float>>& channels, float gain) noexcept;

// ----- quantisation ----------------------------------------------------------------------

// Rounds floats (full scale ±1) to `bits`-bit integers, with optional dither:
// TPDF adds the difference of two uniform random values (±1 LSB, triangular),
// which makes the error independent of the signal (noise, not distortion); noise
// shaping also feeds the error back through a 3-tap E-weighted filter
// (Wannamaker's 1.623, -0.982, 0.109), moving the noise out of the band where hearing
// is most sensitive towards the top of the spectrum. One quantiser per channel
// (it carries the shaping filter's state); the random sequence is deterministic
// for a seed. Values beyond full scale clip.
class Quantiser
{
public:
    Quantiser(int bits, DitherMode mode, std::uint64_t seed = 0x9e3779b97f4a7c15ull) noexcept;

    // The integer sample, in [-2^(bits-1), 2^(bits-1) - 1].
    std::int32_t process(float sample) noexcept;
    // The integer as a float at full scale ±1 (what the integer file will read back as).
    float toFloat(std::int32_t value) const noexcept { return static_cast<float>(value / scale_); }
    int bits() const noexcept { return bits_; }

    static constexpr double shaping[3] = {1.623, -0.982, 0.109};

private:
    double uniform() noexcept; // [0, 1)

    int bits_;
    DitherMode mode_;
    double scale_;
    double min_, max_;
    std::uint64_t state_;
    double e1_ = 0.0, e2_ = 0.0, e3_ = 0.0;
};

} // namespace northlight::engine
