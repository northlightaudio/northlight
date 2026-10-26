#include "engine/Gain.h"
#include "plugins/ChannelStrip.h"
#include "plugins/strip/Modules.h"

#include <algorithm>
#include <array>
#include <cmath>

// The channel strip's curves, shared by the processors (src/plugins/strip) and
// the editors, so what an editor draws is what runs.

namespace northlight::plugins::strip
{

namespace eq
{
bool isCut(int band) noexcept
{
    return band == lowCut || band == highCut;
}

int paramIndex(int band, Field field) noexcept
{
    if (band < 0 || band >= bandCount)
        return -1;
    if (band == lowCut)
        return field <= frequency ? static_cast<int>(field) : -1;
    if (band == highCut)
        return field <= frequency ? 23 + static_cast<int>(field) : -1;
    return 3 + (band - band1) * 5 + static_cast<int>(field);
}

namespace
{
// Butterworth section Qs for an even order (pairs of poles), and for an odd
// order the second-order sections after the first-order one.
void cutQs(int slopeDb, bool& firstOrder, double* qs, int& sections) noexcept
{
    firstOrder = false;
    sections = 0;
    switch (slopeDb)
    {
        case 6:
            firstOrder = true;
            break;
        case 12:
            qs[sections++] = 0.70711;
            break;
        case 18:
            firstOrder = true;
            qs[sections++] = 1.0;
            break;
        case 24:
            qs[sections++] = 0.54120;
            qs[sections++] = 1.30656;
            break;
        case 36:
            qs[sections++] = 0.51764;
            qs[sections++] = 0.70711;
            qs[sections++] = 1.93185;
            break;
        default: // 48
            qs[sections++] = 0.50980;
            qs[sections++] = 0.60134;
            qs[sections++] = 0.89998;
            qs[sections++] = 2.56292;
            break;
    }
}

// The fixed slot layout: four for each cut, one for each parametric band.
constexpr int slotOf(int band) noexcept
{
    return band == lowCut ? 0 : band == highCut ? 8 : 4 + (band - band1);
}
} // namespace

int design(const float* params, double sampleRate, Biquad* out, int band) noexcept
{
    for (int s = 0; s < maxSections; ++s)
        out[s].bypass();
    int active = 0;
    for (int b = 0; b < bandCount; ++b)
    {
        if (band >= 0 && b != band)
            continue;
        if (params[paramIndex(b, on)] < 0.5f)
            continue;
        const double hz = params[paramIndex(b, frequency)];
        auto* slot = out + slotOf(b);
        if (isCut(b))
        {
            const int choice = std::clamp(static_cast<int>(std::lround(params[paramIndex(b, type)])),
                                          0,
                                          static_cast<int>(slopes.size()) - 1);
            bool firstOrder = false;
            double qs[4]{};
            int sections = 0;
            cutQs(slopes[static_cast<size_t>(choice)], firstOrder, qs, sections);
            int k = 0;
            if (firstOrder)
            {
                if (b == lowCut)
                    slot[k++].setHighPass1(sampleRate, hz);
                else
                    slot[k++].setLowPass1(sampleRate, hz);
            }
            for (int i = 0; i < sections; ++i)
            {
                if (b == lowCut)
                    slot[k++].setHighPass(sampleRate, hz, qs[i]);
                else
                    slot[k++].setLowPass(sampleRate, hz, qs[i]);
            }
            active += k;
            continue;
        }
        const double gainDb = params[paramIndex(b, gain)];
        const double q = params[paramIndex(b, Field::q)];
        switch (static_cast<int>(std::lround(params[paramIndex(b, type)])))
        {
            case lowShelf:
                slot->setLowShelf(sampleRate, hz, gainDb, q);
                break;
            case highShelf:
                slot->setHighShelf(sampleRate, hz, gainDb, q);
                break;
            case notch:
                slot->setNotch(sampleRate, hz, q);
                break;
            default:
                slot->setPeaking(sampleRate, hz, q, gainDb);
                break;
        }
        if (!slot->isIdentity())
            ++active;
    }
    return active;
}

double responseDb(const float* params, double sampleRate, double hz, int band) noexcept
{
    Biquad sections[maxSections];
    design(params, sampleRate, sections, band);
    double magnitude = 1.0;
    for (const auto& s : sections)
        if (!s.isIdentity())
            magnitude *= s.magnitude(hz, sampleRate);
    double db = engine::gainToDb(magnitude, engine::magnitudeFloorDb);
    if (band < 0)
        db += params[output];
    return db;
}
} // namespace eq

namespace compressor
{
// Not dsp::softKneeReductionDb (Glue's and Gravity's): the knee here includes
// its top edge, where the quadratic and the straight part agree only to within
// rounding, so sharing it would move the output there; and a knee of 0 is hard.
float gainReductionDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    const float over = levelDb - thresholdDb;
    if (kneeDb > 0.0f && std::abs(over) <= kneeDb * 0.5f)
    {
        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

float autoMakeupDb(float thresholdDb, float ratio) noexcept
{
    // Half the reduction a full-scale signal gets: loud passages stay about as
    // loud, quiet ones come up.
    return 0.5f * gainReductionDb(0.0f, thresholdDb, ratio, 0.0f);
}
} // namespace compressor

namespace opto
{
namespace
{
constexpr float kneeDb = 2.5f; // the softplus knee's width

float softplus(float x) noexcept
{
    const float t = x / kneeDb;
    return t > 20.0f ? x : kneeDb * std::log1p(std::exp(t));
}
} // namespace

float sidechainDriveDb(float peakReduction) noexcept
{
    // 0% hears the output 5 dB down (nothing a mix reaches compresses); 100%
    // lifts it 45 dB (peaks around -45 dBFS start to compress).
    return 0.5f * std::clamp(peakReduction, 0.0f, 100.0f) - 5.0f;
}

float loopGain(int mode) noexcept
{
    // About 3:1 compressing. Limiting, the loop settles in the knee, so a loop
    // gain of 12 gives about 10:1.
    return mode == 1 ? 12.0f : 2.0f;
}

float cellTargetDb(float detectorDb, float peakReduction, int mode) noexcept
{
    return loopGain(mode) * softplus(detectorDb + sidechainDriveDb(peakReduction));
}

float steadyReductionDb(float inputDb, float peakReduction, int mode) noexcept
{
    // The fixed point of r = target(input - r): r - target(input - r) rises
    // with r, so bisect it.
    float lo = 0.0f, hi = 96.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float r = 0.5f * (lo + hi);
        (r - cellTargetDb(inputDb - r, peakReduction, mode) > 0.0f ? hi : lo) = r;
    }
    return 0.5f * (lo + hi);
}

float slowShare(float memory) noexcept
{
    return 0.35f + 0.4f * std::clamp(memory, 0.0f, 1.0f);
}

float slowReleaseSeconds(float memory) noexcept
{
    return 0.4f + 3.6f * std::clamp(memory, 0.0f, 1.0f);
}
} // namespace opto

namespace deesser
{
float reductionDb(float bandDb, float thresholdDb, float rangeDb) noexcept
{
    // 5:1 above the threshold, capped at the range.
    return std::clamp((bandDb - thresholdDb) * 0.8f, 0.0f, rangeDb);
}
} // namespace deesser

namespace saturation
{
float shape(int type, float x) noexcept
{
    return static_cast<float>(curve(type, x));
}

float driveCompensationDb(float driveDb) noexcept
{
    return -0.5f * driveDb;
}
} // namespace saturation

} // namespace northlight::plugins::strip
