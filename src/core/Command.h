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
