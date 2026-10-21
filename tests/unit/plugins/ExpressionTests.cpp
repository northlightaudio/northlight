// The built-in instruments follow the player's expression: pitch bend (+-2
// semitones), the mod wheel and the sustain pedal.
#include "engine/Midi.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/SynthVoices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// Renders `blocks` blocks of a fresh instrument; `events(b, list)` fills block b's events.
std::vector<float> render(const std::string& type,
                          int blocks,
                          const std::function<void(int, engine::MidiEventList&)>& events,
                          bool plain = false)
{
    auto synth = plugins::BuiltInFactory().create(type);
    synth->prepare(rate, block);
    if (plain && type == "northlight.aurora")
    {
        // One sine voice, nothing around it: its period reads cleanly.
        namespace a = plugins::aurora;
        synth->setParameter(a::osc1Shape, 0.0f);
        synth->setParameter(a::unison, 1.0f);
        synth->setParameter(a::osc2Level, 0.0f);
        synth->setParameter(a::sub, 0.0f);
        synth->setParameter(a::noise, 0.0f);
        synth->setParameter(a::shimmer, 0.0f);
    }
    std::vector<float> out;
    std::vector<float> l(block), r(block);
    for (int b = 0; b < blocks; ++b)
    {
        engine::MidiEventList list(64);
        events(b, list);
        synth->processMidi(list, l.data(), r.data(), block);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

// The period (in samples) with the strongest normalised autocorrelation in
// [minLag, maxLag] over `length` samples from `from`.
double period(const std::vector<float>& x, size_t from, size_t length, int minLag, int maxLag)
{
    double best = -2.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0, a = 0.0, b = 0.0;
        for (size_t i = from; i < from + length; ++i)
        {
            sum += x[i] * x[i + static_cast<size_t>(lag)];
            a += x[i] * x[i];
            b += x[i + static_cast<size_t>(lag)] * x[i + static_cast<size_t>(lag)];
        }
        const double c = sum / std::sqrt(a * b + 1.0e-12);
        if (c > best)
        {
            best = c;
            bestLag = lag;
        }
    }
    return bestLag;
}

double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += x[i] * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}
} // namespace

TEST_CASE("The sustain pedal holds released notes until it comes up", "[plugins][synth][expression]")
{
    plugins::synth::Expression x;
    std::vector<int> offs;
    int allOffs = 0;
    const auto send = [&](engine::MidiEvent e)
    {
        plugins::synth::dispatch(
            e, x, [](int, int) {}, [&](int note) { offs.push_back(note); }, [&] { ++allOffs; });
    };
    engine::MidiEventList list(16);
    list.noteOn(0, 60, 100);
    list.controller(0, 64, 127);
    list.noteOff(0, 60);
    list.noteOn(0, 62, 100);
    list.noteOn(0, 62, 0); // a note-on at velocity 0 is a note-off
    list.controller(0, 64, 0);
    for (const auto& e : list)
    {
        send(e);
        if (e.type == engine::MidiEvent::Type::noteOn && e.note == 62 && e.velocity == 0)
            CHECK(offs.empty()); // held by the pedal
    }
    CHECK(offs == std::vector<int>{60, 62});
    CHECK_FALSE(x.pedal);

    // A note played again while held is no longer waiting for the pedal.
    offs.clear();
    engine::MidiEventList again(8);
    again.controller(0, 64, 100);
    again.noteOff(0, 64);
    again.noteOn(0, 64, 90);
    again.controller(0, 64, 0);
    for (const auto& e : again)
        send(e);
    CHECK(offs.empty());

    // Pitch bend, the mod wheel and pressure; reset-all-controllers rests them.
    engine::MidiEventList moves(8);
    moves.pitchBend(0, 8191);
    moves.controller(0, 1, 127);
    moves.channelPressure(0, 64);
    for (const auto& e : moves)
        send(e);
    CHECK(x.bend == Catch::Approx(1.0f));
    CHECK(x.modWheel == Catch::Approx(1.0f));
    CHECK(x.pressure == Catch::Approx(64.0f / 127.0f));
    CHECK(x.advance(0.01) != 0.0);
    engine::MidiEventList reset(2);
    reset.controller(0, 121, 0);
    send(reset[0]);
    CHECK(x.neutral());
    CHECK(x.advance(0.01) == 0.0);
}

TEST_CASE("Pitch bend moves each melodic synth's pitch by up to two semitones",
          "[plugins][synth][expression]")
{
    // A4 (440 Hz, a period of ~109 samples); bent fully up, B4 (~97 samples).
    for (const std::string type : {"northlight.aurora", "northlight.nebula", "northlight.starbit"})
    {
        INFO(type);
        const auto play = [&](int bendValue)
        {
            return render(
                type,
                60,
                [&](int b, engine::MidiEventList& list)
                {
                    if (b == 0)
                    {
                        if (bendValue != 0)
                            list.pitchBend(0, bendValue);
                        list.noteOn(0, 69, 110);
                    }
                },
                true);
        };
        const auto flat = play(0);
        const auto bent = play(8191);
        const double straight = period(flat, 8000, 4096, 80, 140);
        const double up = period(bent, 8000, 4096, 80, 140);
        CHECK(straight == Catch::Approx(rate / 440.0).margin(2.0));
        CHECK(up == Catch::Approx(rate / (440.0 * std::exp2(2.0 / 12.0))).margin(2.0));
    }
}

TEST_CASE("A centred bend and a resting mod wheel change nothing; moving them changes the sound",
          "[plugins][synth][expression]")
{
    for (const std::string type :
         {"northlight.aurora", "northlight.nebula", "northlight.starbit", "northlight.pulsar"})
    {
        INFO(type);
        const auto play = [&](int bendValue, int mod)
        {
            return render(type,
                          40,
                          [&](int b, engine::MidiEventList& list)
                          {
                              if (b == 0)
                              {
                                  list.pitchBend(0, bendValue);
                                  list.controller(0, 1, mod);
                                  list.noteOn(0, 36, 110); // the kick on Pulsar
                              }
                          });
        };
        const auto plain = render(type,
                                  40,
                                  [](int b, engine::MidiEventList& list)
                                  {
                                      if (b == 0)
                                          list.noteOn(0, 36, 110);
                                  });
        CHECK(play(0, 0) == plain);
        CHECK(play(-8192, 0) != plain);
        CHECK(play(0, 127) != plain);
    }
}

TEST_CASE("Aurora holds a released note while the sustain pedal is down", "[plugins][synth][expression]")
{
    const auto play = [](bool pedal)
    {
        return render("northlight.aurora",
                      800,
                      [&](int b, engine::MidiEventList& list)
                      {
                          if (b == 0)
                          {
                              if (pedal)
                                  list.controller(0, 64, 127);
                              list.noteOn(0, 60, 110);
                          }
                          if (b == 20)
                              list.noteOff(0, 60);
                          if (b == 400)
                              list.controller(0, 64, 0);
                      });
    };
    const auto free = play(false);
    const auto held = play(true);
    const auto at = [](int b) { return static_cast<size_t>(b * block); };
    CHECK(rms(free, at(350), at(400)) < 1.0e-3);
    CHECK(rms(held, at(350), at(400)) > 1.0e-2);
    CHECK(rms(held, at(750), at(800)) < 1.0e-3); // released when the pedal came up
}
