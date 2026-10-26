#include "plugins/patina/Heat.h"

#include "plugins/Patina.h"
#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

void Heat::prepare(double sampleRate) noexcept
{
    fs_ = sampleRate;
    reset();
}

void Heat::reset() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        pre_[ch].reset();
        post_[ch].reset();
        bump_[ch].reset();
        tiltLow_[ch].reset();
        tiltHigh_[ch].reset();
        previous_[ch] = 0.0;
        previousF_[ch].character = -1;
        dc_[ch] = dcIn_[ch] = 0.0f;
    }
    designed_ = -1;
}

void Heat::controls(int character, bool first) noexcept
{
    character_ = character;
    if (character != designed_)
    {
        designed_ = character;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            switch (character)
            {
                case tape:
                    pre_[ch].setHighShelf(fs_, 3000.0, 6.0);
                    post_[ch].setHighShelf(fs_, 3000.0, -6.0);
                    break;
                case iron:
                    pre_[ch].setLowShelf(fs_, 160.0, 9.0);
                    post_[ch].setLowShelf(fs_, 160.0, -9.0);
                    break;
                default:
                    pre_[ch].bypass();
                    post_[ch].bypass();
                    break;
            }
        }
    }
    // Drive and its trim glide across the sub-block.
    driveFrom_ = driveTo_;
    compFrom_ = compTo_;
    const double drive = std::pow(10.0, static_cast<double>(heatDriveDb(amount)) / 20.0);
    deadZone_ = heatcurve::fuzzDeadZone(amount);
    driveTo_ = static_cast<float>(drive);
    compTo_ = static_cast<float>(heatcurve::compensation(character, drive, deadZone_));
    if (first || driveFrom_ <= 0.0f)
    {
        driveFrom_ = driveTo_;
        compFrom_ = compTo_;
    }
    // Tape's head bump and the fuzz's top end follow the amount; the
    // tone is a tilt about 900 Hz.
    if (std::abs(amount - shaped_) > 0.002f || std::abs(tone - toneShaped_) > 0.002f || first)
    {
        shaped_ = amount;
        toneShaped_ = tone;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            if (character == tape)
                bump_[ch].setPeaking(fs_, 90.0, 1.0, 2.5 * amount);
            else if (character == fuzz)
                bump_[ch].setLowPass(fs_, 18000.0 * std::pow(5000.0 / 18000.0, amount), 0.6);
            else
                bump_[ch].bypass();
            tiltLow_[ch].setLowShelf(fs_, 900.0, -3.0 * tone);
            tiltHigh_[ch].setHighShelf(fs_, 900.0, 3.0 * tone);
        }
    }
}

float Heat::sample(size_t ch, float x, float t, Accumulator& acc) noexcept
{
    const int character = character_;
    const double drive = driveFrom_ + (driveTo_ - driveFrom_) * t;
    const double comp = compFrom_ + (compTo_ - compFrom_) * t;
    const double u = drive * pre_[ch].process(x);
    // ADAA1: the mean of the curve over the step from the previous input.
    const double previous = previous_[ch];
    const double du = u - previous;
    double y = 0.0;
    if (std::abs(du) > 1.0e-6)
    {
        // The integral at the previous sample was worked out then, unless the
        // curve (character, dead zone) changed since.
        auto& cached = previousF_[ch];
        const double fPrevious = cached.character == character && cached.deadZone == deadZone_
                                     ? cached.value
                                     : heatcurve::integral(character, previous, deadZone_);
        const double fu = heatcurve::integral(character, u, deadZone_);
        y = (fu - fPrevious) / du;
        cached = {fu, character, deadZone_};
    }
    else
    {
        y = heatcurve::curve(character, 0.5 * (u + previous), deadZone_);
        previousF_[ch].character = -1;
    }
    previous_[ch] = u;
    acc.heatDriven = std::max(acc.heatDriven, static_cast<float>(std::abs(u)));
    acc.heatShaped = std::max(acc.heatShaped, static_cast<float>(std::abs(y)));
    float out = static_cast<float>(y * comp);
    // A DC blocker: the tube's bias and the fuzz's asymmetry leave an offset.
    const float blocked = out - dcIn_[ch] + 0.9995f * dc_[ch];
    dcIn_[ch] = out;
    dc_[ch] = dsp::flush(blocked, denormalFloor);
    out = post_[ch].process(blocked);
    out = bump_[ch].process(out);
    return tiltHigh_[ch].process(tiltLow_[ch].process(out));
}

} // namespace northlight::plugins::patina
