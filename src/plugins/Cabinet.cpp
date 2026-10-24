#include "plugins/Cabinet.h"

#include "engine/rt/RealtimeGuard.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <signalsmith-linear/fft.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <numbers>

namespace northlight::plugins::cabinet
{

namespace
{
using Complex = std::complex<double>;
constexpr double soundSpeed = 343.0;  // m/s
constexpr double coneRadius = 0.12;   // m: up close the cone is a surface, not a point
constexpr double maxProximity = 4.0;  // +12 dB at most
constexpr double rearWall = 0.5;      // m behind an open back
constexpr double cabinetDepth = 0.25; // m

// ----- analog sections, |H(j 2 pi f)| -----------------------------------------------------

double peak(double hz, double f0, double q, double gainDb) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const Complex s(0.0, hz / f0);
    return std::abs((s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0));
}

double highPass2(double hz, double f0, double q) noexcept
{
    const Complex s(0.0, hz / f0);
    return std::abs(s * s / (s * s + s / q + 1.0));
}

double lowPass2(double hz, double f0, double q) noexcept
{
    const Complex s(0.0, hz / f0);
    return std::abs(1.0 / (s * s + s / q + 1.0));
}

double highShelf(double hz, double f0, double gainDb) noexcept
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double k = std::sqrt(a) / std::numbers::sqrt2 * 2.0; // sqrt(A) / Q, Q = 1/sqrt(2)
    const Complex s(0.0, hz / f0);
    return std::abs(a * (a * s * s + k * s + 1.0) / (s * s + k * s + a));
}

struct Peak
{
    double hz, q, db;
};

struct Speaker
{
    double highPass, highPassQ;
    std::vector<Peak> peaks;
    double lowPass1, lowPass2;
    Layout layout;
};

std::vector<std::array<double, 2>> grid2x2(double spacing)
{
    return {{0.0, 0.0}, {spacing, 0.0}, {0.0, spacing}, {spacing, spacing}};
}

const Speaker& speaker(int model)
{
    static const std::array<Speaker, modelCount> speakers{{
        // Two bright, tight 12"s in an open back: thin lows (the back cancels
        // them), a strong presence peak from the cone breaking up, a sharp top.
        {75.0,
         0.9,
         {{110.0, 1.4, 3.0},
          {400.0, 0.8, -2.0},
          {1600.0, 1.5, 2.0},
          {3200.0, 2.0, 5.0},
          {4600.0, 3.0, -6.0},
          {5600.0, 3.0, 2.0}},
         6200.0,
         8000.0,
         {{{0.0, 0.0}, {0.31, 0.0}}, 0.30, 0.45, true}},
        // One softer 12", open back.
        {85.0,
         0.8,
         {{120.0, 1.3, 2.0}, {500.0, 0.9, -1.5}, {2400.0, 1.8, 4.0}, {4200.0, 3.0, -5.0}},
         5200.0,
         7000.0,
         {{{0.0, 0.0}}, 0.30, 0.40, true}},
        // Four 10"s in a closed back: a firm low bump, a rounder top.
        {70.0,
         1.1,
         {{100.0, 1.5, 4.0}, {800.0, 1.0, 1.0}, {2800.0, 1.6, 4.0}, {5000.0, 3.0, -4.0}},
         5800.0,
         7500.0,
         {grid2x2(0.27), 0.25, 0.50, false}},
        // A 15" bass cabinet: deep, dark.
        {45.0,
         1.0,
         {{80.0, 1.2, 3.0}, {1200.0, 1.2, 2.0}, {2400.0, 2.0, -3.0}},
         3400.0,
         5000.0,
         {{{0.0, 0.0}}, 0.38, 0.35, false}},
        // Four 10"s for bass: tighter lows, more growl.
        {55.0,
         1.0,
         {{90.0, 1.3, 3.0}, {2200.0, 1.4, 3.0}},
         4500.0,
         6500.0,
         {grid2x2(0.27), 0.25, 0.50, false}},
        // Custom: drawn as the 2x12 (its response is the user's).
        {75.0, 0.9, {}, 6200.0, 8000.0, {{{0.0, 0.0}, {0.31, 0.0}}, 0.30, 0.45, true}},
    }};
    return speakers[static_cast<size_t>(std::clamp(model, 0, modelCount - 1))];
}

struct MicCurve
{
    double highPass;
    std::vector<Peak> peaks;
    double lowPass, lowPassQ;
    double shelfHz, shelfDb;
    double gradient; // the pressure-gradient share: 0.5 cardioid, 1 figure-8
};

const MicCurve& micCurve(int type)
{
    static const std::array<MicCurve, micTypeCount> curves{{
        {80.0, {{2500.0, 1.0, 1.0}, {5500.0, 1.5, 5.0}}, 13000.0, 0.7, 5000.0, 0.0, 0.5},
        {25.0, {{150.0, 0.8, 1.0}}, 9000.0, 0.6, 5000.0, -3.0, 1.0},
        {30.0, {{9000.0, 1.0, 3.0}}, 18000.0, 0.7, 5000.0, 0.0, 0.5},
    }};
    return curves[static_cast<size_t>(std::clamp(type, 0, micTypeCount - 1))];
}

// The speaker heard by the mic: on the cone at `position`, `angle` degrees off
// the axis (beyond 45 for the other speakers and the floor), from `distance`
// metres; `rear` for the open back's rear wave. Not normalised.
double rawMagnitude(int model, const Mic& mic, double hz, double extraAngle, bool rear) noexcept
{
    const auto& sp = speaker(model);
    const double r = std::clamp(static_cast<double>(mic.position), 0.0, 1.0);
    const double angle = std::clamp(static_cast<double>(mic.angleDeg) + extraAngle, 0.0, 90.0);
    const double d = std::clamp(static_cast<double>(mic.distanceCm), 1.0, 100.0) * 0.01;
    const double onAxis = 1.0 - 0.4 * std::min(angle, 45.0) / 45.0;

    double g = highPass2(hz, sp.highPass, sp.highPassQ);
    for (const auto& p : sp.peaks)
    {
        double f = p.hz, db = p.db;
        if (p.hz > 1500.0)
        {
            // The breakup peaks live at the cap; toward the edge they soften and sink.
            f *= 1.0 - 0.12 * r;
            db *= (1.0 - 0.5 * r) * onAxis;
        }
        g *= peak(hz, f, p.q, db);
    }
    g *= lowPass2(hz, sp.lowPass1, 0.7) * lowPass2(hz, sp.lowPass2, 0.7);
    // Toward the edge: darker, a little fuller in the low mids. At the cap: fizz.
    g *= lowPass2(hz, 14000.0 * std::exp(-1.4 * r), 0.7);
    g *= peak(hz, 300.0, 0.8, 2.5 * r);
    if (r < 0.15)
        g *= highShelf(hz, 4000.0, 2.0 * (1.0 - r / 0.15));
    // Off axis the highs fall away (the speaker beams).
    g *= highShelf(hz, 2500.0, std::max(-18.0, -10.0 * angle / 45.0));
    // Backing off blurs the top a little.
    g *= highShelf(hz, 5000.0, -3.0 * std::min(1.0, d));

    const auto& m = micCurve(mic.type);
    g *= highPass2(hz, m.highPass, 0.7);
    for (const auto& p : m.peaks)
        g *= peak(hz, p.hz, p.q, p.db);
    g *= lowPass2(hz, m.lowPass, m.lowPassQ);
    if (m.shelfDb != 0.0)
        g *= highShelf(hz, m.shelfHz, m.shelfDb);
    // Proximity: the gradient part of the pickup rises 6 dB an octave below
    // where the wavelength passes the distance.
    const double k = 2.0 * std::numbers::pi * hz / soundSpeed;
    const double reach = m.gradient / (k * std::hypot(d, coneRadius));
    g *= std::min(maxProximity, std::sqrt(1.0 + reach * reach));
    if (rear)
        g *= lowPass2(hz, 1200.0, 0.7);
    return g;
}

// The mean level (dB) of a magnitude over 150 Hz - 4 kHz, on a log scale.
double bandMeanDb(const std::function<double(double)>& magnitude)
{
    constexpr int points = 32;
    double sum = 0.0;
    for (int i = 0; i < points; ++i)
    {
        const double hz = 150.0 * std::pow(4000.0 / 150.0, static_cast<double>(i) / (points - 1));
        sum += 20.0 * std::log10(std::max(1.0e-9, magnitude(hz)));
    }
    return sum / points;
}

int nextPowerOfTwo(int n) noexcept
{
    int p = 1;
    while (p < n)
        p *= 2;
    return p;
}

// The minimum-phase response of `length` samples with this magnitude: the
// real cepstrum of the log magnitude, folded onto positive quefrencies.
std::vector<float> minimumPhase(const std::function<double(double)>& magnitude, int length, double rate)
{
    const int n = std::max(4096, nextPowerOfTwo(4 * length));
    signalsmith::linear::Pow2FFT<double> fft(static_cast<size_t>(n));
    std::vector<Complex> a(static_cast<size_t>(n)), b(static_cast<size_t>(n));
    for (int k = 0; k <= n / 2; ++k)
    {
        const double hz = static_cast<double>(k) * rate / n;
        const double v = std::log(std::max(1.0e-6, magnitude(std::max(hz, 1.0))));
        a[static_cast<size_t>(k)] = v;
        if (k > 0 && k < n / 2)
            a[static_cast<size_t>(n - k)] = v;
    }
    fft.ifft(a.data(), b.data()); // the cepstrum (times n)
    for (int i = 0; i < n; ++i)
    {
        const double fold = i == 0 || i == n / 2 ? 1.0 : i < n / 2 ? 2.0 : 0.0;
        a[static_cast<size_t>(i)] = b[static_cast<size_t>(i)].real() * fold / n;
    }
    fft.fft(a.data(), b.data());
    for (auto& v : b)
        v = std::exp(v);
    fft.ifft(b.data(), a.data());
    std::vector<float> h(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        h[static_cast<size_t>(i)] = static_cast<float>(a[static_cast<size_t>(i)].real() / n);
    return h;
}

// Adds `src` times `gain`, delayed by a fractional number of samples (cubic
// Lagrange), into `dst`; taps that would fall outside are dropped.
void addDelayed(std::vector<float>& dst, const std::vector<float>& src, double delay, double gain)
{
    const int whole = static_cast<int>(std::floor(delay));
    const double f = delay - whole;
    const double w[4] = {-f * (f - 1.0) * (f - 2.0) / 6.0,
                         (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0,
                         -(f + 1.0) * f * (f - 2.0) / 2.0,
                         (f + 1.0) * f * (f - 1.0) / 6.0};
    const int size = static_cast<int>(dst.size());
    for (int t = 0; t < 4; ++t)
    {
        const int offset = whole + t - 1;
        const double g = gain * w[t];
        if (std::abs(g) < 1.0e-12)
            continue;
        for (int i = 0; i < static_cast<int>(src.size()); ++i)
        {
            const int j = i + offset;
            if (j < 0)
                continue;
            if (j >= size)
                break;
            dst[static_cast<size_t>(j)] += static_cast<float>(g * src[static_cast<size_t>(i)]);
        }
    }
}

void fadeTail(std::vector<float>& h, int fadeLength)
{
    const int n = static_cast<int>(h.size());
    fadeLength = std::min(fadeLength, n);
    for (int i = 0; i < fadeLength; ++i)
    {
        const double x = static_cast<double>(i + 1) / fadeLength;
        h[static_cast<size_t>(n - fadeLength + i)] *=
            static_cast<float>(0.5 + 0.5 * std::cos(std::numbers::pi * x));
    }
}

// A user's response at the project rate (cubic Hermite), cut to the longest
// kept, levelled so its 150 Hz - 4 kHz band averages 0 dB like a built one.
Response fromUser(const UserResponse& user, double rate)
{
    Response out;
    if (user.left.empty())
        return out;
    const double ratio = user.sampleRate / rate;
    const int frames = static_cast<int>(user.left.size());
    const int length =
        std::min(static_cast<int>(std::ceil(frames / ratio)), static_cast<int>(maxUserSeconds * rate));
    const auto resample = [&](const std::vector<float>& x)
    {
        std::vector<float> y(static_cast<size_t>(length));
        const auto at = [&](int i) { return i >= 0 && i < frames ? x[static_cast<size_t>(i)] : 0.0f; };
        for (int n = 0; n < length; ++n)
        {
            const double p = n * ratio;
            const int i = static_cast<int>(p);
            const auto t = static_cast<float>(p - i);
            const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            y[static_cast<size_t>(n)] = ((c3 * t + c2) * t + c1) * t + y1;
        }
        return y;
    };
    out.left = resample(user.left);
    out.right = user.right.empty() ? out.left : resample(user.right);
    if (std::ceil(frames / ratio) > length)
    {
        fadeTail(out.left, static_cast<int>(0.005 * rate));
        fadeTail(out.right, static_cast<int>(0.005 * rate));
    }
    // Its level: the DFT of the mid response at the band's points.
    const auto magnitude = [&](double hz)
    {
        Complex sum = 0.0;
        const double w = -2.0 * std::numbers::pi * hz / rate;
        for (int i = 0; i < length; ++i)
            sum += 0.5 * (out.left[static_cast<size_t>(i)] + out.right[static_cast<size_t>(i)]) *
                   std::polar(1.0, w * i);
        return std::abs(sum);
    };
    const double mean = bandMeanDb(magnitude);
    const auto scale = static_cast<float>(std::pow(10.0, -mean / 20.0));
    for (auto* v : {&out.left, &out.right})
        for (auto& s : *v)
            s *= scale;
    return out;
}
} // namespace

int responseLength(double sampleRate) noexcept
{
    return std::max(512, nextPowerOfTwo(static_cast<int>(std::ceil(0.04 * sampleRate))));
}

double directDb(int model, const Mic& mic, double hz) noexcept
{
    const double mean = bandMeanDb([&](double f) { return rawMagnitude(model, mic, f, 0.0, false); });
    return 20.0 * std::log10(std::max(1.0e-9, rawMagnitude(model, mic, hz, 0.0, false))) - mean;
}

Layout layout(int model)
{
    return speaker(model).layout;
}

const char* modelName(int model) noexcept
{
    static constexpr std::array<const char*, modelCount> names{
        "2x12 Open", "1x12 Open", "4x10 Closed", "1x15 Bass", "4x10 Bass", "Custom IR"};
    return names[static_cast<size_t>(std::clamp(model, 0, modelCount - 1))];
}

const char* micTypeName(int type) noexcept
{
    static constexpr std::array<const char*, micTypeCount> names{"Dynamic", "Ribbon", "Condenser"};
    return names[static_cast<size_t>(std::clamp(type, 0, micTypeCount - 1))];
}

Response build(const Settings& settings, double sampleRate, const UserResponse* user)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("cabinet::build");
    const int length = responseLength(sampleRate);
    if (settings.model == custom)
    {
        if (user != nullptr && !user->left.empty())
            return fromUser(*user, sampleRate);
        Response r{std::vector<float>(static_cast<size_t>(length), 0.0f), {}};
        r.left[0] = 1.0f;
        r.right = r.left;
        return r;
    }

    Response out{std::vector<float>(static_cast<size_t>(length), 0.0f),
                 std::vector<float>(static_cast<size_t>(length), 0.0f)};
    const auto& lay = speaker(settings.model).layout;
    const auto metres = [](const Mic& m)
    { return std::clamp(static_cast<double>(m.distanceCm), 1.0, 100.0) * 0.01; };
    // Delays are counted from the nearest mic, so nothing is added to the latency.
    double nearest = 1.0e9;
    for (const auto& m : settings.mics)
        if (m.on)
            nearest = std::min(nearest, metres(m));
    if (nearest > 1.0e8)
        return out; // no mic: silence
    const double samplesPerMetre = sampleRate / soundSpeed;
    double loudest = 0.0;
    const Mic* first = nullptr;

    for (const auto& m : settings.mics)
    {
        if (!m.on)
            continue;
        if (first == nullptr)
            first = &m;
        const double d = metres(m);
        const double norm = std::pow(
            10.0,
            -bandMeanDb([&](double hz) { return rawMagnitude(settings.model, m, hz, 0.0, false); }) / 20.0);
        const auto shape = [&](double extraAngle, bool rear)
        {
            return minimumPhase([&](double hz)
                                { return norm * rawMagnitude(settings.model, m, hz, extraAngle, rear); },
                                length,
                                sampleRate);
        };
        std::vector<float> heard(static_cast<size_t>(length), 0.0f);
        const double reach = std::hypot(d, coneRadius); // the direct sound's effective distance
        const auto delayFor = [&](double path) { return (path - nearest) * samplesPerMetre; };
        addDelayed(heard, shape(0.0, false), delayFor(d), 1.0);

        // The other speakers, each from its own angle and distance.
        if (lay.speakers.size() > 1)
        {
            double nearestOther = 1.0e9;
            for (size_t i = 1; i < lay.speakers.size(); ++i)
                nearestOther = std::min(nearestOther, std::hypot(lay.speakers[i][0], lay.speakers[i][1]));
            const auto other = shape(std::atan2(nearestOther, d) * 180.0 / std::numbers::pi, false);
            for (size_t i = 1; i < lay.speakers.size(); ++i)
            {
                const double lateral = std::hypot(lay.speakers[i][0], lay.speakers[i][1]);
                const double path = std::hypot(d, lateral);
                addDelayed(heard, other, delayFor(path), reach / std::hypot(path, coneRadius));
            }
        }
        // The floor between the cabinet and the mic.
        const double floorPath = std::hypot(d, 2.0 * lay.height);
        addDelayed(heard,
                   shape(std::atan2(2.0 * lay.height, d) * 180.0 / std::numbers::pi, false),
                   delayFor(floorPath),
                   0.45 * reach / std::hypot(floorPath, coneRadius));
        // An open back's rear wave, off the wall behind, inverted.
        if (lay.openBack)
        {
            const double rearPath = d + cabinetDepth + 2.0 * rearWall;
            addDelayed(heard,
                       shape(60.0, true),
                       delayFor(rearPath),
                       -0.3 * reach / std::hypot(rearPath, coneRadius));
        }

        const double gain = std::pow(10.0, m.levelDb / 20.0) * (m.invert ? -1.0 : 1.0);
        loudest = std::max(loudest, std::abs(gain));
        const double pan = std::clamp(static_cast<double>(m.pan), -1.0, 1.0);
        const double toLeft = std::min(1.0, 1.0 - pan) * gain, toRight = std::min(1.0, 1.0 + pan) * gain;
        for (int i = 0; i < length; ++i)
        {
            out.left[static_cast<size_t>(i)] += static_cast<float>(toLeft * heard[static_cast<size_t>(i)]);
            out.right[static_cast<size_t>(i)] += static_cast<float>(toRight * heard[static_cast<size_t>(i)]);
        }
    }

    // The room: early reflections, darker and wider than the direct sound,
    // different on each side.
    if (settings.room > 0.0f && first != nullptr)
    {
        const double norm = std::pow(
            10.0,
            -bandMeanDb([&](double hz) { return rawMagnitude(settings.model, *first, hz, 0.0, false); }) /
                20.0);
        const auto diffuse = minimumPhase(
            [&](double hz)
            {
                return norm * rawMagnitude(settings.model, *first, hz, 70.0, false) *
                       lowPass2(hz, 3500.0, 0.7) * highPass2(hz, 150.0, 0.7);
            },
            length,
            sampleRate);
        static constexpr std::array<double, 10> leftMs{
            7.1, 9.8, 13.4, 16.9, 19.7, 23.3, 26.8, 29.5, 32.6, 35.4};
        static constexpr std::array<double, 10> rightMs{
            8.3, 11.2, 12.6, 15.8, 21.1, 22.4, 25.9, 28.7, 31.4, 34.9};
        const double amount = std::clamp(static_cast<double>(settings.room), 0.0, 1.0) * 0.5 * loudest;
        for (size_t i = 0; i < leftMs.size(); ++i)
        {
            const double sign = (i % 3 == 1) ? -1.0 : 1.0;
            addDelayed(out.left,
                       diffuse,
                       leftMs[i] * 0.001 * sampleRate,
                       sign * amount * std::exp(-leftMs[i] / 14.0));
            addDelayed(out.right,
                       diffuse,
                       rightMs[i] * 0.001 * sampleRate,
                       -sign * amount * std::exp(-rightMs[i] / 14.0));
        }
    }
    fadeTail(out.left, length / 10);
    fadeTail(out.right, length / 10);
    return out;
}

} // namespace northlight::plugins::cabinet
