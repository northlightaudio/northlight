#include "plugins/midi/MidiEffectBase.h"

#include <algorithm>

namespace northlight::plugins::midi
{

using engine::MidiEvent;

MidiEffectBase::MidiEffectBase(std::string typeName, std::vector<engine::ParameterInfo> infos)
    : typeName_(std::move(typeName)), infos_(std::move(infos)), values_(infos_.size()), held_(16 * 128),
      sounding_(16 * 128, 0), queue_(queueCapacity)
{
    for (size_t i = 0; i < infos_.size(); ++i)
        values_[i].store(infos_[i].defaultValue, std::memory_order_relaxed);
}

void MidiEffectBase::setParameter(int index, float value) noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= infos_.size())
        return;
    const auto& info = infos_[static_cast<size_t>(index)];
    values_[static_cast<size_t>(index)].store(std::clamp(value, info.minimum, info.maximum),
                                              std::memory_order_relaxed);
}

float MidiEffectBase::parameter(int index) const noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= infos_.size())
        return 0.0f;
    return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void MidiEffectBase::prepare(double sampleRate, int)
{
    sampleRate_ = sampleRate;
    // A device restart: nothing is sounding any more, and the clock starts again.
    std::fill(sounding_.begin(), sounding_.end(), std::uint8_t{0});
    std::fill(held_.begin(), held_.end(), Held{});
    queued_ = 0;
    clock_ = 0;
    advanced_ = 0;
    wasBypassed_ = isBypassed();
    reset();
    prepareEffect(sampleRate);
}

int MidiEffectBase::soundingCount() const noexcept
{
    int total = 0;
    for (const auto c : sounding_)
        total += c;
    return total;
}

void MidiEffectBase::processMidiEffect(const engine::MidiEventList& in,
                                       engine::MidiEventList& out,
                                       int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    out_ = &out;
    blockLength_ = numSamples;
    const bool bypassed = isBypassed();
    if (bypassed != wasBypassed_)
    {
        // What the effect made ends; what the player holds is theirs again.
        endEverything(0);
        wasBypassed_ = bypassed;
    }
    if (!bypassed)
        beginBlock();
    for (const auto& source : in)
    {
        MidiEvent e = source;
        e.sampleOffset = std::clamp(e.sampleOffset, 0, numSamples - 1);
        const std::int64_t at = clock_ + e.sampleOffset;
        if (!bypassed)
            advanceTo(at);
        flushQueue(at);
        if (e.type == MidiEvent::Type::allNotesOff)
        {
            endEverything(e.sampleOffset);
            out.push(e);
            continue;
        }
        if (bypassed)
        {
            emit(e); // through the sounding count, so un-bypassing can end it
            continue;
        }
        const bool on = e.type == MidiEvent::Type::noteOn && e.velocity > 0;
        const bool off =
            e.type == MidiEvent::Type::noteOff || (e.type == MidiEvent::Type::noteOn && e.velocity == 0);
        if (on)
        {
            // A second note-on for a held key (no off between): what the first
            // started ends, then it starts again.
            if (held(e.channel, e.note).taken)
            {
                MidiEvent release = e;
                release.type = MidiEvent::Type::noteOff;
                release.velocity = 0;
                noteOff(release);
            }
            noteOn(e);
        }
        else if (off)
        {
            e.type = MidiEvent::Type::noteOff;
            noteOff(e);
        }
        else
            otherEvent(e);
    }
    if (!bypassed)
        advanceTo(clock_ + numSamples);
    flushQueue(clock_ + numSamples - 1);
    clock_ += numSamples;
    out.sortByTime();
    out_ = nullptr;
}

void MidiEffectBase::otherEvent(const MidiEvent& e) noexcept
{
    if (e.type == MidiEvent::Type::polyPressure)
        if (const auto& h = held(e.channel, e.note); h.taken)
        {
            for (int i = 0; i < h.count; ++i)
            {
                MidiEvent moved = e;
                moved.channel = h.channel[static_cast<size_t>(i)];
                moved.note = h.note[static_cast<size_t>(i)];
                emit(moved);
            }
            return;
        }
    emit(e);
}

bool MidiEffectBase::startNote(const MidiEvent& input, int note, int velocity, int channel) noexcept
{
    auto& h = held(input.channel, input.note);
    h.taken = true;
    if (note < 0 || note > 127 || h.count >= maxOutputsPerNote)
        return false;
    MidiEvent on;
    on.sampleOffset = input.sampleOffset;
    on.type = MidiEvent::Type::noteOn;
    on.channel = static_cast<std::uint8_t>(channel & 15);
    on.note = static_cast<std::uint8_t>(note);
    on.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    if (!emit(on))
        return false;
    h.channel[h.count] = on.channel;
    h.note[h.count] = on.note;
    ++h.count;
    return true;
}

void MidiEffectBase::take(const MidiEvent& input) noexcept
{
    held(input.channel, input.note).taken = true;
}

void MidiEffectBase::releaseNote(const MidiEvent& e) noexcept
{
    auto& h = held(e.channel, e.note);
    if (!h.taken)
    {
        // Not one the effect took (it began while bypassed, or before a reset):
        // it passes, and ends the note only if that note is sounding.
        emit(e);
        return;
    }
    for (int i = 0; i < h.count; ++i)
    {
        MidiEvent off;
        off.sampleOffset = e.sampleOffset;
        off.type = MidiEvent::Type::noteOff;
        off.channel = h.channel[static_cast<size_t>(i)];
        off.note = h.note[static_cast<size_t>(i)];
        emit(off);
    }
    h = Held{};
}

bool MidiEffectBase::emit(MidiEvent e) noexcept
{
    if (out_ == nullptr)
        return false;
    e.sampleOffset = std::clamp(e.sampleOffset, 0, std::max(0, blockLength_ - 1));
    if (e.type == MidiEvent::Type::noteOn && e.velocity == 0)
        e.type = MidiEvent::Type::noteOff;
    switch (e.type)
    {
        case MidiEvent::Type::noteOn:
        {
            auto& count = sounding(e.channel, e.note);
            if (!out_->push(e))
                return false;
            if (count < 255)
                ++count;
            return true;
        }
        case MidiEvent::Type::noteOff:
        {
            // Only a sounding note ends, and only when nothing else holds it.
            auto& count = sounding(e.channel, e.note);
            if (count == 0)
                return false;
            if (count > 1)
            {
                --count;
                return true;
            }
            if (!out_->push(e))
                return false;
            count = 0;
            return true;
        }
        case MidiEvent::Type::allNotesOff:
            std::fill(sounding_.begin(), sounding_.end(), std::uint8_t{0});
            return out_->push(e);
        default:
            return out_->push(e);
    }
}

bool MidiEffectBase::schedule(std::int64_t clockTime, const MidiEvent& e) noexcept
{
    if (queued_ >= queueCapacity)
        return false;
    // The queue is kept latest-first, so the next event is popped from the back;
    // an event joins after (earlier than) those later than it and before those
    // at its time, so events at one time keep the order they were scheduled in.
    int i = queued_;
    while (i > 0 && queue_[static_cast<size_t>(i - 1)].time <= clockTime)
    {
        queue_[static_cast<size_t>(i)] = queue_[static_cast<size_t>(i - 1)];
        --i;
    }
    queue_[static_cast<size_t>(i)] = {clockTime, e};
    ++queued_;
    return true;
}

void MidiEffectBase::flushQueue(std::int64_t through) noexcept
{
    while (queued_ > 0 && queue_[static_cast<size_t>(queued_ - 1)].time <= through)
    {
        const auto& next = queue_[static_cast<size_t>(queued_ - 1)];
        MidiEvent e = next.event;
        e.sampleOffset = static_cast<int>(std::max<std::int64_t>(0, next.time - clock_));
        --queued_;
        emit(e);
    }
}

void MidiEffectBase::advanceTo(std::int64_t to) noexcept
{
    if (to <= advanced_)
        return;
    advance(std::max(advanced_, clock_), to);
    advanced_ = to;
}

void MidiEffectBase::endEverything(int offset) noexcept
{
    for (int channel = 0; channel < 16; ++channel)
        for (int note = 0; note < 128; ++note)
        {
            auto& count = sounding(channel, note);
            if (count == 0)
                continue;
            if (out_ != nullptr)
            {
                MidiEvent off;
                off.sampleOffset = offset;
                off.type = MidiEvent::Type::noteOff;
                off.channel = static_cast<std::uint8_t>(channel);
                off.note = static_cast<std::uint8_t>(note);
                out_->push(off);
            }
            count = 0;
        }
    std::fill(held_.begin(), held_.end(), Held{});
    queued_ = 0;
    reset();
}

} // namespace northlight::plugins::midi
