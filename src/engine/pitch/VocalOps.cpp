#include "engine/pitch/VocalOps.h"

#include <algorithm>
#include <cmath>

namespace northlight::engine::pitch
{

float centreOver(const PitchAnalysis& a, std::int64_t start, std::int64_t end)
{
    if (a.hop <= 0)
        return 0.0f;
    const int f0 = std::clamp(static_cast<int>(start / a.hop), 0, a.frames());
    const int f1 = std::clamp(static_cast<int>(end / a.hop), f0, a.frames());
    const int n = f1 - f0;
    const int trim = n > 20 ? std::max(4, static_cast<int>(n * 0.15)) : 0;
    double sum = 0.0, weights = 0.0, plain = 0.0;
    int count = 0;
    for (int f = f0; f < f1; ++f)
    {
        const float c = a.cents[static_cast<size_t>(f)];
        if (c <= 0.0f)
            continue;
        plain += c;
        ++count;
        if (f < f0 + trim || f >= f1 - trim)
            continue;
        const double w = std::pow(10.0, a.levelDb[static_cast<size_t>(f)] / 20.0);
        sum += w * c;
        weights += w;
    }
    if (weights > 0.0)
        return static_cast<float>(sum / weights);
    return count > 0 ? static_cast<float>(plain / count) : 0.0f;
}

bool splitNote(core::VocalEdit& edit, const PitchAnalysis& analysis, core::Id noteId, std::int64_t at)
{
    auto it = std::find_if(
        edit.notes.begin(), edit.notes.end(), [noteId](const core::VocalNote& n) { return n.id == noteId; });
    if (it == edit.notes.end())
        return false;
    const auto minimum = static_cast<std::int64_t>(0.03 * analysis.sampleRate);
    if (at - it->start < minimum || it->end - at < minimum)
        return false;
    const float shift = it->shift();
    core::VocalNote right = *it;
    right.id = 0;
    right.start = at;
    right.centre = centreOver(analysis, at, it->end);
    right.target = right.centre + shift;
    const auto glide = static_cast<std::int64_t>(0.025 * analysis.sampleRate);
    right.transitionIn = std::min(glide, (right.end - right.start) / 3);
    it->end = at;
    it->centre = centreOver(analysis, it->start, at);
    it->target = it->centre + shift;
    it->transitionOut = std::min(glide, (it->end - it->start) / 3);
    edit.notes.insert(it + 1, right);
    return true;
}

bool mergeWithNext(core::VocalEdit& edit, const PitchAnalysis& analysis, core::Id noteId)
{
    auto it = std::find_if(
        edit.notes.begin(), edit.notes.end(), [noteId](const core::VocalNote& n) { return n.id == noteId; });
    if (it == edit.notes.end() || it + 1 == edit.notes.end() || it->end != (it + 1)->start)
        return false;
    const auto& next = *(it + 1);
    // The longer note's shift wins: it is the one the user will have set.
    const float shift = it->length() >= next.length() ? it->shift() : next.shift();
    it->end = next.end;
    it->transitionOut = next.transitionOut;
    it->gainDb = std::max(it->gainDb, next.gainDb);
    it->muted = it->muted && next.muted;
    it->centre = centreOver(analysis, it->start, it->end);
    it->target = it->centre + shift;
    edit.notes.erase(it + 1);
    return true;
}

void setSegments(core::VocalEdit& edit, core::VocalSegmentKind kind, float gainDb, bool muted)
{
    for (auto& s : edit.segments)
        if (s.kind == kind)
        {
            s.gainDb = gainDb;
            s.muted = muted;
        }
}

void resetPitch(core::VocalEdit& edit, const std::vector<core::Id>& notes)
{
    for (auto& n : edit.notes)
        if (std::find(notes.begin(), notes.end(), n.id) != notes.end())
        {
            n.target = n.centre;
            n.drift = 1.0f;
            n.vibrato = 1.0f;
        }
}

void snapToSemitones(core::VocalEdit& edit, const std::vector<core::Id>& notes)
{
    for (auto& n : edit.notes)
        if (std::find(notes.begin(), notes.end(), n.id) != notes.end())
            n.target = std::round(n.target / 100.0f) * 100.0f;
}

} // namespace northlight::engine::pitch
