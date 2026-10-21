#include "app/MidiFiles.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace aural::midifile
{

// ----- time ---------------------------------------------------------------------------------------

TempoMap TempoMap::fromFile(const juce::MidiFile& file)
{
    TempoMap map;
    const short format = file.getTimeFormat();
    if (format > 0)
        map.ticksPerQuarter = format;
    else if (format < 0)
    {
        // SMPTE: frames per second in the high byte (negated), ticks per frame in the low.
        const int fps = -(format >> 8);
        const int perFrame = format & 0xff;
        map.smpteTicksPerSecond = (fps == 29 ? 29.97 : static_cast<double>(fps)) * std::max(1, perFrame);
    }
    juce::MidiMessageSequence tempos;
    file.findAllTempoEvents(tempos);
    for (int i = 0; i < tempos.getNumEvents(); ++i)
    {
        const auto& m = tempos.getEventPointer(i)->message;
        const double seconds = m.getTempoSecondsPerQuarterNote();
        if (seconds > 0.0)
            map.changes.push_back({m.getTimeStamp(), seconds});
    }
    std::stable_sort(map.changes.begin(),
                     map.changes.end(),
                     [](const Change& a, const Change& b) { return a.tick < b.tick; });
    return map;
}

double TempoMap::secondsAt(double tick) const noexcept
{
    if (smpteTicksPerSecond > 0.0)
        return tick / smpteTicksPerSecond;
    const double perQuarter = std::max(1, ticksPerQuarter);
    double seconds = 0.0, from = 0.0, rate = 0.5; // 120 bpm until the first change
    for (const auto& change : changes)
    {
        if (change.tick >= tick)
            break;
        seconds += (change.tick - from) / perQuarter * rate;
        from = change.tick;
        rate = change.secondsPerQuarter;
    }
    return seconds + (tick - from) / perQuarter * rate;
}

double TempoMap::firstBpm() const noexcept
{
    if (changes.empty() || changes.front().tick > 0.0)
        return 120.0;
    return 60.0 / changes.front().secondsPerQuarter;
}

core::SamplePosition ticksToSamples(double tick, const TempoMap& tempo, double sampleRate) noexcept
{
    return static_cast<core::SamplePosition>(std::llround(tempo.secondsAt(tick) * sampleRate));
}

double
samplesToTicks(core::SamplePosition samples, double sampleRate, double bpm, int ticksPerQuarter) noexcept
{
    const double seconds = static_cast<double>(samples) / std::max(1.0, sampleRate);
    return std::round(seconds * std::max(1.0, bpm) / 60.0 * ticksPerQuarter);
}

// ----- import ---------------------------------------------------------------------------------------

core::MidiClip clipFromSequence(const juce::MidiMessageSequence& sequence,
                                const TempoMap& tempo,
                                double sampleRate,
                                int channel)
{
    core::MidiClip clip;
    struct Open
    {
        core::SamplePosition start;
        int velocity;
    };
    std::array<std::vector<Open>, 128> open; // per note, oldest first
    core::SamplePosition last = 0;
    const auto close = [&](int note, core::SamplePosition at)
    {
        auto& held = open[static_cast<size_t>(note)];
        if (held.empty())
            return;
        const auto o = held.front();
        held.erase(held.begin());
        clip.notes.push_back(
            {core::invalidId, note, o.velocity, o.start, std::max<core::SamplePosition>(1, at - o.start)});
    };
    for (int i = 0; i < sequence.getNumEvents(); ++i)
    {
        const auto& m = sequence.getEventPointer(i)->message;
        if (m.isMetaEvent() || m.isSysEx() || (channel > 0 && m.getChannel() != channel))
            continue;
        const auto at = ticksToSamples(m.getTimeStamp(), tempo, sampleRate);
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (!open[static_cast<size_t>(note)].empty())
                close(note, at); // a retrigger ends the note before it
            open[static_cast<size_t>(note)].push_back({at, std::clamp<int>(m.getVelocity(), 1, 127)});
        }
        else if (m.isNoteOff())
            close(m.getNoteNumber(), at);
        else if (m.isController())
            clip.controllers.push_back({core::invalidId,
                                        core::MidiControllerType::controller,
                                        m.getControllerNumber(),
                                        m.getControllerValue(),
                                        at});
        else if (m.isPitchWheel())
            clip.controllers.push_back(
                {core::invalidId, core::MidiControllerType::pitchBend, 0, m.getPitchWheelValue() - 8192, at});
        else if (m.isChannelPressure())
            clip.controllers.push_back({core::invalidId,
                                        core::MidiControllerType::channelPressure,
                                        0,
                                        m.getChannelPressureValue(),
                                        at});
        else if (m.isAftertouch())
            clip.controllers.push_back({core::invalidId,
                                        core::MidiControllerType::polyPressure,
                                        m.getNoteNumber(),
                                        m.getAfterTouchValue(),
                                        at});
        else
            continue;
        last = std::max(last, at);
    }
    for (int note = 0; note < 128; ++note)
        while (!open[static_cast<size_t>(note)].empty())
            close(note, std::max(last, open[static_cast<size_t>(note)].front().start + 1));
    clip.sortNotes();
    clip.sortControllers();
    for (const auto& n : clip.notes)
        clip.length = std::max(clip.length, n.end());
    for (const auto& e : clip.controllers)
        clip.length = std::max(clip.length, e.time + 1);
    return clip;
}

Imported fromMidiFile(const juce::MidiFile& file, double sampleRate)
{
    Imported result;
    result.tempo = TempoMap::fromFile(file);
    juce::MidiMessageSequence signatures;
    file.findAllTimeSigEvents(signatures);
    for (int i = 0; i < signatures.getNumEvents(); ++i)
    {
        const auto& m = signatures.getEventPointer(i)->message;
        if (m.getTimeStamp() > 0.0)
            break; // only the opening signature
        int numerator = 4, denominator = 4;
        m.getTimeSignatureInfo(numerator, denominator);
        result.hasTimeSignature = true;
        result.timeSignature = {std::clamp(numerator, 1, 32), std::clamp(denominator, 1, 32)};
    }
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto* sequence = file.getTrack(t);
        if (sequence == nullptr)
            continue;
        std::string name;
        std::array<bool, 17> channels{};
        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& m = sequence->getEventPointer(i)->message;
            if (m.isTrackNameEvent() && name.empty())
                name = m.getTextFromTextMetaEvent().trim().toStdString();
            else if (!m.isMetaEvent() && !m.isSysEx() && m.getChannel() >= 1 && m.getChannel() <= 16 &&
                     (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isChannelPressure() ||
                      m.isAftertouch()))
                channels[static_cast<size_t>(m.getChannel())] = true;
        }
        const int used = static_cast<int>(std::count(channels.begin() + 1, channels.end(), true));
        for (int channel = 1; channel <= 16; ++channel)
        {
            if (!channels[static_cast<size_t>(channel)])
                continue;
            Part part;
            part.channel = channel;
            part.clip = clipFromSequence(*sequence, result.tempo, sampleRate, channel);
            if (part.clip.notes.empty() && part.clip.controllers.empty())
                continue;
            const auto base = name.empty() ? "Track " + std::to_string(t + 1) : name;
            part.name = used > 1 ? base + " (ch " + std::to_string(channel) + ")" : base;
            part.clip.name = part.name;
            result.parts.push_back(std::move(part));
        }
    }
    return result;
}

Imported readMidiFile(const juce::File& file, double sampleRate)
{
    juce::FileInputStream in(file);
    if (!in.openedOk())
    {
        Imported failed;
        failed.error = "could not open " + file.getFullPathName().toStdString();
        return failed;
    }
    juce::MidiFile midi;
    int type = 0;
    if (!midi.readFrom(in, true, &type))
    {
        Imported failed;
        failed.error = file.getFileName().toStdString() + " is not a Standard MIDI File";
        return failed;
    }
    if (type == 2)
    {
        Imported failed;
        failed.error =
            file.getFileName().toStdString() + " is a type 2 MIDI file (only types 0 and 1 are read)";
        return failed;
    }
    auto imported = fromMidiFile(midi, sampleRate);
    if (imported.parts.empty())
        imported.error = file.getFileName().toStdString() + " has no notes or controllers";
    return imported;
}

// ----- export ---------------------------------------------------------------------------------------

juce::MidiMessageSequence sequenceFromClips(const std::vector<const core::MidiClip*>& clips,
                                            int channel,
                                            double sampleRate,
                                            double bpm,
                                            int ticksPerQuarter)
{
    juce::MidiMessageSequence sequence;
    const int ch = std::clamp(channel, 1, 16);
    const auto add = [&](juce::MidiMessage m, core::SamplePosition at)
    {
        m.setTimeStamp(
            samplesToTicks(std::max<core::SamplePosition>(0, at), sampleRate, bpm, ticksPerQuarter));
        sequence.addEvent(m);
    };
    const auto controller = [&](const core::MidiControllerEvent& e, core::SamplePosition at)
    {
        switch (e.type)
        {
            case core::MidiControllerType::controller:
                add(juce::MidiMessage::controllerEvent(
                        ch, std::clamp(e.number, 0, 127), std::clamp(e.value, 0, 127)),
                    at);
                break;
            case core::MidiControllerType::pitchBend:
                add(juce::MidiMessage::pitchWheel(ch, std::clamp(e.value + 8192, 0, 16383)), at);
                break;
            case core::MidiControllerType::channelPressure:
                add(juce::MidiMessage::channelPressureChange(ch, std::clamp(e.value, 0, 127)), at);
                break;
            case core::MidiControllerType::polyPressure:
                add(juce::MidiMessage::aftertouchChange(
                        ch, std::clamp(e.number, 0, 127), std::clamp(e.value, 0, 127)),
                    at);
                break;
        }
    };
    for (const auto* clip : clips)
    {
        if (clip == nullptr)
            continue;
        // Values a trim left before the clip open it.
        std::map<std::pair<int, int>, const core::MidiControllerEvent*> opening;
        for (const auto& e : clip->controllers)
            if (e.time < 0 && e.type != core::MidiControllerType::polyPressure)
                opening[{static_cast<int>(e.type), e.number}] = &e;
        for (const auto& [lane, e] : opening)
            controller(*e, clip->start);
        for (const auto& e : clip->controllers)
            if (e.time >= 0 && e.time < clip->length)
                controller(e, clip->start + e.time);
        for (const auto& n : clip->notes)
        {
            if (n.start < 0 || n.start >= clip->length)
                continue;
            const auto end = std::min(n.end(), clip->length);
            add(juce::MidiMessage::noteOn(
                    ch, std::clamp(n.note, 0, 127), static_cast<juce::uint8>(std::clamp(n.velocity, 1, 127))),
                clip->start + n.start);
            add(juce::MidiMessage::noteOff(ch, std::clamp(n.note, 0, 127)), clip->start + end);
        }
    }
    sequence.sort();
    sequence.updateMatchedPairs();
    return sequence;
}

juce::MidiFile
toMidiFile(const core::Project& project,
           const std::vector<std::pair<const core::Track*, std::vector<const core::MidiClip*>>>& tracks)
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(exportTicksPerQuarter);
    juce::MidiMessageSequence conductor;
    if (!project.name.empty())
    {
        auto name = juce::MidiMessage::textMetaEvent(3, juce::String::fromUTF8(project.name.c_str()));
        name.setTimeStamp(0.0);
        conductor.addEvent(name);
    }
    const double bpm = std::max(1.0, project.tempoBpm);
    auto tempo = juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(60'000'000.0 / bpm)));
    tempo.setTimeStamp(0.0);
    conductor.addEvent(tempo);
    auto signature = juce::MidiMessage::timeSignatureMetaEvent(project.timeSignature.numerator,
                                                               project.timeSignature.denominator);
    signature.setTimeStamp(0.0);
    conductor.addEvent(signature);
    midi.addTrack(conductor);
    for (const auto& [track, clips] : tracks)
    {
        if (track == nullptr)
            continue;
        // The track's name first, then its events.
        juce::MidiMessageSequence sequence;
        auto name = juce::MidiMessage::textMetaEvent(3, juce::String::fromUTF8(track->name.c_str()));
        name.setTimeStamp(0.0);
        sequence.addEvent(name);
        const auto events =
            sequenceFromClips(clips, track->midiChannel, project.sampleRate, bpm, exportTicksPerQuarter);
        for (int i = 0; i < events.getNumEvents(); ++i)
            sequence.addEvent(events.getEventPointer(i)->message);
        sequence.updateMatchedPairs();
        midi.addTrack(sequence);
    }
    return midi;
}

std::string writeMidiFile(const juce::File& file, const juce::MidiFile& midi)
{
    juce::TemporaryFile temporary(file);
    {
        juce::FileOutputStream out(temporary.getFile());
        if (!out.openedOk())
            return "could not write " + file.getFullPathName().toStdString();
        if (!midi.writeTo(out, 1))
            return "could not write " + file.getFullPathName().toStdString();
        out.flush();
        if (out.getStatus().failed())
            return out.getStatus().getErrorMessage().toStdString();
    }
    if (!temporary.overwriteTargetFileWithTemporary())
        return "could not replace " + file.getFullPathName().toStdString();
    return {};
}

} // namespace aural::midifile
