// Not run by default ([.probe]): prints how much of Starbit's output is not a
// harmonic of the note (aliasing and other artefacts), for investigation.
#include "engine/Midi.h"
#include "plugins/BuiltIns.h"
#include "plugins/Starbit.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

using namespace northlight;

namespace
{
void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u(1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
}

struct Result
{
    double inharmonicDb; // inharmonic power relative to the total
    double worstPeakDb;  // the loudest inharmonic bin relative to the loudest harmonic
    double worstPeakHz;
};

Result analyse(std::vector<float> signal, double f0, double rate)
{
    const size_t n = signal.size();
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i)
        a[i] = signal[i] * (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / (n - 1)));
    fft(a);
    double total = 0.0, inharmonic = 0.0, bestHarmonic = 0.0, worst = 0.0, worstHz = 0.0;
    const double binHz = rate / static_cast<double>(n);
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double p = std::norm(a[k]);
        const double f = static_cast<double>(k) * binHz;
        if (f < 20.0)
            continue; // DC is reported apart

        const double nearest = std::round(f / f0) * f0;
        const bool harmonic = nearest > 0.0 && std::abs(f - nearest) <= 4.0 * binHz;
        total += p;
        if (harmonic)
            bestHarmonic = std::max(bestHarmonic, p);
        else
        {
            inharmonic += p;
            if (p > worst)
            {
                worst = p;
                worstHz = f;
            }
        }
    }
    return {10.0 * std::log10(inharmonic / total), 10.0 * std::log10(worst / bestHarmonic), worstHz};
}

std::vector<float> render(int note, int shape, float bits, float sampleRate)
{
    auto synth = plugins::BuiltInFactory().create(plugins::starbit::type);
    namespace sb = plugins::starbit;
    synth->prepare(48000.0, 512);
    synth->setParameter(sb::wave, static_cast<float>(shape));
    synth->setParameter(sb::duty, 1.0f); // 25%
    synth->setParameter(sb::attack, 0.0f);
    synth->setParameter(sb::sustain, 1.0f);
    synth->setParameter(sb::vibratoDepth, 0.0f);
    synth->setParameter(sb::bits, bits);
    synth->setParameter(sb::rate, sampleRate);
    std::vector<float> out;
    std::vector<float> l(512), r(512);
    for (int block = 0; block < 200; ++block)
    {
        engine::MidiEventList events;
        if (block == 0)
            events.push({0, engine::MidiEvent::Type::noteOn, 0, static_cast<std::uint8_t>(note), 127});
        synth->processMidi(events, l.data(), r.data(), 512);
        if (block >= 20)
            out.insert(out.end(), l.begin(), l.end());
    }
    out.resize(65536);
    return out;
}
} // namespace

TEST_CASE("Starbit spectrum probe", "[.probe]")
{
    const char* shapes[] = {"pulse", "triangle", "saw"};
    for (int shape = 0; shape < 3; ++shape)
        for (const int note : {60, 84})
            for (const auto& [bits, rate, label] : {std::tuple{16.0f, 48000.0f, "lo-fi off (16 bit, 48 kHz)"},
                                                    std::tuple{16.0f, 24000.0f, "16 bit, 24 kHz"},
                                                    std::tuple{16.0f, 11025.0f, "16 bit, 11.025 kHz"},
                                                    std::tuple{16.0f, 7000.0f, "16 bit, 7 kHz"},
                                                    std::tuple{16.0f, 4000.0f, "16 bit, 4 kHz"}})
            {
                const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
                const auto r = analyse(render(note, shape, bits, rate), f0, 48000.0);
                const auto sig = render(note, shape, bits, rate);
                double dc = 0.0, rms = 0.0;
                for (const float v : sig)
                {
                    dc += v;
                    rms += static_cast<double>(v) * v;
                }
                dc /= static_cast<double>(sig.size());
                rms = std::sqrt(rms / static_cast<double>(sig.size()));
                std::printf("  DC %.3f of RMS %.3f\n", dc, rms);
                std::printf(
                    "%-8s note %3d (%7.1f Hz) %-30s inharmonic %6.1f dB  worst %6.1f dB at %7.0f Hz\n",
                    shapes[shape],
                    note,
                    f0,
                    label,
                    r.inharmonicDb,
                    r.worstPeakDb,
                    r.worstPeakHz);
            }
}
