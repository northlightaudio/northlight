#include "plugins/vesper/HaloBank.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/FeedbackNetwork.h"
#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::vesper
{

void HaloBank::prepare(double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HaloBank::prepare");
    sampleRate_ = sampleRate;
    const int size = fdn::powerOfTwoAtLeast(sampleRate / vocal::lowestHz + 8.0);
    for (auto& v : voices_)
        v.buffer.assign(static_cast<size_t>(size), 0.0f);
    mask_ = size - 1;
    lowStep_ = fdn::onePole(dampingHz, sampleRate);
    loopLag_ = (1.0f - lowStep_) / lowStep_; // the low-pass's delay at low frequencies, samples
    fadeStep_ = static_cast<float>(1.0 / (retuneSeconds * sampleRate));
    steadyFrames_ = static_cast<int>(std::lround(steadySeconds / vocal::frameSeconds));
    centreStep_ = static_cast<float>(1.0 - std::exp(-vocal::frameSeconds / centreSeconds));
    envelopeStep_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.05 * sampleRate));
    clear();
}

void HaloBank::clear() noexcept
{
    for (auto& v : voices_)
    {
        std::fill(v.buffer.begin(), v.buffer.end(), 0.0f);
        v.hz = v.pendingHz = 0.0f;
        v.delay = 1.0f;
        v.feedback = v.drive = v.gain = v.low = v.energy = 0.0f;
        v.lastHeard = 0;
    }
    write_ = 0;
    candidateHz_ = centre_ = last_ = 0.0f;
    steady_ = 0;
    current_ = -1;
    clock_ = 0;
}

void HaloBank::setDecay(float seconds) noexcept
{
    decaySeconds_ = std::max(0.1f, seconds);
}

void HaloBank::hear(const vocal::Hearing& h) noexcept
{
    ++clock_;
    if (h.sound != vocal::Sound::voiced || !(h.pitchHz > 0.0f))
    {
        steady_ = 0;
        current_ = -1;
        return;
    }
    // The centre is the note's average pitch in octaves so far, then over its
    // last 300 ms or so; a jump or a glide away from it starts a new note.
    const float octaves = std::log2(h.pitchHz);
    if (candidateHz_ > 0.0f && std::abs(1200.0f * (octaves - last_)) < jumpCents &&
        std::abs(1200.0f * (octaves - centre_)) < glideCents)
    {
        steady_ = std::min(steady_ + 1, 1 << 20);
        centre_ += (octaves - centre_) * std::max(centreStep_, 1.0f / static_cast<float>(steady_ + 1));
    }
    else
    {
        centre_ = octaves;
        steady_ = 0;
        current_ = -1;
    }
    last_ = octaves;
    candidateHz_ = std::exp2(centre_);
    if (steady_ < steadyFrames_)
        return;
    if (current_ >= 0)
    {
        auto& v = voices_[static_cast<size_t>(current_)];
        (v.pendingHz > 0.0f ? v.pendingHz : v.hz) = candidateHz_;
        v.lastHeard = clock_;
        return;
    }
    for (size_t i = 0; i < voices_.size(); ++i)
    {
        auto& v = voices_[i];
        const float held = v.pendingHz > 0.0f ? v.pendingHz : v.hz;
        if (held > 0.0f && std::abs(cents(candidateHz_, held)) < sameNoteCents)
        {
            current_ = static_cast<int>(i);
            v.lastHeard = clock_;
            return;
        }
    }
    // A free string, else the quietest (the longest unheard of equals).
    size_t pick = 0;
    for (size_t i = 0; i < voices_.size(); ++i)
    {
        const auto& v = voices_[i];
        const auto& best = voices_[pick];
        if (v.hz <= 0.0f && v.pendingHz <= 0.0f)
        {
            pick = i;
            break;
        }
        if (v.energy < best.energy || (v.energy == best.energy && v.lastHeard < best.lastHeard))
            pick = i;
    }
    voices_[pick].pendingHz = candidateHz_;
    voices_[pick].lastHeard = clock_;
    current_ = static_cast<int>(pick);
}

float HaloBank::process(float x) noexcept
{
    float out = 0.0f;
    for (auto& v : voices_)
    {
        // A string to retune fades out, then starts again silent at its new note.
        if (v.pendingHz > 0.0f)
        {
            v.gain = std::max(0.0f, v.gain - fadeStep_);
            if (v.gain <= 0.0f)
                retune(v);
        }
        else if (v.hz > 0.0f && v.gain < 1.0f)
            v.gain = std::min(1.0f, v.gain + fadeStep_);
        if (v.hz <= 0.0f)
            continue;
        const float read = dsp::readLinear(v.buffer.data(), mask_, write_, v.delay);
        v.low += lowStep_ * (read - v.low);
        const float y = x * v.drive * v.gain + v.feedback * v.low;
        v.buffer[static_cast<size_t>(write_)] = y;
        const float sound = v.low * v.gain;
        v.energy += (sound * sound - v.energy) * envelopeStep_;
        out += sound;
    }
    write_ = (write_ + 1) & mask_;
    return out;
}

bool HaloBank::ringing() const noexcept
{
    for (const auto& v : voices_)
        if (v.pendingHz > 0.0f || (v.hz > 0.0f && v.energy > 1.0e-12f))
            return true;
    return false;
}

std::array<float, HaloBank::voiceCount> HaloBank::notes() const noexcept
{
    std::array<float, voiceCount> n{};
    for (size_t i = 0; i < n.size(); ++i)
        n[i] = voices_[i].hz;
    return n;
}

void HaloBank::tune() noexcept
{
    for (auto& v : voices_)
        if (v.hz > 0.0f)
            setLoop(v);
}

void HaloBank::flushDenormals() noexcept
{
    for (auto& v : voices_)
    {
        v.low = dsp::flush(v.low, 1.0e-20f);
        if (v.energy < 1.0e-20f)
            v.energy = 0.0f;
    }
}

float HaloBank::cents(float a, float b) noexcept
{
    return 1200.0f * std::log2(a / b);
}

void HaloBank::retune(Voice& v) noexcept
{
    std::fill(v.buffer.begin(), v.buffer.end(), 0.0f);
    v.hz = v.pendingHz;
    v.pendingHz = 0.0f;
    v.low = 0.0f;
    v.energy = 0.0f;
    setLoop(v);
}

void HaloBank::setLoop(Voice& v) noexcept
{
    const auto period = static_cast<float>(sampleRate_ / v.hz);
    v.delay = std::clamp(period - loopLag_, 2.0f, static_cast<float>(mask_ - 2));
    v.feedback = std::pow(10.0f, -3.0f * period / (static_cast<float>(sampleRate_) * decaySeconds_));
    // The comb's gain at its note is about 1 / (1 - feedback): driven by that much
    // less, a held matching note rings about as loud as it went in.
    v.drive = 1.0f - v.feedback;
}

} // namespace northlight::plugins::vesper
