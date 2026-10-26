#include "plugins/vesper/Spaces.h"

#include "plugins/Vesper.h"

namespace northlight::plugins::vesper
{

namespace
{
std::array<Space, spaceCount> makeSpaces()
{
    std::array<Space, spaceCount> s{};
    const auto base = [](Space& x)
    {
        x.tuning.maxScale = maxSizePercent / 100.0f;
        x.tuning.maxModSeconds = 0.001f;
        x.tuning.shimmerWindowSeconds = 0.0f;
        x.tuning.energyCeiling = 0.5f;
    };
    // Plate: dense at once and bright, no early reflections, the highs damped late.
    auto& plate = s[0];
    base(plate);
    plate.tuning.lineMs = {13.1f, 15.7f, 17.9f, 20.3f, 23.1f, 25.7f, 28.9f, 31.9f};
    plate.tuning.allpassMs = {1.3f, 1.9f, 2.3f, 2.9f, 3.1f, 3.7f, 4.1f, 4.7f};
    plate.tuning.diffuserLeftMs = {0.9f, 1.4f, 2.1f, 3.3f};
    plate.tuning.diffuserRightMs = {1.0f, 1.6f, 2.3f, 3.6f};
    plate.tuning.bandDecayRatio = 3.0f;
    plate.tuning.earlyGain = 0.7f;
    plate.inject = 1.0f;
    plate.diffusion = 0.9f;
    plate.modSeconds = 0.0003f;
    plate.modHz = 0.7f;
    // Chamber: a small live room, a little early slap, warm.
    auto& chamber = s[1];
    base(chamber);
    chamber.tuning.lineMs = {17.3f, 19.9f, 23.3f, 26.1f, 29.7f, 33.1f, 37.3f, 41.9f};
    chamber.tuning.allpassMs = {1.9f, 2.7f, 3.3f, 4.1f, 4.9f, 5.7f, 6.3f, 7.1f};
    chamber.tuning.diffuserLeftMs = {1.3f, 2.3f, 3.7f, 5.3f};
    chamber.tuning.diffuserRightMs = {1.5f, 2.5f, 4.1f, 5.9f};
    chamber.tuning.bandDecayRatio = 4.0f;
    chamber.tuning.earlyGain = 0.45f;
    chamber.inject = 0.9f;
    chamber.diffusion = 0.7f;
    chamber.modSeconds = 0.0002f;
    chamber.modHz = 0.5f;
    chamber.taps = 6;
    chamber.tapMs = {8.3f, 11.9f, 15.7f, 19.1f, 23.9f, 29.3f};
    chamber.tapGain = {0.45f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f};
    // Hall: big and slow to build, for ballads.
    auto& hall = s[2];
    base(hall);
    hall.tuning.lineMs = {28.5f, 33.6f, 37.7f, 42.4f, 48.0f, 53.9f, 60.4f, 66.5f};
    hall.tuning.allpassMs = {2.9f, 3.9f, 5.1f, 6.3f, 7.5f, 8.7f, 10.1f, 11.5f};
    hall.tuning.diffuserLeftMs = {1.7f, 2.9f, 5.3f, 7.9f};
    hall.tuning.diffuserRightMs = {1.9f, 3.1f, 5.9f, 8.3f};
    hall.tuning.bandDecayRatio = 4.0f;
    hall.tuning.earlyGain = 0.35f;
    hall.inject = 0.8f;
    hall.diffusion = 0.75f;
    hall.modSeconds = 0.0005f;
    hall.modHz = 0.35f;
    hall.taps = 8;
    hall.tapMs = {12.1f, 17.3f, 23.9f, 29.7f, 36.1f, 43.3f, 51.7f, 59.9f};
    hall.tapGain = {0.35f, 0.32f, 0.3f, 0.27f, 0.24f, 0.2f, 0.17f, 0.14f};
    return s;
}
} // namespace

const std::array<Space, spaceCount>& spaces()
{
    static const auto s = makeSpaces();
    return s;
}

} // namespace northlight::plugins::vesper
