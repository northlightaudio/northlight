#include "plugins/patina/Chamber.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Patina.h"
#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

namespace
{
int ms(double fs, double milliseconds) noexcept
{
    return std::max(1, static_cast<int>(milliseconds * 0.001 * fs));
}
} // namespace

// ----- the room -----------------------------------------------------------------------------------

void Chamber::Room::prepare(double fs)
{
    // Early reflections from an input line, then four lines mixed by a
    // Hadamard matrix with damping in each.
    for (auto& l : input)
        l.prepare(ms(fs, 60.0));
    static constexpr double tapsMs[2][6] = {{3.1, 7.3, 11.9, 17.1, 23.3, 31.7},
                                            {4.3, 8.9, 13.1, 19.7, 26.9, 35.3}};
    for (size_t ch = 0; ch < 2; ++ch)
        for (size_t k = 0; k < 6; ++k)
            tap[ch][k] = ms(fs, tapsMs[ch][k]);
    static constexpr double lengthsMs[] = {19.3, 23.9, 29.1, 34.7};
    for (size_t i = 0; i < 4; ++i)
    {
        length[i] = ms(fs, lengthsMs[i]);
        line[i].prepare(length[i] + 2);
    }
}

void Chamber::Room::reset() noexcept
{
    for (auto& l : input)
        l.clear();
    for (auto& l : line)
        l.clear();
    for (auto& f : damp)
        f = 0.0f;
    for (auto& f : box)
        f.reset();
}

void Chamber::Room::sample(float l, float r, float& wl, float& wr) noexcept
{
    input[0].push(l);
    input[1].push(r);
    // Early reflections: a small, boxy room, the walls answering
    // from alternate sides.
    static constexpr float tapGains[6] = {0.62f, -0.48f, 0.41f, -0.33f, 0.27f, -0.2f};
    float early[2] = {0.0f, 0.0f};
    for (size_t ch = 0; ch < 2; ++ch)
        for (size_t k = 0; k < 6; ++k)
            early[ch] += tapGains[k] * input[ch == 0 ? (k % 2) : 1 - (k % 2)].at(tap[ch][k]);
    float o[4], v[4];
    for (size_t i = 0; i < 4; ++i)
    {
        o[i] = line[i].at(length[i] - 1);
        damp[i] += (o[i] - damp[i]) * dampK;
        v[i] = damp[i] * gain[i];
    }
    // Hadamard: orthogonal, so the loop's decay is the lines' own.
    const float h0 = 0.5f * (v[0] + v[1] + v[2] + v[3]);
    const float h1 = 0.5f * (v[0] - v[1] + v[2] - v[3]);
    const float h2 = 0.5f * (v[0] + v[1] - v[2] - v[3]);
    const float h3 = 0.5f * (v[0] - v[1] - v[2] + v[3]);
    line[0].push(h0 + 0.5f * (l + early[0]));
    line[1].push(h1 + 0.5f * (r + early[1]));
    line[2].push(h2 + 0.5f * early[0]);
    line[3].push(h3 + 0.5f * early[1]);
    wl = box[0].process(0.7f * early[0] + 0.8f * (o[0] + o[2]));
    wr = box[1].process(0.7f * early[1] + 0.8f * (o[1] + o[3]));
}

// ----- the springs --------------------------------------------------------------------------------

void Chamber::Spring::prepare(double fs)
{
    // Two springs (one a side) of a dispersive allpass chain in a feedback loop.
    length[0] = ms(fs, 33.0);
    length[1] = ms(fs, 37.7);
    for (size_t ch = 0; ch < 2; ++ch)
        line[ch].prepare(length[ch] + 2);
}

void Chamber::Spring::reset() noexcept
{
    for (auto& l : line)
        l.clear();
    for (auto& c : chain)
        for (auto& s : c)
            s = {};
    for (auto& f : low)
        f.reset();
    for (auto& f : high)
        f.reset();
}

void Chamber::Spring::sample(float l, float r, float& wl, float& wr) noexcept
{
    const float mono = 0.5f * (l + r);
    float out[2];
    for (size_t ch = 0; ch < 2; ++ch)
    {
        float x = mono + gain[ch] * line[ch].at(length[ch] - 1);
        // The chain's dispersion: low frequencies take longer
        // round the loop than high ones, the spring's chirp.
        for (auto& s : chain[ch])
        {
            const float y = coefficient * x + s.x1 - coefficient * s.y1;
            s.x1 = x;
            s.y1 = dsp::flush(y, denormalFloor);
            x = y;
        }
        x = high[ch].process(x);
        line[ch].push(x);
        out[ch] = low[ch].process(x);
    }
    wl = out[0];
    wr = out[1];
}

// ----- the plate ----------------------------------------------------------------------------------

void Chamber::Plate::prepare(double fs)
{
    const auto scaled = [fs](double samplesAtDesignRate)
    { return std::max(1, static_cast<int>(samplesAtDesignRate * fs / designRate)); };
    // Dattorro's figure-of-eight tank.
    static constexpr double diffusers[] = {142.0, 107.0, 379.0, 277.0};
    for (size_t i = 0; i < 4; ++i)
        diffuser[i].prepare(scaled(diffusers[i]));
    const int swing = scaled(16.0) + 2;
    excursion = static_cast<float>(scaled(8.0));
    tankAp1[0].prepare(scaled(672.0), swing);
    tankAp1[1].prepare(scaled(908.0), swing);
    tankAp2[0].prepare(scaled(1800.0));
    tankAp2[1].prepare(scaled(2656.0));
    delay1Length = {scaled(4453.0), scaled(4217.0)};
    delay2Length = {scaled(3720.0), scaled(3163.0)};
    for (size_t i = 0; i < 2; ++i)
    {
        delay1[i].prepare(delay1Length[i]);
        delay2[i].prepare(delay2Length[i]);
    }
    static constexpr double taps[14] = {266.0,
                                        2974.0,
                                        1913.0,
                                        1996.0,
                                        1990.0,
                                        187.0,
                                        1066.0,
                                        353.0,
                                        3627.0,
                                        1228.0,
                                        2673.0,
                                        2111.0,
                                        335.0,
                                        121.0};
    for (size_t i = 0; i < 14; ++i)
        tap[i] = scaled(taps[i]);
    loopSeconds = (672.0 + 908.0 + 1800.0 + 2656.0 + 4453.0 + 4217.0 + 3720.0 + 3163.0) / designRate;
}

void Chamber::Plate::reset() noexcept
{
    for (auto& ap : diffuser)
        ap.line.clear();
    for (size_t i = 0; i < 2; ++i)
    {
        tankAp1[i].line.clear();
        tankAp2[i].line.clear();
        delay1[i].clear();
        delay2[i].clear();
        damp[i] = 0.0f;
    }
    bandwidth = 0.0f;
    phase = 0.0f;
}

void Chamber::Plate::sample(float l, float r, float& wl, float& wr, double fs) noexcept
{
    float x = 0.5f * (l + r);
    bandwidth += (x - bandwidth) * bandK;
    x = bandwidth;
    x = diffuser[0].process(x, 0.75f);
    x = diffuser[1].process(x, 0.75f);
    x = diffuser[2].process(x, 0.625f);
    x = diffuser[3].process(x, 0.625f);
    phase += twoPi * 1.0f / static_cast<float>(fs);
    if (phase > twoPi)
        phase -= twoPi;
    const float mods[2] = {excursion * (1.0f + std::sin(phase)), excursion * (1.0f + std::cos(phase))};
    // The figure of eight: each tank is fed from the other's end.
    const float feed[2] = {delay2[1].at(delay2Length[1] - 1), delay2[0].at(delay2Length[0] - 1)};
    for (size_t side = 0; side < 2; ++side)
    {
        float v = x + decay * feed[side];
        v = tankAp1[side].process(v, -0.7f, mods[side]);
        delay1[side].push(v);
        v = delay1[side].at(delay1Length[side] - 1);
        damp[side] += (v - damp[side]) * dampK;
        v = damp[side] * decay;
        v = tankAp2[side].process(v, 0.5f);
        delay2[side].push(v);
    }
    // Dattorro's output taps.
    const auto& d1L = delay1[0];
    const auto& d1R = delay1[1];
    const auto& d2L = delay2[0];
    const auto& d2R = delay2[1];
    const auto& a2L = tankAp2[0].line;
    const auto& a2R = tankAp2[1].line;
    const auto& t = tap;
    wl = 0.6f * (d1R.at(t[0]) + d1R.at(t[1]) - a2R.at(t[2]) + d2R.at(t[3]) - d1L.at(t[4]) - a2L.at(t[5]) -
                 d2L.at(t[6]));
    wr = 0.6f * (d1L.at(t[7]) + d1L.at(t[8]) - a2L.at(t[9]) + d2L.at(t[10]) - d1R.at(t[11]) - a2R.at(t[12]) -
                 d2R.at(t[13]));
}

// ----- the stage ----------------------------------------------------------------------------------

void Chamber::prepare(double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("patina::Chamber::prepare");
    fs_ = sampleRate;
    room_.prepare(sampleRate);
    spring_.prepare(sampleRate);
    plate_.prepare(sampleRate);
    reset();
}

void Chamber::reset() noexcept
{
    room_.reset();
    spring_.reset();
    plate_.reset();
    for (auto& f : in_)
        f.reset();
    for (auto& f : low_)
        f.reset();
    for (auto& f : high_)
        f.reset();
    designed_ = -1.0e9f;
}

float Chamber::wetGain() const noexcept
{
    switch (space_)
    {
        case spring:
            return 0.9f;
        case plate:
            return 1.1f;
        default:
            return 0.85f;
    }
}

void Chamber::controls(int space) noexcept
{
    space_ = space;
    const float rt = chamberDecaySeconds(space, decay);
    for (size_t i = 0; i < 4; ++i)
        room_.gain[i] =
            std::pow(10.0f, -3.0f * static_cast<float>(room_.length[i]) / (rt * static_cast<float>(fs_)));
    for (size_t ch = 0; ch < 2; ++ch)
        spring_.gain[ch] =
            std::pow(10.0f, -3.0f * static_cast<float>(spring_.length[ch]) / (rt * static_cast<float>(fs_)));
    plate_.decay = std::pow(10.0f, -0.75f * static_cast<float>(plate_.loopSeconds) / rt);
    // Damping in the loops follows the tone.
    const float brightness = 0.5f * (tone + 1.0f); // 0..1
    room_.dampK = onePoleCoefficient(fs_, 2500.0 * std::pow(4.0, brightness));
    plate_.dampK = onePoleCoefficient(fs_, 3000.0 * std::pow(4.0, brightness));
    plate_.bandK = onePoleCoefficient(fs_, 5000.0 * std::pow(3.0, brightness));
    if (std::abs(tone - designed_) > 0.002f)
    {
        designed_ = tone;
        const double lowpass = 2500.0 * std::pow(5.0, brightness);
        const double highpass = 260.0 * std::pow(0.4, brightness);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            in_[ch].setHighPass1(fs_, 120.0);
            low_[ch].setHighPass(fs_, highpass, 0.6);
            high_[ch].setLowPass(fs_, lowpass, 0.6);
            spring_.low[ch].setHighPass(fs_, 180.0, 0.7);
            spring_.high[ch].setLowPass(fs_, 3200.0 * std::pow(2.0, brightness), 0.7071);
            room_.box[ch].setPeaking(fs_, 650.0, 1.0, 3.0);
        }
    }
}

void Chamber::sample(float l, float r, float& wl, float& wr) noexcept
{
    constexpr float guard = 1.0e-18f; // keeps the tails out of denormals
    l = in_[0].process(l) + guard;
    r = in_[1].process(r) + guard;
    switch (space_)
    {
        case spring:
            spring_.sample(l, r, wl, wr);
            break;
        case plate:
            plate_.sample(l, r, wl, wr, fs_);
            break;
        default:
            room_.sample(l, r, wl, wr);
            break;
    }
    wl = high_[0].process(low_[0].process(wl));
    wr = high_[1].process(low_[1].process(wr));
}

} // namespace northlight::plugins::patina
