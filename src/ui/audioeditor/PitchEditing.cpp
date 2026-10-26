#include "ui/audioeditor/PitchEditing.h"

#include "core/NoteNames.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::pitchedit
{

float dragDelta(float target, float delta, bool free) noexcept
{
    return free ? delta : std::round((target + delta) / 100.0f) * 100.0f - target;
}

juce::String signedCents(float cents)
{
    const int c = static_cast<int>(std::lround(cents));
    return (c > 0   ? "+"
            : c < 0 ? juce::String::fromUTF8("\xe2\x88\x92")
                    : "") +
           juce::String(std::abs(c)) + " ct";
}

juce::String label(float cents)
{
    const float nearest = std::round(cents / 100.0f) * 100.0f;
    juce::String text(core::noteName(static_cast<int>(std::lround(cents / 100.0f))));
    if (std::abs(cents - nearest) >= 1.0f)
        text += " " + signedCents(cents - nearest);
    return text;
}

const core::VocalNote* noteAt(const core::VocalEdit& edit,
                              core::SamplePosition source,
                              float y,
                              const std::function<float(float cents)>& yForCents,
                              float rowHeight)
{
    const core::VocalNote* best = nullptr;
    float bestDistance = 1.0e9f;
    for (const auto& n : edit.notes)
    {
        if (source < n.start || source >= n.end)
            continue;
        const float d = std::abs(yForCents(n.target) - y);
        if (d < rowHeight * 1.2f && d < bestDistance)
        {
            best = &n;
            bestDistance = d;
        }
    }
    return best;
}

const core::VocalNote* joinAt(const std::vector<core::VocalNote>& notes,
                              float x,
                              float y,
                              const std::function<float(double source)>& xForSource,
                              const std::function<float(float cents)>& yForCents,
                              float rowHeight)
{
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        if (notes[i].end != notes[i + 1].start)
            continue;
        const float jx = xForSource(static_cast<double>(notes[i].end));
        const float y0 = yForCents(notes[i].target), y1 = yForCents(notes[i + 1].target);
        if (std::abs(x - jx) <= 4.0f && y >= std::min(y0, y1) - rowHeight &&
            y <= std::max(y0, y1) + rowHeight)
            return &notes[i];
    }
    return nullptr;
}

} // namespace northlight::ui::pitchedit
