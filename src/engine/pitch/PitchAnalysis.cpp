#include "engine/pitch/PitchAnalysis.h"

#include "engine/pitch/BoxSmooth.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <numeric>
#include <thread>

namespace northlight::engine::pitch
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int maxCandidates = 8;
constexpr int thresholdCount = 100;

struct Candidate
{
    float cents = 0.0f;
    float probability = 0.0f;
};

struct FrameCandidates
{
    std::array<Candidate, maxCandidates> c{};
    int count = 0;
    float voiced = 0.0f; // the sum of the candidates' probabilities
};

// A windowed-sinc low-pass, then every `factor`th sample: the pitch tracker runs at
// about 16 kHz, which is plenty for the fundamental and a third of the work.
std::vector<float> decimate(const std::vector<float>& x, int factor)
{
    if (factor <= 1)
        return x;
    constexpr int half = 32;
    std::array<float, 2 * half + 1> h{};
    const double fc = 0.45 / factor; // cycles per input sample
    double sum = 0.0;
    for (int i = -half; i <= half; ++i)
    {
        const double sinc = i == 0 ? 2.0 * fc : std::sin(2.0 * pi * fc * i) / (pi * i);
        const double w = 0.42 + 0.5 * std::cos(pi * i / half) + 0.08 * std::cos(2.0 * pi * i / half);
        h[static_cast<size_t>(i + half)] = static_cast<float>(sinc * w);
        sum += sinc * w;
    }
    for (auto& v : h)
        v = static_cast<float>(v / sum);
    const auto n = static_cast<std::int64_t>(x.size()) / factor;
    std::vector<float> y(static_cast<size_t>(std::max<std::int64_t>(0, n)));
    const auto size = static_cast<std::int64_t>(x.size());
    for (std::int64_t k = 0; k < n; ++k)
    {
        const std::int64_t centre = k * factor;
        double acc = 0.0;
        if (centre - half >= 0 && centre + half < size)
        {
            const float* p = x.data() + centre - half;
            for (int i = 0; i <= 2 * half; ++i)
                acc += static_cast<double>(h[static_cast<size_t>(i)]) * p[i];
        }
        else
            for (int i = -half; i <= half; ++i)
            {
                const std::int64_t j = centre + i;
                if (j >= 0 && j < size)
                    acc += static_cast<double>(h[static_cast<size_t>(i + half)]) * x[static_cast<size_t>(j)];
            }
        y[static_cast<size_t>(k)] = static_cast<float>(acc);
    }
    return y;
}

// The beta(2, 18) prior over YIN thresholds that pYIN uses, normalised.
std::array<double, thresholdCount> thresholdPrior()
{
    std::array<double, thresholdCount> p{};
    double sum = 0.0;
    for (int i = 0; i < thresholdCount; ++i)
    {
        const double s = 0.01 * (i + 1);
        p[static_cast<size_t>(i)] = s * std::pow(1.0 - s, 17.0);
        sum += p[static_cast<size_t>(i)];
    }
    for (auto& v : p)
        v /= sum;
    return p;
}

struct YinSetup
{
    int window = 0; // integration window
    int minLag = 0, maxLag = 0;
    double rate = 0.0; // of the decimated signal
    std::array<double, thresholdCount> prior{};
};

// One frame: YIN's cumulative mean normalised difference, each dip a candidate
// weighted by the share of thresholds that would choose it.
void analyseFrame(const std::vector<float>& x,
                  std::int64_t centre,
                  const YinSetup& s,
                  std::vector<double>& seg,
                  std::vector<double>& d,
                  FrameCandidates& out)
{
    out = FrameCandidates{};
    const int span = s.window + s.maxLag + 2;
    // The difference at lag tau spans [first, first + window + tau): centred on the
    // frame for a typical voice (about 200 Hz) so the curve does not lag.
    const std::int64_t first = centre - (s.window + static_cast<int>(s.rate / 200.0)) / 2;
    const auto size = static_cast<std::int64_t>(x.size());
    double energy = 0.0;
    for (int i = 0; i < span; ++i)
    {
        const std::int64_t j = first + i;
        seg[static_cast<size_t>(i)] = j >= 0 && j < size ? x[static_cast<size_t>(j)] : 0.0;
    }
    for (int i = 0; i < s.window; ++i)
        energy += seg[static_cast<size_t>(i)] * seg[static_cast<size_t>(i)];
    if (energy / s.window < 1.0e-9) // below -90 dBFS: nothing to track
        return;

    d[0] = 1.0;
    double running = 0.0;
    for (int tau = 1; tau <= s.maxLag + 1; ++tau)
    {
        double acc = 0.0;
        const double* a = seg.data();
        const double* b = seg.data() + tau;
        for (int j = 0; j < s.window; ++j)
        {
            const double diff = a[j] - b[j];
            acc += diff * diff;
        }
        running += acc;
        d[static_cast<size_t>(tau)] = running > 0.0 ? acc * tau / running : 1.0;
    }

    // Local minima of the normalised difference in the lag range.
    struct Dip
    {
        double lag;
        double value;
    };
    std::array<Dip, 64> dips{};
    int dipCount = 0;
    for (int tau = std::max(2, s.minLag); tau <= s.maxLag && dipCount < 64; ++tau)
    {
        const double v = d[static_cast<size_t>(tau)];
        if (v < d[static_cast<size_t>(tau - 1)] && v <= d[static_cast<size_t>(tau + 1)])
        {
            const double a = d[static_cast<size_t>(tau - 1)], c = d[static_cast<size_t>(tau + 1)];
            const double denom = a - 2.0 * v + c;
            const double delta =
                std::abs(denom) > 1.0e-12 ? std::clamp(0.5 * (a - c) / denom, -0.5, 0.5) : 0.0;
            dips[static_cast<size_t>(dipCount++)] = Dip{tau + delta, v - 0.25 * (a - c) * delta};
        }
    }
    if (dipCount == 0)
        return;
    int lowest = 0;
    for (int i = 1; i < dipCount; ++i)
        if (dips[static_cast<size_t>(i)].value < dips[static_cast<size_t>(lowest)].value)
            lowest = i;

    std::array<double, 64> probability{};
    for (int t = 0; t < thresholdCount; ++t)
    {
        const double threshold = 0.01 * (t + 1);
        int chosen = -1;
        for (int i = 0; i < dipCount; ++i)
            if (dips[static_cast<size_t>(i)].value < threshold)
            {
                chosen = i;
                break;
            }
        if (chosen >= 0)
            probability[static_cast<size_t>(chosen)] += s.prior[static_cast<size_t>(t)];
        else
            probability[static_cast<size_t>(lowest)] += 0.01 * s.prior[static_cast<size_t>(t)];
    }

    // Keep the most likely candidates.
    std::array<int, 64> order{};
    std::iota(order.begin(), order.begin() + dipCount, 0);
    std::sort(order.begin(),
              order.begin() + dipCount,
              [&probability](int a, int b)
              { return probability[static_cast<size_t>(a)] > probability[static_cast<size_t>(b)]; });
    for (int k = 0; k < dipCount && out.count < maxCandidates; ++k)
    {
        const int i = order[static_cast<size_t>(k)];
        if (probability[static_cast<size_t>(i)] < 1.0e-4)
            break;
        const double hz = s.rate / dips[static_cast<size_t>(i)].lag;
        out.c[static_cast<size_t>(out.count++)] =
            Candidate{hzToCents(hz), static_cast<float>(probability[static_cast<size_t>(i)])};
        out.voiced += static_cast<float>(probability[static_cast<size_t>(i)]);
    }
}

// The most likely path through each frame's candidates and an unvoiced state.
void viterbi(const std::vector<FrameCandidates>& frames, std::vector<float>& cents)
{
    const int n = static_cast<int>(frames.size());
    cents.assign(static_cast<size_t>(n), 0.0f);
    if (n == 0)
        return;
    constexpr int states = maxCandidates + 1; // the last is unvoiced
    constexpr double stay = -0.01005;         // log 0.99
    constexpr double change = -4.6052;        // log 0.01
    constexpr double centsScale = 80.0;       // a semitone jump in one frame costs 1.25
    std::vector<std::array<std::int8_t, states>> back(static_cast<size_t>(n));
    std::array<double, states> score{}, next{};
    const auto emission = [](const FrameCandidates& f, int state)
    {
        if (state == maxCandidates)
            return std::log(std::max(1.0 - f.voiced, 0.02));
        return std::log(std::max(static_cast<double>(f.c[static_cast<size_t>(state)].probability), 1.0e-6));
    };
    const auto valid = [](const FrameCandidates& f, int state)
    { return state == maxCandidates || state < f.count; };
    for (int s = 0; s < states; ++s)
        score[static_cast<size_t>(s)] = valid(frames[0], s) ? emission(frames[0], s) : -1.0e30;
    for (int t = 1; t < n; ++t)
    {
        const auto& prev = frames[static_cast<size_t>(t - 1)];
        const auto& cur = frames[static_cast<size_t>(t)];
        for (int s = 0; s < states; ++s)
        {
            if (!valid(cur, s))
            {
                next[static_cast<size_t>(s)] = -1.0e30;
                back[static_cast<size_t>(t)][static_cast<size_t>(s)] = maxCandidates;
                continue;
            }
            double best = -1.0e300;
            int from = maxCandidates;
            for (int p = 0; p < states; ++p)
            {
                if (!valid(prev, p))
                    continue;
                double transition;
                if (s == maxCandidates)
                    transition = p == maxCandidates ? stay : change;
                else if (p == maxCandidates)
                    transition = change;
                else
                    transition = stay - std::abs(cur.c[static_cast<size_t>(s)].cents -
                                                 prev.c[static_cast<size_t>(p)].cents) /
                                            centsScale;
                const double v = score[static_cast<size_t>(p)] + transition;
                if (v > best)
                {
                    best = v;
                    from = p;
                }
            }
            next[static_cast<size_t>(s)] = best + emission(cur, s);
            back[static_cast<size_t>(t)][static_cast<size_t>(s)] = static_cast<std::int8_t>(from);
        }
        score = next;
    }
    int state = static_cast<int>(std::max_element(score.begin(), score.end()) - score.begin());
    for (int t = n - 1; t >= 0; --t)
    {
        const auto& f = frames[static_cast<size_t>(t)];
        cents[static_cast<size_t>(t)] = state < f.count ? f.c[static_cast<size_t>(state)].cents : 0.0f;
        if (t > 0)
            state = back[static_cast<size_t>(t)][static_cast<size_t>(state)];
    }
}

struct Run
{
    int start = 0, end = 0; // frames
    int length() const noexcept { return end - start; }
};

std::vector<Run> voicedRuns(const std::vector<float>& cents)
{
    std::vector<Run> runs;
    const int n = static_cast<int>(cents.size());
    for (int f = 0; f < n;)
    {
        if (cents[static_cast<size_t>(f)] <= 0.0f)
        {
            ++f;
            continue;
        }
        Run r{f, f};
        while (r.end < n && cents[static_cast<size_t>(r.end)] > 0.0f)
            ++r.end;
        runs.push_back(r);
        f = r.end;
    }
    return runs;
}

// boxSmooth over [a, b) of v.
std::vector<float> smoothed(const std::vector<float>& v, int a, int b, int radius)
{
    return boxSmooth(std::vector<float>(v.begin() + a, v.begin() + b), radius);
}

float median(std::vector<float> v)
{
    if (v.empty())
        return 0.0f;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

// A second-order Butterworth section (bilinear), run forward and backward.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    static Biquad lowPass(double hz, double rate)
    {
        const double k = std::tan(pi * std::min(hz, 0.45 * rate) / rate);
        const double q = 1.0 / std::sqrt(2.0);
        const double norm = 1.0 / (1.0 + k / q + k * k);
        Biquad b;
        b.b0 = k * k * norm;
        b.b1 = 2.0 * b.b0;
        b.b2 = b.b0;
        b.a1 = 2.0 * (k * k - 1.0) * norm;
        b.a2 = (1.0 - k / q + k * k) * norm;
        return b;
    }
    static Biquad highPass(double hz, double rate)
    {
        const double k = std::tan(pi * std::min(hz, 0.45 * rate) / rate);
        const double q = 1.0 / std::sqrt(2.0);
        const double norm = 1.0 / (1.0 + k / q + k * k);
        Biquad b;
        b.b0 = norm;
        b.b1 = -2.0 * norm;
        b.b2 = norm;
        b.a1 = 2.0 * (k * k - 1.0) * norm;
        b.a2 = (1.0 - k / q + k * k) * norm;
        return b;
    }
    void run(std::vector<double>& x) const
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : x)
        {
            const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            v = y;
        }
    }
    void runBothWays(std::vector<double>& x) const
    {
        run(x);
        std::reverse(x.begin(), x.end());
        run(x);
        std::reverse(x.begin(), x.end());
    }
};

// Pitch marks in one voiced run: the strongest low-passed peak of the dominant
// polarity in each period, each searched for about one period after the last.
void markRun(const std::vector<float>& mono,
             const PitchAnalysis& a,
             std::int64_t start,
             std::int64_t end,
             std::vector<std::int64_t>& marks)
{
    const double rate = a.sampleRate;
    const auto maxPeriod = static_cast<std::int64_t>(rate / 50.0);
    const std::int64_t from = std::max<std::int64_t>(0, start - maxPeriod);
    const std::int64_t to = std::min<std::int64_t>(static_cast<std::int64_t>(mono.size()), end + maxPeriod);
    if (to - from < 8)
        return;
    std::vector<double> s(mono.begin() + from, mono.begin() + to);
    Biquad::lowPass(1000.0, rate).runBothWays(s);
    double positive = 0.0, negative = 0.0;
    for (std::int64_t i = start - from; i < end - from; ++i)
    {
        positive = std::max(positive, s[static_cast<size_t>(i)]);
        negative = std::max(negative, -s[static_cast<size_t>(i)]);
    }
    if (negative > positive)
        for (auto& v : s)
            v = -v;
    const auto period = [&](std::int64_t sample)
    {
        float c = a.centsAt(std::clamp(sample, start, end - 1));
        if (c <= 0.0f)
            c = a.centsAt(std::clamp(sample, start + a.hop, end - 1 - a.hop));
        if (c <= 0.0f)
            c = 5500.0f;
        return std::clamp(rate / centsToHz(c), rate / 1200.0, rate / 50.0);
    };
    const auto argmax = [&](std::int64_t lo, std::int64_t hi, double expected, double p)
    {
        lo = std::max(lo, from);
        hi = std::min(hi, to - 1);
        std::int64_t best = -1;
        double bestValue = -1.0e300;
        for (std::int64_t j = lo; j <= hi; ++j)
        {
            const double offset = p > 0.0 ? (static_cast<double>(j) - expected) / (0.3 * p) : 0.0;
            const double v = s[static_cast<size_t>(j - from)] * (1.0 - 0.3 * offset * offset);
            if (v > bestValue)
            {
                bestValue = v;
                best = j;
            }
        }
        return best;
    };
    const double p0 = period(start);
    std::int64_t m = argmax(start, start + static_cast<std::int64_t>(p0), -1.0, 0.0);
    if (m < 0)
        return;
    while (m < end)
    {
        if (marks.empty() || m > marks.back())
            marks.push_back(m);
        const double p = period(m);
        const auto lo = m + static_cast<std::int64_t>(0.7 * p);
        const auto hi = m + static_cast<std::int64_t>(1.3 * p);
        if (lo >= end)
            break;
        const auto next = argmax(lo, hi, static_cast<double>(m) + p, p);
        if (next <= m)
            break;
        m = next;
    }
}

float frameLevelDb(const std::vector<float>& mono, std::int64_t centre, int hop)
{
    const std::int64_t lo = std::max<std::int64_t>(0, centre - hop);
    const std::int64_t hi = std::min<std::int64_t>(static_cast<std::int64_t>(mono.size()), centre + hop);
    if (hi <= lo)
        return -120.0f;
    double acc = 0.0;
    for (std::int64_t i = lo; i < hi; ++i)
        acc += static_cast<double>(mono[static_cast<size_t>(i)]) * mono[static_cast<size_t>(i)];
    const double rms = std::sqrt(acc / static_cast<double>(hi - lo));
    return rms > 1.0e-6 ? static_cast<float>(20.0 * std::log10(rms)) : -120.0f;
}

// Splits a voiced run into notes: where the slow pitch settles on a new level, and
// at loudness dips between syllables sung on one pitch.
std::vector<Run> splitRun(const PitchAnalysis& a, Run run)
{
    std::vector<Run> pieces;
    const int n = run.length();
    if (n < 20)
        return {run};
    const auto slow = smoothed(a.cents, run.start, run.end, 16); // vibrato averages out
    const auto quick = smoothed(a.cents, run.start, run.end, 2);
    std::vector<int> bounds{0};
    const auto stableAt = [&](int i, int frames)
    {
        std::vector<float> v(a.cents.begin() + run.start + i,
                             a.cents.begin() + run.start + std::min(n, i + frames));
        return median(std::move(v));
    };
    // Settling: smoothed enough to ride over vibrato, without the slow curve's
    // look-ahead.
    const auto medium = smoothed(a.cents, run.start, run.end, 8);
    float ref = stableAt(0, 10);
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        const float dev = slow[static_cast<size_t>(i)] - ref;
        count = std::abs(dev) > 70.0f ? count + 1 : 0;
        if (count < 12)
            continue;
        // Where the pitch settles on its new level: the first 50 ms from the start of
        // the move that stay within 60 cents, away from the old level.
        int settle = -1;
        float level = 0.0f;
        for (int g = std::max(bounds.back(), i - count); g + 10 <= n; ++g)
        {
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int k = g; k < g + 10; ++k)
            {
                lo = std::min(lo, medium[static_cast<size_t>(k)]);
                hi = std::max(hi, medium[static_cast<size_t>(k)]);
            }
            const float centre = 0.5f * (lo + hi);
            if (hi - lo < 60.0f && std::abs(centre - ref) > 70.0f)
            {
                settle = g;
                level = stableAt(g, 10);
                break;
            }
        }
        if (settle < 0)
        {
            settle = i;
            level = stableAt(i, 20);
        }
        // The split: where the quick curve crosses halfway between the two levels.
        const float mid = 0.5f * (ref + level);
        int split = settle;
        for (int g = std::max(bounds.back() + 1, i - count - 30); g <= settle; ++g)
            if ((quick[static_cast<size_t>(g)] - mid) * (level - ref) >= 0.0f)
            {
                split = g;
                break;
            }
        if (split - bounds.back() >= 10 && n - split >= 10)
            bounds.push_back(split);
        ref = level;
        count = 0;
        i = std::max(i, settle + 10);
    }
    bounds.push_back(n);

    // Loudness dips inside each piece.
    std::vector<int> all{0};
    for (size_t k = 0; k + 1 < bounds.size(); ++k)
    {
        const int b0 = bounds[k], b1 = bounds[k + 1];
        for (int i = b0 + 16; i < b1 - 16; ++i)
        {
            const int f = run.start + i;
            const float level = a.levelDb[static_cast<size_t>(f)];
            float before = -1000.0f, after = -1000.0f, around = 1000.0f;
            for (int j = std::max(b0, i - 20); j < i; ++j)
                before = std::max(before, a.levelDb[static_cast<size_t>(run.start + j)]);
            for (int j = i + 1; j <= std::min(b1 - 1, i + 20); ++j)
                after = std::max(after, a.levelDb[static_cast<size_t>(run.start + j)]);
            for (int j = std::max(b0, i - 8); j <= std::min(b1 - 1, i + 8); ++j)
                around = std::min(around, a.levelDb[static_cast<size_t>(run.start + j)]);
            if (level <= before - 6.0f && level <= after - 6.0f && level <= around && i - all.back() >= 16)
            {
                all.push_back(i);
                i += 8;
            }
        }
        if (b1 != n)
            all.push_back(b1);
    }
    all.push_back(n);
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    for (size_t k = 0; k + 1 < all.size(); ++k)
        pieces.push_back(Run{run.start + all[k], run.start + all[k + 1]});

    // Pieces shorter than 50 ms join the neighbour nearer in pitch.
    const auto pitchOf = [&](const Run& r)
    { return median(std::vector<float>(a.cents.begin() + r.start, a.cents.begin() + r.end)); };
    for (bool merged = true; merged && pieces.size() > 1;)
    {
        merged = false;
        for (size_t k = 0; k < pieces.size(); ++k)
        {
            if (pieces[k].length() >= 10)
                continue;
            const float p = pitchOf(pieces[k]);
            const bool hasLeft = k > 0, hasRight = k + 1 < pieces.size();
            const bool left = hasLeft && (!hasRight || std::abs(pitchOf(pieces[k - 1]) - p) <=
                                                           std::abs(pitchOf(pieces[k + 1]) - p));
            if (left)
                pieces[k - 1].end = pieces[k].end;
            else
                pieces[k + 1].start = pieces[k].start;
            pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(k));
            merged = true;
            break;
        }
    }
    return pieces;
}

float noteCentre(const PitchAnalysis& a, Run r)
{
    const int n = r.length();
    const int trim = n > 20 ? std::max(4, static_cast<int>(n * 0.15)) : 0;
    double sum = 0.0, weights = 0.0;
    for (int f = r.start + trim; f < r.end - trim; ++f)
    {
        const float c = a.cents[static_cast<size_t>(f)];
        if (c <= 0.0f)
            continue;
        const double w = std::pow(10.0, a.levelDb[static_cast<size_t>(f)] / 20.0);
        sum += w * c;
        weights += w;
    }
    if (weights <= 0.0)
        return median(std::vector<float>(a.cents.begin() + r.start, a.cents.begin() + r.end));
    return static_cast<float>(sum / weights);
}

void findNotes(PitchAnalysis& a, const std::vector<Run>& runs)
{
    for (const auto& run : runs)
    {
        const auto pieces = splitRun(a, run);
        const size_t first = a.notes.size();
        for (const auto& p : pieces)
        {
            core::VocalNote note;
            note.start = static_cast<std::int64_t>(p.start) * a.hop;
            note.end = std::min<std::int64_t>(a.length, static_cast<std::int64_t>(p.end) * a.hop);
            note.centre = noteCentre(a, p);
            note.target = note.centre;
            a.notes.push_back(note);
        }
        // Transitions between the run's joined notes: how far either side of the join
        // the curve is still travelling between the two centres.
        for (size_t k = first; k + 1 < a.notes.size(); ++k)
        {
            auto& left = a.notes[k];
            auto& right = a.notes[k + 1];
            const float interval = right.centre - left.centre;
            const int join = static_cast<int>(left.end / a.hop);
            const int leftFrames = static_cast<int>(left.length() / a.hop);
            const int rightFrames = static_cast<int>(right.length() / a.hop);
            int out = 0, in = 0;
            if (std::abs(interval) > 1.0f)
            {
                const auto progress = [&](int f)
                {
                    return (a.cents[static_cast<size_t>(std::clamp(f, 0, a.frames() - 1))] - left.centre) /
                           interval;
                };
                while (out < leftFrames * 2 / 5 && progress(join - 1 - out) > 0.15f)
                    ++out;
                while (in < rightFrames * 2 / 5 && progress(join + in) < 0.85f)
                    ++in;
            }
            out = std::clamp(out, std::min(4, leftFrames / 3), std::max(1, leftFrames * 2 / 5));
            in = std::clamp(in, std::min(4, rightFrames / 3), std::max(1, rightFrames * 2 / 5));
            left.transitionOut = static_cast<std::int64_t>(out) * a.hop;
            right.transitionIn = static_cast<std::int64_t>(in) * a.hop;
        }
    }
}

double highFrequencyRatio(const std::vector<float>& mono, double rate, std::int64_t start, std::int64_t end)
{
    if (end - start < 16)
        return 0.0;
    std::vector<double> x(mono.begin() + start, mono.begin() + end);
    double total = 0.0;
    for (const double v : x)
        total += v * v;
    if (total <= 1.0e-12)
        return 0.0;
    Biquad::highPass(4000.0, rate).run(x);
    double high = 0.0;
    for (const double v : x)
        high += v * v;
    return high / total;
}

void findSegments(PitchAnalysis& a, const std::vector<Run>& runs, const std::vector<float>& mono)
{
    const int frames = a.frames();
    std::vector<float> voicedLevels;
    for (const auto& r : runs)
        for (int f = r.start; f < r.end; ++f)
            voicedLevels.push_back(a.levelDb[static_cast<size_t>(f)]);
    float phraseLevel = -20.0f;
    if (!voicedLevels.empty())
    {
        const auto at = voicedLevels.begin() + static_cast<std::ptrdiff_t>(voicedLevels.size() * 9 / 10);
        std::nth_element(voicedLevels.begin(), at, voicedLevels.end());
        phraseLevel = *at;
    }
    const float silenceDb = std::max(-65.0f, phraseLevel - 40.0f);
    const auto toSample = [&a](int f)
    { return std::min<std::int64_t>(a.length, static_cast<std::int64_t>(f) * a.hop); };

    std::vector<Run> gaps;
    int cursor = 0;
    for (const auto& r : runs)
    {
        if (r.start > cursor)
            gaps.push_back(Run{cursor, r.start});
        cursor = r.end;
    }
    if (cursor < frames)
        gaps.push_back(Run{cursor, frames});

    for (const auto& gap : gaps)
    {
        // Active stretches (above the silence floor), small holes closed.
        std::vector<Run> active;
        for (int f = gap.start; f < gap.end;)
        {
            if (a.levelDb[static_cast<size_t>(f)] <= silenceDb)
            {
                ++f;
                continue;
            }
            Run r{f, f};
            while (r.end < gap.end && a.levelDb[static_cast<size_t>(r.end)] > silenceDb)
                ++r.end;
            if (!active.empty() && r.start - active.back().end < 4)
                active.back().end = r.end;
            else
                active.push_back(r);
            f = r.end;
        }
        std::erase_if(active, [](const Run& r) { return r.length() < 3; });

        const auto add = [&](Run r, core::VocalSegmentKind kind)
        {
            core::VocalSegment s;
            s.start = toSample(r.start);
            s.end = r.end >= frames ? a.length : toSample(r.end);
            s.kind = kind;
            if (s.end > s.start)
                a.segments.push_back(s);
        };
        int at = gap.start;
        for (const auto& r : active)
        {
            if (r.start > at)
                add(Run{at, r.start}, core::VocalSegmentKind::silence);
            const double seconds = static_cast<double>(r.length()) * a.hop / a.sampleRate;
            const bool touchesVoice =
                (r.start == gap.start && gap.start > 0) || (r.end == gap.end && gap.end < frames);
            double level = 0.0;
            for (int f = r.start; f < r.end; ++f)
                level += a.levelDb[static_cast<size_t>(f)];
            level /= r.length();
            const double hf = highFrequencyRatio(mono, a.sampleRate, toSample(r.start), toSample(r.end));
            core::VocalSegmentKind kind;
            if (hf > 0.55 && seconds < 0.35)
                kind = core::VocalSegmentKind::consonant;
            else if (seconds < 0.09 && touchesVoice)
                kind = core::VocalSegmentKind::consonant;
            else if (seconds >= 0.1 && level < phraseLevel - 4.0)
                kind = core::VocalSegmentKind::breath;
            else
                kind = core::VocalSegmentKind::consonant;
            add(r, kind);
            at = r.end;
        }
        if (at < gap.end)
            add(Run{at, gap.end}, core::VocalSegmentKind::silence);
    }
}

template <typename T> void put(std::string& out, const T& v)
{
    out.append(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T> void putVector(std::string& out, const std::vector<T>& v)
{
    put(out, static_cast<std::uint64_t>(v.size()));
    if (!v.empty())
        out.append(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(T));
}
struct Reader
{
    const std::string& s;
    size_t at = 0;
    bool ok = true;
    template <typename T> T get()
    {
        T v{};
        if (at + sizeof(T) > s.size())
        {
            ok = false;
            return v;
        }
        std::memcpy(&v, s.data() + at, sizeof(T));
        at += sizeof(T);
        return v;
    }
    template <typename T> std::vector<T> getVector()
    {
        const auto n = get<std::uint64_t>();
        if (!ok || n > (s.size() - at) / sizeof(T))
        {
            ok = false;
            return {};
        }
        std::vector<T> v(static_cast<size_t>(n));
        if (n > 0)
            std::memcpy(v.data(), s.data() + at, static_cast<size_t>(n) * sizeof(T));
        at += static_cast<size_t>(n) * sizeof(T);
        return v;
    }
};
constexpr std::uint32_t magic = 0x54495041; // "APIT"
} // namespace

float PitchAnalysis::centsAt(std::int64_t sample) const noexcept
{
    if (hop <= 0 || cents.empty() || sample < 0)
        return 0.0f;
    const double f = static_cast<double>(sample) / hop;
    const auto i = static_cast<size_t>(f);
    if (i >= cents.size())
        return 0.0f;
    const float a = cents[i];
    const float b = i + 1 < cents.size() ? cents[i + 1] : a;
    const auto frac = static_cast<float>(f - static_cast<double>(i));
    if (a > 0.0f && b > 0.0f)
        return a + (b - a) * frac;
    return frac < 0.5f ? a : b;
}

core::VocalEdit PitchAnalysis::initialEdit() const
{
    core::VocalEdit edit;
    edit.analysisVersion = version;
    edit.notes = notes;
    edit.segments = segments;
    return edit;
}

std::string PitchAnalysis::serialise() const
{
    std::string out;
    put(out, magic);
    put(out, static_cast<std::int32_t>(version));
    put(out, sampleRate);
    put(out, length);
    put(out, static_cast<std::int32_t>(hop));
    putVector(out, cents);
    putVector(out, voicing);
    putVector(out, levelDb);
    putVector(out, marks);
    put(out, static_cast<std::uint64_t>(notes.size()));
    for (const auto& n : notes)
    {
        put(out, n.start);
        put(out, n.end);
        put(out, n.centre);
        put(out, n.transitionIn);
        put(out, n.transitionOut);
    }
    put(out, static_cast<std::uint64_t>(segments.size()));
    for (const auto& s : segments)
    {
        put(out, s.start);
        put(out, s.end);
        put(out, static_cast<std::uint8_t>(s.kind));
    }
    return out;
}

bool PitchAnalysis::deserialise(const std::string& bytes, PitchAnalysis& out)
{
    Reader r{bytes};
    if (r.get<std::uint32_t>() != magic || r.get<std::int32_t>() != version)
        return false;
    PitchAnalysis a;
    a.sampleRate = r.get<double>();
    a.length = r.get<std::int64_t>();
    a.hop = r.get<std::int32_t>();
    a.cents = r.getVector<float>();
    a.voicing = r.getVector<float>();
    a.levelDb = r.getVector<float>();
    a.marks = r.getVector<std::int64_t>();
    const auto noteCount = r.get<std::uint64_t>();
    for (std::uint64_t i = 0; r.ok && i < noteCount && i < 1000000; ++i)
    {
        core::VocalNote n;
        n.start = r.get<std::int64_t>();
        n.end = r.get<std::int64_t>();
        n.centre = r.get<float>();
        n.target = n.centre;
        n.transitionIn = r.get<std::int64_t>();
        n.transitionOut = r.get<std::int64_t>();
        a.notes.push_back(n);
    }
    const auto segmentCount = r.get<std::uint64_t>();
    for (std::uint64_t i = 0; r.ok && i < segmentCount && i < 1000000; ++i)
    {
        core::VocalSegment s;
        s.start = r.get<std::int64_t>();
        s.end = r.get<std::int64_t>();
        s.kind = static_cast<core::VocalSegmentKind>(std::min<std::uint8_t>(r.get<std::uint8_t>(), 2));
        a.segments.push_back(s);
    }
    if (!r.ok || a.hop <= 0 || a.cents.size() != a.levelDb.size() || a.cents.size() != a.voicing.size())
        return false;
    out = std::move(a);
    return true;
}

PitchAnalysis analyse(const std::vector<std::vector<float>>& channels,
                      double sampleRate,
                      const std::function<bool(double)>& progress,
                      const AnalysisOptions& options)
{
    PitchAnalysis a;
    a.sampleRate = sampleRate;
    if (channels.empty() || sampleRate <= 0.0)
        return a;
    const size_t length = channels[0].size();
    a.length = static_cast<std::int64_t>(length);
    std::vector<float> mono(length, 0.0f);
    for (const auto& ch : channels)
        for (size_t i = 0; i < std::min(length, ch.size()); ++i)
            mono[i] += ch[i];
    if (channels.size() > 1)
        for (auto& v : mono)
            v /= static_cast<float>(channels.size());

    const int factor = std::max(1, static_cast<int>(std::lround(sampleRate / 16000.0)));
    const auto low = decimate(mono, factor);
    YinSetup setup;
    setup.rate = sampleRate / factor;
    setup.minLag = std::max(2, static_cast<int>(std::floor(setup.rate / options.maxHz)));
    setup.maxLag = static_cast<int>(std::ceil(setup.rate / options.minHz));
    setup.window = std::max(setup.maxLag, static_cast<int>(setup.rate * 0.02));
    setup.prior = thresholdPrior();
    const int hopLow = std::max(1, static_cast<int>(std::lround(setup.rate * 0.005)));
    a.hop = hopLow * factor;
    const int frames = static_cast<int>(a.length / a.hop) + 1;

    std::vector<FrameCandidates> candidates(static_cast<size_t>(frames));
    a.levelDb.assign(static_cast<size_t>(frames), -120.0f);
    std::atomic<int> done{0};
    std::atomic<bool> abandoned{false};
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const int threadCount = std::clamp(
        options.threads > 0 ? options.threads : static_cast<int>(hardware), 1, std::max(1, frames / 64));
    const auto work = [&](int first, int last, bool reporting)
    {
        std::vector<double> seg(static_cast<size_t>(setup.window + setup.maxLag + 4));
        std::vector<double> d(static_cast<size_t>(setup.maxLag + 3));
        for (int f = first; f < last; ++f)
        {
            if (abandoned.load(std::memory_order_relaxed))
                return;
            analyseFrame(low,
                         static_cast<std::int64_t>(f) * hopLow,
                         setup,
                         seg,
                         d,
                         candidates[static_cast<size_t>(f)]);
            a.levelDb[static_cast<size_t>(f)] =
                frameLevelDb(mono, static_cast<std::int64_t>(f) * a.hop, a.hop);
            const int count = done.fetch_add(1, std::memory_order_relaxed) + 1;
            if (reporting && progress && (f - first) % 256 == 0 && !progress(0.9 * count / frames))
                abandoned.store(true);
        }
    };
    std::vector<std::thread> threads;
    const int per = (frames + threadCount - 1) / threadCount;
    for (int t = 1; t < threadCount; ++t)
        threads.emplace_back(work, t * per, std::min(frames, (t + 1) * per), false);
    work(0, std::min(frames, per), true);
    for (auto& t : threads)
        t.join();
    if (abandoned.load())
        return PitchAnalysis{};

    a.voicing.resize(static_cast<size_t>(frames));
    for (int f = 0; f < frames; ++f)
        a.voicing[static_cast<size_t>(f)] = candidates[static_cast<size_t>(f)].voiced;
    viterbi(candidates, a.cents);

    // Clean-up: lone octave errors pulled back to the local median, holes of up to
    // 10 ms filled, voiced flickers under 30 ms dropped.
    {
        auto c = a.cents;
        for (int f = 2; f + 2 < frames; ++f)
        {
            if (c[static_cast<size_t>(f)] <= 0.0f)
                continue;
            std::vector<float> around;
            for (int g = f - 2; g <= f + 2; ++g)
                if (c[static_cast<size_t>(g)] > 0.0f)
                    around.push_back(c[static_cast<size_t>(g)]);
            const float m = median(around);
            if (around.size() >= 3 && std::abs(c[static_cast<size_t>(f)] - m) > 600.0f)
                a.cents[static_cast<size_t>(f)] = m;
        }
    }
    for (int f = 1; f < frames; ++f)
    {
        if (a.cents[static_cast<size_t>(f)] > 0.0f || a.cents[static_cast<size_t>(f - 1)] <= 0.0f)
            continue;
        int g = f;
        while (g < frames && a.cents[static_cast<size_t>(g)] <= 0.0f && g - f < 3)
            ++g;
        if (g < frames && g - f <= 2 && a.cents[static_cast<size_t>(g)] > 0.0f)
        {
            const float from = a.cents[static_cast<size_t>(f - 1)], to = a.cents[static_cast<size_t>(g)];
            for (int k = f; k < g; ++k)
                a.cents[static_cast<size_t>(k)] =
                    from + (to - from) * static_cast<float>(k - f + 1) / (g - f + 1);
        }
    }
    // Fast glides blur the difference function and can read as unvoiced for a few
    // frames: a gap of up to 60 ms that stays loud and still looks a little voiced
    // is bridged in cents.
    {
        const auto runs = voicedRuns(a.cents);
        for (size_t k = 0; k + 1 < runs.size(); ++k)
        {
            const int g0 = runs[k].end, g1 = runs[k + 1].start;
            if (g1 - g0 > 12)
                continue;
            const float edge =
                std::min(a.levelDb[static_cast<size_t>(g0 - 1)], a.levelDb[static_cast<size_t>(g1)]);
            bool bridge = true;
            for (int f = g0; f < g1 && bridge; ++f)
                bridge = a.levelDb[static_cast<size_t>(f)] > edge - 12.0f &&
                         a.voicing[static_cast<size_t>(f)] > 0.05f;
            if (!bridge)
                continue;
            const float from = a.cents[static_cast<size_t>(g0 - 1)], to = a.cents[static_cast<size_t>(g1)];
            for (int f = g0; f < g1; ++f)
                a.cents[static_cast<size_t>(f)] =
                    from + (to - from) * static_cast<float>(f - g0 + 1) / static_cast<float>(g1 - g0 + 1);
        }
    }
    for (const auto& r : voicedRuns(a.cents))
        if (r.length() < 6)
            std::fill(a.cents.begin() + r.start, a.cents.begin() + r.end, 0.0f);

    const auto runs = voicedRuns(a.cents);
    for (const auto& r : runs)
        markRun(mono,
                a,
                static_cast<std::int64_t>(r.start) * a.hop,
                std::min<std::int64_t>(a.length, static_cast<std::int64_t>(r.end) * a.hop),
                a.marks);
    findNotes(a, runs);
    findSegments(a, runs, mono);
    if (progress)
        progress(1.0);
    return a;
}

} // namespace northlight::engine::pitch
