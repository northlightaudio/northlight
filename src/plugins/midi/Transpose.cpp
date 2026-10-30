#include "plugins/midi/Transpose.h"

#include "plugins/midi/MidiTools.h"

#include <cmath>

namespace northlight::plugins::transpose
{

namespace
{
std::vector<engine::ParameterInfo> infos()
{
    std::vector<engine::ParameterInfo> list(count);
    list[steps] = {"Steps", "", -7.0f, 7.0f, 0.0f};
    list[semitones] = {"Semitones", "", -24.0f, 24.0f, 0.0f};
    list[octaves] = {"Octaves", "", -4.0f, 4.0f, 0.0f};
    for (auto& info : list)
        info.integer = true;
    return list;
}

class Transpose final : public midi::MidiEffectBase
{
public:
    Transpose() : MidiEffectBase(type, infos()) {}

private:
    void beginBlock() noexcept override
    {
        steps_ = static_cast<int>(std::lround(value(steps)));
        shift_ = static_cast<int>(std::lround(value(semitones))) +
                 12 * static_cast<int>(std::lround(value(octaves)));
        mask_ = playHead().keyRoot >= 0 ? playHead().keyMask : std::uint16_t{0};
    }
    void noteOn(const engine::MidiEvent& e) noexcept override
    {
        startNote(e, stepInScale(e.note, steps_, mask_) + shift_, e.velocity, e.channel);
    }

    int steps_ = 0;
    int shift_ = 0;
    std::uint16_t mask_ = 0;
};
} // namespace

int stepInScale(int note, int steps, std::uint16_t mask) noexcept
{
    if (steps == 0)
        return note;
    if ((mask & 0x0FFFu) == 0)
        return note + steps;
    // From the scale note at or below, by scale notes, keeping the distance.
    const midi::Key key{0, mask};
    int base = note;
    while (!key.contains(base))
        --base;
    const int offset = note - base;
    const int direction = steps > 0 ? 1 : -1;
    for (int left = steps > 0 ? steps : -steps; left > 0; --left)
    {
        base += direction;
        while (!key.contains(base))
            base += direction;
    }
    return base + offset;
}

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Transpose>();
}

} // namespace northlight::plugins::transpose
