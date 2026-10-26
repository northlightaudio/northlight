#pragma once

#include "core/Model.h"
#include "engine/Insert.h"
#include "ui/Db.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

// An automation lane's vertical scale (screens/arrange.md "Automation"): a
// parameter's value as a height from 0 (the lane's bottom) to 1 (its top) and
// back, and that height as a y in the lane. Volume runs in dB from -60 to +6
// (silence at the bottom), pan from left to right, an insert's parameter over its
// range (on a log scale where it is logarithmic, in whole steps when it has
// choices).
struct AutomationScale
{
    core::AutomationTarget target = core::AutomationTarget::gain;
    const engine::ParameterInfo* info = nullptr; // an insert parameter's; null when unknown

    static constexpr float gainTopDb = 6.0f, gainBottomDb = -60.0f;

    float toNorm(float value) const noexcept
    {
        switch (target)
        {
            case core::AutomationTarget::gain:
                if (value <= 1.0e-6f)
                    return 0.0f;
                return std::clamp((static_cast<float>(db::fromGain(value)) - gainBottomDb) /
                                      (gainTopDb - gainBottomDb),
                                  0.0f,
                                  1.0f);
            case core::AutomationTarget::pan:
                return std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
            case core::AutomationTarget::insertParameter:
                if (info == nullptr)
                    return 0.5f;
                if (info->logarithmic && info->minimum > 0.0f)
                    return std::clamp(std::log(std::max(info->minimum, value) / info->minimum) /
                                          std::log(info->maximum / info->minimum),
                                      0.0f,
                                      1.0f);
                return std::clamp((value - info->minimum) / (info->maximum - info->minimum), 0.0f, 1.0f);
        }
        return 0.0f;
    }

    float fromNorm(float norm) const noexcept
    {
        norm = std::clamp(norm, 0.0f, 1.0f);
        switch (target)
        {
            case core::AutomationTarget::gain:
                if (norm <= 0.002f)
                    return 0.0f;
                return db::toGain(gainBottomDb + norm * (gainTopDb - gainBottomDb));
            case core::AutomationTarget::pan:
                return norm * 2.0f - 1.0f;
            case core::AutomationTarget::insertParameter:
            {
                if (info == nullptr)
                    return 0.0f;
                float v = info->logarithmic && info->minimum > 0.0f
                              ? info->minimum * std::pow(info->maximum / info->minimum, norm)
                              : info->minimum + norm * (info->maximum - info->minimum);
                if (!info->choices.empty())
                    v = std::round(v);
                return v;
            }
        }
        return 0.0f;
    }

    // A height as a y in a lane whose curve area runs from `top` for `height` px,
    // and back: one mapping for drawing, hit testing and dragging.
    static float yFor(float norm, int top, int height) noexcept
    {
        return static_cast<float>(top + height) - norm * static_cast<float>(height);
    }
    static float normAt(float y, int top, int height) noexcept
    {
        return (static_cast<float>(top + height) - y) / static_cast<float>(std::max(1, height));
    }
};

} // namespace northlight::ui
