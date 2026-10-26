#pragma once

#include "core/Command.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// The arrange view's track and range edits (screens/arrange.md): duplicating a
// track, moving several tracks (and folders with what they hold) as a block,
// reordering buses, naming and colouring clips, and removing a time range from
// tracks. Plain model edits, each a Command with apply and revert.

namespace northlight::core
{

// Where a new track goes so that it lands after `selected` (screens/arrange.md
// "new tracks go after the selected track"): the index to insert at and the
// folder it joins. A selected folder takes the new track at the end of what it
// holds; any other track has it right after it, in its folder. With no
// selected track (or a bus): the end, in no folder.
struct TrackPlace
{
    int index = -1; // -1: the end
    Id folder = invalidId;
};
TrackPlace placeAfter(const Project& project, Id selected);

// The index just past a track and, for a folder, everything inside it.
int spanEnd(const Project& project, Id track);

// `tracks` and every track inside a folder among them, in project order.
std::vector<Id> withFolderContents(const Project& project, const std::vector<Id>& tracks);

// The part of a clip inside [from, to): its source offset follows the cut, and a
// fade is kept only at an edge of the clip itself. None when they do not overlap.
std::optional<AudioClip> clipPart(const AudioClip& clip, SamplePosition from, SamplePosition to);
// The same for a MIDI clip: notes that start inside are kept (cut at the end),
// controller events inside are kept, and each controller's value at `from`
// opens the part so it plays the same on its own. Kept notes and events keep
// their ids.
std::optional<MidiClip> midiClipPart(const MidiClip& clip, SamplePosition from, SamplePosition to);

// A copy of a track right after it, in its folder: its clips, MIDI clips, take
// groups, inserts, strip, sends and automation, every part with a new id (kept
// for redo). The copy is named "<name> copy" and is not frozen.
class DuplicateTrack final : public Command
{
public:
    explicit DuplicateTrack(Id trackId);
    std::string name() const override { return "Duplicate track"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id copyId() const noexcept { return copy_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(copy_); }

private:
    Id trackId_;
    Track copy_;
    bool made_ = false;
};

// Moves tracks as a block, in their project order, to just before `before`
// (invalidId: the end), and puts those of them not inside another moved folder
// into `folder` (invalidId: none). A folder moves with what it holds when the
// list carries it (withFolderContents). Fails when `before` is one of the moved
// tracks, or the folder is moved or sits inside a moved one.
class MoveTracks final : public Command
{
public:
    MoveTracks(std::vector<Id> tracks, Id before, Id folder);
    std::string name() const override { return ids_.size() > 1 ? "Move tracks" : "Move track"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    std::vector<Id> ids_;
    Id before_, folder_;
    std::vector<Id> previousOrder_;
    std::vector<std::pair<Id, Id>> previousFolders_; // track, its folder before
};

// Moves a bus to another position among the buses (the arrange view's and the
// mixer's order).
class MoveBus final : public Command
{
public:
    MoveBus(Id busId, int newIndex);
    std::string name() const override { return "Move bus"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id busId_;
    int newIndex_, oldIndex_ = -1;
};

// A clip's name or colour (`midi`: a MIDI clip). What the views show only.
class SetClipName final : public Command
{
public:
    SetClipName(Id trackId, Id clipId, bool midi, std::string name);
    std::string name() const override { return "Rename clip"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    std::string* field(Project& p) const;
    Id trackId_, clipId_;
    bool midi_;
    std::string value_, previous_;
};

// 0 gives the clip back its track's colour.
class SetClipColour final : public Command
{
public:
    SetClipColour(Id trackId, Id clipId, bool midi, std::uint32_t colour);
    std::string name() const override { return "Colour clip"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    std::uint32_t* field(Project& p) const;
    Id trackId_, clipId_;
    bool midi_;
    std::uint32_t value_, previous_ = 0;
};

// Removes [start, end) from tracks (the range tool's Delete): clips inside go,
// clips across an end are cut there (the source stays put), and with
// `automation` each lane loses its points inside and keeps its value at both
// ends. With `ripple`, everything on those tracks after the range (clips, MIDI
// clips, take groups and, with `automation`, points) moves back by its length.
// The first apply works the result out (new parts get ids); redo puts it back.
class RemoveTimeRange final : public Command
{
public:
    RemoveTimeRange(std::vector<Id> tracks,
                    SamplePosition start,
                    SamplePosition end,
                    bool ripple,
                    bool automation,
                    std::string name = {});
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override;

private:
    struct State
    {
        Id track = invalidId;
        std::vector<AudioClip> clips;
        std::vector<MidiClip> midiClips;
        std::vector<TakeGroup> takeGroups;
        std::vector<AutomationLane> automation;
    };
    static State capture(const Track& t);
    static void restore(Track& t, const State& s);
    std::vector<Id> tracks_;
    SamplePosition start_, end_;
    bool ripple_, automation_;
    std::string name_;
    std::vector<State> before_, after_;
    Id nextIdAfter_ = invalidId;
};

} // namespace northlight::core
