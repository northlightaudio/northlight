#include "core/Model.h"

#include <algorithm>
#include <cmath>

namespace aural::core
{

AudioClip* Track::findClip(Id clipId) noexcept
{
    for (auto& c : clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

const AudioClip* Track::findClip(Id clipId) const noexcept
{
    for (const auto& c : clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

void Track::sortClips()
{
    std::stable_sort(
        clips.begin(), clips.end(), [](const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
}

Track* Project::findTrack(Id trackId) noexcept
{
    for (auto& t : tracks)
        if (t.id == trackId)
            return &t;
    return nullptr;
}

const Track* Project::findTrack(Id trackId) const noexcept
{
    for (const auto& t : tracks)
        if (t.id == trackId)
            return &t;
    return nullptr;
}

int Project::trackIndex(Id trackId) const noexcept
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId)
            return static_cast<int>(i);
    return -1;
}

SamplePosition Project::length() const noexcept
{
    SamplePosition end = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.end());
    return end;
}

double samplesPerBeat(double sampleRate, double tempoBpm, TimeSignature signature) noexcept
{
    // tempoBpm counts quarter notes; scale to the signature's beat value.
    const double quarterNotesPerBeat = 4.0 / static_cast<double>(std::max(1, signature.denominator));
    return sampleRate * 60.0 / std::max(1.0, tempoBpm) * quarterNotesPerBeat;
}

BarsBeats samplesToBarsBeats(SamplePosition position,
                             double sampleRate,
                             double tempoBpm,
                             TimeSignature signature) noexcept
{
    const double spb = samplesPerBeat(sampleRate, tempoBpm, signature);
    const int beatsPerBar = std::max(1, signature.numerator);
    const double beatsTotal = static_cast<double>(std::max<SamplePosition>(0, position)) / spb;
    const auto wholeBeats = static_cast<std::int64_t>(std::floor(beatsTotal));
    BarsBeats out;
    out.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
    out.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
    out.tick = static_cast<int>(
        std::floor((beatsTotal - static_cast<double>(wholeBeats)) * BarsBeats::ticksPerBeat));
    return out;
}

SamplePosition
barsBeatsToSamples(BarsBeats position, double sampleRate, double tempoBpm, TimeSignature signature) noexcept
{
    const double spb = samplesPerBeat(sampleRate, tempoBpm, signature);
    const int beatsPerBar = std::max(1, signature.numerator);
    const double beats = static_cast<double>((position.bar - 1) * beatsPerBar + (position.beat - 1)) +
                         static_cast<double>(position.tick) / BarsBeats::ticksPerBeat;
    return static_cast<SamplePosition>(std::llround(beats * spb));
}

} // namespace aural::core
