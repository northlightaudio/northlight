#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

// RBJ cookbook biquad, transposed direct form II. The coefficient designs are
// shared by the processors and the editors, so a curve on screen is exactly
// the filter that runs.

namespace aural::plugins
{

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void bypass() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    bool isIdentity() const noexcept
    {
        return b0 == 1.0f && b1 == 0.0f && b2 == 0.0f && a1 == 0.0f && a2 == 0.0f;
    }

    // Coefficients t of the way from `from` to `to`; the memory is kept.
    void blend(const Biquad& from, const Biquad& to, float t) noexcept
    {
        b0 = from.b0 + (to.b0 - from.b0) * t;
        b1 = from.b1 + (to.b1 - from.b1) * t;
        b2 = from.b2 + (to.b2 - from.b2) * t;
        a1 = from.a1 + (to.a1 - from.a1) * t;
        a2 = from.a2 + (to.a2 - from.a2) * t;
    }

    void setPeaking(double sampleRate, double hz, double q, double gainDb) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = omega(sampleRate, hz);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha / A;
        set((1.0 + alpha * A) / a0,
            (-2.0 * c) / a0,
            (1.0 - alpha * A) / a0,
            (-2.0 * c) / a0,
            (1.0 - alpha / A) / a0);
    }

    // Shelves; q = 1/sqrt(2) is the cookbook's slope S = 1 (no overshoot).
    void
    setLowShelf(double sampleRate, double hz, double gainDb, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        shelf(sampleRate, hz, gainDb, q, true);
    }

    void
    setHighShelf(double sampleRate, double hz, double gainDb, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        shelf(sampleRate, hz, gainDb, q, false);
    }

    void setHighPass(double sampleRate, double hz, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        const double w0 = omega(sampleRate, hz);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        set((1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

    void setLowPass(double sampleRate, double hz, double q = std::numbers::sqrt2 / 2.0) noexcept
    {
        const double w0 = omega(sampleRate, hz);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        set((1.0 - c) / 2.0 / a0, (1.0 - c) / a0, (1.0 - c) / 2.0 / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

    // First-order sections (6 dB/oct), by the bilinear transform.
    void setHighPass1(double sampleRate, double hz) noexcept
    {
        const double k = std::tan(omega(sampleRate, hz) / 2.0);
        set(1.0 / (1.0 + k), -1.0 / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0);
    }

    void setLowPass1(double sampleRate, double hz) noexcept
    {
        const double k = std::tan(omega(sampleRate, hz) / 2.0);
        set(k / (1.0 + k), k / (1.0 + k), 0.0, (k - 1.0) / (k + 1.0), 0.0);
    }

    // Constant 0 dB peak gain band-pass.
    void setBandPass(double sampleRate, double hz, double q) noexcept
    {
        const double w0 = omega(sampleRate, hz);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        set(alpha / a0, 0.0, -alpha / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

    void setNotch(double sampleRate, double hz, double q) noexcept
    {
        const double w0 = omega(sampleRate, hz);
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        set(1.0 / a0, (-2.0 * c) / a0, 1.0 / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

    // |H| at `hz` (linear), from the coefficients as they run.
    double magnitude(double hz, double sampleRate) const noexcept
    {
        const double w = 2.0 * std::numbers::pi * hz / sampleRate;
        const std::complex<double> e1 = std::polar(1.0, -w);
        const std::complex<double> e2 = e1 * e1;
        const auto num =
            static_cast<double>(b0) + static_cast<double>(b1) * e1 + static_cast<double>(b2) * e2;
        const auto den = 1.0 + static_cast<double>(a1) * e1 + static_cast<double>(a2) * e2;
        return std::abs(num / den);
    }

private:
    static double omega(double sampleRate, double hz) noexcept
    {
        return 2.0 * std::numbers::pi * std::clamp(hz, 10.0, sampleRate * 0.49) / sampleRate;
    }

    void shelf(double sampleRate, double hz, double gainDb, double q, bool low) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = omega(sampleRate, hz);
        const double c = std::cos(w0);
        const double s = std::sin(w0);
        const double alpha = s / (2.0 * std::max(0.1, q));
        const double twoRootAAlpha = 2.0 * std::sqrt(A) * alpha;
        if (low)
        {
            const double a0 = (A + 1.0) + (A - 1.0) * c + twoRootAAlpha;
            set(A * ((A + 1.0) - (A - 1.0) * c + twoRootAAlpha) / a0,
                2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0,
                A * ((A + 1.0) - (A - 1.0) * c - twoRootAAlpha) / a0,
                -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0,
                ((A + 1.0) + (A - 1.0) * c - twoRootAAlpha) / a0);
        }
        else
        {
            const double a0 = (A + 1.0) - (A - 1.0) * c + twoRootAAlpha;
            set(A * ((A + 1.0) + (A - 1.0) * c + twoRootAAlpha) / a0,
                -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0,
                A * ((A + 1.0) + (A - 1.0) * c - twoRootAAlpha) / a0,
                2.0 * ((A - 1.0) - (A + 1.0) * c) / a0,
                ((A + 1.0) - (A - 1.0) * c - twoRootAAlpha) / a0);
        }
    }

    void set(double nb0, double nb1, double nb2, double na1, double na2) noexcept
    {
        b0 = static_cast<float>(nb0);
        b1 = static_cast<float>(nb1);
        b2 = static_cast<float>(nb2);
        a1 = static_cast<float>(na1);
        a2 = static_cast<float>(na2);
    }
};

} // namespace aural::plugins
