#include "engine/pitch/Correction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace northlight::engine::pitch
{

namespace
{
int pitchClassOf(float cents) noexcept
{
    const int note = static_cast<int>(std::lround(cents / 100.0f));
    return ((note % 12) + 12) % 12;
}

// The targets a note may take: the scale pitches nearest below-or-at and above
// the centre, and the nearest semitone (which may be outside the key: a chromatic
// note sung in tune stays where it is unless the key clearly wins).
std::array<float, 3> neighbours(float centre, core::MusicalKey key)
{
    const int below = static_cast<int>(std::floor(centre / 100.0f));
    std::array<float, 3> out{centre, centre, std::round(centre / 100.0f) * 100.0f};
    for (int n = below; n > below - 13; --n)
        if (key.contains(n))
        {
            out[0] = static_cast<float>(n) * 100.0f;
            break;
        }
    for (int n = below + 1; n < below + 14; ++n)
        if (key.contains(n))
        {
            out[1] = static_cast<float>(n) * 100.0f;
            break;
        }
    return out;
}

double secondsOf(const core::VocalNote& n, double rate) noexcept
{
    return rate > 0.0 ? static_cast<double>(n.length()) / rate : 0.0;
}
} // namespace

CorrectionResult correctToKey(const std::vector<core::VocalNote>& notes,
                              core::MusicalKey key,
                              double sampleRate,
                              const CorrectionOptions& options,
                              const std::vector<std::uint16_t>& chordTones)
{
    CorrectionResult result;
    result.notes = notes;
    const size_t n = notes.size();
    if (n == 0)
        return result;
    if (!key.isSet())
        key = core::MusicalKey{0, core::ScaleKind::chromatic};

    // Candidates and their own costs.
    constexpr int count = 3;
    std::vector<std::array<float, count>> candidates(n);
    std::vector<std::array<double, count>> own(n);
    for (size_t i = 0; i < n; ++i)
    {
        candidates[i] = neighbours(notes[i].centre, key);
        for (int c = 0; c < count; ++c)
        {
            const float aim = candidates[i][static_cast<size_t>(c)];
            const double distance = std::abs(notes[i].centre - aim) / 100.0;
            double cost = 4.0 * distance * distance;
            if (!key.contains(static_cast<int>(std::lround(aim / 100.0f))))
                cost += 1.2; // outside the key: only when it is clearly what was sung
            if (options.chordAware && i < chordTones.size() && chordTones[i] != 0 &&
                (chordTones[i] >> pitchClassOf(candidates[i][static_cast<size_t>(c)]) & 1u) != 0)
                cost -= 0.5;
            own[i][static_cast<size_t>(c)] = cost;
        }
    }
    // Pairs of neighbouring notes (closer than a second apart) are melodic steps.
    const auto stepCost = [&](size_t i, int a, int b)
    {
        const auto& prev = notes[i - 1];
        const auto& cur = notes[i];
        if (sampleRate > 0.0 && static_cast<double>(cur.start - prev.end) / sampleRate > 1.0)
            return 0.0;
        const double sung = cur.centre - prev.centre;
        const double aimed =
            candidates[i][static_cast<size_t>(b)] - candidates[i - 1][static_cast<size_t>(a)];
        double cost = std::pow(std::abs(aimed - sung) / 100.0, 2.0);
        if (std::abs(sung) > 50.0 && (aimed == 0.0 || (aimed > 0.0) != (sung > 0.0)))
            cost += 4.0; // the melody's direction is kept
        if (std::abs(sung) < 30.0 && aimed != 0.0)
            cost += 1.5; // one pitch sung twice lands on one target
        return cost;
    };
    std::vector<std::array<double, count>> score(n);
    std::vector<std::array<int, count>> back(n);
    score[0] = own[0];
    for (size_t i = 1; i < n; ++i)
        for (int b = 0; b < count; ++b)
        {
            double best = std::numeric_limits<double>::max();
            for (int a = 0; a < count; ++a)
            {
                const double v = score[i - 1][static_cast<size_t>(a)] + stepCost(i, a, b);
                if (v < best)
                {
                    best = v;
                    back[i][static_cast<size_t>(b)] = a;
                }
            }
            score[i][static_cast<size_t>(b)] = best + own[i][static_cast<size_t>(b)];
        }
    std::vector<int> choice(n);
    choice[n - 1] =
        static_cast<int>(std::min_element(score[n - 1].begin(), score[n - 1].end()) - score[n - 1].begin());
    for (size_t i = n - 1; i > 0; --i)
        choice[i - 1] = back[i][static_cast<size_t>(choice[i])];

    for (size_t i = 0; i < n; ++i)
    {
        auto& note = result.notes[i];
        const float aim = candidates[i][static_cast<size_t>(choice[i])];
        const float delta = aim - note.centre;
        const double seconds = secondsOf(note, sampleRate);
        note.vibrato = options.vibrato;
        note.drift = seconds >= 0.25 ? options.drift : 1.0f;
        if (std::abs(delta) <= options.toleranceCents)
        {
            note.target = note.centre;
            continue;
        }
        if (std::abs(delta) > options.maxShiftCents)
        {
            note.target = note.centre;
            result.flagged.push_back(note.id);
            continue;
        }
        const float weight = seconds < 0.09 ? 0.35f : seconds < 0.15 ? 0.7f : 1.0f;
        note.target = note.centre + delta * std::clamp(options.strength, 0.0f, 1.0f) * weight;
    }
    return result;
}

core::MusicalKey detectKey(const std::vector<core::VocalNote>& notes)
{
    static constexpr std::array<double, 12> major{
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr std::array<double, 12> minor{
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    std::array<double, 12> histogram{};
    for (const auto& n : notes)
    {
        // A note between two semitones shares its weight between them.
        const double semis = n.centre / 100.0;
        const double low = std::floor(semis);
        const double frac = semis - low;
        const double w = static_cast<double>(std::max<std::int64_t>(0, n.length()));
        const int pc = ((static_cast<int>(low) % 12) + 12) % 12;
        histogram[static_cast<size_t>(pc)] += w * (1.0 - frac);
        histogram[static_cast<size_t>((pc + 1) % 12)] += w * frac;
    }
    const auto correlate = [&histogram](const std::array<double, 12>& profile, int root)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i)
        {
            mx += histogram[static_cast<size_t>(i)];
            my += profile[static_cast<size_t>((i - root + 12) % 12)];
        }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = histogram[static_cast<size_t>(i)] - mx;
            const double y = profile[static_cast<size_t>((i - root + 12) % 12)] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : -2.0;
    };
    core::MusicalKey best{0, core::ScaleKind::major};
    double bestScore = -3.0;
    for (int root = 0; root < 12; ++root)
    {
        if (const double s = correlate(major, root); s > bestScore)
        {
            bestScore = s;
            best = core::MusicalKey{root, core::ScaleKind::major};
        }
        if (const double s = correlate(minor, root); s > bestScore)
        {
            bestScore = s;
            best = core::MusicalKey{root, core::ScaleKind::minor};
        }
    }
    return best;
}

std::vector<std::uint16_t>
chordTonesUnder(const core::Project& project, core::Id trackId, const core::AudioClip& clip)
{
    std::vector<std::uint16_t> out;
    for (const auto& n : clip.vocal.notes)
    {
        // The note's middle third, on the timeline.
        const auto len = n.length();
        const auto from = clip.start + (n.start - clip.sourceOffset) + len / 3;
        const auto to = clip.start + (n.start - clip.sourceOffset) + 2 * len / 3;
        std::uint16_t mask = 0;
        for (const auto& t : project.tracks)
        {
            if (t.id == trackId || t.midiClips.empty() || t.mute)
                continue;
            for (const auto& mc : t.midiClips)
                for (const auto& note : mc.notes)
                {
                    if (mc.muted)
                        break; // a muted clip plays nothing
                    const auto s = mc.start + note.start, e = mc.start + note.end();
                    if (s < to && e > from && s < mc.end())
                        mask = static_cast<std::uint16_t>(mask | (1u << (note.note % 12)));
                }
        }
        out.push_back(mask);
    }
    return out;
}

} // namespace northlight::engine::pitch
