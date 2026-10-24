#pragma once

#include <array>

// Oversampling for nonlinear processing: cascaded polyphase IIR half-bands
// (two chains of first-order all-passes in z^-2, after Laurent de Soras' HIIR
// design). IIR half-bands are minimum phase, so a round trip adds only a few
// samples of group delay and no latency to report. Up to 8x in three stages:
// the stage next to the base rate is the sharp one (it passes flat to 20 kHz at
// 44.1 kHz), the stages above it only have to clear images an octave or more
// away and are cheaper. JUCE-free, real-time safe.

namespace northlight::plugins
{

// One half-band's all-pass coefficients (`order` of them, at most 8).
struct HalfBandDesign
{
    static constexpr int maxOrder = 8;
    std::array<float, maxOrder> c{};
    int order = maxOrder;
};

// `transition`: the half-width of the transition band as a fraction of the
// filter's own rate (the passband ends about 0.25 - transition, the stopband
// begins about 0.25 + transition). Not real-time (it runs series to converge).
HalfBandDesign designHalfBand(int order, double transition);

// The two all-pass chains of a half-band: even coefficients on path 0, odd on
// path 1. Up: feed the input on both paths and read the two outputs in time
// order. Down: feed the later sample on path 0 and the earlier on path 1, and
// average the two outputs.
struct HalfBandChains
{
    std::array<float, HalfBandDesign::maxOrder> x{}, y{};

    void reset() noexcept
    {
        x.fill(0.0f);
        y.fill(0.0f);
    }

    void run(const HalfBandDesign& d, float& path0, float& path1) noexcept
    {
        for (int i = 0; i + 1 < d.order; i += 2)
        {
            const auto a = static_cast<size_t>(i), b = static_cast<size_t>(i + 1);
            const float t0 = (path0 - y[a]) * d.c[a] + x[a];
            const float t1 = (path1 - y[b]) * d.c[b] + x[b];
            x[a] = path0;
            x[b] = path1;
            y[a] = t0;
            y[b] = t1;
            path0 = t0;
            path1 = t1;
        }
    }

    // Denormal guard for the memories (call once a block).
    void flush() noexcept;
};

// Raises a signal 1x, 2x, 4x or 8x and brings it back down. One instance is
// one channel's round trip: up() and down() each keep their own chains.
class Oversampler
{
public:
    static constexpr int maxFactor = 8;

    // 1, 2, 4 or 8 (anything else rounds down to one of them); clears the state.
    void setFactor(int factor) noexcept;
    int factor() const noexcept { return 1 << stages_; }
    void reset() noexcept;

    // One sample at the base rate to factor() samples at the raised rate.
    void up(float x, float* out) noexcept;
    // factor() samples at the raised rate to one at the base rate.
    float down(const float* in) noexcept;

    void flush() noexcept;

private:
    int stages_ = 0;
    std::array<HalfBandChains, 3> up_{}, down_{};
};

} // namespace northlight::plugins
