#include "plugins/Triode.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins
{

void CurveTable::build(const std::function<double(double)>& f, double lo, double hi, int points)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("CurveTable::build");
    points = std::max(2, points);
    lo_ = lo;
    hi_ = hi;
    step_ = (hi - lo) / (points - 1);
    inverseStep_ = 1.0 / step_;
    values_.resize(static_cast<size_t>(points));
    integrals_.resize(static_cast<size_t>(points));
    for (int i = 0; i < points; ++i)
        values_[static_cast<size_t>(i)] = f(lo + i * step_);
    // The integral from zero (so it stays small where the signal lives): the
    // trapezoids from the low end, then shifted so that the integral at 0 is 0.
    integrals_[0] = 0.0;
    for (size_t i = 1; i < values_.size(); ++i)
        integrals_[i] = integrals_[i - 1] + 0.5 * (values_[i - 1] + values_[i]) * step_;
    const double atZero = std::clamp(0.0, lo, hi) == 0.0 ? integral(0.0) : 0.0;
    for (auto& v : integrals_)
        v -= atZero;
}

double CurveTable::value(double x) const noexcept
{
    if (x <= lo_)
        return values_.front();
    if (x >= hi_)
        return values_.back();
    const double position = (x - lo_) * inverseStep_;
    const auto i = std::min(static_cast<size_t>(position), values_.size() - 2);
    const double t = position - static_cast<double>(i);
    return values_[i] + (values_[i + 1] - values_[i]) * t;
}

double CurveTable::integral(double x) const noexcept
{
    if (x <= lo_)
        return integrals_.front() + values_.front() * (x - lo_);
    if (x >= hi_)
        return integrals_.back() + values_.back() * (x - hi_);
    const double position = (x - lo_) * inverseStep_;
    const auto i = std::min(static_cast<size_t>(position), values_.size() - 2);
    const double t = position - static_cast<double>(i);
    const double y0 = values_[i], y1 = values_[i + 1];
    return integrals_[i] + step_ * t * (y0 + 0.5 * (y1 - y0) * t);
}

double TriodeCircuit::plateCurrent(double vgk, double vpk) const noexcept
{
    if (vpk <= 0.0)
        return 0.0;
    const double z = kp * (1.0 / mu + vgk / std::sqrt(kvb + vpk * vpk));
    const double softplus = z > 30.0 ? z : std::log1p(std::exp(z));
    const double e1 = vpk / kp * softplus;
    return e1 > 0.0 ? 2.0 * std::pow(e1, ex) / kg1 : 0.0;
}

namespace
{
// Root of an increasing function on [lo, hi] by bisection.
template <typename F> double bisect(F&& f, double lo, double hi, int iterations = 56)
{
    for (int i = 0; i < iterations; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) > 0.0)
            hi = mid;
        else
            lo = mid;
    }
    return 0.5 * (lo + hi);
}

// The plate current for an actual grid voltage `g` (DC-referenced to ground).
double currentForGrid(const TriodeCircuit& c, double g, double quiescentCathode)
{
    if (c.bypassed)
    {
        // The capacitor holds the cathode at its quiescent voltage.
        const double top = std::max(0.0, (c.supply - quiescentCathode) / c.plateLoad);
        return bisect(
            [&](double ip)
            {
                return ip -
                       c.plateCurrent(g - quiescentCathode, c.supply - ip * c.plateLoad - quiescentCathode);
            },
            0.0,
            top);
    }
    const double top = c.supply / (c.plateLoad + c.cathode);
    return bisect(
        [&](double ip)
        { return ip - c.plateCurrent(g - ip * c.cathode, c.supply - ip * (c.plateLoad + c.cathode)); },
        0.0,
        top);
}

double gridCurrent(const TriodeCircuit& c, double vgk) noexcept
{
    return vgk > 0.0 ? c.gridCurrent * vgk * std::sqrt(vgk) : 0.0;
}
} // namespace

TriodeCurve solveTriode(const TriodeCircuit& c)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("solveTriode");
    TriodeCurve curve;
    // The operating point: no signal, the cathode resistor carrying the current.
    const double ip0 =
        bisect([&](double ip)
               { return ip - c.plateCurrent(-ip * c.cathode, c.supply - ip * (c.plateLoad + c.cathode)); },
               0.0,
               c.supply / (c.plateLoad + c.cathode));
    curve.quiescentCathode = ip0 * c.cathode;
    curve.quiescentPlate = c.supply - ip0 * c.plateLoad;
    curve.conduction = curve.quiescentCathode;

    const double vk0 = curve.quiescentCathode, vp0 = curve.quiescentPlate;
    const auto cathodeFor = [&](double ip) { return c.bypassed ? vk0 : ip * c.cathode; };
    const auto plateFor = [&](double v)
    {
        // The grid itself, below the drive `v` by the grid current through the
        // source's resistance (only where the grid can conduct).
        double g = v;
        if (v > 0.0)
        {
            g = bisect(
                [&](double grid)
                {
                    const double ip = currentForGrid(c, grid, vk0);
                    return grid + c.sourceResistance * gridCurrent(c, grid - cathodeFor(ip)) - v;
                },
                0.0,
                v,
                40);
        }
        const double ip = currentForGrid(c, g, vk0);
        return c.supply - ip * c.plateLoad - vp0;
    };
    curve.table.build(plateFor, -16.0, 16.0, 2049);
    constexpr double d = 1.0e-3;
    curve.gain = (curve.table.value(d) - curve.table.value(-d)) / (2.0 * d);
    return curve;
}

void TriodeStage::prepare(const TriodeCurve& curve, double sampleRate, double couplingHz, double millerHz)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("TriodeStage::prepare");
    curve_ = &curve;
    couplingHz_ = couplingHz;
    millerHz_ = millerHz;
    retune(sampleRate);
}

void TriodeStage::retune(double sampleRate) noexcept
{
    coupling_.setHighPass1(sampleRate, couplingHz_);
    miller_.setLowPass1(sampleRate, millerHz_);
    // The coupling capacitor charges through the conducting grid in a couple of
    // milliseconds and drains through the grid leak over tens.
    charge_ = dsp::stepForSeconds(0.002, sampleRate);
    discharge_ = dsp::poleForSeconds(0.04, sampleRate);
    reset();
}

void TriodeStage::reset() noexcept
{
    coupling_.reset();
    miller_.reset();
    previous_ = 0.0;
    previousIntegral_ = curve_ != nullptr ? curve_->table.integral(0.0) : 0.0;
    shift_ = 0.0;
}

float TriodeStage::process(float grid) noexcept
{
    const auto& table = curve_->table;
    double x = coupling_.process(grid);
    const double excess = x - shift_ - curve_->conduction;
    if (excess > 0.0)
        shift_ += charge_ * excess;
    shift_ *= discharge_;
    x -= shift_;
    const double integral = table.integral(x);
    const double dx = x - previous_;
    const double y =
        std::abs(dx) > 1.0e-6 ? (integral - previousIntegral_) / dx : table.value(0.5 * (x + previous_));
    previous_ = x;
    previousIntegral_ = integral;
    return miller_.process(static_cast<float>(y));
}

void TriodeStage::flush() noexcept
{
    for (auto* b : {&coupling_, &miller_})
    {
        if (std::abs(b->z1) < 1.0e-20f)
            b->z1 = 0.0f;
        if (std::abs(b->z2) < 1.0e-20f)
            b->z2 = 0.0f;
    }
    if (std::abs(shift_) < 1.0e-20)
        shift_ = 0.0;
}

} // namespace northlight::plugins
