#pragma once

#include <map>
#include <string>
#include <vector>

// What an export writes: the choices of the export sheet as plain values, the
// rules that tie them together (which bit depths a format takes, when dither
// applies) and their persistence as key/value pairs ("export.*" in the user
// settings). JUCE-free so the rules are unit tested.

namespace northlight::engine
{

enum class ExportFormat
{
    wav,
    aiff,
    flac,
    ogg, // Ogg Vorbis, lossy; written from floats, so no bit depth and no dither
};

enum class ExportDepth
{
    int16,
    int24,
    float32,
};

enum class DitherMode
{
    off,
    tpdf,   // triangular PDF, ±1 LSB: removes the quantisation distortion
    shaped, // TPDF with error-feedback noise shaping: less audible noise
};

enum class PeakMode
{
    truePeak,   // 4x oversampled (BS.1770 Annex 2), dBTP
    samplePeak, // the largest sample, dBFS
};

enum class ExportSource
{
    mix,
    stems,         // one file per track
    selectedStems, // one file per selected track
};

enum class ExportRange
{
    project,
    loop,
    region, // a marker region, chosen when exporting (not remembered)
};

struct ExportSettings
{
    ExportSource source = ExportSource::mix;
    ExportRange range = ExportRange::project;
    ExportFormat format = ExportFormat::wav;
    ExportDepth depth = ExportDepth::int24;
    int sampleRate = 0; // 0 = the project rate
    DitherMode dither = DitherMode::tpdf;
    int oggQuality = 6; // index into the Vorbis quality options (0..10, 6 = 192 kbps)
    bool normalise = false;
    double targetLufs = -14.0;
    double ceilingDb = -1.0;
    PeakMode peakMode = PeakMode::truePeak;

    bool operator==(const ExportSettings&) const = default;

    // The same settings with every value made consistent: a bit depth the
    // format takes, a known sample rate, values in range.
    ExportSettings validated() const;

    std::map<std::string, std::string> toKeyValues(const std::string& prefix = "export.") const;
    static ExportSettings fromKeyValues(const std::map<std::string, std::string>& values,
                                        const std::string& prefix = "export.");
};

// The file extension, without the dot.
const char* extensionFor(ExportFormat format) noexcept;
const char* formatName(ExportFormat format) noexcept;
const char* depthName(ExportDepth depth) noexcept;
const char* ditherName(DitherMode mode) noexcept;

// Bit depths the format can write, in menu order; empty for Ogg Vorbis.
std::vector<ExportDepth> depthsFor(ExportFormat format);
// The depth to use for the format: `wanted` if it takes it, else the closest.
ExportDepth depthFor(ExportFormat format, ExportDepth wanted) noexcept;
// Bits per sample of an integer depth; 32 for float.
int bitsOf(ExportDepth depth) noexcept;
bool isInteger(ExportDepth depth) noexcept;
bool isLossy(ExportFormat format) noexcept;

// The sample rates offered besides the project rate.
const std::vector<int>& exportSampleRates();
// The rate a file is written at.
double outputRate(const ExportSettings& settings, double projectRate) noexcept;

// Whether the export quantises to integers with dither: an integer depth on a
// lossless format, with dither on.
bool ditherApplies(const ExportSettings& settings) noexcept;

// The normalisation targets and ceilings the sheet offers.
const std::vector<double>& loudnessTargets();
const std::vector<double>& peakCeilings();

} // namespace northlight::engine
