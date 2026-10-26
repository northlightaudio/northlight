#include "plugins/patina/Drift.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Patina.h"
#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

void Drift::prepare(double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("patina::Drift::prepare");
    fs_ = sampleRate;
    // Up to 2% wow at a quarter of its slowest rate needs about 30 ms.
    for (auto& line : line_)
        line.prepare(static_cast<int>(0.12 * sampleRate));
    reset();
}

void Drift::reset() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        line_[ch].clear();
        wobble_[ch] = {};
        wobble_[ch].rng = dsp::XorShift32(ch == 0 ? 0x1234567u : 0x7654321u);
        wobble_[ch].flutterPhase = ch == 0 ? 0.0f : 1.7f;
        from_[ch] = to_[ch] = 0.0f;
        scrape_[ch] = 0.0f;
    }
    wowDepth_ = flutterDepth_ = 0.0f;
}

void Drift::flush() noexcept
{
    for (auto& v : scrape_)
        v = dsp::flush(v, denormalFloor);
}

std::pair<float, float> Drift::shapes(size_t ch, int machine, float seconds, float rateScale) noexcept
{
    auto& w = wobble_[ch];
    float wowHz = 0.45f, flutterHz = 6.5f;
    if (machine == deck)
    {
        wowHz = 0.9f;
        flutterHz = 11.0f;
    }
    else if (machine == disc)
    {
        wowHz = 33.333f / 60.0f; // the record turning
        flutterHz = 7.5f;
    }
    const float wander = machine == disc ? 0.02f : machine == deck ? 0.35f : 0.25f;
    const float rateNoise = w.wowRate.bipolar(seconds, 0.3f, w.rng);
    w.wowPhase += twoPi * wowHz * rateScale * (1.0f + wander * rateNoise) * seconds;
    w.flutterPhase += twoPi * flutterHz * rateScale * (1.0f + 0.5f * wander * rateNoise) * seconds;
    if (w.wowPhase > twoPi)
        w.wowPhase -= twoPi;
    if (w.flutterPhase > twoPi)
        w.flutterPhase -= twoPi;
    const float shape = w.wowShape.step(seconds, 0.25f, w.rng);
    const float offset = w.wowOffset.bipolar(seconds, 0.15f, w.rng);
    float wowShape = 0.0f;
    if (machine == disc)
        wowShape = 0.82f * std::sin(w.wowPhase) + 0.18f * std::sin(2.0f * w.wowPhase + 0.7f);
    else
        wowShape = std::sin(w.wowPhase) * (0.6f + 0.4f * shape) + (machine == deck ? 0.4f : 0.3f) * offset;
    // Flutter: the capstan's tone with its level wandering, and on a deck
    // a rough scrape of fast noise.
    const float flutterLevel = 0.55f + 0.45f * w.flutterShape.step(seconds, 1.5f, w.rng);
    const float scrapeK = 1.0f - std::exp(-twoPi * 30.0f * seconds);
    scrape_[ch] += (w.rng.bipolar() - scrape_[ch]) * scrapeK;
    float flutterShape = std::sin(w.flutterPhase) * flutterLevel;
    if (machine == deck)
        flutterShape = 0.65f * flutterShape + 0.9f * scrape_[ch];
    else
        flutterShape += 0.25f * scrape_[ch];
    return {std::clamp(wowShape, -1.0f, 1.0f), std::clamp(flutterShape, -1.2f, 1.2f)};
}

void Drift::controls(int machine, int n, float slowSmooth, bool first, Accumulator& acc) noexcept
{
    const float seconds = static_cast<float>(n) / static_cast<float>(fs_);
    const float rateScale = driftRateScale(rate);
    const float wowHz = (machine == deck ? 0.9f : machine == disc ? 0.5556f : 0.45f) * rateScale;
    const float flutterHz = (machine == deck ? 11.0f : machine == disc ? 7.5f : 6.5f) * rateScale;
    // Depths as delay swings: a peak speed error p at f Hz swings the delay
    // by p / (2 pi f) seconds. Up to 2% wow and 0.4% flutter.
    const float wowDepth = 0.02f * wow / (twoPi * wowHz) * static_cast<float>(fs_);
    const float flutterDepth = 0.004f * flutter / (twoPi * flutterHz) * static_cast<float>(fs_);
    wowDepth_ += (wowDepth - wowDepth_) * (first ? 1.0f : slowSmooth);
    flutterDepth_ += (flutterDepth - flutterDepth_) * (first ? 1.0f : slowSmooth);
    const auto left = shapes(0, machine, seconds, rateScale);
    const auto own = shapes(1, machine, seconds, rateScale);
    const float s = stereo;
    const std::pair<float, float> right{left.first + (own.first - left.first) * s,
                                        left.second + (own.second - left.second) * s};
    const float centre = wowDepth_ * 1.05f + flutterDepth_ * 1.25f + 1.0f;
    const float maxDelay = static_cast<float>(line_[0].mask - 8);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto& shape = ch == 0 ? left : right;
        from_[ch] = to_[ch];
        to_[ch] = std::clamp(centre + wowDepth_ * shape.first + flutterDepth_ * shape.second, 1.0f, maxDelay);
        if (first)
            from_[ch] = to_[ch];
        // The speed: the delay growing plays slower.
        const float slope = (to_[ch] - from_[ch]) / static_cast<float>(n);
        const float cents = 1200.0f * std::log2(std::max(0.5f, 1.0f - slope));
        (ch == 0 ? acc.driftLeft : acc.driftRight) += cents * static_cast<float>(n);
    }
    acc.driftCount += n;
}

} // namespace northlight::plugins::patina
