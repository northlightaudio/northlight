#include "plugins/midi/TimeEffects.h"

#include "plugins/ParamHelpers.h"
#include "plugins/midi/MidiEffectBase.h"
#include "plugins/midi/MidiTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::plugins
{

using engine::MidiEvent;
using engine::ParameterInfo;
using midi::rounded;

namespace
{
MidiEvent noteEvent(MidiEvent::Type type, int channel, int note, int velocity) noexcept
{
    return {0,
            type,
            static_cast<std::uint8_t>(channel & 15),
            static_cast<std::uint8_t>(note & 127),
            static_cast<std::uint8_t>(type == MidiEvent::Type::noteOn ? std::clamp(velocity, 1, 127) : 0)};
}

// A held key, for the effects that play from what is held.
struct Key
{
    std::uint8_t channel = 0, note = 0, velocity = 0;
    float current = 0.0f;     // Note Repeat: the velocity the next repeat plays at
    std::int64_t pressed = 0; // when it went down (the effect's clock)
};

// A MIDI effect that plays notes of its own in time.
class TimedEffect : public midi::MidiEffectBase
{
public:
    using MidiEffectBase::MidiEffectBase;

protected:
    // Plays a note at `at` (the effect's clock) for `length` samples.
    void play(std::int64_t at, std::int64_t length, int channel, int note, int velocity) noexcept
    {
        if (note < 0 || note > 127)
            return;
        if (schedule(at, noteEvent(MidiEvent::Type::noteOn, channel, note, velocity)))
            schedule(at + std::max<std::int64_t>(1, length),
                     noteEvent(MidiEvent::Type::noteOff, channel, note, 0));
    }
};
} // namespace

// ----- Arpeggiator -----------------------------------------------------------------------

namespace arpeggiator
{
int position(int mode, int step, int total) noexcept
{
    if (total <= 0)
        return 0;
    step = std::max(0, step);
    switch (mode)
    {
        case down:
            return total - 1 - step % total;
        case upDown:
        case downUp:
        {
            const int period = total > 1 ? 2 * total - 2 : 1;
            const int r = step % period;
            const int i = r < total ? r : period - r;
            return mode == upDown ? i : total - 1 - i;
        }
        default:
            return step % total;
    }
}

bool sortsByPitch(int mode) noexcept
{
    return mode != asPlayed;
}

Step stepAt(int mode, int step, int keyCount, int octaves, int randomPosition) noexcept
{
    if (keyCount <= 0)
        return {};
    octaves = std::max(1, octaves);
    if (mode == chord)
        return {0, keyCount, std::max(0, step) % octaves};
    const int total = keyCount * octaves;
    const int p = mode == random ? std::clamp(randomPosition, 0, total - 1) : position(mode, step, total);
    return {p % keyCount, 1, p / keyCount};
}

namespace
{
class Arpeggiator final : public TimedEffect
{
public:
    Arpeggiator()
        : TimedEffect(type,
                      {choice("Mode", {"Up", "Down", "Up/Down", "Down/Up", "As played", "Random", "Chord"}),
                       choice("Rate", midi::noteValueChoices(), midi::noteValueSixteenth),
                       whole("Octaves", "", 1.0f, 4.0f, 1.0f),
                       whole("Gate", "%", 5.0f, 200.0f, 50.0f),
                       whole("Swing", "%", 0.0f, 75.0f, 0.0f),
                       choice("Latch", {"Off", "On"}),
                       choice("Velocity", {"As played", "Fixed"}),
                       whole("Fixed velocity", "", 1.0f, 127.0f, 100.0f)})
    {
    }
    int midiExpansion() const noexcept override { return 16; }

private:
    void beginBlock() noexcept override
    {
        mode_ = rounded(value(mode));
        rateQuarters_ = midi::noteValueQuarters(value(rate));
        octaves_ = std::clamp(rounded(value(octaves)), 1, 4);
        gate_ = value(gate) / 100.0;
        swing_ = value(swing) / 100.0;
        const bool latched = rounded(value(latch)) == 1;
        if (latch_ && !latched && physical_ == 0)
            clear(); // latch off with no key down: the latched notes stop
        latch_ = latched;
        fixedVelocity_ = rounded(value(velocity)) == 1 ? rounded(value(fixedVelocity)) : 0;
    }
    void reset() noexcept override
    {
        clear();
        physical_ = 0;
    }
    void clear() noexcept
    {
        count_ = 0;
        step_ = 0;
        clock_.stop();
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        take(e);
        if (latch_ && physical_ == 0)
            clear(); // a new chord replaces the latched one
        ++physical_;
        const std::int64_t at = blockClock() + e.sampleOffset;
        if (count_ == 0)
        {
            step_ = 0;
            clock_.restart(at); // stopped: the first step is now
        }
        for (int i = 0; i < count_; ++i)
            if (held_[static_cast<size_t>(i)].note == e.note &&
                held_[static_cast<size_t>(i)].channel == e.channel)
            {
                held_[static_cast<size_t>(i)].velocity = e.velocity;
                return;
            }
        if (count_ < static_cast<int>(held_.size()))
            held_[static_cast<size_t>(count_++)] = {e.channel, e.note, e.velocity, 0.0f, at};
        // The chord as it grows: what the editor shows once the keys are up.
        std::copy(held_.begin(), held_.begin() + count_, full_.begin());
        fullCount_ = count_;
        publishOrder();
    }
    // The held keys in the order pressed, for the editor's step lane; with
    // none held, the last chord at its fullest, so its pattern stays readable.
    void publishOrder() noexcept
    {
        const auto& keys = count_ > 0 ? held_ : full_;
        const int n = count_ > 0 ? count_ : fullCount_;
        auto& f = published();
        for (int i = 0; i < n; ++i)
            f.order[static_cast<size_t>(i)].store(keys[static_cast<size_t>(i)].note,
                                                  std::memory_order_relaxed);
        f.orderCount.store(n, std::memory_order_relaxed);
        f.changed();
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        releaseNote(e); // it started nothing directly; this forgets it
        physical_ = std::max(0, physical_ - 1);
        if (latch_)
            return;
        for (int i = 0; i < count_; ++i)
            if (held_[static_cast<size_t>(i)].note == e.note &&
                held_[static_cast<size_t>(i)].channel == e.channel)
            {
                std::copy(held_.begin() + i + 1, held_.begin() + count_, held_.begin() + i);
                --count_;
                break;
            }
        publishOrder();
        if (count_ == 0)
            clear();
    }
    void advance(std::int64_t from, std::int64_t to) noexcept override
    {
        while (count_ > 0)
        {
            std::int64_t index = 0;
            const std::int64_t at = clock_.next(from, to, blockClock(), playHead(), rateQuarters_, index);
            if (at < 0)
                return;
            playStep(at, index);
            from = at + 1;
        }
    }
    void playStep(std::int64_t at, std::int64_t index) noexcept
    {
        const double stepSamples = midi::samplesPerQuarters(playHead(), rateQuarters_);
        const auto length = static_cast<std::int64_t>(std::max(1.0, gate_ * stepSamples));
        const auto late = (index & 1) != 0 ? static_cast<std::int64_t>(swing_ * 0.5 * stepSamples) : 0;
        // What is held, in the order the mode reads it.
        std::array<Key, 32> order{};
        std::copy(held_.begin(), held_.begin() + count_, order.begin());
        if (sortsByPitch(mode_))
            std::sort(order.begin(),
                      order.begin() + count_,
                      [](const Key& a, const Key& b) { return a.note < b.note; });
        const int k = step_++;
        published().step.store(k, std::memory_order_relaxed);
        published().changed();
        const int drawn = mode_ == random
                              ? static_cast<int>(rng_.bits() % static_cast<std::uint32_t>(count_ * octaves_))
                              : 0;
        const auto s = stepAt(mode_, k, count_, octaves_, drawn);
        for (int i = s.first; i < s.first + s.count; ++i)
        {
            const auto& key = order[static_cast<size_t>(i)];
            play(at + late,
                 length,
                 key.channel,
                 key.note + 12 * s.octave,
                 fixedVelocity_ > 0 ? fixedVelocity_ : key.velocity);
        }
    }

    int mode_ = up;
    double rateQuarters_ = 0.25;
    int octaves_ = 1;
    double gate_ = 0.5, swing_ = 0.0;
    bool latch_ = false;
    int fixedVelocity_ = 0;
    std::array<Key, 32> held_{};
    int count_ = 0;
    std::array<Key, 32> full_{}; // the last chord at its fullest (the editor's)
    int fullCount_ = 0;
    int physical_ = 0; // keys down now (the list can be latched)
    int step_ = 0;
    midi::StepClock clock_;
    dsp::XorShift32 rng_{midi::randomSeed};
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Arpeggiator>();
}
} // namespace arpeggiator

// ----- Note Repeat -----------------------------------------------------------------------

namespace noterepeat
{
namespace
{
class NoteRepeat final : public TimedEffect
{
public:
    NoteRepeat()
        : TimedEffect(type,
                      {choice("Rate", midi::noteValueChoices(), midi::noteValueSixteenth),
                       whole("Gate", "%", 5.0f, 100.0f, 50.0f),
                       whole("Ramp", "%", -50.0f, 50.0f, 0.0f)})
    {
    }
    int midiExpansion() const noexcept override { return 8; }

private:
    void beginBlock() noexcept override
    {
        rateQuarters_ = midi::noteValueQuarters(value(rate));
        gate_ = value(gate) / 100.0;
        ramp_ = value(ramp) / 100.0f;
    }
    void reset() noexcept override
    {
        count_ = 0;
        clock_.stop();
    }
    double stepSamples() const noexcept { return midi::samplesPerQuarters(playHead(), rateQuarters_); }
    void noteOn(const MidiEvent& e) noexcept override
    {
        take(e);
        const std::int64_t at = blockClock() + e.sampleOffset;
        if (count_ == 0)
            clock_.restart(at);
        // The first hit is the key itself, now.
        play(at, static_cast<std::int64_t>(gate_ * stepSamples()), e.channel, e.note, e.velocity);
        if (count_ < static_cast<int>(held_.size()))
            held_[static_cast<size_t>(count_++)] = {
                e.channel, e.note, e.velocity, static_cast<float>(e.velocity), at};
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        releaseNote(e);
        for (int i = 0; i < count_; ++i)
            if (held_[static_cast<size_t>(i)].note == e.note &&
                held_[static_cast<size_t>(i)].channel == e.channel)
            {
                held_[static_cast<size_t>(i)] = held_[static_cast<size_t>(--count_)];
                break;
            }
        if (count_ == 0)
            clock_.stop();
    }
    void advance(std::int64_t from, std::int64_t to) noexcept override
    {
        while (count_ > 0)
        {
            std::int64_t index = 0;
            const std::int64_t at = clock_.next(from, to, blockClock(), playHead(), rateQuarters_, index);
            if (at < 0)
                return;
            const double step = stepSamples();
            const auto length = static_cast<std::int64_t>(std::max(1.0, gate_ * step));
            for (int i = 0; i < count_; ++i)
            {
                auto& key = held_[static_cast<size_t>(i)];
                // A step just after the key went down would hit twice.
                if (static_cast<double>(at - key.pressed) < step * 0.5)
                    continue;
                key.current = std::clamp(key.current * (1.0f + ramp_), 1.0f, 127.0f);
                play(at, length, key.channel, key.note, rounded(key.current));
            }
            from = at + 1;
        }
    }

    double rateQuarters_ = 0.25, gate_ = 0.5;
    float ramp_ = 0.0f;
    std::array<Key, 16> held_{};
    int count_ = 0;
    midi::StepClock clock_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<NoteRepeat>();
}
} // namespace noterepeat

// ----- Note Echo -------------------------------------------------------------------------

namespace noteecho
{
namespace
{
class NoteEcho final : public TimedEffect
{
public:
    NoteEcho()
        : TimedEffect(type,
                      {choice("Time", midi::noteValueChoices(), midi::noteValueEighth),
                       whole("Repeats", "", 1.0f, 16.0f, 3.0f),
                       whole("Feedback", "%", 10.0f, 100.0f, 60.0f),
                       whole("Pitch", "st", -12.0f, 12.0f, 0.0f)}),
          echoes_(16 * 128)
    {
    }
    int midiExpansion() const noexcept override { return 17; }

private:
    // What a note's echoes were when it started, so its release ends them
    // whatever the settings became.
    struct Echoes
    {
        std::int64_t delay = 0;
        std::uint8_t repeats = 0;
        std::int8_t pitch = 0;
    };

    void beginBlock() noexcept override
    {
        delay_ = static_cast<std::int64_t>(
            midi::samplesPerQuarters(playHead(), midi::noteValueQuarters(value(time))));
        repeats_ = std::clamp(rounded(value(repeats)), 1, 16);
        feedback_ = value(noteecho::feedback) / 100.0; // qualified: InsertProcessor has a feedback()
        pitch_ = std::clamp(rounded(value(pitch)), -12, 12);
    }
    void reset() noexcept override { std::fill(echoes_.begin(), echoes_.end(), Echoes{}); }
    void noteOn(const MidiEvent& e) noexcept override
    {
        startNote(e, e.note, e.velocity, e.channel); // the note itself
        const std::int64_t at = blockClock() + e.sampleOffset;
        int made = 0;
        double v = e.velocity;
        for (int k = 1; k <= repeats_; ++k)
        {
            v *= feedback_;
            if (v < 1.0)
                break;
            const int note = e.note + k * pitch_;
            if (note >= 0 && note <= 127)
                schedule(
                    at + k * delay_,
                    noteEvent(MidiEvent::Type::noteOn, e.channel, note, static_cast<int>(std::lround(v))));
            made = k;
        }
        echoes_[slot(e)] = {delay_, static_cast<std::uint8_t>(made), static_cast<std::int8_t>(pitch_)};
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        releaseNote(e);
        auto& echo = echoes_[slot(e)];
        // Each repeat lasts as long as the note did.
        const std::int64_t at = blockClock() + e.sampleOffset;
        for (int k = 1; k <= echo.repeats; ++k)
        {
            const int note = e.note + k * echo.pitch;
            if (note >= 0 && note <= 127)
                schedule(at + k * echo.delay, noteEvent(MidiEvent::Type::noteOff, e.channel, note, 0));
        }
        echo = {};
    }
    static size_t slot(const MidiEvent& e) noexcept
    {
        return static_cast<size_t>((e.channel & 15) * 128 + (e.note & 127));
    }

    std::int64_t delay_ = 1;
    int repeats_ = 3, pitch_ = 0;
    double feedback_ = 0.6;
    std::vector<Echoes> echoes_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<NoteEcho>();
}
} // namespace noteecho

} // namespace northlight::plugins
