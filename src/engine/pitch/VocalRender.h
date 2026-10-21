#pragma once

#include "core/Vocal.h"
#include "engine/pitch/PitchAnalysis.h"

#include <cstdint>
#include <vector>

// Rendering a vocal edit (docs/design/screens/pitch-editing.md, "Resynthesis" and
// "Editing model").
//
// The edit becomes a pitch shift per analysis frame. Within a note the curve as
// sung is split into the note's centre, its slow drift around the centre and its
// vibrato; the new pitch is
//
//     target + drift amount * drift + vibrato amount * vibrato
//
// with the amounts easing back to "as sung" over a note's attack and release so
// scoops and falls survive. Across the join between two notes the shift moves from
// one note's to the other's along a smoothstep, so the singer's own slide keeps its
// shape and simply moves with the notes.
//
// Audio is rebuilt with pitch-synchronous overlap-add: two-period Hann grains taken
// at the pitch marks and laid down at the new period, which keeps the formants. Only
// spans the edit touches are rendered; everywhere else, and wherever the shift is
// zero, the original samples play unchanged.

namespace northlight::engine::pitch
{

// The shift in cents per analysis frame (0 outside notes).
std::vector<float> shiftCurve(const PitchAnalysis& analysis, const core::VocalEdit& edit);
// The edited pitch per frame (0 where unvoiced): what the editor draws.
std::vector<float> editedCents(const PitchAnalysis& analysis, const std::vector<float>& shift);

// A stretch of the media the edit changes, rendered as a unit.
struct SpanPlan
{
    std::int64_t start = 0, end = 0; // source samples
    bool pitch = false;              // needs resynthesis (else only gain changes)
    std::uint64_t key = 0;           // identifies the render: same key, same samples
};
// The spans to render, merged so none overlap. `mediaKey` identifies the media
// (a hash of its path and rate) so keys differ between files.
std::vector<SpanPlan>
planSpans(const PitchAnalysis& analysis, const core::VocalEdit& edit, std::uint64_t mediaKey);

struct RenderedSpan
{
    std::int64_t start = 0;
    std::vector<std::vector<float>> channels;
    std::int64_t length() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels[0].size());
    }
};

// Renders one planned span from the media's samples (`media` at the analysis rate).
RenderedSpan renderSpan(const std::vector<std::vector<float>>& media,
                        const PitchAnalysis& analysis,
                        const core::VocalEdit& edit,
                        const std::vector<float>& shift,
                        const SpanPlan& plan);

std::uint64_t hashBytes(const void* data, size_t size, std::uint64_t seed = 1469598103934665603ull) noexcept;

} // namespace northlight::engine::pitch
