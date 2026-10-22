// Each synth renders a fixed phrase (a chord, a release, all-notes-off, a
// second note) to the same output as when this was written: a refactor of the
// shared voice code must not change how they sound. The fingerprints are the
// output's sum, a position-weighted sum and its peak.
#include "engine/Midi.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace northlight;

TEST_CASE("Each synth renders a fixed phrase as it did when its voices were shared out", "[plugins][synth]")
{
    struct Fingerprint
    {
        double sum, weighted, peak;
    };
    const std::map<std::string, Fingerprint> expected{
        {"northlight.aurora", {7146.622921677, 3566664.141710, 0.711221039}},
        {"northlight.nebula", {9364.366943710, 4674356.443178, 0.624030709}},
        // Starbit: re-measured after band-limiting its steps, the even lo-fi holds and
        // the DC blocker (which lifts a pulse's peak: it swings about zero now).
        {"northlight.starbit", {8592.526227236, 4290393.874162215, 0.967896581}},
        // Pulsar: re-measured after its voices were remodelled on the 808's circuits.
        {"northlight.pulsar", {2771.899556874, 1308191.205925479, 0.724025846}},
    };
    for (const auto& [type, want] : expected)
    {
        INFO(type);
        auto synth = plugins::BuiltInFactory().create(type);
        synth->prepare(48000.0, 256);
        double sum = 0.0, weighted = 0.0, peak = 0.0;
        std::vector<float> l(256), r(256);
        for (int block = 0; block < 400; ++block)
        {
            engine::MidiEventList events;
            if (block == 1)
            {
                events.push({10, engine::MidiEvent::Type::noteOn, 0, 60, 100});
                events.push({20, engine::MidiEvent::Type::noteOn, 0, 64, 90});
                events.push({30, engine::MidiEvent::Type::noteOn, 0, 67, 80});
            }
            if (block == 60)
                events.push({5, engine::MidiEvent::Type::noteOff, 0, 64, 0});
            if (block == 120)
                events.push({0, engine::MidiEvent::Type::allNotesOff, 0, 0, 0});
            if (block == 200)
                events.push({100, engine::MidiEvent::Type::noteOn, 0, 72, 127});
            if (block == 260)
                events.push({1, engine::MidiEvent::Type::noteOff, 0, 72, 0});
            synth->processMidi(events, l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                const double v = std::abs(l[static_cast<size_t>(i)]) + std::abs(r[static_cast<size_t>(i)]);
                sum += v;
                weighted += v * ((block * 256 + i) % 997);
                peak = std::max(peak, v);
            }
        }
        CHECK(sum == Catch::Approx(want.sum).epsilon(1.0e-4));
        CHECK(weighted == Catch::Approx(want.weighted).epsilon(1.0e-4));
        CHECK(peak == Catch::Approx(want.peak).epsilon(1.0e-4));
    }
}
