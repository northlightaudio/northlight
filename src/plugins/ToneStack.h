#pragma once

#include "plugins/dsp/Denormals.h"

#include <array>
#include <complex>

// The treble / middle / bass tone stack of 1950s and 1960s American amps, as
// the passive network it is (the structure D. T. Yeh and J. O. Smith analysed in
// "Discretization of the '59 Fender Bassman Tone Stack", DAFx 2006): the
// third-order transfer function, found exactly for any component values and pot
// positions by a nodal solve of the circuit, then discretised by the bilinear
// transform. The stack is lossy and interactive, as the real one
// is: the middle sets how deep the scoop is, and every knob moves the others'
// curves. JUCE-free; process() is real-time safe.

namespace northlight::plugins::tonestack
{

// The network. Defaults: the 1960s blackface stack (250k treble and bass pots,
// a 10k middle pot, a 100k slope resistor, 250 pF, 100 nF and 47 nF).
struct Components
{
    double treblePot = 250.0e3;   // R1
    double bassPot = 250.0e3;     // R2
    double middlePot = 10.0e3;    // R3
    double slope = 100.0e3;       // R4
    double trebleCap = 250.0e-12; // C1
    double bassCap = 100.0e-9;    // C2
    double middleCap = 47.0e-9;   // C3
};

// H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3), index = power.
struct Analog
{
    std::array<double, 4> b{}, a{};
};

// The network's polynomial for pot positions (0..1 of each pot's resistance in
// circuit: treble's wiper from the bottom, bass's and middle's resistance).
// Real-time safe (a few 5 x 5 determinants).
Analog analog(const Components& c, double treble, double middle, double bass) noexcept;
std::complex<double> response(const Analog& h, double hz) noexcept;

// The pots' tapers: a knob position (0..1) to the share of the pot's
// resistance. Treble and bass are audio taper (10% at half way), middle linear.
double audioTaper(double position) noexcept;

// The digital filter: transposed direct form II in double (the poles sit close
// to 1 at oversampled rates).
class Filter
{
public:
    void design(const Analog& h, double sampleRate) noexcept;
    void reset() noexcept { z_.fill(0.0); }
    double process(double x) noexcept
    {
        const double y = b_[0] * x + z_[0];
        z_[0] = b_[1] * x - a_[1] * y + z_[1];
        z_[1] = b_[2] * x - a_[2] * y + z_[2];
        z_[2] = b_[3] * x - a_[3] * y;
        return y;
    }
    void flush() noexcept
    {
        for (auto& z : z_)
            z = dsp::flush(z, 1.0e-30);
    }
    // |H| at `hz` (linear), from the coefficients as they run.
    double magnitude(double hz, double sampleRate) const noexcept;

private:
    std::array<double, 4> b_{1.0, 0.0, 0.0, 0.0}, a_{1.0, 0.0, 0.0, 0.0};
    std::array<double, 3> z_{};
};

} // namespace northlight::plugins::tonestack
