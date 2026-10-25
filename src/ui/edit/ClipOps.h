#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// The arrange view's clip edits as plain functions: each takes the project and
// the selected clips and returns the commands (and the undo step's name). No
// JUCE, no view state, so the editing rules are tested on their own.

namespace northlight::ui::edit
{

// One selected clip: an audio clip (Track::clips) or a MIDI clip (Track::midiClips).
struct ClipRef
{
    core::Id track = core::invalidId;
    core::Id clip = core::invalidId;
    bool midi = false;
    bool operator==(const ClipRef&) const = default;
};
using Selection = std::vector<ClipRef>;

struct Edit
{
    std::vector<std::unique_ptr<core::Command>> commands;
    std::string name;
    bool empty() const noexcept { return commands.empty(); }
};

// Where a clip sits on the timeline, if it exists.
struct Span
{
    core::SamplePosition start = 0, length = 0;
    core::SamplePosition end() const noexcept { return start + length; }
};
std::optional<Span> spanOf(const core::Project& project, const ClipRef& ref);

// A clip only lands on a track of its own kind (MIDI on MIDI, audio on audio),
// whether it is moved, dragged or pasted.
bool takesClip(const core::Track& track, bool midi) noexcept;
// The track a clip lands on when moved `rows` rows (clamped to the tracks): null
// when that track is of the other kind or the clip's track is gone.
const core::Track* landingTrack(const core::Project& project, const ClipRef& ref, int rows);
bool contains(const Selection& selection, const ClipRef& ref);

// Removes the selected clips; with ripple, each audio clip's gap closes (later
// clips first, so each shift lands on clips already moved).
Edit deleteClips(const core::Project& project, const Selection& selection, bool ripple);
Edit duplicateClips(const core::Project& project, const Selection& selection);
// Splits the selected clips (every clip, with none selected) at a position, or
// at both ends of a range (the later cut first, so the ids stay valid).
Edit splitAt(const core::Project& project, const Selection& selection, core::SamplePosition at);
Edit splitAtRange(const core::Project& project,
                  const Selection& selection,
                  core::SamplePosition start,
                  core::SamplePosition end);
// Joins runs of selected audio clips that continue each other's source, and
// each track's selected MIDI clips into its earliest (across any gaps).
Edit joinClips(const core::Project& project, const Selection& selection);
// The next clip of the same kind on the clip's track (by start), if any.
std::optional<ClipRef> nextClip(const core::Project& project, const ClipRef& ref);
// Joins a clip with the next one (the glue tool), when they can join: audio
// that continues the same source, or any later MIDI clip. Empty otherwise.
Edit joinWithNext(const core::Project& project, const ClipRef& ref);
bool canJoinWithNext(const core::Project& project, const ClipRef& ref);
// Clip mute: whether a clip is muted, and the commands that set the clips'
// state (only for those that differ).
bool isMuted(const core::Project& project, const ClipRef& ref);
Edit setMuted(const core::Project& project, const Selection& selection, bool muted);
// Ctrl+M: mutes the clips when any of them plays, otherwise unmutes them.
Edit toggleMuted(const core::Project& project, const Selection& selection);
// Crossfades selected audio clips that overlap their predecessor on a track.
Edit crossfadeClips(const core::Project& project, const Selection& selection);
// Moves the selected clips by `step` samples (not before 0).
Edit nudgeClips(const core::Project& project, const Selection& selection, core::SamplePosition step);

// ----- drags (the arrange view's gestures, committed on release) ---------------------

// Moves the selected clips by `delta` samples (none before 0) and `rows` tracks,
// each onto a track of its own kind (others stay); a comp's clip moves its whole
// take group (takes, comp and clips) once. `moved` is where the selection goes.
struct Move
{
    Edit edit;
    Selection moved;
};
Move moveClips(const core::Project& project,
               const Selection& selection,
               core::SamplePosition delta,
               int rows);
// A clip's new start and length (and, for audio, where its source starts).
Edit trimClip(const core::Project& project,
              const ClipRef& ref,
              core::SamplePosition start,
              core::SamplePosition length,
              core::SamplePosition sourceOffset);
// An audio clip stretched to `length` (its transposition and tempo following kept).
Edit stretchClip(const core::Project& project, const ClipRef& ref, core::SamplePosition length);
// One of an audio clip's fades set (the other kept).
Edit setFade(
    const core::Project& project, const ClipRef& ref, bool fadeIn, core::SamplePosition length, float curve);
Edit setClipGain(const core::Project& project, const ClipRef& ref, float gain);

// The clipboard: copies of the clips with their track offsets from the topmost.
struct ClipboardItem
{
    bool midi = false;
    core::AudioClip audio;
    core::MidiClip midiClip;
    int trackOffset = 0; // rows from the first copied clip's track
};
std::vector<ClipboardItem> copyClips(const core::Project& project, const Selection& selection);
// Pastes at `at` with the topmost clip on track row `row`; a clip only lands on
// its own kind of track (MIDI on MIDI, audio on audio).
Edit pasteClips(const core::Project& project,
                const std::vector<ClipboardItem>& clipboard,
                int row,
                core::SamplePosition at);

} // namespace northlight::ui::edit
