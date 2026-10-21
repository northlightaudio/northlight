#include "plugins/StripPresets.h"

#include "plugins/ChannelStrip.h"

#include <algorithm>
#include <utility>

namespace northlight::plugins::strip
{

namespace
{
// A preset being written: every module off at its defaults to start with.
class Builder
{
public:
    Builder(std::string name, std::string category, std::string description)
    {
        preset_.name = std::move(name);
        preset_.category = std::move(category);
        preset_.description = std::move(description);
        preset_.modules = defaultStrip();
    }

    Builder& on(const std::string& type)
    {
        module(type).bypass = false;
        return *this;
    }
    Builder& set(const std::string& type, int index, float value)
    {
        auto& m = module(type);
        if (index >= 0 && static_cast<size_t>(index) < m.params.size())
            m.params[static_cast<size_t>(index)] = value;
        return *this;
    }
    // EQ helpers: a band (shape, frequency, gain, Q) and a cut (frequency, slope in dB/oct).
    Builder& band(int b, eq::Shape shape, float hz, float gainDb, float q)
    {
        set(types::eq, eq::paramIndex(b, eq::on), 1.0f);
        set(types::eq, eq::paramIndex(b, eq::type), static_cast<float>(shape));
        set(types::eq, eq::paramIndex(b, eq::frequency), hz);
        set(types::eq, eq::paramIndex(b, eq::gain), gainDb);
        return set(types::eq, eq::paramIndex(b, eq::q), q);
    }
    Builder& cut(int b, float hz, int slopeDb)
    {
        const auto at = std::find(eq::slopes.begin(), eq::slopes.end(), slopeDb);
        set(types::eq, eq::paramIndex(b, eq::on), 1.0f);
        set(types::eq, eq::paramIndex(b, eq::frequency), hz);
        return set(types::eq, eq::paramIndex(b, eq::type), static_cast<float>(at - eq::slopes.begin()));
    }
    // The bands a preset leaves flat are switched off (no node in the way).
    Builder& flat(int b) { return set(types::eq, eq::paramIndex(b, eq::on), 0.0f); }
    Builder& compressor(
        float thresholdDb, float ratio, float attackMs, float releaseMs, float kneeDb, float mix = 100.0f)
    {
        on(types::compressor);
        set(types::compressor, compressor::threshold, thresholdDb);
        set(types::compressor, compressor::ratio, ratio);
        set(types::compressor, compressor::attack, attackMs);
        set(types::compressor, compressor::release, releaseMs);
        set(types::compressor, compressor::knee, kneeDb);
        set(types::compressor, compressor::autoMakeup, 1.0f);
        return set(types::compressor, compressor::mix, mix);
    }
    Builder& gate(float thresholdDb,
                  float rangeDb,
                  float attackMs,
                  float holdMs,
                  float releaseMs,
                  float keyHp = 20.0f,
                  float keyLp = 20000.0f)
    {
        on(types::gate);
        set(types::gate, gate::threshold, thresholdDb);
        set(types::gate, gate::range, rangeDb);
        set(types::gate, gate::attack, attackMs);
        set(types::gate, gate::hold, holdMs);
        set(types::gate, gate::release, releaseMs);
        set(types::gate, gate::keyHighPass, keyHp);
        return set(types::gate, gate::keyLowPass, keyLp);
    }
    Builder& deesser(float hz, float thresholdDb, float rangeDb)
    {
        on(types::deesser);
        set(types::deesser, deesser::frequency, hz);
        set(types::deesser, deesser::threshold, thresholdDb);
        return set(types::deesser, deesser::range, rangeDb);
    }
    Builder& saturation(saturation::Shape shape, float driveDb, float tone, float mix)
    {
        on(types::saturation);
        set(types::saturation, saturation::type, static_cast<float>(shape));
        set(types::saturation, saturation::drive, driveDb);
        set(types::saturation, saturation::tone, tone);
        return set(types::saturation, saturation::mix, mix);
    }
    Builder& limiter(float inputDb, float ceilingDb, float releaseMs)
    {
        on(types::limiter);
        set(types::limiter, limiter::input, inputDb);
        set(types::limiter, limiter::ceiling, ceilingDb);
        return set(types::limiter, limiter::release, releaseMs);
    }

    Preset done() { return std::move(preset_); }

private:
    core::Insert& module(const std::string& type)
    {
        for (auto& m : preset_.modules)
            if (m.type == type)
                return m;
        return preset_.modules.front(); // every type is in the default strip
    }

    Preset preset_;
};

std::vector<Preset> build()
{
    using S = eq::Shape;
    std::vector<Preset> list;

    // ----- vocals -----
    list.push_back(
        Builder("Lead vocal", "Vocals", "Rumble cut, less mud, presence and air; steady level, tamed esses")
            .on(types::eq)
            .cut(eq::lowCut, 90.0f, 24)
            .flat(eq::band1)
            .band(eq::band2, S::peak, 300.0f, -2.5f, 1.2f)
            .band(eq::band3, S::peak, 3500.0f, 2.0f, 0.9f)
            .band(eq::band4, S::highShelf, 10000.0f, 2.5f, 0.7f)
            .compressor(-20.0f, 3.5f, 5.0f, 80.0f, 6.0f)
            .deesser(6500.0f, -32.0f, 6.0f)
            .saturation(saturation::tape, 4.0f, 0.1f, 40.0f)
            .done());
    list.push_back(
        Builder("Backing vocals", "Vocals", "Thinner and further back than the lead, firmly levelled")
            .on(types::eq)
            .cut(eq::lowCut, 140.0f, 24)
            .flat(eq::band1)
            .band(eq::band2, S::peak, 400.0f, -3.0f, 1.0f)
            .band(eq::band3, S::peak, 2500.0f, -1.0f, 1.0f)
            .band(eq::band4, S::highShelf, 10000.0f, 1.5f, 0.7f)
            .compressor(-24.0f, 4.0f, 8.0f, 100.0f, 6.0f)
            .deesser(6000.0f, -35.0f, 8.0f)
            .done());
    list.push_back(
        Builder("Spoken word", "Vocals", "Podcast and voice-over: noise gated, clear, loud and even")
            .gate(-50.0f, 15.0f, 2.0f, 80.0f, 200.0f, 100.0f)
            .on(types::eq)
            .cut(eq::lowCut, 100.0f, 24)
            .flat(eq::band1)
            .band(eq::band2, S::peak, 300.0f, -3.0f, 1.2f)
            .band(eq::band3, S::peak, 3000.0f, 2.0f, 1.0f)
            .flat(eq::band4)
            .compressor(-24.0f, 4.0f, 5.0f, 100.0f, 6.0f)
            .deesser(6000.0f, -30.0f, 8.0f)
            .limiter(3.0f, -1.0f, 60.0f)
            .done());

    // ----- instruments -----
    list.push_back(Builder("Acoustic guitar", "Instruments", "Less boom, more sparkle, gentle levelling")
                       .on(types::eq)
                       .cut(eq::lowCut, 80.0f, 12)
                       .flat(eq::band1)
                       .band(eq::band2, S::peak, 200.0f, -3.0f, 1.4f)
                       .band(eq::band3, S::peak, 5000.0f, 2.0f, 0.8f)
                       .band(eq::band4, S::highShelf, 12000.0f, 1.5f, 0.7f)
                       .compressor(-18.0f, 2.5f, 15.0f, 150.0f, 8.0f)
                       .done());
    list.push_back(
        Builder("Electric guitar", "Instruments", "Tidy lows and highs, a forward midrange, tube warmth")
            .on(types::eq)
            .cut(eq::lowCut, 100.0f, 12)
            .flat(eq::band1)
            .band(eq::band2, S::peak, 400.0f, -2.0f, 1.0f)
            .band(eq::band3, S::peak, 2500.0f, 2.0f, 1.0f)
            .flat(eq::band4)
            .cut(eq::highCut, 12000.0f, 12)
            .compressor(-16.0f, 2.0f, 20.0f, 120.0f, 6.0f)
            .saturation(saturation::tube, 6.0f, 0.0f, 60.0f)
            .done());
    list.push_back(Builder("Bass", "Instruments", "Solid lows without mud, some growl, even notes")
                       .on(types::eq)
                       .cut(eq::lowCut, 35.0f, 24)
                       .band(eq::band1, S::lowShelf, 80.0f, 2.0f, 0.7f)
                       .band(eq::band2, S::peak, 250.0f, -3.0f, 1.2f)
                       .band(eq::band3, S::peak, 800.0f, 1.5f, 1.0f)
                       .flat(eq::band4)
                       .cut(eq::highCut, 8000.0f, 12)
                       .compressor(-20.0f, 4.0f, 20.0f, 120.0f, 6.0f)
                       .saturation(saturation::tape, 6.0f, -0.2f, 50.0f)
                       .done());
    list.push_back(
        Builder("Piano", "Instruments", "Clear of rumble and boxiness, a little air, light levelling")
            .on(types::eq)
            .cut(eq::lowCut, 40.0f, 12)
            .flat(eq::band1)
            .band(eq::band2, S::peak, 300.0f, -2.0f, 1.0f)
            .flat(eq::band3)
            .band(eq::band4, S::highShelf, 10000.0f, 1.5f, 0.7f)
            .compressor(-20.0f, 2.0f, 20.0f, 200.0f, 10.0f)
            .done());
    list.push_back(Builder("Synth pad", "Instruments", "Out of the way of the low end and the vocal")
                       .on(types::eq)
                       .cut(eq::lowCut, 150.0f, 24)
                       .flat(eq::band1)
                       .band(eq::band2, S::peak, 3000.0f, -2.0f, 0.8f)
                       .flat(eq::band3)
                       .flat(eq::band4)
                       .compressor(-22.0f, 2.0f, 30.0f, 300.0f, 10.0f)
                       .done());

    // ----- drums -----
    list.push_back(Builder("Kick", "Drums", "Gated to the hits, weight and click, the box scooped out")
                       .gate(-35.0f, 30.0f, 0.5f, 40.0f, 80.0f, 20.0f, 2000.0f)
                       .on(types::eq)
                       .cut(eq::lowCut, 30.0f, 24)
                       .band(eq::band1, S::peak, 60.0f, 3.0f, 1.4f)
                       .band(eq::band2, S::peak, 350.0f, -4.0f, 1.5f)
                       .band(eq::band3, S::peak, 4000.0f, 3.0f, 1.2f)
                       .flat(eq::band4)
                       .compressor(-16.0f, 4.0f, 10.0f, 80.0f, 4.0f)
                       .saturation(saturation::clip, 3.0f, 0.0f, 30.0f)
                       .done());
    list.push_back(Builder("Snare", "Drums", "Gated against spill, body and crack, a little tape")
                       .gate(-30.0f, 20.0f, 0.2f, 30.0f, 120.0f, 150.0f)
                       .on(types::eq)
                       .cut(eq::lowCut, 80.0f, 12)
                       .flat(eq::band1)
                       .band(eq::band2, S::peak, 200.0f, 2.0f, 1.2f)
                       .band(eq::band3, S::peak, 900.0f, -2.0f, 2.0f)
                       .band(eq::band4, S::highShelf, 8000.0f, 2.5f, 0.7f)
                       .compressor(-18.0f, 4.0f, 5.0f, 90.0f, 4.0f)
                       .saturation(saturation::tape, 5.0f, 0.1f, 50.0f)
                       .done());
    list.push_back(
        Builder("Drum bus", "Drums", "Parallel punch, weight and air, glued and caught by the limiter")
            .on(types::eq)
            .band(eq::band1, S::lowShelf, 80.0f, 1.5f, 0.7f)
            .flat(eq::band2)
            .flat(eq::band3)
            .band(eq::band4, S::highShelf, 10000.0f, 1.5f, 0.7f)
            .compressor(-14.0f, 4.0f, 30.0f, 100.0f, 6.0f, 50.0f)
            .saturation(saturation::tape, 6.0f, 0.0f, 35.0f)
            .limiter(2.0f, -1.0f, 50.0f)
            .done());

    // ----- buses -----
    list.push_back(
        Builder("Mix bus glue", "Buses", "A gentle 2:1 glue, a touch of tape and air, a safety ceiling")
            .on(types::eq)
            .cut(eq::lowCut, 25.0f, 12)
            .flat(eq::band1)
            .flat(eq::band2)
            .flat(eq::band3)
            .band(eq::band4, S::highShelf, 12000.0f, 1.0f, 0.7f)
            .compressor(-10.0f, 2.0f, 30.0f, 200.0f, 6.0f)
            .set(types::compressor, compressor::autoMakeup, 0.0f)
            .set(types::compressor, compressor::makeup, 1.0f)
            .saturation(saturation::tape, 2.0f, 0.0f, 30.0f)
            .limiter(2.0f, -0.3f, 80.0f)
            .done());
    list.push_back(Builder("Reverb return", "Buses", "Keeps a reverb out of the lows and soft in the highs")
                       .on(types::eq)
                       .cut(eq::lowCut, 200.0f, 24)
                       .flat(eq::band1)
                       .band(eq::band2, S::peak, 500.0f, -2.0f, 0.8f)
                       .flat(eq::band3)
                       .band(eq::band4, S::highShelf, 8000.0f, -3.0f, 0.7f)
                       .done());
    return list;
}
} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = build();
    return list;
}

const Preset* findFactoryPreset(const std::string& name)
{
    for (const auto& p : factoryPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

} // namespace northlight::plugins::strip
