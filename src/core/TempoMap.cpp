#include "core/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace northlight::core
{

namespace
{
constexpr double epsilon = 1.0e-9;
constexpr double minimumBpm = 1.0;
constexpr double maximumBpm = 999.0;

double clampBpm(double bpm) noexcept
{
    return std::clamp(std::isfinite(bpm) ? bpm : 120.0, minimumBpm, maximumBpm);
}

TimeSignature clampSignature(TimeSignature s) noexcept
{
    return {std::clamp(s.numerator, 1, 32), std::clamp(s.denominator, 1, 32)};
}
} // namespace

TempoMap::TempoMap() noexcept : TempoMap(48000.0, 120.0, TimeSignature{}) {}

TempoMap::TempoMap(double sampleRate,
                   double bpm,
                   TimeSignature signature,
                   const std::vector<TempoChange>& tempoChanges,
                   const std::vector<MeterChange>& meterChanges)
    : sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    // Tempo: the start, then the changes in order (one per position, the last
    // given wins; positions at or before the start are ignored).
    std::vector<TempoChange> changes;
    for (const auto& c : tempoChanges)
        if (std::isfinite(c.quarter) && c.quarter > epsilon)
            changes.push_back(c);
    std::stable_sort(changes.begin(),
                     changes.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.quarter < b.quarter; });
    std::vector<TempoChange> unique;
    for (const auto& c : changes)
    {
        if (!unique.empty() && std::abs(unique.back().quarter - c.quarter) < epsilon)
            unique.back() = c;
        else
            unique.push_back(c);
    }
    tempo_.push_back({0.0, 0.0, clampBpm(bpm), clampBpm(bpm)});
    for (const auto& c : unique)
        tempo_.push_back({c.quarter, 0.0, clampBpm(c.bpm), clampBpm(c.bpm)});
    for (size_t i = 0; i + 1 < tempo_.size(); ++i)
        if (unique[i].ramp)
            tempo_[i].bpm1 = tempo_[i + 1].bpm0;
    // Each point's sample position: the samples of the segments before it.
    const double perMinute = sampleRate_ * 60.0;
    for (size_t i = 1; i < tempo_.size(); ++i)
    {
        const auto& p = tempo_[i - 1];
        const double dq = tempo_[i].quarter - p.quarter;
        double ds = 0.0;
        if (p.bpm1 == p.bpm0)
            ds = dq * perMinute / p.bpm0;
        else
        {
            const double k = (p.bpm1 - p.bpm0) / dq;
            ds = perMinute / k * std::log(p.bpm1 / p.bpm0);
        }
        tempo_[i].sample = p.sample + ds;
    }

    // Meter: the start, then the changes by bar.
    std::vector<MeterChange> meters;
    for (const auto& m : meterChanges)
        if (m.bar > 0)
            meters.push_back(m);
    std::stable_sort(meters.begin(),
                     meters.end(),
                     [](const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
    meter_.push_back({0, 0.0, clampSignature(signature)});
    for (const auto& m : meters)
    {
        const auto& prev = meter_.back();
        const double quarter = prev.quarter + (m.bar - prev.bar) * quartersPerBar(prev.signature);
        if (prev.bar == m.bar)
            meter_.back().signature = clampSignature(m.signature);
        else
            meter_.push_back({m.bar, quarter, clampSignature(m.signature)});
    }
}

TempoMap tempoMapOf(const Project& project)
{
    return TempoMap(project.sampleRate,
                    project.tempoBpm,
                    project.timeSignature,
                    project.tempoChanges,
                    project.meterChanges);
}

const TempoMap& TempoMapCache::of(const Project& project)
{
    if (!valid_ || project.sampleRate != sampleRate_ || project.tempoBpm != bpm_ ||
        !(project.timeSignature == signature_) || project.tempoChanges != tempoChanges_ ||
        project.meterChanges != meterChanges_)
    {
        sampleRate_ = project.sampleRate;
        bpm_ = project.tempoBpm;
        signature_ = project.timeSignature;
        tempoChanges_ = project.tempoChanges;
        meterChanges_ = project.meterChanges;
        map_ = tempoMapOf(project);
        valid_ = true;
    }
    return map_;
}

// ----- time --------------------------------------------------------------------------

size_t TempoMap::tempoIndexAtQuarter(double quarters) const noexcept
{
    // The last point at or before the position (the first for anything before it).
    size_t lo = 0, hi = tempo_.size();
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        (tempo_[mid].quarter <= quarters ? lo : hi) = mid;
    }
    return lo;
}

size_t TempoMap::tempoIndexAtSample(double samples) const noexcept
{
    size_t lo = 0, hi = tempo_.size();
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        (tempo_[mid].sample <= samples ? lo : hi) = mid;
    }
    return lo;
}

double TempoMap::quarterEnd(size_t index) const noexcept
{
    return index + 1 < tempo_.size() ? tempo_[index + 1].quarter : std::numeric_limits<double>::infinity();
}

double TempoMap::samplesAt(double quarters) const noexcept
{
    const double perMinute = sampleRate_ * 60.0;
    if (quarters < 0.0)
        return quarters * perMinute / tempo_.front().bpm0;
    const auto& p = tempo_[tempoIndexAtQuarter(quarters)];
    const double dq = quarters - p.quarter;
    if (p.bpm1 == p.bpm0)
        return p.sample + dq * perMinute / p.bpm0;
    const size_t i = static_cast<size_t>(&p - tempo_.data());
    const double k = (p.bpm1 - p.bpm0) / (quarterEnd(i) - p.quarter);
    return p.sample + perMinute / k * std::log((p.bpm0 + k * dq) / p.bpm0);
}

double TempoMap::quartersAt(double samples) const noexcept
{
    const double perMinute = sampleRate_ * 60.0;
    if (samples < 0.0)
        return samples * tempo_.front().bpm0 / perMinute;
    const size_t i = tempoIndexAtSample(samples);
    const auto& p = tempo_[i];
    const double ds = samples - p.sample;
    if (p.bpm1 == p.bpm0)
        return p.quarter + ds * p.bpm0 / perMinute;
    const double k = (p.bpm1 - p.bpm0) / (quarterEnd(i) - p.quarter);
    return p.quarter + p.bpm0 * (std::exp(k * ds / perMinute) - 1.0) / k;
}

SamplePosition TempoMap::positionAt(double quarters) const noexcept
{
    return static_cast<SamplePosition>(std::llround(samplesAt(quarters)));
}

double TempoMap::bpmAtQuarter(double quarters) const noexcept
{
    if (quarters < 0.0)
        return tempo_.front().bpm0;
    const size_t i = tempoIndexAtQuarter(quarters);
    const auto& p = tempo_[i];
    if (p.bpm1 == p.bpm0)
        return p.bpm0;
    const double t = (quarters - p.quarter) / (quarterEnd(i) - p.quarter);
    return p.bpm0 + (p.bpm1 - p.bpm0) * std::clamp(t, 0.0, 1.0);
}

double TempoMap::samplesPerQuarterAt(SamplePosition position) const noexcept
{
    return sampleRate_ * 60.0 / bpmAt(position);
}

// ----- bars --------------------------------------------------------------------------

double TempoMap::quartersPerBeat(TimeSignature signature) noexcept
{
    return 4.0 / static_cast<double>(std::max(1, signature.denominator));
}

double TempoMap::quartersPerBar(TimeSignature signature) noexcept
{
    return quartersPerBeat(signature) * std::max(1, signature.numerator);
}

size_t TempoMap::meterIndexAtBar(int bar) const noexcept
{
    size_t lo = 0, hi = meter_.size();
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        (meter_[mid].bar <= bar ? lo : hi) = mid;
    }
    return lo;
}

size_t TempoMap::meterIndexAtQuarter(double quarters) const noexcept
{
    size_t lo = 0, hi = meter_.size();
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        (meter_[mid].quarter <= quarters + epsilon ? lo : hi) = mid;
    }
    return lo;
}

TimeSignature TempoMap::signatureAtBar(int bar) const noexcept
{
    return meter_[meterIndexAtBar(bar)].signature;
}

TimeSignature TempoMap::signatureAt(SamplePosition position) const noexcept
{
    return signatureAtBar(barAtQuarter(quartersAt(static_cast<double>(position))));
}

double TempoMap::barStartQuarter(int bar) const noexcept
{
    if (bar < 0)
        return bar * quartersPerBar(meter_.front().signature);
    const auto& m = meter_[meterIndexAtBar(bar)];
    return m.quarter + (bar - m.bar) * quartersPerBar(m.signature);
}

int TempoMap::barAtQuarter(double quarters) const noexcept
{
    if (quarters < 0.0)
        return static_cast<int>(std::floor(quarters / quartersPerBar(meter_.front().signature) + epsilon));
    const auto& m = meter_[meterIndexAtQuarter(quarters)];
    return m.bar +
           static_cast<int>(std::floor((quarters - m.quarter) / quartersPerBar(m.signature) + epsilon));
}

BarsBeats TempoMap::barsBeatsAt(SamplePosition position) const noexcept
{
    const double q = quartersAt(static_cast<double>(position));
    const int bar = barAtQuarter(q);
    const auto signature = signatureAtBar(bar);
    const double beats = std::max(0.0, (q - barStartQuarter(bar)) / quartersPerBeat(signature));
    const auto whole = static_cast<int>(std::floor(beats + epsilon));
    BarsBeats out;
    out.bar = bar + 1;
    out.beat = std::min(whole, std::max(1, signature.numerator) - 1) + 1;
    const double fraction = std::max(0.0, beats - whole);
    out.tick = std::clamp(static_cast<int>(std::floor(fraction * BarsBeats::ticksPerBeat + 1.0e-6)),
                          0,
                          BarsBeats::ticksPerBeat - 1);
    return out;
}

SamplePosition TempoMap::positionAt(BarsBeats position) const noexcept
{
    const int bar = position.bar - 1;
    const auto signature = signatureAtBar(bar);
    const double beats =
        static_cast<double>(position.beat - 1) + static_cast<double>(position.tick) / BarsBeats::ticksPerBeat;
    return positionAt(barStartQuarter(bar) + beats * quartersPerBeat(signature));
}

// ----- grid --------------------------------------------------------------------------

namespace
{
struct Lines
{
    double below = 0.0, above = 0.0; // quarter positions of the lines around a position
};
} // namespace

SamplePosition TempoMap::snap(SamplePosition position, GridStep step) const noexcept
{
    const double q = quartersAt(static_cast<double>(position));
    Lines lines;
    const int bar = barAtQuarter(q);
    if (step.bars > 0)
    {
        const int first = static_cast<int>(std::floor(static_cast<double>(bar) / step.bars)) * step.bars;
        lines.below = barStartQuarter(first);
        lines.above = barStartQuarter(first + step.bars);
    }
    else
    {
        const double division = std::max(1.0e-6, step.quarters);
        const double start = barStartQuarter(bar);
        const double end = barStartQuarter(bar + 1);
        const double k = std::floor((q - start) / division + epsilon);
        lines.below = start + k * division;
        lines.above = std::min(lines.below + division, end);
    }
    const auto lower = positionAt(lines.below);
    const auto upper = positionAt(lines.above);
    return position - lower < upper - position ? lower : upper;
}

SamplePosition TempoMap::nextLine(SamplePosition position, GridStep step) const noexcept
{
    const auto at = snap(position, step);
    if (at >= position)
        return at;
    return snap(move(at, step, 1), step);
}

SamplePosition TempoMap::move(SamplePosition position, GridStep step, int count) const noexcept
{
    if (count == 0)
        return position;
    const double q = quartersAt(static_cast<double>(position));
    if (step.bars > 0)
    {
        const int bar = barAtQuarter(q);
        const double offset = q - barStartQuarter(bar);
        const int to = bar + count * step.bars;
        const double length = barStartQuarter(to + 1) - barStartQuarter(to);
        return positionAt(barStartQuarter(to) + std::min(offset, length));
    }
    return positionAt(q + count * step.quarters);
}

GridStep TempoMap::adaptiveStep(double samplesPerPixel, int minPixels, SamplePosition at) const noexcept
{
    const auto signature = signatureAt(at);
    const double perQuarter = samplesPerQuarterAt(at);
    const double minimum = std::max(1.0, samplesPerPixel * std::max(1, minPixels));
    const double beat = quartersPerBeat(signature);
    for (const double division : {beat / 8.0, beat / 4.0, beat / 2.0, beat})
        if (division * perQuarter >= minimum)
            return {0, division};
    const double bar = quartersPerBar(signature) * perQuarter;
    int bars = 1;
    while (bar * bars < minimum && bars < (1 << 20))
        bars *= 2;
    return {bars, 0.0};
}

double TempoMap::stepSamples(GridStep step, SamplePosition at) const noexcept
{
    const double perQuarter = samplesPerQuarterAt(at);
    if (step.bars > 0)
        return step.bars * quartersPerBar(signatureAt(at)) * perQuarter;
    return step.quarters * perQuarter;
}

TempoMap::Beat TempoMap::beatAtOrAfter(double samples) const noexcept
{
    const double q = quartersAt(samples);
    int bar = barAtQuarter(q);
    const auto signature = signatureAtBar(bar);
    const double beatLength = quartersPerBeat(signature);
    const double start = barStartQuarter(bar);
    auto beat = static_cast<int>(std::ceil((q - start) / beatLength - epsilon));
    beat = std::max(0, beat);
    if (beat >= std::max(1, signature.numerator))
    {
        ++bar;
        beat = 0;
    }
    Beat out{0, bar, beat};
    out.position = positionAt(barStartQuarter(bar) + beat * quartersPerBeat(signatureAtBar(bar)));
    if (static_cast<double>(out.position) < samples - 0.5)
        return nextBeat(out); // rounding put it a sample early
    return out;
}

TempoMap::Beat TempoMap::nextBeat(const Beat& beat) const noexcept
{
    Beat out = beat;
    ++out.beat;
    if (out.beat >= std::max(1, signatureAtBar(out.bar).numerator))
    {
        ++out.bar;
        out.beat = 0;
    }
    out.position = positionAt(barStartQuarter(out.bar) + out.beat * quartersPerBeat(signatureAtBar(out.bar)));
    return out;
}

// ----- remapping ---------------------------------------------------------------------

void remapMusicalTime(Project& project, const TempoMap& from, const TempoMap& to)
{
    const auto map = [&](SamplePosition s) { return to.positionAt(from.quartersAt(static_cast<double>(s))); };
    const auto remapLanes = [&](Channel& channel)
    {
        for (auto& lane : channel.automation)
            for (auto& point : lane.points)
                point.position = map(point.position);
    };
    for (auto& track : project.tracks)
    {
        for (auto& clip : track.midiClips)
        {
            const auto start = map(clip.start);
            const auto end = map(clip.end());
            for (auto& note : clip.notes)
            {
                const auto noteStart = map(clip.start + note.start);
                const auto noteEnd = map(clip.start + note.end());
                note.start = noteStart - start;
                note.length = std::max<SamplePosition>(1, noteEnd - noteStart);
            }
            for (auto& event : clip.controllers)
                event.time = map(clip.start + event.time) - start;
            clip.start = start;
            clip.length = std::max<SamplePosition>(1, end - start);
        }
        for (auto& clip : track.clips)
        {
            if (!clip.followTempo || clip.length <= 0)
                continue;
            const auto start = map(clip.start);
            const auto end = std::max(start + 1, map(clip.end()));
            const double factor = static_cast<double>(end - start) / static_cast<double>(clip.length);
            clip.stretch *= factor;
            clip.fadeIn =
                static_cast<SamplePosition>(std::llround(static_cast<double>(clip.fadeIn) * factor));
            clip.fadeOut =
                static_cast<SamplePosition>(std::llround(static_cast<double>(clip.fadeOut) * factor));
            clip.start = start;
            clip.length = end - start;
        }
        track.sortClips();
        track.sortMidiClips();
        remapLanes(track);
    }
    for (auto& bus : project.buses)
        remapLanes(bus);
    for (auto& marker : project.markers)
    {
        const auto start = map(marker.position);
        const auto end = marker.isRegion() ? map(marker.end()) : start;
        marker.position = start;
        marker.length = marker.isRegion() ? std::max<SamplePosition>(1, end - start) : 0;
    }
    project.sortMarkers();
}

} // namespace northlight::core
