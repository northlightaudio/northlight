#include "engine/Click.h"

#include <cmath>
#include <numbers>

namespace aural::engine
{

void ClickGenerator::trigger(bool accent, double sampleRate) noexcept
{
    length_ = static_cast<int>(sampleRate * burstSeconds);
    remaining_ = length_;
    phase_ = 0.0;
    phaseStep_ = 2.0 * std::numbers::pi * (accent ? accentHz : beatHz) / sampleRate;
}

void ClickGenerator::render(core::SamplePosition start,
                            int numSamples,
                            double sampleRate,
                            double samplesPerBeat,
                            int beatsPerBar,
                            float gain,
                            float* left,
                            float* right) noexcept
{
    if (numSamples <= 0 || samplesPerBeat <= 1.0 || sampleRate <= 0.0)
        return;
    if (beatsPerBar < 1)
        beatsPerBar = 1;

    // First beat index whose sample position is >= start (floor division for negatives).
    auto beatIndex = static_cast<std::int64_t>(std::ceil(static_cast<double>(start) / samplesPerBeat));
    auto nextBeat =
        static_cast<core::SamplePosition>(std::llround(static_cast<double>(beatIndex) * samplesPerBeat));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = start + i;
        while (nextBeat <= position)
        {
            if (nextBeat == position)
            {
                const auto inBar = ((beatIndex % beatsPerBar) + beatsPerBar) % beatsPerBar;
                trigger(inBar == 0, sampleRate);
            }
            ++beatIndex;
            nextBeat = static_cast<core::SamplePosition>(
                std::llround(static_cast<double>(beatIndex) * samplesPerBeat));
        }

        if (remaining_ > 0)
        {
            const float envelope = static_cast<float>(remaining_) / static_cast<float>(length_);
            const float sample = static_cast<float>(std::sin(phase_)) * envelope * envelope * gain;
            left[i] += sample;
            right[i] += sample;
            phase_ += phaseStep_;
            --remaining_;
        }
    }
}

void ClickGenerator::render(core::SamplePosition start,
                            int numSamples,
                            const core::TempoMap& map,
                            float gain,
                            float* left,
                            float* right) noexcept
{
    const double sampleRate = map.sampleRate();
    if (numSamples <= 0 || sampleRate <= 0.0)
        return;
    auto beat = map.beatAtOrAfter(static_cast<double>(start));
    for (int i = 0; i < numSamples; ++i)
    {
        const auto position = start + i;
        while (beat.position <= position)
        {
            if (beat.position == position)
                trigger(beat.beat == 0, sampleRate);
            const auto next = map.nextBeat(beat);
            if (next.position <= beat.position)
                break; // a degenerate map: never spin
            beat = next;
        }

        if (remaining_ > 0)
        {
            const float envelope = static_cast<float>(remaining_) / static_cast<float>(length_);
            const float sample = static_cast<float>(std::sin(phase_)) * envelope * envelope * gain;
            left[i] += sample;
            right[i] += sample;
            phase_ += phaseStep_;
            --remaining_;
        }
    }
}

} // namespace aural::engine
