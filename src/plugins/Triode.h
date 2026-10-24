#pragma once

#include "plugins/Biquad.h"

#include <cmath>
#include <functional>
#include <vector>

// A triode gain stage for amp and pedal models: a static transfer curve solved
// offline from Norman Koren's triode equations on the stage's load line, read
// from a table with first-order antiderivative anti-aliasing, between the
// stage's coupling high-pass and its Miller low-pass. Grid current (the grid
// conducting once it goes positive of the cathode, through the source's
// resistance) is solved into the curve, and its slow side effect, the coupling
// capacitor charging and shifting the bias down after a loud note (the
// "bloom"), runs per sample. Signals are in volts: grid volts in, plate volts
// out (AC, inverted, as a common-cathode stage inverts). JUCE-free; real-time
// safe after the table is built.

namespace northlight::plugins
{

// A static curve on a uniform grid with its exact antiderivative (the curve is
// piecewise linear, so its integral is piecewise quadratic). Outside the grid
// the curve holds its end values.
class CurveTable
{
public:
    // Not real-time.
    void build(const std::function<double(double)>& f, double lo, double hi, int points);

    double value(double x) const noexcept;
    double integral(double x) const noexcept;
    double lo() const noexcept { return lo_; }
    double hi() const noexcept { return hi_; }
    bool empty() const noexcept { return values_.empty(); }

private:
    std::vector<double> values_, integrals_;
    double lo_ = 0.0, hi_ = 1.0, step_ = 1.0, inverseStep_ = 1.0;
};

// Koren's triode model and a stage's circuit. Defaults: a 12AX7 with a 100k
// plate load from 270 V and a 1.5k cathode resistor, bypassed (the first stage
// of a 1960s American amp).
struct TriodeCircuit
{
    // The tube (12AX7).
    double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    // Grid current: kGrid * Vgk^1.5 amps above zero volts.
    double gridCurrent = 2.0e-4;
    // The circuit.
    double supply = 270.0;            // volts at the top of the plate load
    double plateLoad = 100.0e3;       // ohms
    double cathode = 1500.0;          // ohms
    bool bypassed = true;             // a capacitor across the cathode resistor (more gain)
    double sourceResistance = 68.0e3; // what drives the grid (grid current drops across it)

    // Plate current (amps) for a grid-to-cathode and a plate-to-cathode voltage.
    double plateCurrent(double vgk, double vpk) const noexcept;
};

// A stage's curve and its operating point.
struct TriodeCurve
{
    CurveTable table;          // grid volts (AC) to plate volts (AC)
    double quiescentPlate = 0; // volts
    double quiescentCathode = 0;
    double gain = 0; // small-signal slope at zero (negative: the stage inverts)
    // The grid voltage (AC) where the grid starts to conduct: the coupling
    // capacitor charges above it.
    double conduction = 0;
};

// Not real-time (thousands of bisections).
TriodeCurve solveTriode(const TriodeCircuit& circuit);

// The stage as it runs: coupling high-pass, the bias shift, the curve (ADAA1),
// the Miller low-pass.
class TriodeStage
{
public:
    // `curve` must outlive the stage. Not real-time.
    void prepare(const TriodeCurve& curve, double sampleRate, double couplingHz, double millerHz);
    // A new rate for the same stage (the amp's oversampling changed); clears
    // the state. Real-time safe.
    void retune(double sampleRate) noexcept;
    void reset() noexcept;

    float process(float grid) noexcept;
    // Denormal guard (once a block).
    void flush() noexcept;

private:
    const TriodeCurve* curve_ = nullptr;
    Biquad coupling_, miller_;
    double couplingHz_ = 10.0, millerHz_ = 20000.0;
    double previous_ = 0.0, previousIntegral_ = 0.0;
    double shift_ = 0.0;                    // the coupling capacitor's charge (volts)
    double charge_ = 0.0, discharge_ = 1.0; // per sample
};

} // namespace northlight::plugins
