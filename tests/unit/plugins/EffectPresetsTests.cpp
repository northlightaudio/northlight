#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/Cirrus.h"
#include "plugins/EffectPresets.h"
#include "plugins/Lumen.h"
#include "plugins/Nebula.h"
#include "plugins/ParamHelpers.h"
#include "plugins/Patina.h"
#include "plugins/Starbit.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace northlight;

TEST_CASE("Every built-in effect has factory presets, the first its defaults", "[plugins][presets]")
{
    plugins::BuiltInFactory factory;
    const auto withPresets = plugins::typesWithPresets();
    for (const auto& type : factory.typeNames())
    {
        if (plugins::BuiltInFactory::isInstrument(type))
            continue;
        INFO(type);
        CHECK(std::find(withPresets.begin(), withPresets.end(), type) != withPresets.end());
        const auto& list = plugins::factoryPresets(type);
        REQUIRE(list.size() >= 3);
        CHECK(list.front().values == plugins::defaultParameters(type));
    }
    // Every name and choice in the lists resolved.
    for (const auto& error : plugins::factoryPresetErrors())
        FAIL_CHECK(error);
}

TEST_CASE("Factory presets are full, in range, named once and short", "[plugins][presets]")
{
    for (const auto& type : plugins::typesWithPresets())
    {
        const auto& infos = plugins::parameterInfo(type);
        std::set<std::string> names;
        for (const auto& preset : plugins::factoryPresets(type))
        {
            INFO(type << " / " << preset.name);
            CHECK(names.insert(preset.name).second);
            CHECK(preset.name.size() <= 20);
            REQUIRE(preset.values.size() == infos.size());
            for (size_t i = 0; i < infos.size(); ++i)
            {
                CHECK(preset.values[i] >= infos[i].minimum);
                CHECK(preset.values[i] <= infos[i].maximum);
            }
        }
    }
}

TEST_CASE("Every factory preset plays a tone without blowing up", "[plugins][presets]")
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    plugins::BuiltInFactory factory;
    for (const auto& type : plugins::typesWithPresets())
        for (const auto& preset : plugins::factoryPresets(type))
        {
            INFO(type << " / " << preset.name);
            auto p = factory.create(type);
            REQUIRE(p != nullptr);
            p->prepare(rate, block);
            p->setBypassed(false);
            for (size_t i = 0; i < preset.values.size(); ++i)
                p->setParameter(static_cast<int>(i), preset.values[i]);
            std::vector<float> left(block), right(block);
            float peak = 0.0f;
            for (int b = 0; b < 100; ++b)
            {
                for (int n = 0; n < block; ++n)
                {
                    const double t = static_cast<double>(b * block + n) / rate;
                    left[static_cast<size_t>(n)] = right[static_cast<size_t>(n)] =
                        b < 60 ? 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * t))
                               : 0.0f;
                }
                p->process(left.data(), right.data(), block);
                for (size_t n = 0; n < left.size(); ++n)
                {
                    REQUIRE(std::isfinite(left[n]));
                    REQUIRE(std::isfinite(right[n]));
                    peak = std::max({peak, std::abs(left[n]), std::abs(right[n])});
                }
            }
            CHECK(peak < 4.0f);
        }
}

TEST_CASE("The instruments' and Patina's presets are full and in range", "[plugins][presets]")
{
    // They are built by parameter index, so a table that changed under them
    // shows up here (and in factoryPresetErrors).
    const auto check = [](const std::string& type, const std::vector<plugins::EffectPreset>& list)
    {
        const auto& infos = plugins::parameterInfo(type);
        REQUIRE_FALSE(list.empty());
        CHECK(list.front().values == plugins::defaultParameters(type));
        for (const auto& preset : list)
        {
            INFO(type << " / " << preset.name);
            REQUIRE(preset.values.size() == infos.size());
            for (size_t i = 0; i < infos.size(); ++i)
            {
                INFO(infos[i].name);
                CHECK(preset.values[i] >= infos[i].minimum);
                CHECK(preset.values[i] <= infos[i].maximum);
            }
        }
    };
    check(plugins::aurora::type, plugins::aurora::presets());
    check(plugins::nebula::type, plugins::nebula::presets());
    check(plugins::starbit::type, plugins::starbit::presets());
    check(plugins::lumen::type, plugins::lumen::presets());
    check(plugins::cirrus::type, plugins::cirrus::presets());
    check(plugins::patina::type, plugins::patina::presets());
    CHECK(plugins::factoryPresetErrors().empty());
}

TEST_CASE("A preset built by index reports a value out of range or a missing parameter", "[plugins][presets]")
{
    plugins::PresetBuilder builder("test", {{"Level", "dB", -60.0f, 6.0f, 0.0f, false}});
    builder.add("Fine", {{0, -6.0}});
    CHECK(builder.errors().empty());
    builder.add("Too loud", {{0, 12.0}});
    builder.add("No such", {{3, 0.0}});
    REQUIRE(builder.errors().size() == 2);
    CHECK(builder.errors()[0] == "test / Too loud / Level: out of range");
    CHECK(builder.errors()[1] == "test / No such: no parameter 3");
}
