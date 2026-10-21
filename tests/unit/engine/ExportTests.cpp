#include "engine/ExportDsp.h"
#include "engine/ExportSettings.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace aural::engine;
using Catch::Approx;

namespace
{
std::vector<float> sine(double hz, double amplitude, double rate, std::size_t length, double phase = 0.0)
{
    std::vector<float> s(length);
    for (std::size_t i = 0; i < length; ++i)
        s[i] = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate + phase));
    return s;
}

double rms(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

struct ErrorStats
{
    double mean = 0.0, variance = 0.0;
    int minimum = 0, maximum = 0;
};

// Quantises a constant (in LSBs) many times and returns the statistics of the
// error (output - input) in LSBs.
ErrorStats errorOf(DitherMode mode, double inputLsb, int bits = 16, int count = 200000)
{
    Quantiser q(bits, mode, 12345);
    const double scale = std::ldexp(1.0, bits - 1);
    const auto input = static_cast<float>(inputLsb / scale);
    ErrorStats s;
    s.minimum = 1 << 30;
    s.maximum = -(1 << 30);
    double sum = 0.0, sumSq = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const int v = q.process(input);
        s.minimum = std::min(s.minimum, v);
        s.maximum = std::max(s.maximum, v);
        const double e = v - static_cast<double>(input) * scale;
        sum += e;
        sumSq += e * e;
    }
    s.mean = sum / count;
    s.variance = sumSq / count - s.mean * s.mean;
    return s;
}
} // namespace

// ----- settings --------------------------------------------------------------------------

TEST_CASE("Each export format offers the bit depths it can write", "[engine][export]")
{
    CHECK(depthsFor(ExportFormat::wav).size() == 3);
    CHECK(depthsFor(ExportFormat::aiff) == std::vector<ExportDepth>{ExportDepth::int16, ExportDepth::int24});
    CHECK(depthsFor(ExportFormat::flac) == std::vector<ExportDepth>{ExportDepth::int16, ExportDepth::int24});
    CHECK(depthsFor(ExportFormat::ogg).empty());
    CHECK(depthFor(ExportFormat::flac, ExportDepth::float32) == ExportDepth::int24);
    CHECK(depthFor(ExportFormat::wav, ExportDepth::float32) == ExportDepth::float32);
    CHECK(depthFor(ExportFormat::aiff, ExportDepth::int16) == ExportDepth::int16);
    CHECK(std::string(extensionFor(ExportFormat::flac)) == "flac");
    CHECK(std::string(extensionFor(ExportFormat::aiff)) == "aiff");
}

TEST_CASE("Dither applies only when a lossless export is reduced to integers", "[engine][export]")
{
    ExportSettings s;
    s.format = ExportFormat::wav;
    s.depth = ExportDepth::int16;
    s.dither = DitherMode::tpdf;
    CHECK(ditherApplies(s));
    s.depth = ExportDepth::float32;
    CHECK_FALSE(ditherApplies(s));
    s.format = ExportFormat::flac; // no float in FLAC: 24-bit, dithered
    CHECK(ditherApplies(s));
    s.format = ExportFormat::ogg;
    CHECK_FALSE(ditherApplies(s));
    s.format = ExportFormat::wav;
    s.depth = ExportDepth::int24;
    s.dither = DitherMode::off;
    CHECK_FALSE(ditherApplies(s));
}

TEST_CASE("Export settings survive a round trip through key/value pairs", "[engine][export]")
{
    ExportSettings s;
    s.source = ExportSource::stems;
    s.range = ExportRange::loop;
    s.format = ExportFormat::flac;
    s.depth = ExportDepth::int16;
    s.sampleRate = 44100;
    s.dither = DitherMode::shaped;
    s.oggQuality = 9;
    s.normalise = true;
    s.targetLufs = -16.0;
    s.ceilingDb = -0.5;
    s.peakMode = PeakMode::samplePeak;
    const auto values = s.toKeyValues();
    CHECK(values.at("export.format") == "flac");
    CHECK(ExportSettings::fromKeyValues(values) == s);

    // Unknown or out-of-range values fall back to something valid.
    auto broken = values;
    broken["export.format"] = "mp3";
    broken["export.sampleRate"] = "12345";
    broken["export.targetLufs"] = "loud";
    broken["export.oggQuality"] = "99";
    const auto read = ExportSettings::fromKeyValues(broken);
    CHECK(read.format == ExportFormat::wav);
    CHECK(read.sampleRate == 0);
    CHECK(read.targetLufs == -14.0);
    CHECK(read.oggQuality == 10);
    CHECK(ExportSettings::fromKeyValues({}) == ExportSettings{});

    // A region is picked per export; what is remembered is the whole project.
    s.range = ExportRange::region;
    CHECK(ExportSettings::fromKeyValues(s.toKeyValues()).range == ExportRange::project);
}

// ----- quantisation and dither -----------------------------------------------------------

TEST_CASE("TPDF dither of silence at 16-bit stays within one LSB", "[engine][export][dither]")
{
    const auto s = errorOf(DitherMode::tpdf, 0.0);
    CHECK(s.minimum == -1);
    CHECK(s.maximum == 1);
    CHECK(s.mean == Approx(0.0).margin(0.01));
    // The triangular dither rounds to -1 or +1 a quarter of the time: 1/4 LSB^2.
    CHECK(s.variance == Approx(0.25).epsilon(0.05));
}

TEST_CASE("TPDF dither makes the error independent of the signal", "[engine][export][dither]")
{
    // Total error = rounding (1/12) + TPDF (1/6) = 1/4 LSB^2 with zero mean, wherever
    // the signal sits between two steps: no distortion, no noise modulation.
    for (const double offset : {0.0, 0.1, 0.25, 0.5, 0.75, 1234.3, -987.6})
    {
        INFO("offset " << offset);
        const auto s = errorOf(DitherMode::tpdf, offset);
        CHECK(s.mean == Approx(0.0).margin(0.01));
        CHECK(s.variance == Approx(0.25).epsilon(0.05));
    }
    // Without dither a quarter-LSB signal rounds away to nothing every time.
    const auto plain = errorOf(DitherMode::off, 0.25);
    CHECK(plain.minimum == 0);
    CHECK(plain.maximum == 0);
    CHECK(plain.mean == Approx(-0.25));
}

TEST_CASE("Dither at 24-bit works in 24-bit steps and full scale clips", "[engine][export][dither]")
{
    const auto s = errorOf(DitherMode::tpdf, 0.0, 24, 50000);
    CHECK(s.minimum >= -1);
    CHECK(s.maximum <= 1);
    Quantiser q(24, DitherMode::tpdf);
    CHECK(q.process(2.0f) == (1 << 23) - 1);
    CHECK(q.process(-2.0f) == -(1 << 23));
    Quantiser q16(16, DitherMode::off);
    CHECK(q16.process(0.5f) == 16384);
    CHECK(q16.toFloat(16384) == 0.5f);
}

TEST_CASE("Noise shaping moves the dither noise from the low band to the top", "[engine][export][dither]")
{
    // The error at 44.1 kHz, split crudely into a low band (a 16-sample moving
    // average keeps roughly below 1.4 kHz) and a high band (first difference).
    const auto bands = [](DitherMode mode)
    {
        Quantiser q(16, mode, 777);
        const auto input = sine(1000.0, 0.25, 44100.0, 1 << 17);
        std::vector<double> e(input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            e[i] = q.process(input[i]) - static_cast<double>(input[i]) * 32768.0;
        double low = 0.0, high = 0.0;
        for (std::size_t i = 16; i < e.size(); ++i)
        {
            double avg = 0.0;
            for (std::size_t k = 0; k < 16; ++k)
                avg += e[i - k];
            avg /= 16.0;
            low += avg * avg;
            high += (e[i] - e[i - 1]) * (e[i] - e[i - 1]);
        }
        return std::pair{low, high};
    };
    const auto [flatLow, flatHigh] = bands(DitherMode::tpdf);
    const auto [shapedLow, shapedHigh] = bands(DitherMode::shaped);
    CHECK(shapedLow < flatLow * 0.5);
    CHECK(shapedHigh > flatHigh * 1.5);
}

// ----- sample-rate conversion ------------------------------------------------------------

TEST_CASE("Resampling keeps a sine's amplitude, frequency and phase", "[engine][export][resample]")
{
    struct Case
    {
        double from, to, hz;
    };
    for (const auto& c : {Case{44100.0, 48000.0, 1000.0},
                          Case{48000.0, 44100.0, 1000.0},
                          Case{96000.0, 44100.0, 997.0},
                          Case{44100.0, 96000.0, 5000.0},
                          Case{48000.0, 44100.0, 15000.0},
                          Case{48000.0, 88200.0, 18000.0}})
    {
        INFO(c.from << " -> " << c.to << " at " << c.hz << " Hz");
        const std::size_t n = static_cast<std::size_t>(c.from); // one second
        const auto in = sine(c.hz, 0.5, c.from, n);
        const auto out = resample(in.data(), in.size(), c.from, c.to);
        CHECK(out.size() == static_cast<std::size_t>(c.to));
        // Away from the edges the output is the same sine at the new rate.
        const auto ideal = sine(c.hz, 0.5, c.to, out.size());
        double worst = 0.0;
        for (std::size_t i = out.size() / 10; i < out.size() * 9 / 10; ++i)
            worst = std::max(worst, std::abs(static_cast<double>(out[i]) - ideal[i]));
        CHECK(worst < 0.5 * 0.002); // within about -54 dB of the signal: right frequency and phase
        const double level = gainToDb(rms(out, out.size() / 10, out.size() * 9 / 10) /
                                      rms(ideal, out.size() / 10, out.size() * 9 / 10));
        CHECK(std::abs(level) < 0.02);
    }
}

TEST_CASE("Downsampling removes what lies above the new Nyquist frequency", "[engine][export][resample]")
{
    const auto in = sine(30000.0, 0.5, 96000.0, 96000);
    const auto out = resample(in.data(), in.size(), 96000.0, 44100.0);
    const double level = gainToDb(rms(out, 4410, out.size() - 4410) / (0.5 / std::sqrt(2.0)));
    CHECK(level < -70.0);
}

TEST_CASE("Resampling at equal rates is a copy", "[engine][export][resample]")
{
    const auto in = sine(440.0, 0.3, 48000.0, 1000);
    CHECK(resample(in.data(), in.size(), 48000.0, 48000.0) == in);
    CHECK(resampledLength(48000, 48000.0, 44100.0) == 44100);
}

// ----- measurement and normalisation -----------------------------------------------------

TEST_CASE("True peak finds the peak between the samples", "[engine][export][peak]")
{
    // A quarter-rate sine sampled 45 degrees off its peaks: every sample is at
    // 0.707 of the amplitude, the waveform itself reaches the full amplitude.
    const auto s = sine(12000.0, 0.5, 48000.0, 4800, std::numbers::pi / 4.0);
    CHECK(gainToDb(samplePeak(s.data(), s.size())) == Approx(gainToDb(0.5) - 3.01).margin(0.02));
    CHECK(gainToDb(truePeak(s.data(), s.size())) == Approx(gainToDb(0.5)).margin(0.1));
    // A low tone's true peak is its sample peak.
    const auto low = sine(997.0, 0.5, 48000.0, 48000);
    CHECK(gainToDb(truePeak(low.data(), low.size())) == Approx(gainToDb(0.5)).margin(0.02));
}

TEST_CASE("Normalisation brings a signal to the loudness target within 0.1 LU", "[engine][export][loudness]")
{
    std::vector<std::vector<float>> mix{sine(997.0, dbToGain(-30.0), 48000.0, 48000 * 10),
                                        sine(997.0, dbToGain(-30.0), 48000.0, 48000 * 10)};
    mix[1] = sine(997.0, dbToGain(-36.0), 48000.0, 48000 * 10); // unequal channels
    const auto before = analyse(mix, 48000.0, true);
    CHECK(before.integratedLufs < -25.0);
    for (const double target : {-14.0, -16.0, -23.0})
    {
        INFO("target " << target);
        auto copy = mix;
        const auto n = normalisationGain(before.integratedLufs, before.truePeakDb, target, -1.0);
        CHECK_FALSE(n.limitedByCeiling);
        applyGain(copy, static_cast<float>(dbToGain(n.gainDb)));
        CHECK(integratedLoudness(copy, 48000.0) == Approx(target).margin(0.1));
    }
}

TEST_CASE("The peak ceiling limits the normalisation gain", "[engine][export][loudness]")
{
    std::vector<std::vector<float>> mix{sine(997.0, 0.1, 48000.0, 48000 * 5),
                                        sine(997.0, 0.1, 48000.0, 48000 * 5)};
    const auto before = analyse(mix, 48000.0, true);
    // 0 LUFS would need a peak near +0.7 dBFS.
    const auto n = normalisationGain(before.integratedLufs, before.truePeakDb, 0.0, -1.0);
    CHECK(n.limitedByCeiling);
    applyGain(mix, static_cast<float>(dbToGain(n.gainDb)));
    const auto after = analyse(mix, 48000.0, true);
    CHECK(after.truePeakDb == Approx(-1.0).margin(0.05));
    CHECK(after.integratedLufs < -1.0);

    // Silence gets no gain at all.
    const auto silent = normalisationGain(-100.0, -200.0, -14.0, -1.0);
    CHECK(silent.gainDb == 0.0);
    CHECK_FALSE(silent.limitedByCeiling);
}
