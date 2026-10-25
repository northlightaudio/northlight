#include "plugins/VocalListener.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/FeedbackNetwork.h"
#include "plugins/Yin.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::vocal
{

namespace
{
constexpr double analysisRate = 11025.0; // the input is decimated to at least this
constexpr double windowSeconds = 0.020;  // YIN's integration window
constexpr float yinThreshold = 0.2f, yinFallback = 0.3f;
constexpr float floorDb = -60.0f;           // quieter is silence
constexpr float silenceUnderLineDb = 30.0f; // or this far under the line
constexpr float breathUnderLineDb = 12.0f;  // a breath is at least this far under the line
constexpr float breathPauseSeconds = 0.1f;  // and comes this long after the last pitch
constexpr float voicedMaxBright = 0.5f;     // a sound with more of its power over 4 kHz is not a vowel
constexpr float breathMaxBright = 0.55f;    // brighter than this in a pause is a consonant
constexpr float lineFallDbPerSecond = 2.0f;
constexpr float syllableRiseDb = 6.0f; // a rise this far from a dip inside a held voice is a new syllable
constexpr float syllableFallDb = 3.0f; // after a fall this far from the last syllable's peak
constexpr float rateWindowSeconds = 3.0f;

float toDb(float power) noexcept
{
    return 10.0f * std::log10(power + 1.0e-12f);
}

void flush(Biquad& f) noexcept
{
    if (std::abs(f.z1) < 1.0e-20f)
        f.z1 = 0.0f;
    if (std::abs(f.z2) < 1.0e-20f)
        f.z2 = 0.0f;
}
} // namespace

void Listener::prepare(double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("Listener::prepare");
    sampleRate_ = sampleRate;
    decimation_ = std::max(1, static_cast<int>(std::floor(sampleRate / analysisRate)));
    rate_ = sampleRate / decimation_;
    for (auto& f : antiAlias_)
        f.setLowPass(sampleRate, std::min(4000.0, 0.4 * rate_));
    span_ = static_cast<int>(std::lround(windowSeconds * rate_));
    tauMin_ = std::max(2, static_cast<int>(std::floor(rate_ / highestHz)));
    tauMax_ = static_cast<int>(std::ceil(rate_ / lowestHz));
    windowSize_ = span_ + tauMax_ + 2;
    const int ringSize = fdn::powerOfTwoAtLeast(2.0 * windowSize_);
    ring_.assign(static_cast<size_t>(ringSize), 0.0f);
    ringMask_ = ringSize - 1;
    window_.assign(static_cast<size_t>(windowSize_), 0.0f);
    diff_.assign(static_cast<size_t>(tauMax_ + 2), 0.0f);
    normalised_.assign(static_cast<size_t>(tauMax_ + 2), 0.0f);

    rumble_.setHighPass(sampleRate, 80.0);
    for (auto& f : high_)
        f.setHighPass(sampleRate, 4000.0);
    sibilant_[0].setHighPass(sampleRate, 5000.0);
    sibilant_[1].setHighPass(sampleRate, 5000.0);
    sibilant_[2].setLowPass(sampleRate, std::min(9000.0, 0.45 * sampleRate));
    envelopeStep_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.003 * sampleRate));
    frameSamples_ = std::max(1, static_cast<int>(std::lround(frameSeconds * sampleRate)));
    frameDuration_ = static_cast<float>(frameSamples_ / sampleRate);
    reset();
}

void Listener::reset() noexcept
{
    for (auto& f : antiAlias_)
        f.reset();
    rumble_.reset();
    for (auto& f : high_)
        f.reset();
    for (auto& f : sibilant_)
        f.reset();
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    written_ = 0;
    phase_ = 0;
    frameCount_ = 0;
    power_ = highPower_ = sibilantPower_ = 0.0f;
    hearing_ = {};
    frame_ = 0;
    lastVoicedFrame_ = lastActiveFrame_ = -1000000;
    yinVoiced_ = active_ = wasVoiced_ = false;
    yinHz_ = 0.0f;
    syllablePeakDb_ = syllableDipDb_ = 0.0f;
    syllableArmed_ = false;
    onsets_.fill(-1000000);
    onsetWrite_ = 0;
}

void Listener::setGapSeconds(float seconds) noexcept
{
    gapSeconds_ = std::clamp(seconds, minGapSeconds, maxGapSeconds);
}

bool Listener::push(float x) noexcept
{
    const float r = rumble_.process(x);
    power_ += envelopeStep_ * (r * r - power_);
    const float h = high_[1].process(high_[0].process(x));
    highPower_ += envelopeStep_ * (h * h - highPower_);
    const float s = sibilant_[2].process(sibilant_[1].process(sibilant_[0].process(x)));
    sibilantPower_ += envelopeStep_ * (s * s - sibilantPower_);

    const float a = antiAlias_[1].process(antiAlias_[0].process(x));
    if (++phase_ >= decimation_)
    {
        phase_ = 0;
        ring_[static_cast<size_t>(written_ & ringMask_)] = a;
        ++written_;
    }
    if (++frameCount_ < frameSamples_)
        return false;
    frameCount_ = 0;
    analyseFrame();
    return true;
}

bool Listener::detectPitch(float& hz) noexcept
{
    if (written_ < windowSize_)
        return false;
    const std::int64_t start = written_ - windowSize_;
    float peak = 0.0f;
    for (int i = 0; i < windowSize_; ++i)
    {
        const float v = ring_[static_cast<size_t>((start + i) & ringMask_)];
        window_[static_cast<size_t>(i)] = v;
        peak = std::max(peak, std::abs(v));
    }
    if (peak < 1.0e-4f)
        return false;
    yin::difference(window_.data(), span_, 1, tauMax_ + 1, diff_.data());
    float clarity = 0.0f;
    const float period =
        yin::pick(diff_.data(), normalised_.data(), tauMin_, tauMax_, yinThreshold, yinFallback, &clarity);
    if (!(period > 0.0f))
        return false;
    hz = static_cast<float>(rate_ / period);
    return hz >= lowestHz && hz <= highestHz;
}

void Listener::analyseFrame() noexcept
{
    ++frame_;
    auto& h = hearing_;
    h.lineEnd = false;

    const float levelDb = toDb(power_);
    const float bright = highPower_ / (power_ + 1.0e-12f);
    const float sibilantDb = toDb(sibilantPower_) - levelDb;

    // Pitch every other frame (5 ms); between, the last answer stands.
    if (frame_ % 2 == 0)
    {
        float hz = 0.0f;
        yinVoiced_ = detectPitch(hz);
        if (yinVoiced_)
            yinHz_ = hz;
    }

    // The line's level: the peak of the voice, falling slowly.
    h.lineLevelDb =
        std::max(floorDb + breathUnderLineDb, h.lineLevelDb - lineFallDbPerSecond * frameDuration_);

    const float sincePitch = static_cast<float>(frame_ - lastVoicedFrame_) * frameDuration_;
    Sound sound = Sound::silence;
    if (levelDb >= floorDb && levelDb >= h.lineLevelDb - silenceUnderLineDb)
    {
        if (yinVoiced_ && bright < voicedMaxBright)
            sound = Sound::voiced;
        else if (sincePitch >= breathPauseSeconds && levelDb <= h.lineLevelDb - breathUnderLineDb &&
                 bright < breathMaxBright)
            sound = Sound::breath;
        else
            sound = Sound::consonant;
    }
    h.sound = sound;
    h.levelDb = levelDb;

    const bool voiced = sound == Sound::voiced;
    if (voiced)
    {
        h.lineLevelDb = std::max(h.lineLevelDb, levelDb);
        h.pitchHz = yinHz_;
        h.heldPitchHz = yinHz_;
        // Syllables: a voice starting after a pause, or rising again from a dip
        // inside one. A flicker out of voice (under 30 ms) changes nothing.
        bool onset = false;
        if (!wasVoiced_ && sincePitch >= 0.03f)
            onset = true;
        else if (!syllableArmed_)
        {
            syllablePeakDb_ = std::max(syllablePeakDb_, levelDb);
            if (levelDb < syllablePeakDb_ - syllableFallDb)
            {
                syllableArmed_ = true;
                syllableDipDb_ = levelDb;
            }
        }
        else
        {
            syllableDipDb_ = std::min(syllableDipDb_, levelDb);
            onset = levelDb > syllableDipDb_ + syllableRiseDb;
        }
        if (onset)
        {
            onsets_[static_cast<size_t>(onsetWrite_)] = frame_;
            onsetWrite_ = (onsetWrite_ + 1) % static_cast<int>(onsets_.size());
            syllablePeakDb_ = levelDb;
            syllableArmed_ = false;
        }
        lastVoicedFrame_ = frame_;
    }
    else
        h.pitchHz = 0.0f;
    wasVoiced_ = voiced;

    h.sibilance = sound == Sound::silence || sound == Sound::breath
                      ? 0.0f
                      : std::clamp((sibilantDb + 18.0f) / 12.0f, 0.0f, 1.0f);

    // Lines: voice or consonants, then a pause as long as the gap.
    if (voiced || sound == Sound::consonant)
    {
        lastActiveFrame_ = frame_;
        active_ = true;
    }
    h.pauseSeconds = static_cast<float>(frame_ - lastActiveFrame_) * frameDuration_;
    if (active_ && h.pauseSeconds >= gapSeconds_)
    {
        h.lineEnd = true;
        active_ = false;
    }

    const auto window = static_cast<std::int64_t>(rateWindowSeconds / frameDuration_);
    int count = 0;
    for (const auto at : onsets_)
        if (frame_ - at < window)
            ++count;
    h.syllablesPerSecond = static_cast<float>(count) / rateWindowSeconds;

    for (auto& f : antiAlias_)
        flush(f);
    flush(rumble_);
    for (auto& f : high_)
        flush(f);
    for (auto& f : sibilant_)
        flush(f);
    if (power_ < 1.0e-20f)
        power_ = 0.0f;
    if (highPower_ < 1.0e-20f)
        highPower_ = 0.0f;
    if (sibilantPower_ < 1.0e-20f)
        sibilantPower_ = 0.0f;
}

} // namespace northlight::plugins::vocal
