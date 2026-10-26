#include "ui/edit/NoteOps.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>

namespace northlight::ui::noteops
{

namespace
{
bool contains(const std::vector<Id>& ids, Id id) noexcept
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int clampVelocity(int v) noexcept
{
    return std::clamp(v, 1, 127);
}

// Every selected note through `change`; those it changed go into the edit.
template <typename Change>
NoteEdit eachSelected(const core::MidiClip& clip, const std::vector<Id>& selection, Change&& change)
{
    NoteEdit edit;
    for (const auto& n : clip.notes)
    {
        if (!contains(selection, n.id))
            continue;
        auto next = n;
        change(next);
        if (!(next == n))
            edit.set.push_back(next);
    }
    return edit;
}
} // namespace

std::vector<core::MidiNote> selected(const core::MidiClip& clip, const std::vector<Id>& selection)
{
    std::vector<core::MidiNote> out;
    for (const auto& n : clip.notes)
        if (contains(selection, n.id))
            out.push_back(n);
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

// ----- the clipboard -------------------------------------------------------------------------

Clipboard copy(const core::MidiClip& clip, const std::vector<Id>& selection, SamplePosition gridSamples)
{
    Clipboard out;
    out.notes = selected(clip, selection);
    if (out.notes.empty())
        return out;
    const auto first = out.notes.front().start;
    SamplePosition last = 0;
    for (auto& n : out.notes)
    {
        n.start -= first;
        n.id = core::invalidId;
        last = std::max(last, n.end());
    }
    const auto grid = std::max<SamplePosition>(1, gridSamples);
    out.span = std::max(grid, (last + grid - 1) / grid * grid);
    return out;
}

NoteEdit paste(const core::MidiClip& clip, const Clipboard& clipboard, SamplePosition at)
{
    NoteEdit edit;
    for (auto n : clipboard.notes)
    {
        n.start += std::max<SamplePosition>(0, at);
        n.id = core::invalidId;
        if (n.start < clip.length)
            edit.add.push_back(n);
    }
    return edit;
}

// ----- lengths and timing --------------------------------------------------------------------

NoteEdit legato(const core::MidiClip& clip, const std::vector<Id>& selection)
{
    // Among the selection when it has several notes; else to the next note of the clip.
    const auto chosen = selected(clip, selection);
    const bool amongSelection = chosen.size() > 1;
    NoteEdit edit;
    for (const auto& n : chosen)
    {
        SamplePosition next = -1;
        for (const auto& other : amongSelection ? chosen : clip.notes)
            if (other.start > n.start && (next < 0 || other.start < next))
                next = other.start;
        if (next < 0)
            continue;
        auto changed = n;
        changed.length = next - n.start;
        if (changed.length != n.length)
            edit.set.push_back(changed);
    }
    return edit;
}

NoteEdit setLength(const core::MidiClip& clip, const std::vector<Id>& selection, SamplePosition length)
{
    return eachSelected(
        clip, selection, [&](core::MidiNote& n) { n.length = std::max<SamplePosition>(1, length); });
}

NoteEdit scaleLength(const core::MidiClip& clip,
                     const std::vector<Id>& selection,
                     double factor,
                     SamplePosition minimum)
{
    return eachSelected(clip,
                        selection,
                        [&](core::MidiNote& n)
                        {
                            n.length = std::max<SamplePosition>(std::max<SamplePosition>(1, minimum),
                                                                static_cast<SamplePosition>(std::llround(
                                                                    static_cast<double>(n.length) * factor)));
                        });
}

NoteEdit addLength(const core::MidiClip& clip,
                   const std::vector<Id>& selection,
                   SamplePosition delta,
                   SamplePosition minimum)
{
    return eachSelected(
        clip,
        selection,
        [&](core::MidiNote& n)
        { n.length = std::max<SamplePosition>(std::max<SamplePosition>(1, minimum), n.length + delta); });
}

namespace
{
// The nearest line of the grid to `position` (timeline samples), every second
// line `swing` of a step late.
SamplePosition nearestLine(const core::TempoMap& map,
                           const NoteGrid& grid,
                           double samplesPerPixel,
                           SamplePosition position,
                           float swing)
{
    const auto step = noteGridStep(grid, map, position, samplesPerPixel);
    if (step.bars > 0 || swing <= 0.0f)
        return map.snap(position, step);
    const double q = map.quartersAt(static_cast<double>(position));
    const int bar = map.barAtQuarter(q);
    const double start = map.barStartQuarter(bar);
    const double end = map.barStartQuarter(bar + 1);
    const double d = std::max(1.0e-6, step.quarters);
    const auto k0 = static_cast<long long>(std::floor((q - start) / d));
    double best = start, bestDistance = 1.0e18;
    for (long long k = k0 - 1; k <= k0 + 2; ++k)
    {
        if (k < 0)
            continue;
        double line =
            start + static_cast<double>(k) * d + (k % 2 != 0 ? static_cast<double>(swing) * d : 0.0);
        line = std::min(line, end);
        if (const double distance = std::abs(line - q); distance < bestDistance)
        {
            bestDistance = distance;
            best = line;
        }
    }
    return map.positionAt(best);
}
} // namespace

NoteEdit quantise(const core::MidiClip& clip,
                  const std::vector<Id>& selection,
                  const core::TempoMap& map,
                  const NoteGrid& grid,
                  double samplesPerPixel,
                  const QuantiseOptions& options)
{
    NoteEdit edit;
    const float strength = std::clamp(options.strength, 0.0f, 1.0f);
    const float swing = std::clamp(options.swing, 0.0f, 0.5f);
    const auto toward = [strength](SamplePosition from, SamplePosition to)
    { return from + static_cast<SamplePosition>(std::llround(static_cast<double>(to - from) * strength)); };
    for (const auto& n : clip.notes)
    {
        if (!selection.empty() && !contains(selection, n.id))
            continue;
        const auto absolute = clip.start + n.start;
        auto moved = n;
        moved.start = std::max<SamplePosition>(
            0, toward(absolute, nearestLine(map, grid, samplesPerPixel, absolute, swing)) - clip.start);
        if (options.ends)
        {
            const auto end = clip.start + n.end();
            const auto step = std::max<SamplePosition>(
                1, std::llround(map.stepSamples(noteGridStep(grid, map, end, samplesPerPixel), end)));
            const auto target = toward(end, nearestLine(map, grid, samplesPerPixel, end, swing)) - clip.start;
            moved.length = std::max(step, target - moved.start);
        }
        if (!(moved == n))
            edit.set.push_back(moved);
    }
    return edit;
}

NoteEdit humanise(const core::MidiClip& clip,
                  const std::vector<Id>& selection,
                  SamplePosition timing,
                  int velocity,
                  std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    return eachSelected(
        clip,
        selection,
        [&](core::MidiNote& n)
        {
            const double t = unit(random), v = unit(random);
            n.start = std::max<SamplePosition>(
                0, n.start + static_cast<SamplePosition>(std::llround(t * static_cast<double>(timing))));
            n.velocity = clampVelocity(n.velocity + static_cast<int>(std::lround(v * velocity)));
        });
}

// ----- velocity ------------------------------------------------------------------------------

NoteEdit dragVelocity(
    const core::MidiClip& clip, const std::vector<Id>& selection, Id dragged, int value, bool absolute)
{
    const auto* pressed = clip.findNote(dragged);
    if (pressed == nullptr)
        return {};
    auto ids = selection;
    if (!contains(ids, dragged))
        ids = {dragged};
    if (absolute)
        return setVelocity(clip, ids, value);
    return offsetVelocity(clip, ids, clampVelocity(value) - pressed->velocity);
}

NoteEdit setVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, int value)
{
    return eachSelected(clip, selection, [&](core::MidiNote& n) { n.velocity = clampVelocity(value); });
}

NoteEdit offsetVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, int delta)
{
    return eachSelected(
        clip, selection, [&](core::MidiNote& n) { n.velocity = clampVelocity(n.velocity + delta); });
}

NoteEdit scaleVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, double factor)
{
    return eachSelected(clip,
                        selection,
                        [&](core::MidiNote& n)
                        { n.velocity = clampVelocity(static_cast<int>(std::lround(n.velocity * factor))); });
}

NoteEdit compressVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, double amount)
{
    const auto chosen = selected(clip, selection);
    if (chosen.empty())
        return {};
    const double mean =
        std::accumulate(chosen.begin(),
                        chosen.end(),
                        0.0,
                        [](double sum, const core::MidiNote& n) { return sum + n.velocity; }) /
        static_cast<double>(chosen.size());
    return eachSelected(
        clip,
        selection,
        [&](core::MidiNote& n)
        { n.velocity = clampVelocity(static_cast<int>(std::lround(mean + (n.velocity - mean) * amount))); });
}

NoteEdit
randomiseVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, int range, std::uint32_t seed)
{
    std::mt19937 random(seed);
    std::uniform_int_distribution<int> offset(-std::abs(range), std::abs(range));
    return eachSelected(
        clip, selection, [&](core::MidiNote& n) { n.velocity = clampVelocity(n.velocity + offset(random)); });
}

// ----- pitch, mute, split and join -----------------------------------------------------------

NoteEdit transpose(const core::MidiClip& clip, const std::vector<Id>& selection, int semitones)
{
    // The whole selection moves together, kept inside 0..127.
    const auto chosen = selected(clip, selection);
    if (chosen.empty())
        return {};
    int lowest = 127, highest = 0;
    for (const auto& n : chosen)
    {
        lowest = std::min(lowest, n.note);
        highest = std::max(highest, n.note);
    }
    const int delta = std::clamp(semitones, -lowest, 127 - highest);
    return eachSelected(clip, selection, [&](core::MidiNote& n) { n.note += delta; });
}

NoteEdit toggleMute(const core::MidiClip& clip, const std::vector<Id>& selection)
{
    const auto chosen = selected(clip, selection);
    const bool allMuted =
        !chosen.empty() &&
        std::all_of(chosen.begin(), chosen.end(), [](const core::MidiNote& n) { return n.muted; });
    return eachSelected(clip, selection, [&](core::MidiNote& n) { n.muted = !allMuted; });
}

NoteEdit
split(const core::MidiClip& clip, const std::vector<Id>& notes, SamplePosition at, SamplePosition minimum)
{
    NoteEdit edit;
    for (const auto& n : clip.notes)
    {
        if (!contains(notes, n.id) || at <= n.start + minimum || at >= n.end() - minimum)
            continue;
        auto left = n;
        left.length = at - n.start;
        auto right = n;
        right.id = core::invalidId;
        right.start = at;
        right.length = n.end() - at;
        edit.set.push_back(left);
        edit.add.push_back(right);
    }
    return edit;
}

NoteEdit join(const core::MidiClip& clip, const std::vector<Id>& notes)
{
    NoteEdit edit;
    std::map<int, std::vector<core::MidiNote>> byPitch;
    for (const auto& n : selected(clip, notes))
        byPitch[n.note].push_back(n);
    for (auto& [pitch, run] : byPitch)
    {
        if (run.size() < 2)
            continue;
        auto first = run.front();
        SamplePosition end = first.end();
        for (size_t i = 1; i < run.size(); ++i)
        {
            end = std::max(end, run[i].end());
            edit.remove.push_back(run[i].id);
        }
        first.length = end - first.start;
        edit.set.push_back(first);
    }
    return edit;
}

// ----- selection -----------------------------------------------------------------------------

Id stepSelection(const core::MidiClip& clip, const std::vector<Id>& selection, int direction)
{
    if (clip.notes.empty())
        return core::invalidId;
    std::vector<const core::MidiNote*> order;
    for (const auto& n : clip.notes)
        order.push_back(&n);
    std::stable_sort(order.begin(),
                     order.end(),
                     [](const auto* a, const auto* b)
                     { return a->start != b->start ? a->start < b->start : a->note < b->note; });
    int at = -1;
    for (int i = 0; i < static_cast<int>(order.size()); ++i)
        if (contains(selection, order[static_cast<size_t>(i)]->id))
        {
            if (direction > 0 || at < 0)
                at = i;
            if (direction < 0)
                break;
        }
    const int next =
        at < 0 ? (direction > 0 ? 0 : static_cast<int>(order.size()) - 1) : at + (direction > 0 ? 1 : -1);
    if (next < 0 || next >= static_cast<int>(order.size()))
        return core::invalidId;
    return order[static_cast<size_t>(next)]->id;
}

std::vector<Id> notesOfPitch(const core::MidiClip& clip, int pitch)
{
    std::vector<Id> out;
    for (const auto& n : clip.notes)
        if (n.note == pitch)
            out.push_back(n.id);
    return out;
}

core::MidiClip applied(core::MidiClip clip, const NoteEdit& edit, Id nextId)
{
    for (const auto& n : edit.set)
        if (auto* note = clip.findNote(n.id))
            *note = n;
    std::erase_if(clip.notes, [&](const core::MidiNote& n) { return contains(edit.remove, n.id); });
    for (auto n : edit.add)
    {
        n.id = nextId++;
        clip.notes.push_back(n);
    }
    clip.sortNotes();
    return clip;
}

} // namespace northlight::ui::noteops
