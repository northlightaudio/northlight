#pragma once

#include "core/Model.h"
#include "ui/Icons.h"

namespace northlight::ui
{

// The icon for a kind of track (design-system.md 2.5): shown in the track header
// in place of the number and beside the inspector's caption.
inline Icon trackIcon(core::TrackKind kind) noexcept
{
    switch (kind)
    {
        case core::TrackKind::audio:
            return Icon::trackAudio;
        case core::TrackKind::midi:
            return Icon::trackMidi;
        case core::TrackKind::instrument:
            return Icon::instrument;
        case core::TrackKind::aux:
            return Icon::trackAux;
        case core::TrackKind::folder:
            return Icon::folder;
    }
    return Icon::trackAudio;
}

// The kind of track in words ("Audio track"), for tooltips and accessibility.
inline const char* trackKindName(core::TrackKind kind) noexcept
{
    switch (kind)
    {
        case core::TrackKind::audio:
            return "Audio track";
        case core::TrackKind::midi:
            return "MIDI track";
        case core::TrackKind::instrument:
            return "Instrument channel";
        case core::TrackKind::aux:
            return "Instrument output";
        case core::TrackKind::folder:
            return "Folder track";
    }
    return "Track";
}

} // namespace northlight::ui
