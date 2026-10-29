#include "plugins/midi/Utilities.h"

#include "plugins/midi/MidiEffectBase.h"
#include "plugins/midi/MidiTools.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace northlight::plugins
{

using engine::MidiEvent;
using engine::ParameterInfo;

namespace
{
int rounded(float v) noexcept
{
    return static_cast<int>(std::lround(v));
}

ParameterInfo choice(const char* name, std::vector<std::string> choices, int defaultIndex = 0)
{
    ParameterInfo info;
    info.name = name;
    info.minimum = 0.0f;
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = static_cast<float>(defaultIndex);
    info.choices = std::move(choices);
    return info;
}

ParameterInfo range(const char* name, const char* unit, float minimum, float maximum, float defaultValue)
{
    ParameterInfo info;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    return info;
}

// A whole-number setting (a count, semitones, a velocity).
ParameterInfo whole(const char* name, const char* unit, float minimum, float maximum, float defaultValue)
{
    auto info = range(name, unit, minimum, maximum, defaultValue);
    info.integer = true;
    return info;
}

// Its minimum means off.
ParameterInfo off(ParameterInfo info)
{
    info.minimumText = "Off";
    return info;
}

// Shown as a note name.
ParameterInfo note(ParameterInfo info)
{
    info.showsNote = true;
    return info;
}

std::vector<std::string> blockPass()
{
    return {"Block", "Pass"};
}
} // namespace

// ----- Scale -----------------------------------------------------------------------------

namespace scale
{
int place(int note, int mode, int root, std::uint16_t mask) noexcept
{
    const midi::Key key{root, mask};
    if ((mask & 0x0FFFu) == 0)
        return note;
    int out = note;
    switch (mode)
    {
        case nearest:
            // The closer scale note; a tie goes down.
            for (int d = 0; d <= 6; ++d)
            {
                if (key.contains(note - d))
                {
                    out = note - d;
                    break;
                }
                if (key.contains(note + d))
                {
                    out = note + d;
                    break;
                }
            }
            break;
        case up:
            while (!key.contains(out))
                ++out;
            break;
        case down:
            while (!key.contains(out))
                --out;
            break;
        case block:
            out = key.contains(note) ? note : -1;
            break;
        case whiteKeys:
        {
            // C D E F G A B of each octave play the scale's degrees from the root.
            static constexpr int whiteIndex[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
            const int pc = ((note % 12) + 12) % 12;
            if (whiteIndex[pc] < 0)
                return -1;
            const auto degrees = midi::degreesOf(key);
            out = midi::degreeNote(degrees, note - pc + root, whiteIndex[pc]);
            break;
        }
        default:
            break;
    }
    return out >= 0 && out <= 127 ? out : -1;
}

namespace
{
class Scale final : public midi::MidiEffectBase
{
public:
    Scale()
        : MidiEffectBase(type,
                         {choice("Mode", {"Nearest", "Up", "Down", "Block", "White keys"}),
                          choice("Key", {"Own", "Project"}, 1),
                          choice("Root", midi::rootChoices()),
                          choice("Scale", midi::scaleChoices())})
    {
    }

private:
    void beginBlock() noexcept override
    {
        mode_ = rounded(value(mode));
        key_ = midi::keyFor(playHead(), rounded(value(follow)) == 1, value(root), value(kind));
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        const int n = place(e.note, mode_, key_.root, key_.mask);
        if (n < 0)
            take(e);
        else
            startNote(e, n, e.velocity, e.channel);
    }

    int mode_ = nearest;
    midi::Key key_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Scale>();
}
} // namespace scale

// ----- Velocity --------------------------------------------------------------------------

namespace velocity
{
int shape(int v, const Settings& s, float random) noexcept
{
    if (s.fixed >= 1.0f)
        return std::clamp(rounded(s.fixed), 1, 127);
    double x = s.centre + (static_cast<double>(v) - s.centre) * s.dynamics / 100.0;
    x = x * s.gain / 100.0 + s.offset + random;
    const double lo = std::min(s.minimum, s.maximum), hi = std::max(s.minimum, s.maximum);
    return std::clamp(static_cast<int>(std::lround(std::clamp(x, lo, hi))), 1, 127);
}

namespace
{
class Velocity final : public midi::MidiEffectBase
{
public:
    Velocity()
        : MidiEffectBase(type,
                         {whole("Dynamics", "%", 0.0f, 200.0f, 100.0f),
                          whole("Centre", "", 1.0f, 127.0f, 64.0f),
                          whole("Gain", "%", 0.0f, 200.0f, 100.0f),
                          whole("Offset", "", -127.0f, 127.0f, 0.0f),
                          whole("Random", "", 0.0f, 64.0f, 0.0f),
                          whole("Minimum", "", 1.0f, 127.0f, 1.0f),
                          whole("Maximum", "", 1.0f, 127.0f, 127.0f),
                          off(whole("Fixed", "", 0.0f, 127.0f, 0.0f))})
    {
    }

private:
    void beginBlock() noexcept override
    {
        settings_ = {value(dynamics),
                     value(centre),
                     value(gain),
                     value(offset),
                     value(minimum),
                     value(maximum),
                     value(fixed)};
        random_ = value(random);
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        const float r = random_ > 0.0f ? static_cast<float>(rng_.bipolar()) * random_ : 0.0f;
        startNote(e, e.note, shape(e.velocity, settings_, r), e.channel);
    }

    Settings settings_;
    float random_ = 0.0f;
    midi::Random rng_;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Velocity>();
}
} // namespace velocity

// ----- Key Range -------------------------------------------------------------------------

namespace keyrange
{
namespace
{
class KeyRange final : public midi::MidiEffectBase
{
public:
    KeyRange()
        : MidiEffectBase(type,
                         {note(whole("Low", "", 0.0f, 127.0f, 0.0f)),
                          note(whole("High", "", 0.0f, 127.0f, 127.0f)),
                          choice("Outside", {"Block", "Fold in"})})
    {
    }

private:
    void beginBlock() noexcept override
    {
        low_ = rounded(value(low));
        high_ = rounded(value(high));
        if (low_ > high_)
            std::swap(low_, high_);
        fold_ = rounded(value(mode)) == 1;
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        int n = e.note;
        if (fold_)
        {
            while (n < low_ && n + 12 <= 127)
                n += 12;
            while (n > high_ && n - 12 >= 0)
                n -= 12;
        }
        if (n < low_ || n > high_)
            take(e);
        else
            startNote(e, n, e.velocity, e.channel);
    }

    int low_ = 0, high_ = 127;
    bool fold_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<KeyRange>();
}
} // namespace keyrange

// ----- MIDI Filter -----------------------------------------------------------------------

namespace midifilter
{
namespace
{
class MidiFilter final : public midi::MidiEffectBase
{
public:
    MidiFilter()
        : MidiEffectBase(type,
                         {choice("Notes", blockPass(), 1),
                          choice("Controllers", blockPass(), 1),
                          choice("Pitch bend", blockPass(), 1),
                          choice("Channel pressure", blockPass(), 1),
                          choice("Poly pressure", blockPass(), 1),
                          off(whole("Move controller", "CC", -1.0f, 127.0f, -1.0f)),
                          whole("To controller", "CC", 0.0f, 127.0f, 1.0f)})
    {
    }

private:
    void beginBlock() noexcept override
    {
        for (int i = notes; i <= polyPressure; ++i)
            pass_[static_cast<size_t>(i)] = rounded(value(i)) == 1;
        from_ = rounded(value(mapFrom));
        to_ = std::clamp(rounded(value(mapTo)), 0, 127);
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        if (pass_[notes])
            startNote(e, e.note, e.velocity, e.channel);
        else
            take(e);
    }
    void otherEvent(const MidiEvent& e) noexcept override
    {
        MidiEvent out = e;
        switch (e.type)
        {
            case MidiEvent::Type::controller:
                if (!pass_[controllers])
                    return;
                if (from_ >= 0 && e.note == from_)
                    out.note = static_cast<std::uint8_t>(to_);
                break;
            case MidiEvent::Type::pitchBend:
                if (!pass_[pitchBend])
                    return;
                break;
            case MidiEvent::Type::channelPressure:
                if (!pass_[channelPressure])
                    return;
                break;
            case MidiEvent::Type::polyPressure:
                if (!pass_[polyPressure])
                    return;
                break;
            default:
                break;
        }
        MidiEffectBase::otherEvent(out);
    }

    std::array<bool, 5> pass_{true, true, true, true, true};
    int from_ = -1, to_ = 1;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<MidiFilter>();
}
} // namespace midifilter

// ----- Note Length -----------------------------------------------------------------------

namespace notelength
{
namespace
{
class NoteLength final : public midi::MidiEffectBase
{
public:
    NoteLength()
        : MidiEffectBase(type,
                         {choice("Length", {"As played", "Note value", "Time", "Legato"}),
                          choice("Note value", midi::noteValueChoices(), midi::noteValueSixteenth),
                          whole("Time", "ms", 5.0f, 4000.0f, 100.0f),
                          choice("Sustain pedal", {"Pass", "Hold notes"})})
    {
    }

private:
    struct Legato
    {
        std::uint8_t channel = 0, note = 0;
        std::int64_t started = 0;
    };

    void beginBlock() noexcept override
    {
        mode_ = rounded(value(mode));
        hold_ = rounded(value(pedal)) == 1;
        if (mode_ == noteValue)
            length_ = static_cast<std::int64_t>(
                midi::samplesPerQuarters(playHead(), midi::noteValueQuarters(value(notelength::value))));
        else
            length_ = static_cast<std::int64_t>(std::max(1.0, value(time) * 0.001 * sampleRate()));
    }
    void reset() noexcept override
    {
        pedalDown_ = false;
        deferredCount_ = 0;
        legatoCount_ = 0;
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        // A key pressed again while the pedal holds its last press: the pedal
        // no longer owns it.
        undefer(e.channel, e.note);
        const std::int64_t at = blockClock() + e.sampleOffset;
        switch (mode_)
        {
            case noteValue:
            case fixedTime:
            {
                take(e);
                MidiEvent off = e;
                off.type = MidiEvent::Type::noteOff;
                off.velocity = 0;
                if (emit(e))
                    schedule(at + length_, off);
                return;
            }
            case legato:
            {
                // Everything an earlier note started ends where this one starts
                // (notes struck together, a chord, stay together).
                int kept = 0;
                for (int i = 0; i < legatoCount_; ++i)
                {
                    const auto& l = legato_[static_cast<size_t>(i)];
                    if (l.started < at)
                    {
                        MidiEvent off{e.sampleOffset, MidiEvent::Type::noteOff, l.channel, l.note, 0};
                        emit(off);
                    }
                    else
                        legato_[static_cast<size_t>(kept++)] = l;
                }
                legatoCount_ = kept;
                take(e);
                if (emit(e) && legatoCount_ < static_cast<int>(legato_.size()))
                    legato_[static_cast<size_t>(legatoCount_++)] = {e.channel, e.note, at};
                return;
            }
            default:
                startNote(e, e.note, e.velocity, e.channel);
                return;
        }
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        if (hold_ && pedalDown_ && mode_ == asPlayed)
        {
            defer(e.channel, e.note);
            return;
        }
        releaseNote(e);
    }
    void otherEvent(const MidiEvent& e) noexcept override
    {
        if (hold_ && e.type == MidiEvent::Type::controller && e.note == 64)
        {
            const bool down = e.velocity >= 64;
            if (pedalDown_ && !down)
            {
                // The pedal lifts: what it held ends.
                for (int i = 0; i < deferredCount_; ++i)
                {
                    const auto& d = deferred_[static_cast<size_t>(i)];
                    releaseNote({e.sampleOffset, MidiEvent::Type::noteOff, d.channel, d.note, 0});
                }
                deferredCount_ = 0;
            }
            pedalDown_ = down;
            return; // the pedal itself is not sent
        }
        MidiEffectBase::otherEvent(e);
    }

    void defer(std::uint8_t channel, std::uint8_t note) noexcept
    {
        undefer(channel, note);
        if (deferredCount_ < static_cast<int>(deferred_.size()))
            deferred_[static_cast<size_t>(deferredCount_++)] = {channel, note, 0};
    }
    void undefer(std::uint8_t channel, std::uint8_t note) noexcept
    {
        for (int i = 0; i < deferredCount_; ++i)
            if (deferred_[static_cast<size_t>(i)].channel == channel &&
                deferred_[static_cast<size_t>(i)].note == note)
            {
                deferred_[static_cast<size_t>(i)] = deferred_[static_cast<size_t>(--deferredCount_)];
                return;
            }
    }

    int mode_ = asPlayed;
    bool hold_ = false;
    std::int64_t length_ = 1;
    bool pedalDown_ = false;
    std::array<Legato, 128> deferred_{};
    int deferredCount_ = 0;
    std::array<Legato, 64> legato_{};
    int legatoCount_ = 0;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<NoteLength>();
}
} // namespace notelength

// ----- Humanise --------------------------------------------------------------------------

namespace humanise
{
namespace
{
class Humanise final : public midi::MidiEffectBase
{
public:
    Humanise()
        : MidiEffectBase(type,
                         {whole("Timing", "ms", 0.0f, 50.0f, 10.0f),
                          whole("Velocity", "", 0.0f, 64.0f, 8.0f),
                          whole("Chance", "%", 0.0f, 100.0f, 100.0f),
                          whole("Seed", "", 0.0f, 9999.0f, 1.0f)}),
          delays_(16 * 128, -1)
    {
    }

private:
    void beginBlock() noexcept override
    {
        timing_ = value(timing) * 0.001 * sampleRate();
        velocity_ = value(velocity);
        chance_ = value(chance) / 100.0;
        if (const int s = rounded(value(seed)); s != seededWith_)
            reseed(s);
    }
    // A jump plays the same variations from the same place.
    void reset() noexcept override
    {
        std::fill(delays_.begin(), delays_.end(), -1);
        reseed(rounded(value(seed)));
    }
    void reseed(int s) noexcept
    {
        seededWith_ = s;
        rng_.seed(static_cast<std::uint32_t>(s) * 2654435761u + 1u);
    }
    void noteOn(const MidiEvent& e) noexcept override
    {
        take(e);
        // Draw all three every note, so changing one amount does not change the
        // others' variations.
        const double play = rng_.unit();
        const double late = rng_.unit();
        const double vary = rng_.bipolar();
        if (play >= chance_)
            return; // this note does not play; its release ends nothing
        const auto delay = static_cast<std::int64_t>(late * timing_);
        MidiEvent on = e;
        on.velocity = static_cast<std::uint8_t>(
            std::clamp(rounded(static_cast<float>(e.velocity + vary * velocity_)), 1, 127));
        if (schedule(blockClock() + e.sampleOffset + delay, on))
            delays_[slot(e)] = static_cast<std::int32_t>(delay);
    }
    void noteOff(const MidiEvent& e) noexcept override
    {
        auto& delay = delays_[slot(e)];
        if (delay < 0)
        {
            releaseNote(e); // not one it played: passes, ends only what sounds
            return;
        }
        // It ends as late as it started, so it keeps its length.
        schedule(blockClock() + e.sampleOffset + delay, e);
        delay = -1;
        releaseNote(e); // forgets the input (it started nothing directly)
    }
    static size_t slot(const MidiEvent& e) noexcept
    {
        return static_cast<size_t>((e.channel & 15) * 128 + (e.note & 127));
    }

    double timing_ = 0.0, chance_ = 1.0;
    float velocity_ = 0.0f;
    int seededWith_ = -1;
    midi::Random rng_;
    std::vector<std::int32_t> delays_; // per input note: its delay, -1 when not playing
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Humanise>();
}
} // namespace humanise

} // namespace northlight::plugins
