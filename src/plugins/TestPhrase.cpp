#include "plugins/TestPhrase.h"

#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace northlight::plugins::phrase
{

std::vector<Cue> standardPhrase()
{
    std::vector<Cue> cues;
    double t = 0.5;
    for (int note = singleLowest; note <= singleHighest; note += singleStep)
        for (const int velocity : singleVelocities)
        {
            cues.push_back({Cue::Kind::note, t, note, velocity, singleHold, "single"});
            t += singleSpacing;
        }
    // Chords: a close voicing in the middle, a wide one low down, held.
    t += 1.0;
    for (const int note : {60, 64, 67, 71})
        cues.push_back({Cue::Kind::note, t, note, 90, 2.0, "chord"});
    t += 3.0;
    for (const int note : {41, 48, 51, 55, 58, 62})
        cues.push_back({Cue::Kind::note, t, note, 105, 2.5, "chord"});
    t += 3.5;
    // One key struck again while it rings, softer each time.
    for (int i = 0; i < 6; ++i)
        cues.push_back({Cue::Kind::note, t + 0.18 * i, 64, 120 - 15 * i, 0.12, "repeat"});
    t += 2.5;
    // A run under the pedal, each key let go at once; the pedal lifts after it.
    cues.push_back({Cue::Kind::pedalDown, t, 0, 127, 0.0, "pedal"});
    int i = 0;
    for (const int note : {48, 55, 60, 64, 67, 72, 76, 79, 84})
        cues.push_back({Cue::Kind::note, t + 0.05 + 0.2 * i++, note, 70 + 4 * i, 0.12, "pedal"});
    cues.push_back({Cue::Kind::pedalUp, t + 4.0, 0, 0, 0.0, "pedal"});
    return cues;
}

std::optional<Rendered>
render(const std::string& type, const std::vector<Cue>& cues, double sampleRate, double tailSeconds)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("phrase::render");
    const auto* builtIn = findBuiltIn(type);
    if (builtIn == nullptr || builtIn->kind != BuiltInKind::instrument)
        return std::nullopt;
    auto synth = BuiltInFactory().create(type);
    if (synth == nullptr)
        return std::nullopt;
    constexpr int block = 256;
    synth->prepare(sampleRate, block);

    // Every MIDI event in time order.
    struct Timed
    {
        long long at;
        engine::MidiEvent event;
    };
    std::vector<Timed> events;
    double end = 0.0;
    const auto at = [&](double seconds)
    { return static_cast<long long>(std::llround(seconds * sampleRate)); };
    for (const auto& c : cues)
    {
        switch (c.kind)
        {
            case Cue::Kind::note:
                events.push_back({at(c.seconds),
                                  {0,
                                   engine::MidiEvent::Type::noteOn,
                                   0,
                                   static_cast<std::uint8_t>(c.note),
                                   static_cast<std::uint8_t>(c.velocity)}});
                events.push_back(
                    {at(c.seconds + c.length),
                     {0, engine::MidiEvent::Type::noteOff, 0, static_cast<std::uint8_t>(c.note), 0}});
                end = std::max(end, c.seconds + c.length);
                break;
            case Cue::Kind::pedalDown:
            case Cue::Kind::pedalUp:
                events.push_back({at(c.seconds),
                                  {0,
                                   engine::MidiEvent::Type::controller,
                                   0,
                                   64,
                                   static_cast<std::uint8_t>(c.kind == Cue::Kind::pedalDown ? 127 : 0)}});
                end = std::max(end, c.seconds);
                break;
        }
    }
    std::stable_sort(
        events.begin(), events.end(), [](const Timed& a, const Timed& b) { return a.at < b.at; });

    Rendered out;
    out.sampleRate = sampleRate;
    const long long total = at(end + tailSeconds);
    out.left.reserve(static_cast<size_t>(total));
    out.right.reserve(static_cast<size_t>(total));
    std::vector<float> l(block), r(block);
    engine::MidiEventList list(256);
    size_t next = 0;
    for (long long start = 0; start < total; start += block)
    {
        list.clear();
        while (next < events.size() && events[next].at < start + block)
        {
            auto e = events[next++].event;
            e.sampleOffset = static_cast<int>(std::max(0LL, events[next - 1].at - start));
            list.push(e);
        }
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        synth->processMidi(list, l.data(), r.data(), block);
        const auto n = static_cast<size_t>(std::min<long long>(block, total - start));
        out.left.insert(out.left.end(), l.begin(), l.begin() + static_cast<std::ptrdiff_t>(n));
        out.right.insert(out.right.end(), r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return out;
}

std::string cueSheet(const std::vector<Cue>& cues)
{
    std::string text = "# seconds kind note velocity length part\n";
    char line[160];
    for (const auto& c : cues)
    {
        const char* kind = c.kind == Cue::Kind::note        ? "note"
                           : c.kind == Cue::Kind::pedalDown ? "pedal-down"
                                                            : "pedal-up";
        std::snprintf(line,
                      sizeof line,
                      "%.6f %s %d %d %.6f %s\n",
                      c.seconds,
                      kind,
                      c.note,
                      c.velocity,
                      c.length,
                      c.part.c_str());
        text += line;
    }
    return text;
}

} // namespace northlight::plugins::phrase
