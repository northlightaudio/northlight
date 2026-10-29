#include "plugins/Cirrus.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Oversampler.h"
#include "plugins/ParamHelpers.h"
#include "plugins/SynthVoices.h"
#include "plugins/dsp/Denormals.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"
#include "plugins/dsp/Svf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace northlight::plugins::cirrus
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr int controlInterval = 16; // base-rate samples between pitch, LFO and level updates
constexpr int ops = fm::operatorCount;
constexpr float voiceGain = 0.3f;

using synth::Envelope;

struct Voice
{
    int note = -1;
    float velocity = 0.0f; // 0..1
    std::uint32_t age = 0;
    double frequency = 440.0, target = 440.0; // Hz (gliding towards target in mono)
    float pitchEnvelope = 0.0f;               // 1 at the note's start, falling to 0
    std::array<std::uint32_t, ops> phase{}, step{};
    std::array<Envelope, ops> envelope{};
    // Each operator's level (velocity, key scale, macros, LFO), stepped towards
    // its next control value across the interval so nothing zips.
    std::array<float, ops> amp{}, ampStep{};
    float feedback1 = 0.0f, feedback2 = 0.0f; // operator 4's last two outputs
    bool released = false;

    bool active() const noexcept { return note >= 0; }
};

class Cirrus final : public MeteredBuiltIn
{
public:
    // The output tap feeds the editor's spectrum.
    Cirrus() : MeteredBuiltIn(type, cirrus::parameterInfo(), true) {}

    bool acceptsMidi() const noexcept override { return true; }

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        age_ = 0;
        held_.fill(-1);
        heldCount_ = 0;
        lastVelocity_ = 100;
        lfoPhase_ = 0.0;
        lfoHeld_ = lfo_ = 0.0f;
        controlCountdown_ = 0;
        expression_.reset();
        expressionRatio_ = 1.0;
        for (auto& f : highCutFilter_)
            f.reset();
        factor_ = 0; // set on the first block
    }

    static double noteHz(double note) noexcept { return 440.0 * std::exp2((note - 69.0) / 12.0); }

    bool mono() const noexcept { return parameter(voiceMode) >= 0.5f; }

    // Starts a voice on a note, or (legato, in mono) moves it there keeping its
    // envelopes.
    void startVoice(Voice& v, int note, int velocityValue, bool legato) noexcept
    {
        v.velocity = static_cast<float>(velocityValue) / 127.0f;
        v.target = noteHz(note);
        v.age = ++age_;
        if (legato && parameter(glide) > 0.5f)
        {
            v.note = note; // glide there from where it is
            return;
        }
        v.frequency = v.target;
        if (legato)
        {
            v.note = note;
            return;
        }
        if (!v.active())
        {
            // A fresh voice starts every operator at phase 0, so each note's attack
            // is the same.
            v.phase.fill(0u);
            v.feedback1 = v.feedback2 = 0.0f;
            for (auto& e : v.envelope)
                e.value = 0.0f;
        }
        v.note = note;
        v.released = false;
        v.pitchEnvelope = 1.0f;
        for (auto& e : v.envelope)
            e.start();
        // Its pitch and levels now, so its first sample is right; the next
        // control update moves on from there.
        updateVoice(v, false, 1);
    }

    void noteOn(int note, int velocityValue) noexcept
    {
        // The held notes, newest last (for mono's note priority).
        if (heldCount_ < static_cast<int>(held_.size()))
            held_[static_cast<size_t>(heldCount_++)] = note;
        lastVelocity_ = velocityValue;
        if (mono())
        {
            auto& v = voices_[0];
            const bool legato = v.active() && !v.released && heldCount_ > 1;
            startVoice(v, note, velocityValue, legato);
            return;
        }
        Voice& v = synth::allocateVoice(voices_.data(), voiceCount, note);
        startVoice(v, note, velocityValue, false);
    }

    void releaseVoice(Voice& v) noexcept
    {
        v.released = true;
        for (auto& e : v.envelope)
            e.release();
    }

    void noteOff(int note) noexcept
    {
        int write = 0;
        for (int i = 0; i < heldCount_; ++i)
            if (held_[static_cast<size_t>(i)] != note)
                held_[static_cast<size_t>(write++)] = held_[static_cast<size_t>(i)];
        heldCount_ = write;
        if (mono())
        {
            auto& v = voices_[0];
            if (v.note != note)
                return;
            if (heldCount_ > 0)
                startVoice(v, held_[static_cast<size_t>(heldCount_ - 1)], lastVelocity_, true);
            else
                releaseVoice(v);
            return;
        }
        for (auto& v : voices_)
            if (v.note == note && !v.released)
                releaseVoice(v);
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        synth::dispatch(
            e,
            expression_,
            [this](int note, int velocityValue) { noteOn(note, velocityValue); },
            [this](int note) { noteOff(note); },
            [this]
            {
                heldCount_ = 0;
                for (auto& v : voices_)
                    releaseVoice(v);
            });
    }

    float lfoValue(int shape) const noexcept
    {
        const double p = lfoPhase_;
        switch (shape)
        {
            case 0:
                return static_cast<float>(std::sin(twoPi * p));
            case 1:
                return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case 2:
                return static_cast<float>(2.0 * p - 1.0);
            case 3:
                return p < 0.5 ? 1.0f : -1.0f;
            default:
                return lfoHeld_;
        }
    }

    // What a chunk reads once: the parameters in the form the voices use.
    struct Settings
    {
        fm::Algorithm algorithm;
        std::array<const std::array<float, fm::tableSize + 1>*, ops> table{};
        std::array<double, ops> multiple{}; // of the note's frequency (0 when fixed)
        std::array<double, ops> fixedHz{};  // when fixed
        std::array<float, ops> levelGain{}, velocity{}, keyScale{};
        std::array<float, ops> attackStep{}, decayCoef{}, sustain{}, releaseCoef{};
        std::array<float, ops> scale{}; // modulators: cycles at full; carriers: their share
        std::array<float, ops> panL{}, panR{};
        float feedback = 0.0f;
        float toneGain = 1.0f, lfoTone = 0.0f;
        float pitchEnvelope = 0.0f, pitchCoef = 0.0f; // semitones; per control interval
        double glideCoef = 0.0;                       // per control interval
        float lfoPitch = 0.0f;
        int lfoShape = 0;
        double lfoStep = 0.0; // cycles per base-rate sample
    };

    Settings read(double rate) noexcept
    {
        Settings s;
        const int alg =
            std::clamp(static_cast<int>(std::lround(parameter(algorithm))), 0, fm::algorithmCount - 1);
        s.algorithm = fm::algorithms[static_cast<size_t>(alg)];
        const float timeScale = std::exp2(2.0f * parameter(time) * 0.01f);
        const float ratePerMs = static_cast<float>(rate) * 0.001f;
        const int carriers = std::max(1, s.algorithm.carrierCount());
        const float widthAmount = parameter(width) * 0.01f;
        int carrierIndex = 0;
        const auto& tables = fm::waveTables();
        for (int op = 0; op < ops; ++op)
        {
            const auto o = static_cast<size_t>(op);
            const auto p = [&](OpField f) { return parameter(opParam(op, f)); };
            const int waveIndex = std::clamp(static_cast<int>(std::lround(p(wave))), 0, fm::waveCount - 1);
            s.table[o] = &tables[static_cast<size_t>(waveIndex)];
            const double detuneRatio = std::exp2(static_cast<double>(p(detune)) / 1200.0);
            if (p(fixed) >= 0.5f)
            {
                s.multiple[o] = 0.0;
                s.fixedHz[o] = p(fixedHz) * detuneRatio;
            }
            else
                s.multiple[o] = ratioOf(p(ratio)) * (1.0 + p(fine) * 0.01) * detuneRatio;
            s.levelGain[o] = fm::levelGain(p(level));
            s.velocity[o] = p(velocity);
            s.keyScale[o] = p(keyScale);
            s.attackStep[o] = 1.0f / std::max(1.0f, p(attack) * timeScale * ratePerMs);
            s.decayCoef[o] = dsp::decay60ForMs(p(decay) * timeScale, static_cast<float>(rate));
            s.sustain[o] = p(sustain);
            s.releaseCoef[o] = dsp::decay60ForMs(p(release) * timeScale, static_cast<float>(rate));
            s.scale[o] = s.algorithm.isCarrier(op) ? 1.0f / static_cast<float>(carriers) : fm::maxIndexCycles;
            // The carriers spread evenly from left to right in operator order (one
            // alone stays centred).
            float pan = 0.0f;
            if (carriers > 1 && s.algorithm.isCarrier(op))
            {
                const float position = static_cast<float>(carrierIndex++) / static_cast<float>(carriers - 1);
                pan = (2.0f * position - 1.0f) * widthAmount;
            }
            s.panL[o] = std::sqrt(0.5f * (1.0f - pan));
            s.panR[o] = std::sqrt(0.5f * (1.0f + pan));
        }
        s.feedback = parameter(cirrus::feedback) * fm::maxFeedbackCycles;
        s.toneGain = std::exp2(2.0f * parameter(tone) * 0.01f);
        s.lfoTone = parameter(lfoTone);
        s.pitchEnvelope = parameter(pitchEnvelope);
        const float controlRate = static_cast<float>(sampleRate_) / static_cast<float>(controlInterval);
        s.pitchCoef = dsp::decay60ForMs(parameter(pitchTime) * timeScale, controlRate);
        const float glideMs = parameter(glide);
        s.glideCoef = mono() && glideMs > 0.5f
                          ? std::exp(-1.0 / (static_cast<double>(glideMs) * 0.001 * controlRate))
                          : 0.0;
        s.lfoPitch = parameter(lfoPitch);
        s.lfoShape = std::clamp(static_cast<int>(std::lround(parameter(lfoShape))), 0, 4);
        const double syncSeconds = sync::seconds(parameter(lfoSync), playHead().tempoBpm);
        const double hz = sync::quarters(parameter(lfoSync)) > 0.0 && syncSeconds > 0.0 ? 1.0 / syncSeconds
                                                                                        : parameter(lfoRate);
        s.lfoStep = hz / sampleRate_;
        return s;
    }

    // Once per control interval: the LFO and the player's expression, then each
    // voice's pitch and its operators' levels, which the interval's samples step
    // towards.
    void control(int samplesAhead) noexcept
    {
        lfo_ = lfoValue(s_.lfoShape);
        expressionRatio_ = synth::Expression::ratio(expression_.advance(controlInterval / sampleRate_));
        for (auto& v : voices_)
            if (v.active())
                updateVoice(v, true, samplesAhead);
    }

    // A voice's operator steps and levels. `advance` moves its glide and pitch
    // envelope on by an interval; without it (a note starting) the levels are
    // set at once instead of stepped to.
    void updateVoice(Voice& v, bool advance, int samplesAhead) noexcept
    {
        const Settings& s = s_;
        const double lfoRatio = s.lfoPitch != 0.0f ? std::exp2(lfo_ * s.lfoPitch / 1200.0) : 1.0;
        const float modulatorGain = s.toneGain * std::exp2(2.0f * lfo_ * s.lfoTone);
        // Sidebands past this are faded out before they fold back (Carson's rule:
        // a modulation reaches about (index + 1) times the modulator's frequency).
        const double ceiling = 0.45 * rate_;
        if (advance)
        {
            if (s.glideCoef > 0.0)
                v.frequency = v.target + (v.frequency - v.target) * s.glideCoef;
            else
                v.frequency = v.target;
        }
        const double semis = static_cast<double>(s.pitchEnvelope) * v.pitchEnvelope;
        if (advance)
            v.pitchEnvelope = dsp::flush(v.pitchEnvelope * s.pitchCoef, 1.0e-6f);
        const double hz =
            v.frequency * lfoRatio * expressionRatio_ * (semis != 0.0 ? std::exp2(semis / 12.0) : 1.0);
        std::array<double, ops> opHz{};
        for (int op = 0; op < ops; ++op)
        {
            const auto o = static_cast<size_t>(op);
            opHz[o] = s.multiple[o] > 0.0 ? hz * s.multiple[o] : s.fixedHz[o];
            v.step[o] = fm::phaseStep(std::min(opHz[o], ceiling) / rate_);
        }
        const float octavesAboveC4 = std::max(0.0f, static_cast<float>(v.note - 60) / 12.0f);
        const float stepScale = 1.0f / static_cast<float>(std::max(1, samplesAhead));
        for (int op = 0; op < ops; ++op)
        {
            const auto o = static_cast<size_t>(op);
            const float velocityScale = 1.0f - s.velocity[o] + s.velocity[o] * v.velocity;
            // Key scale: -1 falls 6 dB an octave above C4, 1 rises as much.
            const float keyGain = std::exp2(s.keyScale[o] * octavesAboveC4);
            float gain = s.levelGain[o] * velocityScale * keyGain;
            if (!s.algorithm.isCarrier(op))
            {
                gain *= modulatorGain;
                // The highest target this operator modulates.
                double targetHz = 0.0;
                for (int t = 0; t < ops; ++t)
                    if (((s.algorithm.modulators[static_cast<size_t>(t)] >> op) & 1u) != 0)
                        targetHz = std::max(targetHz, opHz[static_cast<size_t>(t)]);
                const double index = twoPi * gain * s.scale[o];
                if (opHz[o] > 0.0 && index > 0.0)
                {
                    const double allowed = (ceiling - targetHz) / opHz[o] - 1.0;
                    if (allowed <= 0.0)
                        gain = 0.0f;
                    else if (index > allowed)
                        gain *= static_cast<float>(allowed / index);
                }
            }
            gain *= s.scale[o];
            if (advance)
                v.ampStep[o] = (gain - v.amp[o]) * stepScale;
            else
            {
                v.amp[o] = gain;
                v.ampStep[o] = 0.0f;
            }
        }
    }

    // One sample of one voice at the running rate.
    void renderVoice(Voice& v, float& left, float& right) const noexcept
    {
        const Settings& s = s_;
        std::array<float, ops> out{};
        bool sounding = false;
        for (int op = ops - 1; op >= 0; --op)
        {
            const auto o = static_cast<size_t>(op);
            float modulation = 0.0f;
            const std::uint8_t mask = s.algorithm.modulators[o];
            for (int m = op + 1; m < ops; ++m)
                if (((mask >> m) & 1u) != 0)
                    modulation += out[static_cast<size_t>(m)];
            if (op == ops - 1 && s.feedback > 0.0f)
                modulation += s.feedback * 0.5f * (v.feedback1 + v.feedback2);
            const float env =
                v.envelope[o].step(s.attackStep[o], s.decayCoef[o], s.sustain[o], s.releaseCoef[o]);
            v.amp[o] += v.ampStep[o];
            const float value = fm::lookup(*s.table[o], v.phase[o] + fm::phaseOffset(modulation));
            v.phase[o] += v.step[o];
            out[o] = value * env * v.amp[o];
            if (op == ops - 1)
            {
                v.feedback2 = v.feedback1;
                v.feedback1 = value * env; // its own wave, before the level
            }
            if (s.algorithm.isCarrier(op))
            {
                left += out[o] * s.panL[o];
                right += out[o] * s.panR[o];
                sounding = sounding || v.envelope[o].active();
            }
        }
        if (!sounding)
            v.note = -1;
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const int wanted = parameter(quality) >= 0.5f ? 2 : 1;
        if (wanted != factor_)
        {
            factor_ = wanted;
            for (auto& o : oversampler_)
                o.setFactor(factor_);
        }
        rate_ = sampleRate_ * factor_;
        s_ = read(rate_);
        const float cutoff = std::min(parameter(highCut), static_cast<float>(sampleRate_) * 0.45f);
        const float g =
            std::tan(static_cast<float>(std::numbers::pi) * cutoff / static_cast<float>(sampleRate_));
        constexpr float butterworthK = 1.41421356f;
        const float outGain = dbToGain(parameter(outputLevel)) * voiceGain;

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        std::array<float, Oversampler::maxFactor> upL{}, upR{};
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);

            if (controlCountdown_ <= 0)
            {
                controlCountdown_ = controlInterval;
                control(controlInterval * factor_);
            }
            --controlCountdown_;
            lfoPhase_ += s_.lfoStep;
            if (lfoPhase_ >= 1.0)
            {
                lfoPhase_ -= 1.0;
                lfoHeld_ = rng_.bipolar();
            }

            for (int k = 0; k < factor_; ++k)
            {
                float l = 0.0f, r = 0.0f;
                for (auto& v : voices_)
                    if (v.active())
                        renderVoice(v, l, r);
                upL[static_cast<size_t>(k)] = l;
                upR[static_cast<size_t>(k)] = r;
            }
            float outL = factor_ > 1 ? oversampler_[0].down(upL.data()) : upL[0];
            float outR = factor_ > 1 ? oversampler_[1].down(upR.data()) : upR[0];

            float low, band, high;
            highCutFilter_[0].run(outL, g, butterworthK, low, band, high);
            outL = low;
            highCutFilter_[1].run(outR, g, butterworthK, low, band, high);
            outR = low;
            left[i] = outL * outGain;
            right[i] = outR * outGain;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);

        for (auto& o : oversampler_)
            o.flush();
        for (auto& f : highCutFilter_)
        {
            f.ic1 = dsp::flush(f.ic1, 1.0e-15f);
            f.ic2 = dsp::flush(f.ic2, 1.0e-15f);
        }
        int active = 0;
        for (const auto& v : voices_)
            active += v.active() ? 1 : 0;
        // For the editor: how many voices sound (the frame's "reduction" slot)
        // and the LFO, 0..1 (its extra).
        blockReduction_ = static_cast<float>(active);
        blockExtra_ = 0.5f * (lfo_ + 1.0f) + 1.0e-3f;
    }

    std::array<Voice, voiceCount> voices_{};
    std::uint32_t age_ = 0;
    std::array<int, 16> held_{};
    int heldCount_ = 0, lastVelocity_ = 100;
    Settings s_;            // this chunk's parameters
    double rate_ = 48000.0; // the voices' rate: the sample rate times the oversampling
    dsp::Lcg rng_{0x51C0FFEEu};
    synth::Expression expression_;
    double expressionRatio_ = 1.0;
    double lfoPhase_ = 0.0;
    float lfoHeld_ = 0.0f, lfo_ = 0.0f;
    int controlCountdown_ = 0;
    int factor_ = 0;
    std::array<Oversampler, 2> oversampler_{};
    std::array<dsp::Svf, 2> highCutFilter_{};
};

const std::vector<std::string>& waveNames()
{
    static const std::vector<std::string> names{
        "Sine", "Half", "Rectified", "Quarters", "Double", "Double half", "Soft square", "Triangle"};
    return names;
}

std::vector<std::string> ratioNames()
{
    std::vector<std::string> names{"0.5"};
    for (int i = 1; i <= 32; ++i)
        names.push_back(std::to_string(i));
    return names;
}
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("cirrus::create");
    return std::make_unique<Cirrus>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    using P = engine::ParameterInfo;
    std::vector<P> p(count);
    // The default sound: algorithm 5, two stacks. 2 into 1 is the body of a
    // clean electric piano, 4 into 3 its tine.
    struct OpDefaults
    {
        float ratio, level, attack, decay, sustain, release, velocity, keyScale;
    };
    const std::array<OpDefaults, ops> defaults{{
        {1.0f, 99.0f, 2.0f, 2200.0f, 0.25f, 500.0f, 0.3f, 0.0f},
        {1.0f, 74.0f, 2.0f, 1100.0f, 0.15f, 500.0f, 0.8f, -0.3f},
        {1.0f, 82.0f, 1.0f, 600.0f, 0.0f, 300.0f, 0.4f, 0.0f},
        {14.0f, 58.0f, 1.0f, 180.0f, 0.0f, 150.0f, 0.9f, -0.5f},
    }};
    for (int op = 0; op < ops; ++op)
    {
        const auto& d = defaults[static_cast<size_t>(op)];
        const std::string n = "Op " + std::to_string(op + 1) + " ";
        p[static_cast<size_t>(opParam(op, ratio))] = choice(n + "ratio", ratioNames(), d.ratio);
        p[static_cast<size_t>(opParam(op, fine))] = {n + "fine", "%", 0.0f, 99.0f, 0.0f, false, {}};
        p[static_cast<size_t>(opParam(op, detune))] = {n + "detune", "ct", -20.0f, 20.0f, 0.0f, false, {}};
        p[static_cast<size_t>(opParam(op, fixed))] = onOff(n + "fixed", false);
        p[static_cast<size_t>(opParam(op, fixedHz))] = {
            n + "frequency", "Hz", 1.0f, 9750.0f, 440.0f, true, {}};
        p[static_cast<size_t>(opParam(op, wave))] = choice(n + "wave", waveNames(), 0.0f);
        p[static_cast<size_t>(opParam(op, level))] = {n + "level", "", 0.0f, 99.0f, d.level, false, {}};
        p[static_cast<size_t>(opParam(op, attack))] = {
            n + "attack", "ms", 1.0f, 10000.0f, d.attack, true, {}};
        p[static_cast<size_t>(opParam(op, decay))] = {n + "decay", "ms", 1.0f, 20000.0f, d.decay, true, {}};
        p[static_cast<size_t>(opParam(op, sustain))] = {n + "sustain", "", 0.0f, 1.0f, d.sustain, false, {}};
        p[static_cast<size_t>(opParam(op, release))] = {
            n + "release", "ms", 1.0f, 20000.0f, d.release, true, {}};
        p[static_cast<size_t>(opParam(op, velocity))] = {
            n + "velocity", "", 0.0f, 1.0f, d.velocity, false, {}};
        p[static_cast<size_t>(opParam(op, keyScale))] = {
            n + "key scale", "", -1.0f, 1.0f, d.keyScale, false, {}};
    }
    p[algorithm] = choice("Algorithm", {"1", "2", "3", "4", "5", "6", "7", "8"}, 4.0f);
    p[feedback] = {"Feedback", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[voiceMode] = choice("Voice mode", {"Poly", "Mono"}, 0.0f);
    p[glide] = {"Glide", "ms", 0.0f, 2000.0f, 0.0f, false, {}};
    p[pitchEnvelope] = {"Pitch envelope", "st", -24.0f, 24.0f, 0.0f, false, {}};
    p[pitchTime] = {"Pitch time", "ms", 1.0f, 5000.0f, 60.0f, true, {}};
    p[lfoShape] = choice("LFO shape", {"Sine", "Tri", "Saw", "Square", "Random"}, 0.0f);
    p[lfoRate] = {"LFO rate", "Hz", 0.05f, 20.0f, 4.5f, true, {}};
    p[lfoSync] = choice("LFO sync", sync::choices(), 0.0f);
    p[lfoPitch] = {"LFO pitch", "ct", 0.0f, 100.0f, 0.0f, false, {}};
    p[lfoTone] = {"LFO tone", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[tone] = percent("Tone", 0.0f, -100.0f, 100.0f);
    p[time] = percent("Time", 0.0f, -100.0f, 100.0f);
    p[highCut] = {"High cut", "Hz", 1000.0f, 20000.0f, 20000.0f, true, {}};
    p[width] = percent("Width", 40.0f);
    p[quality] = choice("Quality", {"Eco", "High"}, 1.0f);
    p[outputLevel] = {"Level", "dB", -60.0f, 6.0f, -3.0f, false, {}};
    return p;
}

const std::vector<EffectPreset>& presets()
{
    static const std::vector<EffectPreset> list = []
    {
        PresetBuilder made(type, parameterInfo());
        made.add("Cirrus", {});
        return made.done();
    }();
    return list;
}

} // namespace northlight::plugins::cirrus
