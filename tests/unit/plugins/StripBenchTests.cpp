#include "plugins/ChannelStrip.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
namespace strip = plugins::strip;

// A benchmark, hidden from the default run (northlight_unit_tests "[.bench]"): what
// a channel strip costs per track, all modules on and all off, in percent of
// one core at 48 kHz with 256-sample blocks.
TEST_CASE("Channel strip cost per track", "[.bench][strip]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    constexpr int seconds = 10;
    const int blocks = static_cast<int>(rate * seconds / block);
    std::vector<float> l(block), r(block);
    for (int i = 0; i < block; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / rate));

    for (const bool on : {true, false})
    {
        std::vector<std::unique_ptr<engine::InsertProcessor>> modules;
        for (const auto& type : strip::moduleTypes())
        {
            auto m = strip::create(type);
            m->setBypassed(!on);
            m->prepare(rate, block);
            modules.push_back(std::move(m));
        }
        if (on) // the EQ with every band doing something, and the analyser taps running
        {
            auto& eq = *modules[1];
            eq.setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::on), 1.0f);
            eq.setParameter(strip::eq::paramIndex(strip::eq::highCut, strip::eq::on), 1.0f);
            eq.setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::type), 5.0f);
            eq.setParameter(strip::eq::paramIndex(strip::eq::highCut, strip::eq::type), 5.0f);
            for (int b = strip::eq::band1; b <= strip::eq::band4; ++b)
                eq.setParameter(strip::eq::paramIndex(b, strip::eq::gain), 3.0f);
            eq.feedback()->setTapsEnabled(true);
        }
        const auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < blocks; ++n)
            for (auto& m : modules)
                m->process(l.data(), r.data(), block);
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double percent = 100.0 * elapsed / seconds;
        std::printf("strip %s: %.3f%% of a core per track (%.0f tracks per core)\n",
                    on ? "all on" : "all off",
                    percent,
                    100.0 / std::max(1.0e-9, percent));
        CHECK(percent < (on ? 5.0 : 0.5)); // generous: a debug build is slow
    }
}
