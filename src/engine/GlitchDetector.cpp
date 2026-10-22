#include "engine/GlitchDetector.h"

#include <algorithm>
#include <cmath>

namespace northlight::engine
{

namespace
{
constexpr double pi = 3.14159265358979323846;

// Levinson-Durbin: predictor coefficients a[0..order] (a[0] = 1) from the
// autocorrelation r[0..order]. Leaves the coefficients at zero for silence.
void levinson(const std::vector<double>& r, std::vector<double>& a, int order)
{
    std::fill(a.begin(), a.end(), 0.0);
    a[0] = 1.0;
    double error = r[0];
    if (error <= 1.0e-12)
        return;
    double previous[GlitchDetector::order + 1];
    for (int i = 1; i <= order; ++i)
    {
        double acc = r[static_cast<size_t>(i)];
        for (int j = 1; j < i; ++j)
            acc += a[static_cast<size_t>(j)] * r[static_cast<size_t>(i - j)];
        const double k = -acc / error;
        for (int j = 0; j <= order; ++j)
            previous[j] = a[static_cast<size_t>(j)];
        for (int j = 1; j < i; ++j)
            a[static_cast<size_t>(j)] = previous[j] + k * previous[i - j];
        a[static_cast<size_t>(i)] = k;
        error *= 1.0 - k * k;
        if (error <= 1.0e-12)
            return;
    }
}
} // namespace

GlitchDetector::GlitchDetector(int numChannels, double sampleRate)
    : numChannels_(std::max(1, numChannels)), sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0),
      hold_(static_cast<std::int64_t>(holdSeconds * sampleRate_)),
      maxGap_(static_cast<std::int64_t>(maxGapSeconds * sampleRate_)),
      maxLag_(static_cast<int>(std::ceil(maxPeriodSeconds * sampleRate_))),
      pastLength_(maxLag_ + matchWindow), channels_(static_cast<size_t>(numChannels_)),
      levelCoefficient_(1.0 / (0.01 * sampleRate_)), // about 10 ms
      scratch_(order + 1), window_(frameSize), coefficients_(order + 1), residual_(frameSize)
{
    for (auto& c : channels_)
    {
        c.history.assign(order, 0.0f);
        c.frame.assign(frameSize, 0.0f);
        c.residual.assign(static_cast<size_t>(pastLength_ + frameSize), 0.0);
    }
    for (int i = 0; i < frameSize; ++i)
        window_[static_cast<size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * pi * (i + 0.5) / frameSize);
}

void GlitchDetector::reset()
{
    for (auto& c : channels_)
    {
        std::fill(c.history.begin(), c.history.end(), 0.0f);
        std::fill(c.residual.begin(), c.residual.end(), 0.0);
    }
    filled_ = 0;
    frameStart_ = processed_;
    level_ = 0.0;
    levelAtRunStart_ = 0.0;
    zeroRun_ = 0;
}

void GlitchDetector::process(const float* interleaved, int numFrames, std::vector<Glitch>& out)
{
    std::vector<Glitch> found;
    for (int n = 0; n < numFrames; ++n)
    {
        const float* frame = interleaved + static_cast<size_t>(n) * static_cast<size_t>(numChannels_);
        float loudest = 0.0f;
        for (int ch = 0; ch < numChannels_; ++ch)
        {
            channels_[static_cast<size_t>(ch)].frame[static_cast<size_t>(filled_)] = frame[ch];
            loudest = std::max(loudest, std::abs(frame[ch]));
        }

        // Gaps: a run of exact zeros on every channel, inside a signal.
        const std::int64_t at = processed_ + n;
        if (loudest == 0.0f)
        {
            if (zeroRun_ == 0)
                levelAtRunStart_ = level_;
            ++zeroRun_;
        }
        else
        {
            if (zeroRun_ >= minGapFrames && zeroRun_ <= maxGap_ && levelAtRunStart_ >= gapLevelFloor)
                found.push_back({Glitch::Kind::gap, at - zeroRun_, -1, static_cast<double>(zeroRun_)});
            zeroRun_ = 0;
        }
        level_ += (static_cast<double>(loudest) - level_) * levelCoefficient_;

        if (++filled_ == frameSize)
        {
            analyseFrames(found);
            filled_ = 0;
            frameStart_ += frameSize;
        }
    }
    processed_ += numFrames;

    // Merge detections of the same kind closer than the hold time: one per click,
    // across channels.
    std::stable_sort(
        found.begin(), found.end(), [](const Glitch& a, const Glitch& b) { return a.frame < b.frame; });
    const size_t firstNew = out.size();
    for (const auto& g : found)
    {
        Glitch* merged = nullptr;
        for (size_t i = out.size(); i > firstNew; --i)
        {
            auto& previous = out[i - 1];
            if (g.frame - previous.frame >= hold_)
                break;
            if (previous.kind == g.kind)
            {
                merged = &previous;
                break;
            }
        }
        if (merged != nullptr)
        {
            if (merged->channel != g.channel)
                merged->channel = -1;
            merged->size = std::max(merged->size, g.size);
        }
        else
        {
            out.push_back(g);
        }
    }
}

void GlitchDetector::analyseFrames(std::vector<Glitch>& found)
{
    std::vector<double> r(order + 1);
    for (int ch = 0; ch < numChannels_; ++ch)
    {
        auto& c = channels_[static_cast<size_t>(ch)];

        // Fit the predictor to this frame (windowed autocorrelation, with a little
        // white noise added so a pure tone stays well conditioned).
        for (int lag = 0; lag <= order; ++lag)
        {
            double acc = 0.0;
            for (int i = lag; i < frameSize; ++i)
                acc += c.frame[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)] *
                       c.frame[static_cast<size_t>(i - lag)] * window_[static_cast<size_t>(i - lag)];
            r[static_cast<size_t>(lag)] = acc;
        }
        r[0] *= 1.0 + 1.0e-6;
        levinson(r, coefficients_, order);

        // Prediction error for every sample, using the samples before the frame.
        // The previous frames' errors move down to make room.
        const auto sampleAt = [&c](int i) -> double
        { return i >= 0 ? c.frame[static_cast<size_t>(i)] : c.history[static_cast<size_t>(order + i)]; };
        std::copy(c.residual.end() - pastLength_, c.residual.end(), c.residual.begin());
        for (int i = 0; i < frameSize; ++i)
        {
            double e = 0.0;
            for (int j = 0; j <= order; ++j)
                e += coefficients_[static_cast<size_t>(j)] * sampleAt(i - j);
            c.residual[static_cast<size_t>(pastLength_ + i)] = e;
            residual_[static_cast<size_t>(i)] = std::abs(e);
        }

        // The frame's typical error: the median, scaled to a standard deviation.
        scratch_.assign(residual_.begin(), residual_.end());
        auto middle = scratch_.begin() + frameSize / 2;
        std::nth_element(scratch_.begin(), middle, scratch_.end());
        const double typical = *middle / 0.6745;
        const double threshold = std::max(errorFloor, errorRatio * typical);

        for (int i = 0; i < frameSize; ++i)
        {
            const double e = residual_[static_cast<size_t>(i)];
            if (e <= threshold)
                continue;
            // A cut to silence, or a start within `order` samples of silence (the
            // stream starting, a reset, the end of a gap), is the gap check's
            // business: the predictor has nothing to go on there.
            if (c.frame[static_cast<size_t>(i)] == 0.0f)
                continue;
            bool nearSilence = false;
            for (int j = 1; j <= order && !nearSilence; ++j)
                nearSilence = sampleAt(i - j) == 0.0;
            if (nearSilence)
                continue;
            if (repeatsEarlier(c.residual, pastLength_ + i))
                continue;
            found.push_back({Glitch::Kind::jump, frameStart_ + i, ch, e});
        }

        std::copy(c.frame.end() - order, c.frame.end(), c.history.begin());
    }
}

bool GlitchDetector::repeatsEarlier(const std::vector<double>& residual, int index) const
{
    // The errors around the spike, compared with the same stretch one candidate
    // period earlier. The window is short so a high note whose period is not a
    // whole number of samples still lines up.
    const int end = std::min(index + matchWindow - 3, pastLength_ + frameSize);
    const int start = end - matchWindow;
    double energy = 0.0;
    for (int i = start; i < end; ++i)
        energy += residual[static_cast<size_t>(i)] * residual[static_cast<size_t>(i)];
    if (energy <= 0.0)
        return false;
    for (int lag = minPeriod; lag <= maxLag_; ++lag)
    {
        if (blockSize_ > 0)
        {
            const int r = lag % blockSize_;
            if (r <= 1 || r >= blockSize_ - 1)
                continue; // a repeat every block is the bug this looks for
        }
        double dot = 0.0, earlier = 0.0;
        for (int i = start; i < end; ++i)
        {
            const double past = residual[static_cast<size_t>(i - lag)];
            dot += residual[static_cast<size_t>(i)] * past;
            earlier += past * past;
        }
        // Like in shape and of a similar size.
        if (dot > 0.0 && earlier > 0.25 * energy && earlier < 4.0 * energy &&
            dot * dot > periodMatch * periodMatch * energy * earlier)
            return true;
    }
    return false;
}

} // namespace northlight::engine
