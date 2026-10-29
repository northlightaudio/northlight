#include "plugins/Lumen.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/ParamHelpers.h"
#include "plugins/SynthVoices.h"
#include "plugins/dsp/Modulation.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace northlight::plugins::lumen
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr double ln1000 = 6.907755278982137; // a fall of 60 dB
constexpr int controlInterval = 16;

// The beam modes against the fundamental. A clamped-free rod's first is 6.27;
// a tine's tuning spring and tip mass lift it, and jRhodes3d's measure about 6.4
// through the middle of the keyboard (docs/plans/electric-piano.md, calibration).
constexpr std::array<double, modeCount> beamRatios{1.0, 6.4, 17.547, 34.386};
// Each mode's share of the tip's motion before Bell.
constexpr std::array<double, modeCount> tipWeights{1.0, 0.5, 0.15, 0.08};

// The fundamental's ring (seconds to fall 60 dB, key held) at keys across the
// keyboard, measured from jRhodes3d (a 1977 Mark I) and smoothed to fall
// steadily; between keys it is read on a log scale.
constexpr std::array<std::pair<int, double>, 11> ringTimes{{{28, 24.0},
                                                            {40, 24.0},
                                                            {50, 21.0},
                                                            {59, 16.5},
                                                            {65, 14.5},
                                                            {71, 9.7},
                                                            {78, 6.3},
                                                            {86, 6.0},
                                                            {91, 3.6},
                                                            {96, 1.7},
                                                            {100, 1.2}}};

// The hammer's contact (seconds) at a full strike with Hardness at 50%: long on
// the soft bass tips, then nearly level from the middle up, which is what keeps
// jRhodes3d's bell strong from D3 to D4 (about -15 dB on a hard strike), faint
// from F4 to A5 (about -55 dB), and short again at the top, where its bell
// returns a little.
constexpr std::array<std::pair<int, double>, 8> contactTimes{{{28, 2.0e-3},
                                                              {50, 1.0e-3},
                                                              {60, 0.8e-3},
                                                              {72, 0.85e-3},
                                                              {78, 0.62e-3},
                                                              {86, 0.28e-3},
                                                              {96, 0.26e-3},
                                                              {100, 0.25e-3}}};

// A table of (key, value) read between keys on a log scale, held past its ends.
template <size_t N> double fromTable(const std::array<std::pair<int, double>, N>& table, int note) noexcept
{
    if (note <= table.front().first)
        return table.front().second;
    for (size_t i = 1; i < N; ++i)
        if (note <= table[i].first)
        {
            const auto [k0, v0] = table[i - 1];
            const auto [k1, v1] = table[i];
            const double t = static_cast<double>(note - k0) / (k1 - k0);
            return v0 * std::pow(v1 / v0, t);
        }
    return table.back().second;
}

// How far a tine swings past its pickup at a full strike, against a middle
// tine's: the long bass tines swing far and bark when hit hard, the short top
// ones hardly at all and stay nearly pure (fitted to jRhodes3d's harmonics).
constexpr std::array<std::pair<int, double>, 7> swings{
    {{28, 3.2}, {50, 2.0}, {60, 1.7}, {76, 1.0}, {86, 0.45}, {96, 0.32}, {100, 0.3}}};

double ringTime(int note) noexcept
{
    return fromTable(ringTimes, note);
}

// The hammer's felt stiffens as it compresses (force ~ compression^p), so by
// Hertz's law a strike at speed v stays on the tine for t ~ v^-((p-1)/(p+1)):
// a harder strike is shorter, and so brighter. The force over that time is a
// smooth bell (a Gaussian), whose spectrum exp(-(w s)^2 / 2) falls steadily with
// frequency, with none of the dips a sharp-edged pulse has, so brightness always
// rises with speed. It falls steeply too, as a felt's does. Neoprene stiffens
// sharply: p = 7 (a contact time going as v^-0.75) and this pulse make the bell
// partial grow 50 to 60 dB from the softest strike to the hardest, as
// jRhodes3d's does (docs/plans/electric-piano.md, calibration). It carries the
// hammer's momentum, given back with the felt's restitution.
constexpr double feltExponent = 7.0;
constexpr double restitution = 0.7;
constexpr double pulseWidth = 0.18; // s, the bell's standard deviation, as a share of the contact time
constexpr double pulseReach = 4.0;  // the pulse is cut at +-4 s
// The tip's travel for a unit of momentum: a full strike swings about 0.5.
constexpr double strikeScale = 0.3;

constexpr int faderCount = 4;
constexpr double stealFadeMs = 5.0;

// a towards b as t goes 0..1, on a log scale (t may run past either end).
double logLerp(double a, double b, double t) noexcept
{
    return a * std::pow(b / a, t);
}

// The pickup: 1 / (1 + ((x - s) / d)^2), a bump the tip moves across, scaled so
// its steepest slope (at d / sqrt(3) from the centre) is 1.
struct Pickup
{
    double d = 1.0, s = 0.5, norm = 1.0;

    static Pickup forSettings(float barkAmount, float symmetryAmount) noexcept
    {
        Pickup p;
        p.d = 2.0 * std::pow(0.1, std::clamp(static_cast<double>(barkAmount), 0.0, 1.0));
        p.s = p.d * std::clamp(static_cast<double>(symmetryAmount), 0.0, 1.0);
        p.norm = p.d * 8.0 / (3.0 * std::sqrt(3.0));
        return p;
    }
    double flux(double x) const noexcept
    {
        const double u = (x - s) / d;
        return norm / (1.0 + u * u);
    }
    double fluxIntegral(double x) const noexcept { return norm * d * std::atan((x - s) / d); }
};

// A note's fixed detune for Age, -1..1: the same every time, like a piano a
// little out of tune.
double ageOffset(int note) noexcept
{
    dsp::XorShift32 rng(static_cast<std::uint32_t>(note + 1) * 2654435761u);
    rng.bits();
    return static_cast<double>(rng.bipolarWord());
}

// A decaying resonance excited by a short burst of noise: the action's knock
// and the damper's thump.
struct Knock
{
    double re = 0.0, im = 0.0, poleRe = 0.0, poleIm = 0.0;
    int burst = 0;
    float gain = 0.0f;

    void start(double hz, double t60, double burstSeconds, float level, double sampleRate) noexcept
    {
        const double r = std::exp(-ln1000 / (t60 * sampleRate));
        const double w = twoPi * std::min(hz, sampleRate * 0.45) / sampleRate;
        poleRe = r * std::cos(w);
        poleIm = r * std::sin(w);
        burst = std::max(1, static_cast<int>(burstSeconds * sampleRate));
        gain = level;
    }
    bool active() const noexcept { return burst > 0 || re * re + im * im > 1.0e-14; }
    float step(dsp::XorShift32& rng) noexcept
    {
        double in = 0.0;
        if (burst > 0)
        {
            --burst;
            in = static_cast<double>(rng.bipolar() * gain);
        }
        const double nr = re * poleRe - im * poleIm + in;
        const double ni = re * poleIm + im * poleRe;
        re = nr;
        im = ni;
        return static_cast<float>(im);
    }
};

struct Voice
{
    int note = -1;
    std::uint32_t age = 0;
    bool sounding = false;
    bool released = false;

    NoteModel model;
    int modes = 0; // the modes below the Nyquist limit
    std::array<double, modeCount> re{}, im{}, poleRe{}, poleIm{}, gain{};
    double omega0 = 0.01; // the fundamental, radians a sample

    // The hammer while it touches the tine: the force, sample by sample.
    bool contact = false;
    int pulseAt = 0, pulseLength = 0;
    double pulseCentre = 0.0, pulseSigma = 1.0, pulseScale = 0.0;
    double strikeTrim = 1.0; // the level trim for this strike (NoteModel::levelTrim at its contact)

    // The damper: an extra decay a sample, eased in as the felt lands.
    double damp = 1.0, dampTarget = 1.0;

    // The pickup's ADAA memory: the tip, the flux's integral there and the last mean flux.
    double lastX = 0.0, lastIntegral = 0.0, lastMean = 0.0;

    Knock knock, thump;
    float fade = 1.0f, fadeStep = 0.0f; // a stolen voice fading out (faders only)

    double energy() const noexcept
    {
        double e = 0.0;
        for (int k = 0; k < modes; ++k)
            e += re[static_cast<size_t>(k)] * re[static_cast<size_t>(k)] +
                 im[static_cast<size_t>(k)] * im[static_cast<size_t>(k)];
        return e;
    }
};

class Lumen final : public MeteredBuiltIn
{
public:
    Lumen() : MeteredBuiltIn(type, lumen::parameterInfo(), true) {}

    bool acceptsMidi() const noexcept override { return true; }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        for (auto& f : faders_)
            f = Voice{};
        age_ = 0;
        expression_.reset();
        expressionRatio_ = 1.0;
        controlCountdown_ = 0;
        tremolo_.reset();
        phaser_.prepare(sampleRate);
        chorus_.prepare(sampleRate);
        dcBlock_.setHighPass1(sampleRate, 8.0);
        dcBlock_.reset();
        lowShelf_.reset();
        highShelf_.reset();
        shelvesBass_ = shelvesTreble_ = 1000.0f; // forces the first design
        driveLast_ = driveLastIntegral_ = 0.0;
        levelGain_ = -1.0f;
    }

    // Every voice that can sound: the playing ones and the stolen ones fading out.
    template <typename Fn> void forEachVoice(Fn&& fn) noexcept
    {
        for (auto& v : voices_)
            fn(v);
        for (auto& f : faders_)
            fn(f);
    }

    void values(std::array<float, count>& out) const noexcept
    {
        for (int i = 0; i < count; ++i)
            out[static_cast<size_t>(i)] = parameter(i);
    }

    // The voice's poles for its note at the player's pitch offset.
    void tuneVoice(Voice& v, double ratio) const noexcept
    {
        v.omega0 = twoPi * v.model.hz * ratio / sampleRate_;
        v.modes = 0;
        for (int k = 0; k < modeCount; ++k)
        {
            const auto ks = static_cast<size_t>(k);
            const double w = v.omega0 * v.model.ratio[ks];
            if (w >= std::numbers::pi * 0.9)
                break;
            const double r = std::exp(-ln1000 / (v.model.t60[ks] * sampleRate_));
            v.poleRe[ks] = r * std::cos(w);
            v.poleIm[ks] = r * std::sin(w);
            v.modes = k + 1;
        }
    }

    Voice* findVoice(int note, int limit) noexcept
    {
        for (int i = 0; i < maxVoices; ++i)
            if (voices_[static_cast<size_t>(i)].sounding && voices_[static_cast<size_t>(i)].note == note)
                return &voices_[static_cast<size_t>(i)];
        for (int i = 0; i < limit; ++i)
            if (!voices_[static_cast<size_t>(i)].sounding)
                return &voices_[static_cast<size_t>(i)];
        // All busy: the quietest, released ones first; it fades out on a fader.
        Voice* quietest = nullptr;
        double best = 1.0e300;
        for (int i = 0; i < limit; ++i)
        {
            auto& v = voices_[static_cast<size_t>(i)];
            const double score = v.energy() * (v.released ? 1.0 : 1.0e6);
            if (score < best)
            {
                best = score;
                quietest = &v;
            }
        }
        if (quietest == nullptr)
            return nullptr;
        Voice* slot = &faders_[0];
        for (auto& f : faders_)
            if (!f.sounding || f.fade < slot->fade)
                slot = &f;
        *slot = *quietest;
        slot->fade = 1.0f;
        slot->fadeStep = static_cast<float>(1.0 / (stealFadeMs * 0.001 * sampleRate_));
        quietest->sounding = false;
        return quietest;
    }

    void noteOn(int note, int midiVelocity) noexcept
    {
        const int limit = std::clamp(static_cast<int>(std::lround(parameter(voices))), 8, maxVoices);
        Voice* v = findVoice(note, limit);
        if (v == nullptr)
            return;
        std::array<float, count> p{};
        values(p);
        const bool fresh = !v->sounding;
        if (fresh)
        {
            *v = Voice{};
            v->note = note;
        }
        v->age = ++age_;
        v->sounding = true;
        v->released = false;
        v->dampTarget = 1.0;
        v->model = noteModel(note, p.data(), ageOffset(note) * 5.0 * p[age] * 0.01);
        tuneVoice(*v, expressionRatio_);
        const double bellScale = 2.0 * p[bell] * 0.01;
        for (int k = 0; k < modeCount; ++k)
        {
            const auto ks = static_cast<size_t>(k);
            v->gain[ks] = strikeScale * v->model.swing * v->model.weight[ks] * (k == 0 ? 1.0 : bellScale) /
                          v->model.ratio[ks];
        }
        if (fresh)
        {
            const Pickup pickup = Pickup::forSettings(p[bark] * 0.01f, p[symmetry] * 0.01f);
            v->lastX = 0.0;
            v->lastIntegral = pickup.fluxIntegral(0.0);
            v->lastMean = pickup.flux(0.0);
        }
        // The strike: the note's contact time at full speed, longer for a slower
        // strike (Hertz's law for the felt), so a soft note is darker.
        const double speed = strikeSpeed(midiVelocity, p[velocity] * 0.01f);
        const double contact =
            v->model.contactSeconds * std::pow(speed, -(feltExponent - 1.0) / (feltExponent + 1.0));
        const double sigma = std::max(0.3, pulseWidth * contact * sampleRate_);
        v->pulseSigma = sigma;
        v->pulseCentre = std::ceil(pulseReach * sigma);
        v->pulseLength = 2 * static_cast<int>(v->pulseCentre) + 1;
        v->pulseAt = 0;
        // Scaled so the samples add up to the momentum given: s sqrt(2 pi) for a
        // wide pulse, counted for a narrow one.
        double area = sigma * std::sqrt(twoPi);
        if (sigma < 2.0)
        {
            area = 0.0;
            for (int n = 0; n < v->pulseLength; ++n)
            {
                const double u = (n - v->pulseCentre) / sigma;
                area += std::exp(-0.5 * u * u);
            }
        }
        v->pulseScale = (1.0 + restitution) * speed / area;
        // The pulse's roll-off of the fundamental at this strike's contact, and the
        // tine's swing, evened out (at most 20 dB), so a soft top note still speaks.
        const double ws = twoPi * v->model.hz * pulseWidth * contact;
        v->strikeTrim =
            std::min(10.0 * v->model.levelTrim, 1.0 / (v->model.swing * std::exp(-0.5 * ws * ws)));
        v->contact = true;
        const float noiseLevel = p[noise] * 0.01f;
        if (noiseLevel > 0.0f)
            v->knock.start(420.0 * std::exp2(v->model.register01 * 0.7),
                           0.022,
                           0.0015,
                           noiseLevel * static_cast<float>(speed) * 0.05f,
                           sampleRate_);
    }

    void noteOff(int note) noexcept
    {
        const float noiseLevel = parameter(noise) * 0.01f;
        for (auto& v : voices_)
            if (v.sounding && v.note == note && !v.released)
                release(v, noiseLevel);
    }

    void release(Voice& v, float noiseLevel) noexcept
    {
        v.released = true;
        v.dampTarget = std::exp(-ln1000 / (v.model.damperT60 * sampleRate_));
        if (noiseLevel > 0.0f)
            v.thump.start(150.0, 0.05, 0.003, noiseLevel * 0.02f, sampleRate_);
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        synth::dispatch(
            e,
            expression_,
            [this](int note, int vel) { noteOn(note, vel); },
            [this](int note) { noteOff(note); },
            [this]
            {
                const float noiseLevel = parameter(noise) * 0.01f;
                for (auto& v : voices_)
                    if (v.sounding && !v.released)
                        release(v, noiseLevel);
            });
    }

    // One sample of a voice: the strike, the modes, the damper and the pickup.
    float runVoice(Voice& v, const Pickup& pickup, double dampEase) noexcept
    {
        double impulse = 0.0;
        if (v.contact)
        {
            const double u = (v.pulseAt - v.pulseCentre) / v.pulseSigma;
            impulse = v.pulseScale * std::exp(-0.5 * u * u);
            if (++v.pulseAt >= v.pulseLength)
                v.contact = false;
        }
        v.damp = v.dampTarget + (v.damp - v.dampTarget) * dampEase;
        double x = 0.0;
        for (int k = 0; k < v.modes; ++k)
        {
            const auto ks = static_cast<size_t>(k);
            const double r = v.re[ks] * v.poleRe[ks] - v.im[ks] * v.poleIm[ks] + impulse * v.gain[ks];
            const double i = v.re[ks] * v.poleIm[ks] + v.im[ks] * v.poleRe[ks];
            v.re[ks] = r * v.damp;
            v.im[ks] = i * v.damp;
            x += v.im[ks];
        }
        // The pickup's flux, its mean over the step (ADAA), then its change: the
        // voltage. Divided by the fundamental's rate so every note's fundamental
        // sits at the same level and only the overtones keep their lift.
        const double integral = pickup.fluxIntegral(x);
        const double dx = x - v.lastX;
        const double mean =
            std::abs(dx) > 1.0e-9 ? (integral - v.lastIntegral) / dx : pickup.flux(0.5 * (x + v.lastX));
        const double out = (mean - v.lastMean) / v.omega0 * v.strikeTrim;
        v.lastX = x;
        v.lastIntegral = integral;
        v.lastMean = mean;
        float sample = static_cast<float>(out);
        if (v.knock.active())
            sample += v.knock.step(rng_);
        if (v.thump.active())
            sample += v.thump.step(rng_);
        return sample;
    }

    // Whether a voice has died away (checked at the control rate).
    static bool finished(const Voice& v) noexcept
    {
        return !v.contact && v.energy() < 1.0e-12 && !v.knock.active() && !v.thump.active();
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        std::array<float, count> p{};
        values(p);
        const Pickup pickup = Pickup::forSettings(p[bark] * 0.01f, p[symmetry] * 0.01f);
        if (pickup.d != pickup_.d || pickup.s != pickup_.s)
        {
            // A new voicing: the ADAA memory follows it, so the step does not click.
            forEachVoice(
                [&](Voice& v)
                {
                    if (v.sounding)
                    {
                        v.lastIntegral = pickup.fluxIntegral(v.lastX);
                        v.lastMean = pickup.flux(v.lastX);
                    }
                });
            pickup_ = pickup;
        }
        const double dampEase = dsp::poleForMs(4.0, sampleRate_);

        // The preamp's tone.
        if (p[bass] != shelvesBass_ || p[treble] != shelvesTreble_)
        {
            lowShelf_.setLowShelf(sampleRate_, 220.0, p[bass]);
            highShelf_.setHighShelf(sampleRate_, 2800.0, p[treble]);
            shelvesBass_ = p[bass];
            shelvesTreble_ = p[treble];
        }
        const double driveGain = dbToGain(p[drive] * 0.01f * 30.0f) * 0.5;
        constexpr double driveBias = 0.25;
        const double driveOffset = std::tanh(driveBias);
        const double driveNorm = 1.0 / (driveGain * (1.0 - driveOffset * driveOffset));

        // The tremolo's rate: free, or a note value (locked to the song while it plays).
        const auto head = playHead();
        const double syncQuarters = sync::quarters(p[tremoloSync]);
        double tremoloStep = p[tremoloRate] / sampleRate_;
        if (syncQuarters > 0.0)
        {
            tremoloStep = head.quartersPerSample() / syncQuarters;
            if (head.playing)
            {
                tremolo_.phase = head.quarterNotes / syncQuarters;
                tremolo_.phase -= std::floor(tremolo_.phase);
            }
        }
        const auto mode =
            static_cast<dsp::Tremolo::Mode>(std::clamp(static_cast<int>(std::lround(p[tremoloMode])), 0, 2));
        const float shape = p[tremoloShape] * 0.01f;
        const float targetLevel = dbToGain(p[level]);
        if (levelGain_ < 0.0f)
            levelGain_ = targetLevel;
        const float levelStep = (targetLevel - levelGain_) / static_cast<float>(std::max(1, numSamples));

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);

            if (controlCountdown_ <= 0)
            {
                controlCountdown_ = controlInterval;
                // Pressure's vibrato and the bend; the mod wheel is the tremolo's here.
                const float wheel = expression_.modWheel;
                expression_.modWheel = 0.0f;
                const double ratio =
                    synth::Expression::ratio(expression_.advance(controlInterval / sampleRate_));
                expression_.modWheel = wheel;
                const bool retune = ratio != expressionRatio_;
                expressionRatio_ = ratio;
                forEachVoice(
                    [&](Voice& v)
                    {
                        if (!v.sounding)
                            return;
                        if (retune)
                            tuneVoice(v, ratio);
                        if (finished(v))
                            v.sounding = false;
                    });
            }
            --controlCountdown_;

            float sum = 0.0f;
            for (auto& v : voices_)
                if (v.sounding)
                    sum += runVoice(v, pickup, dampEase);
            for (auto& f : faders_)
                if (f.sounding)
                {
                    sum += runVoice(f, pickup, dampEase) * f.fade;
                    f.fade -= f.fadeStep;
                    if (f.fade <= 0.0f)
                        f.sounding = false;
                }

            // The preamp: tone, then a biased valve curve (ADAA), then the DC it leaves.
            float x = highShelf_.process(lowShelf_.process(sum * voiceLevel));
            const double in = static_cast<double>(x) * driveGain;
            const double integral = dsp::tubeIntegral(in, driveBias, driveOffset);
            const double shaped = dsp::adaa1(in,
                                             driveLast_,
                                             integral,
                                             driveLastIntegral_,
                                             1.0e-5,
                                             [&](double u) { return dsp::tube(u, driveBias, driveOffset); });
            driveLast_ = in;
            driveLastIntegral_ = integral;
            x = dcBlock_.process(static_cast<float>(shaped * driveNorm));

            float l = x, r = x;
            const float depth = std::min(1.0f, p[tremoloDepth] * 0.01f + expression_.modWheel);
            tremolo_.process(l, r, mode, tremoloStep, depth, shape);
            left[i] = l;
            right[i] = r;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);

        phaser_.process(left,
                        right,
                        numSamples,
                        p[phaserRate],
                        p[phaserDepth] * 0.01f,
                        p[phaserFeedback] * 0.01f,
                        p[phaserMix] * 0.01f);
        chorus_.process(left, right, numSamples, p[chorusRate], p[chorusDepth] * 0.01f, p[chorusMix] * 0.01f);

        for (int i = 0; i < numSamples; ++i)
        {
            levelGain_ += levelStep;
            left[i] = safety(left[i] * levelGain_);
            right[i] = safety(right[i] * levelGain_);
        }
        levelGain_ = targetLevel;
        for (auto* f : {&lowShelf_, &highShelf_, &dcBlock_})
            f->flushDenormals();

        int active = 0;
        for (const auto& v : voices_)
            if (v.sounding)
                ++active;
        // For the editor: the voices sounding (the frame's "reduction" slot) and
        // the tremolo's wave, 0..1 (its extra).
        blockReduction_ = static_cast<float>(active);
        blockExtra_ = 0.5f * (tremolo_.value + 1.0f) + 1.0e-3f;
    }

    // Unity below 0.9, then a soft knee to 1: a ceiling, not a sound.
    static float safety(float x) noexcept
    {
        const float a = std::abs(x);
        if (a <= 0.9f)
            return x;
        const float over = 0.1f * std::tanh((a - 0.9f) / 0.1f);
        return std::copysign(0.9f + over, x);
    }

    static constexpr float voiceLevel = 0.35f;

    std::array<Voice, maxVoices> voices_{};
    std::array<Voice, faderCount> faders_{};
    std::uint32_t age_ = 0;
    dsp::XorShift32 rng_{0x4C554D45u};
    synth::Expression expression_;
    double expressionRatio_ = 1.0;
    int controlCountdown_ = 0;
    Pickup pickup_;
    Biquad lowShelf_, highShelf_, dcBlock_;
    float shelvesBass_ = 1000.0f, shelvesTreble_ = 1000.0f;
    double driveLast_ = 0.0, driveLastIntegral_ = 0.0;
    dsp::Tremolo tremolo_;
    dsp::Phaser phaser_;
    dsp::Chorus chorus_;
    float levelGain_ = -1.0f;
};
} // namespace

NoteModel noteModel(int note, const float* values, double detuneCents) noexcept
{
    NoteModel m;
    const double r = std::clamp(static_cast<double>(note - lowestKey) / (highestKey - lowestKey), -0.4, 1.4);
    m.register01 = r;
    m.hz = static_cast<double>(values[tune]) * std::exp2((note - 69) / 12.0 + detuneCents / 1200.0);
    const double decayScale = std::clamp(static_cast<double>(values[decay]) * 0.01, 0.1, 10.0);
    // Long in the bass, short at the top; the beam modes die within a moment.
    const std::array<double, modeCount> low{0.0, 0.9, 0.25, 0.08};
    const std::array<double, modeCount> high{0.0, 0.15, 0.05, 0.02};
    for (size_t k = 0; k < modeCount; ++k)
    {
        m.ratio[k] = beamRatios[k];
        m.weight[k] = tipWeights[k];
        m.t60[k] = (k == 0 ? ringTime(note) : logLerp(low[k], high[k], r)) * decayScale;
    }
    const double damperAmount = std::clamp(static_cast<double>(values[damper]) * 0.01, 0.0, 1.0);
    m.damperT60 = std::min(m.t60[0], logLerp(0.7, 0.05, damperAmount) * (1.0 + 0.5 * std::max(0.0, r)));
    // Neoprene tips graded from soft in the bass to hard at the top; Hardness
    // halves or doubles the contact time.
    const double hardnessAmount = std::clamp(static_cast<double>(values[hardness]) * 0.01, 0.0, 1.0);
    m.contactSeconds = fromTable(contactTimes, note) * std::exp2((0.5 - hardnessAmount) * 2.0);
    m.swing = fromTable(swings, note);
    // The strike's pulse rolls off its own fundamental on the top notes, and a
    // wider swing is louder: both are evened out, so the keyboard plays at one
    // level and only the timbre follows the register.
    const double ws = twoPi * m.hz * pulseWidth * m.contactSeconds;
    const double rolloff = std::exp(-0.5 * ws * ws);
    m.levelTrim = 1.0 / (m.swing * rolloff);
    return m;
}

double pickupFlux(double x, float barkAmount, float symmetryAmount) noexcept
{
    return Pickup::forSettings(barkAmount, symmetryAmount).flux(x);
}

double strikeSpeed(int midiVelocity, float velocityAmount) noexcept
{
    const double v = std::clamp(midiVelocity, 1, 127) / 127.0;
    const double curve = 0.3 + 1.7 * std::clamp(static_cast<double>(velocityAmount), 0.0, 1.0);
    return std::max(0.02, std::pow(v, curve));
}

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("lumen::create");
    return std::make_unique<Lumen>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    using P = engine::ParameterInfo;
    std::vector<P> p(count);
    p[hardness] = {"Hardness", "%", 0.0f, 100.0f, 50.0f, false, {}};
    p[velocity] = {"Velocity", "%", 0.0f, 100.0f, 30.0f, false, {}};
    p[noise] = {"Noise", "%", 0.0f, 100.0f, 30.0f, false, {}};
    p[bell] = {"Bell", "%", 0.0f, 100.0f, 50.0f, false, {}};
    p[decay] = {"Decay", "%", 25.0f, 400.0f, 100.0f, true, {}};
    p[damper] = {"Damper", "%", 0.0f, 100.0f, 50.0f, false, {}};
    p[age] = {"Age", "%", 0.0f, 100.0f, 15.0f, false, {}};
    p[bark] = {"Bark", "%", 0.0f, 100.0f, 40.0f, false, {}};
    p[symmetry] = {"Symmetry", "%", 0.0f, 100.0f, 45.0f, false, {}};
    p[bass] = {"Bass", "dB", -12.0f, 12.0f, 0.0f, false, {}};
    p[treble] = {"Treble", "dB", -12.0f, 12.0f, 0.0f, false, {}};
    p[drive] = {"Drive", "%", 0.0f, 100.0f, 15.0f, false, {}};
    p[tremoloMode] = choice("Tremolo", {"Off", "Pan", "Amplitude"}, 1.0f);
    p[tremoloRate] = {"Tremolo rate", "Hz", 0.5f, 12.0f, 4.5f, true, {}};
    p[tremoloSync] = {
        "Tremolo sync", "", 0.0f, static_cast<float>(sync::choiceCount - 1), 0.0f, false, sync::choices()};
    p[tremoloDepth] = {"Tremolo depth", "%", 0.0f, 100.0f, 35.0f, false, {}};
    p[tremoloShape] = {"Tremolo shape", "%", 0.0f, 100.0f, 30.0f, false, {}};
    p[phaserRate] = {"Phaser rate", "Hz", 0.05f, 5.0f, 0.4f, true, {}};
    p[phaserDepth] = {"Phaser depth", "%", 0.0f, 100.0f, 60.0f, false, {}};
    p[phaserFeedback] = {"Phaser feedback", "%", 0.0f, 90.0f, 40.0f, false, {}};
    p[phaserMix] = {"Phaser mix", "%", 0.0f, 100.0f, 0.0f, false, {}};
    p[chorusRate] = {"Chorus rate", "Hz", 0.1f, 5.0f, 0.6f, true, {}};
    p[chorusDepth] = {"Chorus depth", "%", 0.0f, 100.0f, 40.0f, false, {}};
    p[chorusMix] = {"Chorus mix", "%", 0.0f, 100.0f, 0.0f, false, {}};
    p[voices] = {"Voices", "", 8.0f, static_cast<float>(maxVoices), 32.0f, false, {}};
    p[tune] = {"Tune", "Hz", 430.0f, 450.0f, 440.0f, false, {}};
    p[level] = {"Level", "dB", -60.0f, 6.0f, -3.0f, false, {}};
    return p;
}

const std::vector<EffectPreset>& presets()
{
    static const std::vector<EffectPreset> list = []
    {
        PresetBuilder made(type, parameterInfo());
        made.add("Lumen", {});
        made.add("Ballad",
                 {{hardness, 25.0},
                  {velocity, 40.0},
                  {bell, 30.0},
                  {decay, 140.0},
                  {damper, 35.0},
                  {bark, 20.0},
                  {symmetry, 65.0},
                  {treble, -4.0},
                  {drive, 5.0},
                  {tremoloDepth, 25.0},
                  {tremoloRate, 3.5}});
        made.add("Bark",
                 {{hardness, 65.0},
                  {velocity, 75.0},
                  {bark, 85.0},
                  {symmetry, 40.0},
                  {bass, 2.0},
                  {drive, 55.0},
                  {tremoloMode, 0.0}});
        made.add("Bell Keys",
                 {{hardness, 90.0},
                  {bell, 90.0},
                  {bark, 25.0},
                  {symmetry, 70.0},
                  {treble, 5.0},
                  {drive, 5.0},
                  {tremoloMode, 0.0},
                  {chorusMix, 45.0},
                  {chorusDepth, 35.0}});
        made.add("Phase Funk",
                 {{hardness, 60.0},
                  {bark, 60.0},
                  {drive, 30.0},
                  {tremoloMode, 0.0},
                  {phaserMix, 100.0},
                  {phaserRate, 0.6},
                  {phaserFeedback, 55.0}});
        made.add(
            "Suitcase Pan",
            {{bark, 45.0}, {drive, 20.0}, {tremoloDepth, 85.0}, {tremoloRate, 5.5}, {tremoloShape, 55.0}});
        made.add("Lo-fi Keys",
                 {{hardness, 35.0},
                  {noise, 55.0},
                  {age, 80.0},
                  {decay, 70.0},
                  {treble, -7.0},
                  {drive, 25.0},
                  {tremoloMode, 2.0},
                  {tremoloDepth, 30.0},
                  {chorusMix, 60.0},
                  {chorusRate, 0.4},
                  {chorusDepth, 70.0}});
        return made.done();
    }();
    return list;
}

} // namespace northlight::plugins::lumen
