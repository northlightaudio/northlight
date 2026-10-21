#include "engine/pitch/VocalRender.h"

#include <algorithm>
#include <cmath>

namespace northlight::engine::pitch
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int attackFrames = 12; // 60 ms of the note's start keeps its scoop
constexpr int releaseFrames = 8; // 40 ms of its end keeps its fall
constexpr int driftRadius = 16;  // frames: the drift is the pitch averaged over ~165 ms, twice
constexpr double spanMarginSeconds = 0.025;
constexpr double noteFadeSeconds = 0.010;
constexpr double segmentFadeSeconds = 0.015;

std::vector<float> boxSmooth(const std::vector<float>& in, int radius)
{
    auto out = in;
    const int n = static_cast<int>(in.size());
    std::vector<double> prefix(static_cast<size_t>(n) + 1);
    for (int pass = 0; pass < 2; ++pass)
    {
        prefix[0] = 0.0;
        for (int i = 0; i < n; ++i)
            prefix[static_cast<size_t>(i) + 1] = prefix[static_cast<size_t>(i)] + out[static_cast<size_t>(i)];
        for (int i = 0; i < n; ++i)
        {
            const int lo = std::max(0, i - radius), hi = std::min(n, i + radius + 1);
            out[static_cast<size_t>(i)] = static_cast<float>(
                (prefix[static_cast<size_t>(hi)] - prefix[static_cast<size_t>(lo)]) / (hi - lo));
        }
    }
    return out;
}

bool joins(const core::VocalNote& a, const core::VocalNote& b) noexcept
{
    return a.end == b.start;
}

float smoothstep(float s) noexcept
{
    s = std::clamp(s, 0.0f, 1.0f);
    return s * s * (3.0f - 2.0f * s);
}

// Cubic (Catmull-Rom) read of x at a fractional position; zero outside.
float readAt(const std::vector<float>& x, double position) noexcept
{
    const auto i = static_cast<std::int64_t>(std::floor(position));
    const auto t = static_cast<float>(position - static_cast<double>(i));
    const auto n = static_cast<std::int64_t>(x.size());
    const auto at = [&](std::int64_t k) { return k >= 0 && k < n ? x[static_cast<size_t>(k)] : 0.0f; };
    const float p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
    return p1 +
           0.5f * t *
               (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

// The gain the edit applies at each sample of [start, end): muted or turned-down
// notes and segments, faded over a few milliseconds inside their edges.
std::vector<float>
gainEnvelope(const core::VocalEdit& edit, double rate, std::int64_t start, std::int64_t end)
{
    std::vector<float> env(static_cast<size_t>(std::max<std::int64_t>(0, end - start)), 1.0f);
    const auto apply = [&](std::int64_t s, std::int64_t e, float gainDb, bool muted, double fadeSeconds)
    {
        if (e <= start || s >= end || e <= s)
            return;
        const float g = muted ? 0.0f : std::pow(10.0f, gainDb / 20.0f);
        const auto fade = std::max<std::int64_t>(
            1, std::min<std::int64_t>(static_cast<std::int64_t>(fadeSeconds * rate), (e - s) / 2));
        for (std::int64_t t = std::max(s, start); t < std::min(e, end); ++t)
        {
            float v = g;
            if (t < s + fade)
                v = 1.0f + (g - 1.0f) * static_cast<float>(t - s) / static_cast<float>(fade);
            else if (t >= e - fade)
                v = g + (1.0f - g) * static_cast<float>(t - (e - fade)) / static_cast<float>(fade);
            env[static_cast<size_t>(t - start)] *= v;
        }
    };
    for (const auto& n : edit.notes)
        if (n.muted || n.gainDb != 0.0f)
            apply(n.start, n.end, n.gainDb, n.muted, noteFadeSeconds);
    for (const auto& s : edit.segments)
        if (!s.isIdentity())
            apply(s.start, s.end, s.gainDb, s.muted, segmentFadeSeconds);
    return env;
}

bool changesPitch(const core::VocalNote& n) noexcept
{
    return n.target != n.centre || n.drift != 1.0f || n.vibrato != 1.0f;
}

struct VoicedRun
{
    std::int64_t start, end;
};

std::vector<VoicedRun> voicedRuns(const PitchAnalysis& a, std::int64_t from, std::int64_t to)
{
    std::vector<VoicedRun> runs;
    const int frames = a.frames();
    int f = std::max(0, static_cast<int>(from / a.hop) - 1);
    const int last = std::min(frames, static_cast<int>(to / a.hop) + 2);
    while (f < last)
    {
        if (a.cents[static_cast<size_t>(f)] <= 0.0f)
        {
            ++f;
            continue;
        }
        int g = f;
        while (g < frames && a.cents[static_cast<size_t>(g)] > 0.0f)
            ++g;
        runs.push_back(VoicedRun{static_cast<std::int64_t>(f) * a.hop,
                                 std::min<std::int64_t>(a.length, static_cast<std::int64_t>(g) * a.hop)});
        f = g;
    }
    return runs;
}

// Pitch-synchronous overlap-add over one voiced run, mixed into `out` (which holds
// the original samples of the span starting at spanStart) where the shift is not 0.
void psolaRun(const std::vector<std::vector<float>>& media,
              const PitchAnalysis& a,
              const std::vector<float>& shift,
              VoicedRun run,
              std::int64_t spanStart,
              std::vector<std::vector<float>>& out)
{
    const auto first = std::lower_bound(a.marks.begin(), a.marks.end(), run.start);
    const auto last = std::lower_bound(a.marks.begin(), a.marks.end(), run.end);
    const auto count = static_cast<int>(last - first);
    if (count < 2)
        return;
    const std::int64_t* marks = &*first;
    const double rate = a.sampleRate;
    const auto shiftAt = [&](double t)
    {
        const double f = t / a.hop;
        const auto i = static_cast<std::int64_t>(std::floor(f));
        if (i < 0 || i >= static_cast<std::int64_t>(shift.size()))
            return 0.0f;
        const float s0 = shift[static_cast<size_t>(i)];
        const float s1 =
            i + 1 < static_cast<std::int64_t>(shift.size()) ? shift[static_cast<size_t>(i + 1)] : s0;
        return s0 + (s1 - s0) * static_cast<float>(f - static_cast<double>(i));
    };
    const auto periodAt = [&](int k)
    {
        // The spacing of the marks around mark k.
        const double left =
            k > 0 ? static_cast<double>(marks[k] - marks[k - 1]) : static_cast<double>(marks[1] - marks[0]);
        const double right = k + 1 < count ? static_cast<double>(marks[k + 1] - marks[k])
                                           : static_cast<double>(marks[k] - marks[k - 1]);
        return std::pair{std::clamp(left, rate / 1200.0, rate / 50.0),
                         std::clamp(right, rate / 1200.0, rate / 50.0)};
    };

    const auto maxPeriod = static_cast<std::int64_t>(rate / 50.0) + 2;
    const std::int64_t bufferStart = run.start - maxPeriod;
    const std::int64_t bufferEnd = run.end + maxPeriod;
    const auto bufferLength = static_cast<size_t>(bufferEnd - bufferStart);
    const size_t channels = out.size();
    std::vector<std::vector<float>> grains(channels, std::vector<float>(bufferLength, 0.0f));
    std::vector<float> weight(bufferLength, 0.0f);

    double t = static_cast<double>(marks[0]);
    int k = 0;
    const double end = static_cast<double>(marks[count - 1]) + 0.5;
    while (t <= end)
    {
        // The analysis mark nearest the synthesis time.
        while (k + 1 < count &&
               std::abs(static_cast<double>(marks[k + 1]) - t) <= std::abs(static_cast<double>(marks[k]) - t))
            ++k;
        while (k > 0 &&
               std::abs(static_cast<double>(marks[k - 1]) - t) < std::abs(static_cast<double>(marks[k]) - t))
            --k;
        const auto [left, right] = periodAt(k);
        const auto lo = static_cast<std::int64_t>(std::ceil(t - left));
        const auto hi = static_cast<std::int64_t>(std::floor(t + right));
        for (std::int64_t j = lo; j <= hi; ++j)
        {
            if (j < bufferStart || j >= bufferEnd)
                continue;
            const double u = static_cast<double>(j) - t;
            const double w =
                u < 0.0 ? 0.5 * (1.0 + std::cos(pi * u / left)) : 0.5 * (1.0 + std::cos(pi * u / right));
            if (w <= 0.0)
                continue;
            const double source = static_cast<double>(marks[k]) + u;
            const auto index = static_cast<size_t>(j - bufferStart);
            for (size_t ch = 0; ch < channels; ++ch)
                grains[ch][index] += static_cast<float>(w) * readAt(media[ch % media.size()], source);
            weight[index] += static_cast<float>(w);
        }
        const double ratio = std::exp2(std::clamp(static_cast<double>(shiftAt(t)), -1200.0, 1200.0) / 1200.0);
        const double period = 0.5 * (left + right);
        t += std::max(8.0, period / ratio);
    }

    // Where the shift is 0 (more than 15 ms from any shifted frame) the original
    // plays; the mix eases between the two over a frame.
    const int frames = a.frames();
    const auto runFrame0 = static_cast<int>(run.start / a.hop);
    const auto runFrame1 = std::min(frames, static_cast<int>((run.end + a.hop - 1) / a.hop));
    std::vector<float> active(static_cast<size_t>(std::max(0, runFrame1 - runFrame0)) + 1, 0.0f);
    for (int f = runFrame0; f < runFrame1; ++f)
        if (std::abs(shift[static_cast<size_t>(f)]) > 0.5f)
            for (int g = std::max(runFrame0, f - 3); g <= std::min(runFrame1 - 1, f + 3); ++g)
                active[static_cast<size_t>(g - runFrame0)] = 1.0f;
    const auto mixAt = [&](std::int64_t sample)
    {
        const double f = static_cast<double>(sample) / a.hop - runFrame0;
        const auto i = static_cast<std::int64_t>(std::floor(f));
        if (i < 0)
            return active.empty() ? 0.0f : active[0];
        if (i + 1 >= static_cast<std::int64_t>(active.size()))
            return active.size() >= 2 ? active[active.size() - 2] : 0.0f;
        const float m0 = active[static_cast<size_t>(i)], m1 = active[static_cast<size_t>(i + 1)];
        return m0 + (m1 - m0) * static_cast<float>(f - static_cast<double>(i));
    };

    const auto spanLength = static_cast<std::int64_t>(out[0].size());
    const std::int64_t from = std::max(bufferStart, spanStart);
    const std::int64_t to = std::min(bufferEnd, spanStart + spanLength);
    for (std::int64_t j = from; j < to; ++j)
    {
        const float mix = mixAt(std::clamp(j, run.start, run.end - 1));
        if (mix <= 0.0f)
            continue;
        const auto index = static_cast<size_t>(j - bufferStart);
        const double ratio = std::exp2(
            std::clamp(static_cast<double>(shiftAt(static_cast<double>(j))), -1200.0, 1200.0) / 1200.0);
        // Outside the first and last marks the grains thin out: the original fills in.
        float coverage = 1.0f;
        if (j < marks[0] || j > marks[count - 1])
            coverage = std::clamp(static_cast<float>(weight[index] / ratio), 0.0f, 1.0f);
        for (size_t ch = 0; ch < channels; ++ch)
        {
            auto& o = out[ch][static_cast<size_t>(j - spanStart)];
            const float original = o;
            const float shifted =
                static_cast<float>(grains[ch][index] / ratio) + (1.0f - coverage) * original;
            o = original + mix * (shifted - original);
        }
    }
}
} // namespace

std::uint64_t hashBytes(const void* data, size_t size, std::uint64_t seed) noexcept
{
    auto h = seed;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<float> shiftCurve(const PitchAnalysis& a, const core::VocalEdit& edit)
{
    const int frames = a.frames();
    std::vector<float> shift(static_cast<size_t>(frames), 0.0f);
    if (a.hop <= 0)
        return shift;
    const auto& notes = edit.notes;
    const auto frameOf = [&a, frames](std::int64_t sample)
    { return std::clamp(static_cast<int>((sample + a.hop / 2) / a.hop), 0, frames); };

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const int fs = frameOf(n.start), fe = n.end >= a.length ? frames : frameOf(n.end);
        if (fe <= fs)
            continue;
        const bool joinedBefore = i > 0 && joins(notes[i - 1], n);
        const bool joinedAfter = i + 1 < notes.size() && joins(n, notes[i + 1]);
        const int len = fe - fs;
        const int tIn = joinedBefore ? std::min(len / 2, static_cast<int>(n.transitionIn / a.hop)) : 0;
        const int tOut = joinedAfter ? std::min(len / 2, static_cast<int>(n.transitionOut / a.hop)) : 0;
        const float base = n.shift();
        if (n.drift == 1.0f && n.vibrato == 1.0f)
        {
            std::fill(shift.begin() + fs, shift.begin() + fe, base);
            continue;
        }
        std::vector<float> dev(static_cast<size_t>(len), 0.0f);
        for (int f = fs; f < fe; ++f)
            if (a.cents[static_cast<size_t>(f)] > 0.0f)
                dev[static_cast<size_t>(f - fs)] = a.cents[static_cast<size_t>(f)] - n.centre;
        const auto drift = boxSmooth(dev, driftRadius);
        const int coreStart = fs + tIn, coreEnd = fe - tOut;
        const int coreLen = std::max(1, coreEnd - coreStart);
        const int attack = std::min(attackFrames, coreLen / 3);
        const int release = std::min(releaseFrames, coreLen / 3);
        for (int f = fs; f < fe; ++f)
        {
            float w = 1.0f;
            if (f < coreStart || f >= coreEnd)
                w = 0.0f;
            else if (attack > 0 && f < coreStart + attack)
                w = static_cast<float>(f - coreStart) / static_cast<float>(attack);
            else if (release > 0 && f >= coreEnd - release)
                w = static_cast<float>(coreEnd - f) / static_cast<float>(release);
            const float d = drift[static_cast<size_t>(f - fs)];
            const float v = dev[static_cast<size_t>(f - fs)] - d;
            shift[static_cast<size_t>(f)] = base + w * ((n.drift - 1.0f) * d + (n.vibrato - 1.0f) * v);
        }
    }
    // Transitions: the shift glides from one joined note's to the next's.
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& left = notes[i];
        const auto& right = notes[i + 1];
        if (!joins(left, right) || left.shift() == right.shift())
            continue;
        const int join = frameOf(left.end);
        int lo =
            join - std::min(static_cast<int>(left.transitionOut / a.hop), (join - frameOf(left.start)) / 2);
        int hi =
            join + std::min(static_cast<int>(right.transitionIn / a.hop), (frameOf(right.end) - join) / 2);
        if (hi - lo < 4) // never a step: at least 20 ms of glide
        {
            lo = std::max(frameOf(left.start), join - 2);
            hi = std::min(frameOf(right.end), join + 2);
        }
        for (int f = lo; f < hi; ++f)
        {
            const float s = smoothstep((static_cast<float>(f - lo) + 0.5f) / static_cast<float>(hi - lo));
            shift[static_cast<size_t>(f)] = left.shift() + (right.shift() - left.shift()) * s;
        }
    }
    return shift;
}

std::vector<float> editedCents(const PitchAnalysis& a, const std::vector<float>& shift)
{
    std::vector<float> out(a.cents.size(), 0.0f);
    for (size_t f = 0; f < out.size(); ++f)
        if (a.cents[f] > 0.0f)
            out[f] = a.cents[f] + (f < shift.size() ? shift[f] : 0.0f);
    return out;
}

std::vector<SpanPlan> planSpans(const PitchAnalysis& a, const core::VocalEdit& edit, std::uint64_t mediaKey)
{
    std::vector<SpanPlan> spans;
    if (!edit.active() || a.hop <= 0)
        return spans;
    const auto margin = static_cast<std::int64_t>(spanMarginSeconds * a.sampleRate);
    const auto& notes = edit.notes;
    // Phrases: runs of joined notes.
    for (size_t i = 0; i < notes.size();)
    {
        size_t j = i + 1;
        while (j < notes.size() && joins(notes[j - 1], notes[j]))
            ++j;
        bool pitch = false, gain = false;
        for (size_t k = i; k < j; ++k)
        {
            pitch = pitch || changesPitch(notes[k]);
            gain = gain || notes[k].muted || notes[k].gainDb != 0.0f;
        }
        if (pitch || gain)
            spans.push_back(SpanPlan{std::max<std::int64_t>(0, notes[i].start - margin),
                                     std::min(a.length, notes[j - 1].end + margin),
                                     pitch,
                                     0});
        i = j;
    }
    for (const auto& s : edit.segments)
        if (!s.isIdentity())
            spans.push_back(SpanPlan{s.start, s.end, false, 0});
    std::sort(
        spans.begin(), spans.end(), [](const SpanPlan& x, const SpanPlan& y) { return x.start < y.start; });
    std::vector<SpanPlan> merged;
    for (const auto& s : spans)
    {
        if (!merged.empty() && s.start < merged.back().end)
        {
            merged.back().end = std::max(merged.back().end, s.end);
            merged.back().pitch = merged.back().pitch || s.pitch;
        }
        else
            merged.push_back(s);
    }
    // Keys: everything that decides the span's samples.
    for (auto& s : merged)
    {
        std::uint64_t h = hashBytes(&mediaKey, sizeof mediaKey);
        const int version = PitchAnalysis::version;
        h = hashBytes(&version, sizeof version, h);
        h = hashBytes(&s.start, sizeof s.start, h);
        h = hashBytes(&s.end, sizeof s.end, h);
        h = hashBytes(&s.pitch, sizeof s.pitch, h);
        for (const auto& n : notes)
            if (n.end > s.start - margin && n.start < s.end + margin)
            {
                const float values[] = {
                    n.centre, n.target, n.drift, n.vibrato, n.gainDb, n.muted ? 1.0f : 0.0f};
                const std::int64_t times[] = {n.start, n.end, n.transitionIn, n.transitionOut};
                h = hashBytes(values, sizeof values, h);
                h = hashBytes(times, sizeof times, h);
            }
        for (const auto& seg : edit.segments)
            if (seg.end > s.start && seg.start < s.end)
            {
                const float values[] = {seg.gainDb, seg.muted ? 1.0f : 0.0f};
                const std::int64_t times[] = {seg.start, seg.end};
                h = hashBytes(values, sizeof values, h);
                h = hashBytes(times, sizeof times, h);
            }
        s.key = h;
    }
    return merged;
}

RenderedSpan renderSpan(const std::vector<std::vector<float>>& media,
                        const PitchAnalysis& a,
                        const core::VocalEdit& edit,
                        const std::vector<float>& shift,
                        const SpanPlan& plan)
{
    RenderedSpan out;
    out.start = plan.start;
    const auto length = std::max<std::int64_t>(0, plan.end - plan.start);
    const size_t channels = std::max<size_t>(1, media.size());
    out.channels.assign(channels, std::vector<float>(static_cast<size_t>(length), 0.0f));
    if (media.empty())
        return out;
    for (size_t ch = 0; ch < channels; ++ch)
    {
        const auto& src = media[ch];
        for (std::int64_t i = 0; i < length; ++i)
        {
            const std::int64_t j = plan.start + i;
            out.channels[ch][static_cast<size_t>(i)] =
                j >= 0 && j < static_cast<std::int64_t>(src.size()) ? src[static_cast<size_t>(j)] : 0.0f;
        }
    }
    if (plan.pitch)
        for (const auto& run : voicedRuns(a, plan.start, plan.end))
            if (run.end > plan.start && run.start < plan.end)
                psolaRun(media, a, shift, run, plan.start, out.channels);
    const auto env = gainEnvelope(edit, a.sampleRate, plan.start, plan.end);
    for (auto& ch : out.channels)
        for (size_t i = 0; i < ch.size(); ++i)
            ch[i] *= env[i];
    return out;
}

} // namespace northlight::engine::pitch
