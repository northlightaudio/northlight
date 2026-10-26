#include "plugins/patina/Grain.h"

#include "plugins/Patina.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

void Grain::prepare(double sampleRate) noexcept
{
    fs_ = sampleRate;
    reset();
}

void Grain::reset() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        noise_[ch] = {};
        noise_[ch].rng = dsp::XorShift32(ch == 0 ? 0x2468aceu : 0x13579bdu);
    }
    humPhase_ = 0.0f;
    humLevel_ = {};
    recordPhase_ = 0.0f;
    designed_ = -1;
    textureDesigned_ = -1.0f;
}

void Grain::flush() noexcept
{
    envelope_ = dsp::flush(envelope_, denormalFloor);
}

void Grain::controls(int source, float seconds) noexcept
{
    source_ = source;
    const float a = amount;
    // Level: -60 dB rising to -16 dB at full; nothing at all at zero.
    gain_ = a <= 0.0f ? 0.0f : dbToGain(-60.0f + 44.0f * a) * std::min(1.0f, a * 12.0f);
    if (source != designed_ || std::abs(texture - textureDesigned_) > 0.002f)
    {
        designed_ = source;
        textureDesigned_ = texture;
        const double t = texture;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = noise_[ch];
            c.clickTone.setHighPass(fs_, 1500.0, 0.7);
            c.popTone.setLowPass(fs_, 1800.0, 0.8);
            c.surface.setHighPass(fs_, 900.0, 0.7);
            c.surfaceTop.setLowPass(fs_, 7000.0, 0.7);
            c.rumble.setLowPass(fs_, 24.0, 0.9);
            c.hissLow.setHighPass(fs_, 180.0, 0.7);
            c.hissPeak.setPeaking(fs_, 6000.0, 0.8, 4.0);
            c.hissTop.setLowPass(fs_, 4000.0 * std::pow(4.5, t), 0.7);
            c.airTop.setLowPass(fs_, 1200.0 * std::pow(7.0, t), 0.6);
            c.airDrone.setBandPass(fs_, 95.0, 2.5);
        }
        // The hum's harmonics: a pure tone at no texture, a buzz at full.
        float total = 0.0f;
        for (size_t k = 0; k < humHarmonics; ++k)
        {
            const auto harmonic = static_cast<float>(k + 1);
            const float buzz = static_cast<float>(t);
            float amp = 1.0f;
            if (k > 0)
                amp = (0.08f + 0.92f * std::pow(buzz, 0.7f)) * ((k + 1) % 2 == 1 ? 0.9f : 0.55f) /
                      std::pow(harmonic, 1.3f - 0.7f * buzz);
            humAmp_[k] = amp;
            total += amp * amp;
        }
        const float norm = 1.0f / std::sqrt(std::max(1.0e-6f, 0.5f * total));
        for (auto& amp : humAmp_)
            amp *= norm;
    }
    clickProbability_ = crackleRate(texture) / static_cast<float>(fs_);
    humLevel_.step(seconds, 0.2f, noise_[0].rng);
}

float Grain::followGain() const noexcept
{
    // The input's level from -50 to -20 dBFS as 0..1.
    const float x = std::clamp((gainToDb(envelope_) + 50.0f) / 30.0f, 0.0f, 1.0f);
    if (follow >= 0.0f)
        return 1.0f + (x * x - 1.0f) * follow;             // rides: gone in silence
    return 1.0f + (dbToGain(-18.0f * x) - 1.0f) * -follow; // ducks under the music
}

void Grain::sample(float& outL, float& outR) noexcept
{
    const int source = source_;
    if (source == hum50 || source == hum60)
    {
        const float hz = source == hum50 ? 50.0f : 60.0f;
        humPhase_ += twoPi * hz / static_cast<float>(fs_);
        if (humPhase_ > twoPi)
            humPhase_ -= twoPi;
        const float s1 = std::sin(humPhase_), c2 = 2.0f * std::cos(humPhase_);
        float previous = 0.0f, current = s1, sum = 0.0f;
        for (size_t k = 0; k < humHarmonics; ++k)
        {
            sum += humAmp_[k] * current;
            const float next = c2 * current - previous;
            previous = current;
            current = next;
        }
        const float level = 0.9f + 0.2f * humLevel_.b;
        outL = sum * level;
        outR = sum * level * 0.97f;
        return;
    }
    if (source == vinyl)
    {
        recordPhase_ += twoPi * (33.333f / 60.0f) / static_cast<float>(fs_);
        if (recordPhase_ > twoPi)
            recordPhase_ -= twoPi;
        // A shared event decides where each crackle lands.
        auto& rng = noise_[0].rng;
        if (rng.uniform() < clickProbability_)
        {
            const float u = std::max(1.0e-4f, rng.uniform());
            const bool pop = rng.uniform() < 0.07f;
            const float amp = std::min(1.0f, 0.18f * std::pow(-std::log(u), 1.6f)) * (pop ? 2.5f : 1.0f);
            const float pan = rng.uniform();
            const float gl = pan < 0.5f ? 1.0f : 2.0f * (1.0f - pan) + 0.15f;
            const float gr = pan > 0.5f ? 1.0f : 2.0f * pan + 0.15f;
            const float decay = std::exp(
                -1.0f / (static_cast<float>(fs_) * (pop ? 0.0018f : 0.00018f) * (0.5f + rng.uniform())));
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& c = noise_[ch];
                const float g = amp * (ch == 0 ? gl : gr);
                if (pop)
                {
                    c.popEnv = std::max(c.popEnv, g);
                    c.popDecay = decay;
                }
                else
                {
                    c.clickEnv = std::max(c.clickEnv, g);
                    c.clickDecay = decay;
                }
            }
        }
        const float swish = 0.75f + 0.25f * std::sin(recordPhase_);
        const float rumble = noise_[0].rumble.process(noise_[0].rng.bipolar()) * 18.0f;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = noise_[ch];
            const float white = c.rng.bipolar();
            const float click = c.clickTone.process(white * c.clickEnv) * 14.0f;
            const float popSign = white > 0.0f ? 1.0f : -1.0f;
            const float pop = c.popTone.process(popSign * c.popEnv) * 3.0f;
            c.clickEnv = dsp::flush(c.clickEnv * c.clickDecay, denormalFloor);
            c.popEnv = dsp::flush(c.popEnv * c.popDecay, denormalFloor);
            const float surface = c.surfaceTop.process(c.surface.process(c.rng.bipolar())) * 2.4f * swish;
            (ch == 0 ? outL : outR) = click + pop + surface + rumble;
        }
        return;
    }
    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto& c = noise_[ch];
        const float white = c.rng.bipolar();
        float y = 0.0f;
        if (source == hiss)
            y = c.hissTop.process(c.hissPeak.process(c.hissLow.process(white))) * 2.6f;
        else
        {
            // Room air: soft, wide noise over a low drone.
            const float drift = 0.8f + 0.4f * c.level.step(1.0f / static_cast<float>(fs_), 0.12f, c.rng);
            y = c.airTop.process(c.pink.process(white)) * 1.9f * drift + c.airDrone.process(white) * 3.0f;
        }
        (ch == 0 ? outL : outR) = y;
    }
}

} // namespace northlight::plugins::patina
