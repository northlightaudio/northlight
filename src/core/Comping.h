#pragma once

#include "core/Command.h"
#include "core/Model.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Take lanes and comping (docs/design/14-comping.md): the pure comp edits on a
// TakeGroup, the clips a comp plays as, and the commands that change a group
// with undo. A group's comp is materialised as ordinary clips in Track::clips
// (TakeGroup::clips names them); every command here that changes what plays
// rebuilds those clips from the takes and the segments, reusing their ids.

namespace northlight::core
{

// The default crossfade at a comp boundary: 10 ms at the rate.
SamplePosition defaultCompCrossfade(double sampleRate) noexcept;

// What a comp plays: one clip per segment on its take's media (ids left
// invalid), in time order. At a boundary between two takes the clips overlap by
// the group's crossfade (at most half of either segment), centred on the
// boundary, with matching linear fades, so the renderer crossfades them at a
// constant sum; where a take does not reach across the boundary the splice is
// butt-joined. A segment plays only where its take has audio.
std::vector<AudioClip> compClips(const TakeGroup& group);

// Sorts the comp, drops segments of missing takes and empty ones, closes gaps
// (the earlier segment extends), clips it to the group's range and merges
// neighbours that play the same take. An empty comp plays the newest take.
void normaliseComp(TakeGroup& group);
// Makes `take` play over [start, end) (clamped to the take's audio). False when
// nothing changes.
bool selectTakeForRange(TakeGroup& group, Id take, SamplePosition start, SamplePosition end);
// Moves the boundary at `boundary` to `to` (clamped to its neighbours' outer
// edges; a segment squeezed to nothing goes). False when nothing changes.
bool moveCompBoundary(TakeGroup& group, SamplePosition boundary, SamplePosition to);
// The segment playing at a position, or null.
const CompSegment* segmentAt(const TakeGroup& group, SamplePosition position) noexcept;
// The track's group whose range holds a position, or null.
const TakeGroup* takeGroupAt(const Track& track, SamplePosition position) noexcept;

// Whether a finished recording's passes on a track become takes rather than
// plain clips: several passes (loop recording), a punch, or passes over a
// take group already on the track.
bool recordingMakesTakes(const Track& track, const std::vector<AudioClip>& passes, bool punch);

// Changes one take group: the subclass edits a copy, and the command replaces
// the group and rebuilds its clips (or drops it, or flattens it) in one step.
// Ids allocated on the first apply are kept for redo.
class TakeGroupEdit : public Command
{
public:
    void apply(Project& project) final;
    void revert(Project& project) final;
    Id groupId() const noexcept { return groupId_; }

protected:
    TakeGroupEdit(Id trackId, Id groupId);
    enum class Result
    {
        refuse,  // nothing changes
        rebuild, // the group stays; its clips are rebuilt from the comp
        remove,  // the group and its clips go
        flatten, // the group goes; its clips stay as ordinary clips
    };
    // Edits `group`: a copy of the current one, or a new empty one (with an id
    // and the default crossfade) when the command makes the group.
    virtual Result edit(Project& project, Track& track, TakeGroup& group) = 0;
    // For a command made without a group: the group to join (invalidId: a new one).
    virtual Id chooseGroup(const Track&) const { return invalidId; }

    Id trackId_;

private:
    Id groupId_;
    bool computed_ = false;
    Result result_ = Result::refuse;
    std::optional<TakeGroup> before_, after_;
    std::vector<AudioClip> removed_, added_;
};

// A finished recording's passes on one track (in recording order) as takes:
// they join the take group they overlap, or form a new one, and each newest
// pass plays over its own range.
class RecordTakes final : public TakeGroupEdit
{
public:
    RecordTakes(Id trackId, std::vector<AudioClip> passes);
    std::string name() const override { return "Record takes"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
    Id chooseGroup(const Track& track) const override;
    std::vector<AudioClip> passes_;
};

// Swipe comping: a take plays over a range (start >= end: over all of the take,
// the comp's "use this take").
class SelectTake final : public TakeGroupEdit
{
public:
    SelectTake(Id trackId, Id groupId, Id takeId, SamplePosition start = 0, SamplePosition end = 0);
    std::string name() const override { return "Select take"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
    Id takeId_;
    SamplePosition start_, end_;
};

// Deletes a take; where it played, the newest take left plays. The last take
// takes the group (and its clips) with it.
class RemoveTake final : public TakeGroupEdit
{
public:
    RemoveTake(Id trackId, Id groupId, Id takeId);
    std::string name() const override { return "Delete take"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
    Id takeId_;
};

class MoveCompBoundary final : public TakeGroupEdit
{
public:
    MoveCompBoundary(Id trackId, Id groupId, SamplePosition boundary, SamplePosition to);
    std::string name() const override { return "Move comp boundary"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
    SamplePosition boundary_, to_;
};

// The comp's clips become ordinary clips; the takes go.
class FlattenComp final : public TakeGroupEdit
{
public:
    FlattenComp(Id trackId, Id groupId);
    std::string name() const override { return "Flatten comp"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
};

class RenameTake final : public TakeGroupEdit
{
public:
    RenameTake(Id trackId, Id groupId, Id takeId, std::string name);
    std::string name() const override { return "Rename take"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
    Id takeId_;
    std::string name_;
};

// Shows or hides a group's take lanes (a view change: no engine work).
class SetTakeLanesExpanded final : public Command
{
public:
    SetTakeLanesExpanded(Id trackId, Id groupId, bool expanded);
    std::string name() const override { return expanded_ ? "Show take lanes" : "Hide take lanes"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, groupId_;
    bool expanded_, previous_ = false;
};

// Deletes a whole take group: its takes and its comp's clips.
class RemoveTakeGroup final : public TakeGroupEdit
{
public:
    RemoveTakeGroup(Id trackId, Id groupId);
    std::string name() const override { return "Delete takes"; }

private:
    Result edit(Project&, Track&, TakeGroup&) override;
};

// Moves a take group (its takes, its comp and the comp's clips as they are) by
// `delta` samples, onto another audio track when `toTrackId` differs; the group
// is pinned at zero. Moving a comp clip in the arrange view moves its group.
class MoveTakeGroup final : public Command
{
public:
    MoveTakeGroup(Id fromTrackId, Id groupId, Id toTrackId, SamplePosition delta);
    std::string name() const override { return "Move takes"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    void move(Project& p, Id from, Id to, SamplePosition delta);
    Id fromTrackId_, groupId_, toTrackId_;
    SamplePosition delta_, applied_ = 0;
    bool moved_ = false;
};

// "Move to new track": a new audio track right under the take's track, with the
// take as its clip, and the take deleted from the group (one undo step when run
// as a group). Empty when the take does not exist.
std::vector<std::unique_ptr<Command>>
moveTakeToNewTrack(const Project& project, Id trackId, Id groupId, Id takeId);

} // namespace northlight::core
