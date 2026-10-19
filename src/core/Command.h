#pragma once

#include "core/Model.h"

#include <memory>
#include <string>
#include <vector>

// Every edit to the Project is a Command with apply/revert. UndoStack owns the
// history. Commands capture whatever they need to revert at apply time, so they can
// be constructed before the model state they revert to is known.

namespace aural::core
{

class Command
{
public:
    virtual ~Command() = default;
    virtual std::string name() const = 0;
    virtual void apply(Project& project) = 0;
    virtual void revert(Project& project) = 0;
    // A live command changes a value the engine takes from the model without a
    // recompile (gains, pans, insert parameters and bypass): the session pushes
    // the new values instead of publishing a new snapshot.
    virtual bool isLive() const noexcept { return false; }
};

class UndoStack
{
public:
    UndoStack();
    ~UndoStack();
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    // Applies the command and pushes it; clears the redo history.
    void execute(Project& project, std::unique_ptr<Command> command);
    bool undo(Project& project);
    bool redo(Project& project);

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    std::string undoName() const;
    std::string redoName() const;
    void clear();

    // Groups the next `execute` calls into one undoable step until endGroup().
    void beginGroup(std::string name);
    void endGroup();

private:
    class Group;
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::unique_ptr<Group> openGroup_;
};

// ----- concrete commands ------------------------------------------------------------------

class AddTrack final : public Command
{
public:
    // Inserts a copy of `track` at `index` (or at the end when index < 0). The track's
    // id is allocated on apply if it is invalidId.
    AddTrack(Track track, int index = -1);
    std::string name() const override { return "Add track"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id trackId() const noexcept { return track_.id; }

private:
    Track track_;
    int index_;
};

class RemoveTrack final : public Command
{
public:
    explicit RemoveTrack(Id trackId);
    std::string name() const override { return "Remove track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    Track removed_;
    int index_ = -1;
};

class AddBus final : public Command
{
public:
    AddBus(Bus bus, int index = -1);
    std::string name() const override { return "Add bus"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id busId() const noexcept { return bus_.id; }

private:
    Bus bus_;
    int index_;
};

// Removing a bus also detaches everything routed or sent to it (restored on undo).
class RemoveBus final : public Command
{
public:
    explicit RemoveBus(Id busId);
    std::string name() const override { return "Remove bus"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id busId_;
    Bus removed_;
    int index_ = -1;
    std::vector<std::pair<Id, Id>> detachedOutputs_; // channel id, previous output bus
    struct DetachedSend
    {
        Id channel;
        Send send;
        size_t index; // where it sat in the channel's sends
    };
    std::vector<DetachedSend> detachedSends_;
};

// ----- fader groups ----------------------------------------------------------------------------

class AddGroup final : public Command
{
public:
    // The group's id is allocated on apply if it is invalidId. `members` join it
    // in the same step (and leave it again on undo).
    explicit AddGroup(Group group, std::vector<Id> members = {});
    std::string name() const override { return "Add group"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id groupId() const noexcept { return group_.id; }

private:
    Group group_;
    std::vector<Id> members_;
    std::vector<std::pair<Id, Id>> previousGroups_;
};

// Removing a group releases its members (restored on undo).
class RemoveGroup final : public Command
{
public:
    explicit RemoveGroup(Id groupId);
    std::string name() const override { return "Remove group"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id groupId_;
    Group removed_;
    int index_ = -1;
    std::vector<Id> members_;
};

// Puts a track or bus in a group (invalidId = none).
class SetChannelGroup final : public Command
{
public:
    SetChannelGroup(Id channelId, Id groupId);
    std::string name() const override { return "Set group"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, value_, previous_ = invalidId;
};

// Scales every member's gain by `ratio` (a fader move in dB applied to all),
// clamped to maxChannelGain; undo restores each member's own value.
class SetGroupGain final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    SetGroupGain(Id groupId, float ratio);
    std::string name() const override { return "Set group gain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id groupId_;
    float ratio_;
    std::vector<std::pair<Id, float>> previous_;
};

// Mutes or solos every member; undo restores each member's own state.
class SetGroupMute final : public Command
{
public:
    SetGroupMute(Id groupId, bool mute);
    std::string name() const override { return "Set group mute"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id groupId_;
    bool value_;
    std::vector<std::pair<Id, bool>> previous_;
};

class SetGroupSolo final : public Command
{
public:
    SetGroupSolo(Id groupId, bool solo);
    std::string name() const override { return "Set group solo"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id groupId_;
    bool value_;
    std::vector<std::pair<Id, bool>> previous_;
};

// Routes a track or bus to a bus (invalidId = master).
class SetOutputBus final : public Command
{
public:
    SetOutputBus(Id channelId, Id busId);
    std::string name() const override { return "Set output"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, value_, previous_ = invalidId;
};

// Adds or updates a send from a track or bus to a bus.
class SetSend final : public Command
{
public:
    SetSend(Id channelId, Id busId, float level, bool preFader);
    std::string name() const override { return "Set send"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    Send value_;
    bool existed_ = false;
    Send previous_;
};

class RemoveSend final : public Command
{
public:
    RemoveSend(Id channelId, Id busId);
    std::string name() const override { return "Remove send"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, busId_;
    bool existed_ = false;
    Send previous_;
};

class SetPhaseInvert final : public Command
{
public:
    SetPhaseInvert(Id channelId, bool invert);
    std::string name() const override { return "Invert phase"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    bool value_, previous_ = false;
};

class SetColour final : public Command
{
public:
    SetColour(Id channelId, std::uint32_t colour);
    std::string name() const override { return "Set colour"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    std::uint32_t value_, previous_ = 0;
};

class SetMasterGain final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    explicit SetMasterGain(float gain);
    std::string name() const override { return "Set master gain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    float value_, previous_ = 1.0f;
};

class AddInsert final : public Command
{
public:
    // Inserts at `index` (or the end when index < 0); the id is allocated on apply.
    AddInsert(Id channelId, Insert insert, int index = -1);
    std::string name() const override { return "Add insert"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id insertId() const noexcept { return insert_.id; }

private:
    Id channelId_;
    Insert insert_;
    int index_;
};

class RemoveInsert final : public Command
{
public:
    RemoveInsert(Id channelId, Id insertId);
    std::string name() const override { return "Remove insert"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    Insert removed_;
    int index_ = -1;
};

class MoveInsert final : public Command
{
public:
    MoveInsert(Id channelId, Id insertId, int newIndex);
    std::string name() const override { return "Move insert"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    int newIndex_, previousIndex_ = -1;
};

class SetInsertParameter final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    SetInsertParameter(Id channelId, Id insertId, int parameter, float value);
    std::string name() const override { return "Set parameter"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    int parameter_;
    float value_, previous_ = 0.0f;
    size_t previousSize_ = 0; // the params vector may have grown on apply
};

// Wet/dry mix of one insert slot (0..1).
class SetInsertMix final : public Command
{
public:
    SetInsertMix(Id channelId, Id insertId, float mix);
    std::string name() const override { return "Set insert mix"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    float value_, previous_ = 1.0f;
};

// The sidechain (key) source of one insert slot: a track or bus id, invalidId
// for none. Resolved at compile time; an impossible source is ignored there.
class SetInsertSidechain final : public Command
{
public:
    SetInsertSidechain(Id channelId, Id insertId, Id sourceId);
    std::string name() const override { return "Set insert sidechain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_, value_, previous_ = invalidId;
};

class SetInsertBypass final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    SetInsertBypass(Id channelId, Id insertId, bool bypass);
    std::string name() const override { return "Bypass insert"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    bool value_, previous_ = false;
};

// The fader-section commands below take a track or bus id.
class SetTrackName final : public Command
{
public:
    SetTrackName(Id trackId, std::string name);
    std::string name() const override { return "Rename track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    std::string value_, previous_;
};

class SetTrackGain final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    SetTrackGain(Id trackId, float gain);
    std::string name() const override { return "Set track gain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    float value_, previous_ = 1.0f;
};

class SetTrackPan final : public Command
{
public:
    bool isLive() const noexcept override { return true; }
    SetTrackPan(Id trackId, float pan);
    std::string name() const override { return "Set track pan"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    float value_, previous_ = 0.0f;
};

class SetTrackMute final : public Command
{
public:
    SetTrackMute(Id trackId, bool mute);
    std::string name() const override { return "Mute track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    bool value_, previous_ = false;
};

class SetTrackInput final : public Command
{
public:
    SetTrackInput(Id trackId, int inputChannel);
    std::string name() const override { return "Set track input"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    int value_, previous_ = -1;
};

class SetTrackArmed final : public Command
{
public:
    SetTrackArmed(Id trackId, bool armed);
    std::string name() const override { return "Arm track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    bool value_, previous_ = false;
};

class SetTrackMonitor final : public Command
{
public:
    SetTrackMonitor(Id trackId, MonitorMode mode);
    std::string name() const override { return "Set track monitoring"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    MonitorMode value_, previous_ = MonitorMode::automatic;
};

class SetTrackSolo final : public Command
{
public:
    SetTrackSolo(Id trackId, bool solo);
    std::string name() const override { return "Solo track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    bool value_, previous_ = false;
};

class AddClip final : public Command
{
public:
    // The clip's id is allocated on apply if it is invalidId.
    AddClip(Id trackId, AudioClip clip);
    std::string name() const override { return "Add clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id clipId() const noexcept { return clip_.id; }

private:
    Id trackId_;
    AudioClip clip_;
};

class RemoveClip final : public Command
{
public:
    RemoveClip(Id trackId, Id clipId);
    std::string name() const override { return "Remove clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    AudioClip removed_;
};

class MoveClip final : public Command
{
public:
    // Moves a clip to a new start position, optionally onto another track.
    MoveClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart);
    std::string name() const override { return "Move clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id fromTrackId_, clipId_, toTrackId_;
    SamplePosition newStart_, previousStart_ = 0;
};

class TrimClip final : public Command
{
public:
    // Sets new start/length/sourceOffset together (trim either edge, slip).
    TrimClip(Id trackId, Id clipId, SamplePosition start, SamplePosition length, SamplePosition sourceOffset);
    std::string name() const override { return "Trim clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    SamplePosition start_, length_, sourceOffset_;
    SamplePosition previousStart_ = 0, previousLength_ = 0, previousOffset_ = 0;
};

// Fade lengths in samples, clamped to the clip.
class SetClipFades final : public Command
{
public:
    SetClipFades(Id trackId, Id clipId, SamplePosition fadeIn, SamplePosition fadeOut);
    std::string name() const override { return "Set clip fades"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    SamplePosition fadeIn_, fadeOut_, previousIn_ = 0, previousOut_ = 0;
};

// Splits a clip at a timeline position strictly inside it. The left part keeps
// the id (and loses its fade-out); the right part gets a new id (and the fade-out).
class SplitClip final : public Command
{
public:
    SplitClip(Id trackId, Id clipId, SamplePosition at);
    std::string name() const override { return "Split clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id rightClipId() const noexcept { return right_.id; }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    AudioClip original_, right_;
};

// Joins two clips that are contiguous on the timeline and in the same source
// (a split undone by hand): the left grows, the right goes.
class JoinClips final : public Command
{
public:
    JoinClips(Id trackId, Id leftClipId, Id rightClipId);
    std::string name() const override { return "Join clips"; }
    void apply(Project&) override;
    void revert(Project&) override;
    static bool canJoin(const AudioClip& left, const AudioClip& right) noexcept;

private:
    Id trackId_, leftId_, rightId_;
    AudioClip previousLeft_, removedRight_;
};

// A copy of the clip placed right after it (or at `at` when given).
class DuplicateClip final : public Command
{
public:
    DuplicateClip(Id trackId, Id clipId, SamplePosition at = -1);
    std::string name() const override { return "Duplicate clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id newClipId() const noexcept { return copy_.id; }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    AudioClip copy_;
};

// Crossfades two overlapping clips on one track over their overlap: the earlier
// fades out, the later fades in.
class CrossfadeClips final : public Command
{
public:
    CrossfadeClips(Id trackId, Id leftClipId, Id rightClipId);
    std::string name() const override { return "Crossfade clips"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, leftId_, rightId_;
    SamplePosition previousOut_ = 0, previousIn_ = 0;
};

// Moves every clip starting at or after `from` by `delta` (ripple editing);
// clips that would start before zero are pinned at zero. Undo moves them back.
class ShiftClips final : public Command
{
public:
    ShiftClips(Id trackId, SamplePosition from, SamplePosition delta);
    std::string name() const override { return "Shift clips"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    SamplePosition from_, delta_;
    std::vector<std::pair<Id, SamplePosition>> previous_; // clip id, start before
};

class SetClipGain final : public Command
{
public:
    SetClipGain(Id trackId, Id clipId, float gain);
    std::string name() const override { return "Set clip gain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    float value_, previous_ = 1.0f;
};

class SetTempo final : public Command
{
public:
    explicit SetTempo(double bpm);
    std::string name() const override { return "Set tempo"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    double value_, previous_ = 120.0;
};

} // namespace aural::core
