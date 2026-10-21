#include "engine/ExportSettings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace aural::engine
{

namespace
{
template <typename Enum> struct Name
{
    Enum value;
    const char* text;
};

constexpr Name<ExportSource> sourceNames[] = {
    {ExportSource::mix, "mix"}, {ExportSource::stems, "stems"}, {ExportSource::selectedStems, "selected"}};
constexpr Name<ExportRange> rangeNames[] = {
    {ExportRange::project, "project"}, {ExportRange::loop, "loop"}, {ExportRange::region, "region"}};
constexpr Name<ExportFormat> formatNames[] = {{ExportFormat::wav, "wav"},
                                              {ExportFormat::aiff, "aiff"},
                                              {ExportFormat::flac, "flac"},
                                              {ExportFormat::ogg, "ogg"}};
constexpr Name<ExportDepth> depthNames[] = {
    {ExportDepth::int16, "16"}, {ExportDepth::int24, "24"}, {ExportDepth::float32, "32f"}};
constexpr Name<DitherMode> ditherNames[] = {
    {DitherMode::off, "off"}, {DitherMode::tpdf, "tpdf"}, {DitherMode::shaped, "shaped"}};
constexpr Name<PeakMode> peakNames[] = {{PeakMode::truePeak, "true"}, {PeakMode::samplePeak, "sample"}};

template <typename Enum, size_t N> const char* textOf(const Name<Enum> (&names)[N], Enum value)
{
    for (const auto& n : names)
        if (n.value == value)
            return n.text;
    return names[0].text;
}

template <typename Enum, size_t N>
Enum valueOf(const Name<Enum> (&names)[N], const std::string& text, Enum fallback)
{
    for (const auto& n : names)
        if (text == n.text)
            return n.value;
    return fallback;
}

std::string numberText(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

double parseNumber(const std::string& text, double fallback)
{
    if (text.empty())
        return fallback;
    double value = fallback;
    const auto* first = text.data();
    const auto [ptr, error] = std::from_chars(first, first + text.size(), value);
    return error == std::errc() && std::isfinite(value) ? value : fallback;
}
} // namespace

ExportSettings ExportSettings::validated() const
{
    ExportSettings s = *this;
    // Ogg has no depth of its own: keep the one chosen for the other formats.
    if (s.format != ExportFormat::ogg)
        s.depth = depthFor(s.format, s.depth);
    const auto& rates = exportSampleRates();
    if (s.sampleRate != 0 && std::find(rates.begin(), rates.end(), s.sampleRate) == rates.end())
        s.sampleRate = 0;
    s.oggQuality = std::clamp(s.oggQuality, 0, 10);
    s.targetLufs = std::clamp(s.targetLufs, -40.0, 0.0);
    s.ceilingDb = std::clamp(s.ceilingDb, -12.0, 0.0);
    // A region is chosen per export, so it is never what is remembered.
    if (s.range == ExportRange::region)
        s.range = ExportRange::project;
    return s;
}

std::map<std::string, std::string> ExportSettings::toKeyValues(const std::string& prefix) const
{
    const auto s = validated();
    return {
        {prefix + "source", textOf(sourceNames, s.source)},
        {prefix + "range", textOf(rangeNames, s.range)},
        {prefix + "format", textOf(formatNames, s.format)},
        {prefix + "depth", textOf(depthNames, s.depth)},
        {prefix + "sampleRate", std::to_string(s.sampleRate)},
        {prefix + "dither", textOf(ditherNames, s.dither)},
        {prefix + "oggQuality", std::to_string(s.oggQuality)},
        {prefix + "normalise", s.normalise ? "1" : "0"},
        {prefix + "targetLufs", numberText(s.targetLufs)},
        {prefix + "ceilingDb", numberText(s.ceilingDb)},
        {prefix + "peak", textOf(peakNames, s.peakMode)},
    };
}

ExportSettings ExportSettings::fromKeyValues(const std::map<std::string, std::string>& values,
                                             const std::string& prefix)
{
    const auto get = [&](const char* key) -> std::string
    {
        const auto it = values.find(prefix + key);
        return it == values.end() ? std::string{} : it->second;
    };
    ExportSettings s;
    s.source = valueOf(sourceNames, get("source"), s.source);
    s.range = valueOf(rangeNames, get("range"), s.range);
    s.format = valueOf(formatNames, get("format"), s.format);
    s.depth = valueOf(depthNames, get("depth"), s.depth);
    s.sampleRate = static_cast<int>(parseNumber(get("sampleRate"), 0.0));
    s.dither = valueOf(ditherNames, get("dither"), s.dither);
    s.oggQuality = static_cast<int>(parseNumber(get("oggQuality"), s.oggQuality));
    s.normalise = get("normalise") == "1";
    s.targetLufs = parseNumber(get("targetLufs"), s.targetLufs);
    s.ceilingDb = parseNumber(get("ceilingDb"), s.ceilingDb);
    s.peakMode = valueOf(peakNames, get("peak"), s.peakMode);
    return s.validated();
}

const char* extensionFor(ExportFormat format) noexcept
{
    switch (format)
    {
        case ExportFormat::wav:
            return "wav";
        case ExportFormat::aiff:
            return "aiff";
        case ExportFormat::flac:
            return "flac";
        case ExportFormat::ogg:
            return "ogg";
    }
    return "wav";
}

const char* formatName(ExportFormat format) noexcept
{
    switch (format)
    {
        case ExportFormat::wav:
            return "WAV";
        case ExportFormat::aiff:
            return "AIFF";
        case ExportFormat::flac:
            return "FLAC";
        case ExportFormat::ogg:
            return "Ogg Vorbis";
    }
    return "WAV";
}

const char* depthName(ExportDepth depth) noexcept
{
    switch (depth)
    {
        case ExportDepth::int16:
            return "16-bit";
        case ExportDepth::int24:
            return "24-bit";
        case ExportDepth::float32:
            return "32-bit float";
    }
    return "24-bit";
}

const char* ditherName(DitherMode mode) noexcept
{
    switch (mode)
    {
        case DitherMode::off:
            return "Off";
        case DitherMode::tpdf:
            return "TPDF";
        case DitherMode::shaped:
            return "TPDF, noise shaped";
    }
    return "Off";
}

std::vector<ExportDepth> depthsFor(ExportFormat format)
{
    switch (format)
    {
        case ExportFormat::wav:
            return {ExportDepth::int16, ExportDepth::int24, ExportDepth::float32};
        case ExportFormat::aiff: // JUCE's AIFF writer has no float
        case ExportFormat::flac:
            return {ExportDepth::int16, ExportDepth::int24};
        case ExportFormat::ogg:
            return {};
    }
    return {};
}

ExportDepth depthFor(ExportFormat format, ExportDepth wanted) noexcept
{
    if (format == ExportFormat::ogg)
        return ExportDepth::float32;
    if (format != ExportFormat::wav && wanted == ExportDepth::float32)
        return ExportDepth::int24;
    return wanted;
}

int bitsOf(ExportDepth depth) noexcept
{
    switch (depth)
    {
        case ExportDepth::int16:
            return 16;
        case ExportDepth::int24:
            return 24;
        case ExportDepth::float32:
            return 32;
    }
    return 32;
}

bool isInteger(ExportDepth depth) noexcept
{
    return depth != ExportDepth::float32;
}

bool isLossy(ExportFormat format) noexcept
{
    return format == ExportFormat::ogg;
}

const std::vector<int>& exportSampleRates()
{
    static const std::vector<int> rates{44100, 48000, 88200, 96000};
    return rates;
}

double outputRate(const ExportSettings& settings, double projectRate) noexcept
{
    return settings.sampleRate > 0 ? static_cast<double>(settings.sampleRate) : projectRate;
}

bool ditherApplies(const ExportSettings& settings) noexcept
{
    return !isLossy(settings.format) && isInteger(depthFor(settings.format, settings.depth)) &&
           settings.dither != DitherMode::off;
}

const std::vector<double>& loudnessTargets()
{
    // Streaming (-14), podcasts and Apple (-16), broadcast EBU R128 (-23).
    static const std::vector<double> targets{-9.0, -11.0, -14.0, -16.0, -18.0, -23.0};
    return targets;
}

const std::vector<double>& peakCeilings()
{
    static const std::vector<double> ceilings{-0.1, -0.3, -0.5, -1.0, -2.0};
    return ceilings;
}

} // namespace aural::engine
