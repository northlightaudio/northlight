#include "plugins/Orbit.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/Oversampler.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::orbit
{

namespace
{
int index(float choice, int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(choice)), 0, count - 1);
}

double cornerOf(double tauSeconds) noexcept
{
    return 1.0 / (2.0 * std::numbers::pi * tauSeconds);
}

// The record pre-emphasis: a shelf this many dB up at twice the time constant's
// corner (its midpoint); playback takes exactly as much back out.
constexpr double emphasisDb = 12.0;
// NAB's low-frequency time constant (3180 us, 50 Hz): the record EQ trims the
// lows and playback restores them.
constexpr double lowEmphasisHz = 70.0, lowEmphasisDb = 6.0;
} // namespace

float speedIps(float speedChoice) noexcept
{
    constexpr float ips[] = {7.5f, 15.0f, 30.0f};
    return ips[index(speedChoice, 3)];
}

float operatingLevelDb(float levelChoice) noexcept
{
    constexpr float db[] = {3.0f, 6.0f, 9.0f};
    return db[index(levelChoice, 3)];
}

double emphasisHz(float speedChoice, float eqChoice) noexcept
{
    const int s = index(speedChoice, 3);
    if (s == ips30)
        return cornerOf(17.5e-6); // AES
    if (index(eqChoice, 2) == nab)
        return cornerOf(50.0e-6);
    return cornerOf(s == ips7 ? 70.0e-6 : 35.0e-6);
}

bool hasLowEmphasis(float speedChoice, float eqChoice) noexcept
{
    return index(speedChoice, 3) != ips30 && index(eqChoice, 2) == nab;
}

double headBumpHz(float speedChoice) noexcept
{
    return 3.5 * speedIps(speedChoice);
}

double headBumpDb(float speedChoice) noexcept
{
    constexpr double db[] = {3.0, 2.4, 1.8};
    return db[index(speedChoice, 3)];
}

double gapLossHz(float speedChoice, double sampleRate) noexcept
{
    constexpr double hz[] = {13000.0, 19000.0, 24000.0};
    return std::min(hz[index(speedChoice, 3)], 0.45 * sampleRate);
}

float hissDbfs(float hissPercent, float speedChoice) noexcept
{
    if (hissPercent <= 0.0f)
        return minusInfinityDb;
    const float amount = std::min(hissPercent, 100.0f) * 0.01f;
    return -100.0f + 48.0f * amount + 3.0f * std::log2(15.0f / speedIps(speedChoice));
}

TapeModel tapeModel(float formulaChoice, float biasPercent) noexcept
{
    // Saturation and the reversible share at calibrated bias, per formula.
    constexpr double ms[] = {1.17, 1.40, 1.55};
    constexpr double c0[] = {0.95, 0.955, 0.962};
    const int f = index(formulaChoice, 3);
    TapeModel m;
    m.ms = ms[f];
    m.a = m.ms / 3.01;
    // Under-biased, the hysteresis shows (gritty even quietly); over-biased,
    // the tape follows its anhysteretic curve almost exactly.
    const double b = std::clamp(static_cast<double>(biasPercent) * 0.01, -1.0, 1.0);
    m.c = b < 0.0 ? c0[f] + (c0[f] - 0.80) * b : c0[f] + (0.985 - c0[f]) * b;
    return m;
}

double fieldGain(float levelChoice) noexcept
{
    // At +6 a tone at 0 VU (-18 dBFS) sits where the tape's distortion is about
    // 0.2%; each 3 dB of operating level drives it 3 dB harder.
    return 1.2 * std::pow(10.0, (operatingLevelDb(levelChoice) - 6.0) / 20.0);
}

double makeupGain(const TapeModel& m) noexcept
{
    // The small-signal slope is the reversible share times the anhysteretic
    // curve's slope at the origin (Ms / 3a); the irreversible part adds little.
    return 1.0 / (m.c * m.ms / (3.0 * m.a));
}

// ----- the hysteresis ------------------------------------------------------------------------

namespace
{
// The Langevin function L(q) = coth(q) - 1/q and its derivative, from one exp.
inline void langevin(double q, double& value, double& slope) noexcept
{
    const double aq = std::abs(q);
    if (aq < 1.0e-3)
    {
        value = q / 3.0;
        slope = 1.0 / 3.0 - q * q / 15.0;
        return;
    }
    const double e = std::exp(-2.0 * aq);
    const double coth = (1.0 + e) / (1.0 - e);
    value = std::copysign(coth - 1.0 / aq, q);
    const double invSinh = 2.0 * std::sqrt(e) / (1.0 - e);
    slope = 1.0 / (aq * aq) - invSinh * invSinh;
}
} // namespace

void Hysteresis::prepare(double sampleRate) noexcept
{
    t_ = 1.0 / sampleRate;
    reset();
}

void Hysteresis::reset() noexcept
{
    m_ = h1_ = hd1_ = 0.0;
}

double Hysteresis::slope(double m, double h, double hd) const noexcept
{
    const auto& p = model_;
    double l = 0.0, lSlope = 0.0;
    langevin((h + p.alpha * m) / p.a, l, lSlope);
    const double diff = p.ms * l - m;
    const double delta = hd >= 0.0 ? 1.0 : -1.0;
    // The irreversible part only moves the magnetisation towards the curve.
    const double deltaM = (delta > 0.0) == (diff > 0.0) ? 1.0 : 0.0;
    const double msOverA = p.ms / p.a;
    const double irreversible =
        (1.0 - p.c) * deltaM * diff / ((1.0 - p.c) * delta * p.k - p.alpha * diff) * hd;
    const double reversible = p.c * msOverA * hd * lSlope;
    return (irreversible + reversible) / (1.0 - p.c * p.alpha * msOverA * lSlope);
}

double Hysteresis::process(double h) noexcept
{
    // The field's rate of change (an alpha-transform of the difference, which
    // keeps the solver steady), then one second-order Runge-Kutta step.
    constexpr double d = 0.75;
    const double hd = (1.0 + d) / t_ * (h - h1_) - d * hd1_;
    const double k1 = t_ * slope(m_, h1_, hd1_);
    const double k2 = t_ * slope(m_ + 0.5 * k1, 0.5 * (h + h1_), 0.5 * (hd + hd1_));
    double m = m_ + k2;
    if (!std::isfinite(m) || std::abs(m) > 10.0)
        m = 0.0;
    m_ = m;
    h1_ = h;
    hd1_ = hd;
    return m;
}

// ----- the machine ---------------------------------------------------------------------------

namespace
{
// The noise for the hiss and the flutter's wander starts here on every reset.
constexpr std::uint32_t noiseSeed = 0x2545F491u;

std::vector<engine::ParameterInfo> parameterInfos()
{
    return {{"Input", "dB", -12.0f, 18.0f, 0.0f, false},
            {"Bias", "%", -100.0f, 100.0f, 0.0f, false},
            choice("Speed", {"7.5 ips", "15 ips", "30 ips"}, ips15),
            choice("EQ", {"NAB", "CCIR"}, nab),
            choice("Level", {"+3 dB", "+6 dB", "+9 dB"}, plus6),
            choice("Tape", {"Classic", "Hot", "Modern"}, classic),
            {"Hiss", "%", 0.0f, 100.0f, 0.0f, false},
            {"Flutter", "%", 0.0f, maxFlutterPercent, 0.03f, false},
            {"Output", "dB", -24.0f, 12.0f, 0.0f, false},
            {"Mix", "%", 0.0f, 100.0f, 100.0f, false},
            choice("Link", {"Off", "On"}, 1.0f),
            choice("Meter", {"Input", "Tape", "Output"}, meterTape)};
}

// The oversampling for a session rate: the tape runs at about 192 kHz.
int factorFor(double sampleRate) noexcept
{
    if (sampleRate <= 50000.0)
        return 4;
    return sampleRate <= 100000.0 ? 2 : 1;
}

// The oversampler's round trip delays the tape a few samples (its half-bands are
// minimum phase): measured on a 2 kHz tone, so the tape's delay line can take it
// off and the dry signal lines up with the wet. Not real-time (it runs a tone).
double roundTripDelay(int factor, double sampleRate) noexcept
{
    Oversampler probe;
    probe.setFactor(factor);
    float raised[Oversampler::maxFactor];
    constexpr double hz = 2000.0;
    constexpr int settle = 2048, length = 4096;
    double xr = 0.0, xi = 0.0, yr = 0.0, yi = 0.0;
    for (int i = 0; i < settle + length; ++i)
    {
        const double a = 2.0 * std::numbers::pi * hz * i / sampleRate;
        const auto x = static_cast<float>(std::sin(a));
        probe.up(x, raised);
        const float y = probe.down(raised);
        if (i < settle)
            continue;
        xr += x * std::cos(a);
        xi += x * std::sin(a);
        yr += y * std::cos(a);
        yi += y * std::sin(a);
    }
    double phase = std::atan2(yi, yr) - std::atan2(xi, xr); // what the output lags by
    while (phase < 0.0)
        phase += 2.0 * std::numbers::pi;
    return std::clamp(phase / (2.0 * std::numbers::pi * hz / sampleRate), 0.0, 16.0);
}

class Orbit final : public ProcessorBase
{
public:
    Orbit() : ProcessorBase(type, parameterInfos()) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }
    int latencySamples() const noexcept override { return delaySamples_; }

private:
    struct Channel
    {
        Biquad recLow, recHigh, playHigh, playLow, biasShelf, bump, dip, lowCut, gapLoss, hissColour;
        Oversampler oversampler;
        Hysteresis tape;
        std::vector<float> line, dry;
        double pre = 0.0, post = 0.0;    // the tape's input and output power, smoothed
        float vu = 0.0f, vuInner = 0.0f; // the meter point's VU: the rectified signal through a VU's movement
    };

    void prepareAudio(double sampleRate, int) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Orbit::prepareAudio");
        feedback_.prepare(sampleRate);
        factor_ = factorFor(sampleRate);
        delaySamples_ = static_cast<int>(std::lround(flutterDelaySeconds * sampleRate));
        tapeDelay_ = static_cast<double>(delaySamples_) - roundTripDelay(factor_, sampleRate);
        lineSize_ = 2 * delaySamples_ + 16;
        for (auto& c : channels_)
        {
            c.oversampler.setFactor(factor_);
            c.tape.prepare(sampleRate * factor_);
            c.line.assign(static_cast<size_t>(lineSize_), 0.0f);
            c.dry.assign(static_cast<size_t>(lineSize_), 0.0f);
            for (auto* b : {&c.recLow,
                            &c.recHigh,
                            &c.playHigh,
                            &c.playLow,
                            &c.biasShelf,
                            &c.bump,
                            &c.dip,
                            &c.lowCut,
                            &c.gapLoss,
                            &c.hissColour})
                b->reset();
            c.pre = c.post = 0.0;
        }
        write_ = 0;
        smoothStep_ = dsp::stepForSeconds(0.015f, static_cast<float>(sampleRate));
        powerStep_ = dsp::stepForSeconds(0.05, sampleRate);
        // A VU's movement: two equal lags reaching 99% of a step in 300 ms.
        vuStep_ = dsp::stepForSeconds(0.045f, static_cast<float>(sampleRate));
        wowPhase_ = flutterPhase_ = 0.0;
        wowDrift_ = flutterDrift_ = {};
        rng_ = dsp::XorShift32{noiseSeed};
        designed_ = false;
        primed_ = false;
    }

    // The filters follow the speed, the standard and the bias, redesigned only
    // when one of them moved.
    void design() noexcept
    {
        const float s = parameter(speed), e = parameter(eq), f = parameter(tape);
        const float b = std::round(parameter(bias) * 2.0f) * 0.5f;
        if (designed_ && s == designedSpeed_ && e == designedEq_ && b == designedBias_ && f == designedTape_)
            return;
        designed_ = true;
        designedSpeed_ = s;
        designedEq_ = e;
        designedBias_ = b;
        designedTape_ = f;
        const double fs = sampleRate_;
        const double emphasis = std::min(2.0 * emphasisHz(s, e), 0.42 * fs);
        const bool low = hasLowEmphasis(s, e);
        const double bumpHz = headBumpHz(s), bumpDb = headBumpDb(s);
        const double biasAmount = static_cast<double>(b) * 0.01;
        const auto model = tapeModel(f, b);
        makeup_ = makeupGain(model);
        for (auto& c : channels_)
        {
            c.recHigh.setHighShelf(fs, emphasis, emphasisDb);
            c.playHigh.setHighShelf(fs, emphasis, -emphasisDb);
            if (low)
            {
                c.recLow.setLowShelf(fs, lowEmphasisHz, -lowEmphasisDb);
                c.playLow.setLowShelf(fs, lowEmphasisHz, lowEmphasisDb);
            }
            else
            {
                c.recLow.bypass();
                c.playLow.bypass();
            }
            // Over-biased tape loses its highs; under-biased keeps a little more.
            const double biasDb = biasAmount > 0.0 ? -6.0 * biasAmount : -1.5 * biasAmount;
            c.biasShelf.setHighShelf(fs, std::min(9000.0, 0.42 * fs), biasDb);
            c.bump.setPeaking(fs, bumpHz, 1.3, bumpDb);
            c.dip.setPeaking(fs, bumpHz * 2.5, 1.4, -0.35 * bumpDb);
            c.lowCut.setHighPass(fs, bumpHz * 0.38, 0.6);
            c.gapLoss.setLowPass(fs, gapLossHz(s, fs), 0.6);
            c.hissColour.setHighPass1(fs, 400.0);
            c.tape.setModel(model);
        }
    }

    // The gains the block moves towards (smoothed per sample).
    void targets(float& inputGain, float& outputGain, float& wet, float& field) const noexcept
    {
        const float inDb = parameter(input);
        inputGain = dbToGain(inDb);
        outputGain = dbToGain(parameter(output) - (parameter(link) >= 0.5f ? inDb : 0.0f));
        wet = std::clamp(parameter(mix) * 0.01f, 0.0f, 1.0f);
        field = static_cast<float>(fieldGain(parameter(level)));
    }

    float readLine(const std::vector<float>& line, double delay) const noexcept
    {
        double position = static_cast<double>(write_) - delay;
        while (position < 0.0)
            position += lineSize_;
        const int i1 = static_cast<int>(position);
        const auto fraction = static_cast<float>(position - i1);
        const auto at = [&](int i) { return line[static_cast<size_t>((i + lineSize_) % lineSize_)]; };
        const float y0 = at(i1 - 1), y1 = at(i1), y2 = at(i1 + 1), y3 = at(i1 + 2);
        // Catmull-Rom.
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
    }

    void publishTransport() noexcept
    {
        const auto head = playHead();
        feedback_.setTransport(head.samples / std::max(1.0, sampleRate_), head.playing);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        publishTransport();
        design();
        float inT = 1.0f, outT = 1.0f, wetT = 1.0f, fieldT = 1.0f;
        targets(inT, outT, wetT, fieldT);
        if (!primed_)
        {
            inGain_ = inT;
            outGain_ = outT;
            wet_ = wetT;
            field_ = fieldT;
            primed_ = true;
        }
        const float s = parameter(speed);
        // Uniform noise's rms is 1/sqrt(3); the delay line's fractional read softens
        // its very top by about 1.6 dB, made up here.
        const float hissGain = dbToGain(hissDbfs(parameter(hiss), s)) * 1.7320508f * 1.2f;
        const int meterPoint = index(parameter(meter), 3);
        const auto fs = static_cast<float>(sampleRate_);
        // Wow and flutter: the speed's deviation, peak, split between a slow wow and
        // the capstan's flutter (faster at a higher speed); the delay wanders by
        // its integral.
        const double peak =
            std::clamp(parameter(flutter), 0.0f, maxFlutterPercent) * 0.01 * std::numbers::sqrt2;
        constexpr double wowHz = 0.45;
        const double flutterHz = 8.0 * speedIps(s) / 15.0;
        const double wowSeconds = 0.7 * peak / (2.0 * std::numbers::pi * wowHz);
        const double flutterSeconds = 0.3 * peak / (2.0 * std::numbers::pi * flutterHz);
        const double wowStep = 2.0 * std::numbers::pi * wowHz / sampleRate_;
        const double flutterStep = 2.0 * std::numbers::pi * flutterHz / sampleRate_;
        float raised[Oversampler::maxFactor];
        float* io[2] = {left, right};

        for (int n = 0; n < numSamples; ++n)
        {
            inGain_ += (inT - inGain_) * smoothStep_;
            outGain_ += (outT - outGain_) * smoothStep_;
            wet_ += (wetT - wet_) * smoothStep_;
            field_ += (fieldT - field_) * smoothStep_;
            const double fieldToTape = field_;
            const double fromTape = makeup_ / fieldToTape;

            wowPhase_ += wowStep;
            if (wowPhase_ > 2.0 * std::numbers::pi)
                wowPhase_ -= 2.0 * std::numbers::pi;
            flutterPhase_ += flutterStep;
            if (flutterPhase_ > 2.0 * std::numbers::pi)
                flutterPhase_ -= 2.0 * std::numbers::pi;
            const double wowShape = 0.5 * std::sin(wowPhase_) + 0.5 * wowDrift_.next(rng_, fs * 1.8f);
            const double flutterShape = 0.7 * std::sin(flutterPhase_) +
                                        0.3 * flutterDrift_.next(rng_, fs / static_cast<float>(flutterHz));
            const double delay =
                tapeDelay_ + (wowSeconds * wowShape + flutterSeconds * flutterShape) * sampleRate_;

            float inPeak = 0.0f, outPeak = 0.0f, meters[2] = {0.0f, 0.0f};
            double squash = 0.0;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& c = channels_[ch];
                const float in = io[ch][n];
                inPeak = std::max(inPeak, std::abs(in));
                c.dry[static_cast<size_t>(write_)] = in;
                const float driven = in * inGain_;

                // Record: the EQ, then the tape at the raised rate.
                float u = c.recHigh.process(c.recLow.process(driven));
                c.pre += (static_cast<double>(u) * u - c.pre) * powerStep_;
                c.oversampler.up(u, raised);
                for (int i = 0; i < factor_; ++i)
                    raised[i] = static_cast<float>(c.tape.process(raised[i] * fieldToTape) * fromTape);
                float v = c.oversampler.down(raised);
                c.post += (static_cast<double>(v) * v - c.post) * powerStep_;

                // Playback: the EQ, the bias's highs, the head bump, the gap loss, the hiss.
                v = c.playLow.process(c.playHigh.process(v));
                v = c.biasShelf.process(v);
                v = c.dip.process(c.bump.process(v));
                v = c.gapLoss.process(c.lowCut.process(v));
                if (hissGain > 0.0f)
                    v += c.hissColour.process(rng_.bipolarWord() * hissGain);

                // The transport's wander, then the output.
                c.line[static_cast<size_t>(write_)] = v;
                const float played = readLine(c.line, delay);
                const float dry = readLine(c.dry, delaySamples_);
                const float out = dry + (played * outGain_ - dry) * wet_;
                io[ch][n] = out;
                outPeak = std::max(outPeak, std::abs(out));
                // A VU reads the rectified average (a sine's is 0.9 of its rms), so a
                // tone at the reference reads 0 and music's peaks do not pin it.
                const float point = std::abs(meterPoint == meterInput  ? dry * inGain_
                                             : meterPoint == meterTape ? played
                                                                       : out);
                c.vuInner += (point - c.vuInner) * vuStep_;
                c.vu += (c.vuInner - c.vu) * vuStep_;
                meters[ch] = c.vu * 1.1107207f;
                if (c.post > 1.0e-12)
                    squash = std::max(squash, c.pre / c.post);
            }
            if (++write_ == lineSize_)
                write_ = 0;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
            {
                const float reduction = squash > 1.0 ? static_cast<float>(10.0 * std::log10(squash)) : 0.0f;
                feedback_.addSample(inPeak, outPeak, reduction, meters[0], meters[1]);
            }
        }
        for (auto& c : channels_)
        {
            c.oversampler.flush();
            if (c.vu < 1.0e-20f)
                c.vu = c.vuInner = 0.0f;
            if (c.pre < 1.0e-30)
                c.pre = 0.0;
            if (c.post < 1.0e-30)
                c.post = 0.0;
        }
    }

    // Bypassed, the input passes delayed by the latency the graph was solved with.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        publishTransport();
        float* io[2] = {left, right};
        for (int n = 0; n < numSamples; ++n)
        {
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& c = channels_[ch];
                c.dry[static_cast<size_t>(write_)] = io[ch][n];
                c.line[static_cast<size_t>(write_)] = 0.0f;
                io[ch][n] = readLine(c.dry, delaySamples_);
            }
            if (++write_ == lineSize_)
                write_ = 0;
        }
        feedback_.addBypassed(left, right, numSamples);
    }

    engine::ProcessorFeedback feedback_;
    std::array<Channel, 2> channels_;
    int factor_ = 4, delaySamples_ = 72, lineSize_ = 160, write_ = 0;
    float smoothStep_ = 0.01f;
    double powerStep_ = 0.001;
    float vuStep_ = 0.001f;
    float inGain_ = 1.0f, outGain_ = 1.0f, wet_ = 1.0f, field_ = 1.0f;
    double makeup_ = 1.0;
    double tapeDelay_ = 72.0; // the line's share of the latency (the oversampler takes the rest)
    double wowPhase_ = 0.0, flutterPhase_ = 0.0;
    dsp::Drift wowDrift_, flutterDrift_;
    dsp::XorShift32 rng_{noiseSeed};
    float designedSpeed_ = -1.0f, designedEq_ = -1.0f, designedBias_ = -1000.0f, designedTape_ = -1.0f;
    bool designed_ = false, primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Orbit>();
}

} // namespace northlight::plugins::orbit
