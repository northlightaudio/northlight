// Not run by default ([.probe]): prints each Pulsar pad's peak, loudness, DC,
// brightness and length, and with NORTHLIGHT_PROBE_DIR set writes each pad (and a
// short pattern) there as a WAV to listen to.
#include "plugins/Pulsar.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace northlight::plugins;

namespace
{
void writeWav(const std::string& path, const std::vector<float>& mono, int sampleRate)
{
    std::ofstream f(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(mono.size() * 2);
    f.write("RIFF", 4);
    put32(36 + bytes);
    f.write("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(1);
    put32(static_cast<std::uint32_t>(sampleRate));
    put32(static_cast<std::uint32_t>(sampleRate * 2));
    put16(2);
    put16(16);
    f.write("data", 4);
    put32(bytes);
    for (const float v : mono)
        put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
}
} // namespace

TEST_CASE("Pulsar pad probe", "[.probe]")
{
    constexpr int rate = 48000;
    const auto info = pulsar::parameterInfo();
    std::vector<float> params;
    for (const auto& p : info)
        params.push_back(p.defaultValue);
    const char* dir = std::getenv("NORTHLIGHT_PROBE_DIR");
    for (int pad = 0; pad < pulsar::padCount; ++pad)
    {
        const auto out = pulsar::renderPad(params.data(), pad, rate, rate * 3);
        double peak = 0.0, sum = 0.0, energy = 0.0, diff = 0.0;
        int last = 0;
        const int window = rate / 5;
        for (int i = 0; i < static_cast<int>(out.size()); ++i)
        {
            const double v = out[static_cast<size_t>(i)];
            peak = std::max(peak, std::abs(v));
            sum += v;
            if (i < window)
            {
                energy += v * v;
                if (i > 0)
                    diff += std::pow(v - out[static_cast<size_t>(i - 1)], 2.0);
            }
            if (std::abs(v) > 0.001)
                last = i;
        }
        // Brightness: the frequency of a sine with the same slope-to-level ratio.
        const double bright = energy > 0.0 ? std::sqrt(diff / energy) * rate / (2.0 * 3.14159265) : 0.0;
        std::printf("%-10s peak %.3f  rms200 %.3f  dc %+.5f  bright %6.0f Hz  len %5.0f ms\n",
                    pulsar::padName(pad),
                    peak,
                    std::sqrt(energy / window),
                    sum / static_cast<double>(out.size()),
                    bright,
                    last * 1000.0 / rate);
        if (dir != nullptr)
            writeWav(std::string(dir) + "/" + pulsar::padShortName(pad) + ".wav", out, rate);
    }
    if (dir != nullptr)
    {
        // A bar at 100 bpm on sixteenths: kick, hats, snare and clap, toms at the end.
        auto synth = pulsar::create();
        synth->prepare(rate, 256);
        const int step = rate * 60 / 100 / 4;
        const char* pattern[] = {"K.h.", "S.h.", "K.hK", "SCO.", "K.h.", "S.hK", "K.hR", "SCLH"};
        std::vector<float> mix;
        std::vector<float> l(256), r(256);
        const int total = step * 16 * 2 + rate;
        for (int at = 0; at < total; at += 256)
        {
            northlight::engine::MidiEventList events;
            for (int s = 0; s < 64; ++s)
            {
                const int when = s * step % (step * 32) + (s >= 32 ? step * 32 : 0);
                if (s >= 32 || when < at || when >= at + 256)
                    continue;
                const char c = pattern[(s / 4) % 8][s % 4];
                const int note = c == 'K'   ? 36
                                 : c == 'S' ? 38
                                 : c == 'C' ? 39
                                 : c == 'h' ? 42
                                 : c == 'O' ? 46
                                 : c == 'R' ? 37
                                 : c == 'L' ? 45
                                 : c == 'H' ? 48
                                            : -1;
                if (note >= 0)
                    events.noteOn(when - at, note, 110);
            }
            synth->processMidi(events, l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i)
                mix.push_back(0.5f * (l[static_cast<size_t>(i)] + r[static_cast<size_t>(i)]));
        }
        writeWav(std::string(dir) + "/pattern.wav", mix, rate);
    }
}
