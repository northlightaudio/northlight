#pragma once

#include "core/Model.h"
#include "core/TempoMap.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
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
    // What a command changes, from the least costly to the most: the session
    // picks the engine work from it (and from a group's widest member).
    //   cosmetic:   only what the views show (a row height); no engine work.
    //   parameters: insert parameters; pushed to their processors.
    //   values:     fader, pan, mute and bypass values the running graph reads
    //               without a recompile; every channel's values are pushed.
    //   structure:  anything else; a new snapshot is compiled.
    enum class ChangeKind : std::uint8_t
    {
        cosmetic,
        parameters,
        values,
        structure,
    };
    virtual ChangeKind changeKind() const noexcept { return ChangeKind::structure; }
    bool isCosmetic() const noexcept { return changeKind() == ChangeKind::cosmetic; }
    // A command that copies inserts for undo (removing or replacing them) needs
    // the live plugin state pulled into the model first, so undo brings back
    // what the user had rather than what the last save held.
    virtual bool capturesInsertState() const noexcept { return false; }
    // A new track or bus takes these strip modules when it has none (the
    // session supplies its default strip). Before apply only.
    virtual void setDefaultStrip(const std::vector<Insert>&) {}
    // A value that, while the transport plays in a writing automation mode, is
    // written into automation instead of set (a fader, pan or parameter move).
    struct AutomationWrite
    {
        Id channel = invalidId;
        AutomationTarget target = AutomationTarget::gain;
        Id insert = invalidId;
        int parameter = 0;
        float value = 0.0f;
    };
    virtual std::optional<AutomationWrite> automationWrite() const noexcept { return std::nullopt; }
    // The insert parameters this command sets (or restores on undo), so the
    // session can push exactly those to processors that own their parameters.
    struct ParameterTouch
    {
        Id insertId = invalidId;
        int parameter = 0;
    };
    virtual void touchedParameters(std::vector<ParameterTouch>&) const {}
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
    // The command a successful undo() moved to the redo side, and vice versa.
    const Command* redoTop() const noexcept { return redo_.empty() ? nullptr : redo_.back().get(); }
    const Command* undoTop() const noexcept { return undo_.empty() ? nullptr : undo_.back().get(); }
    std::string undoName() const;
    std::string redoName() const;
    void clear();

    // Groups the next `execute` calls into one undoable step until endGroup().
    // Groups nest: an inner begin/end pair joins the open group (keeping its
    // name) and only the outermost endGroup() commits it.
    void beginGroup(std::string name);
    void endGroup();
    bool groupOpen() const noexcept { return openGroup_ != nullptr; }

private:
    class Group;
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::unique_ptr<Group> openGroup_;
    int groupDepth_ = 0;
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
    // Gives the track these strip modules when it has none (the session supplies
    // the default strip; ids are allocated on apply). Before apply only.
    void setDefaultStrip(const std::vector<Insert>& modules) override;

private:
    Track track_;
    int index_;
};

// Adds an instrument channel and, right above it, a MIDI track aimed at it (one
// undo step): what adding a MIDI track with a new instrument makes. Ids are allocated on
// apply (kept for redo).
class AddInstrumentTrack final : public Command
{
public:
    AddInstrumentTrack(Track instrumentChannel, Track midiTrack, int index = -1);
    std::string name() const override { return "Add MIDI track and instrument"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id channelId() const noexcept { return channel_.id; }
    Id midiTrackId() const noexcept { return midi_.id; }
    // The channel strip for the instrument channel (the MIDI track has none).
    void setDefaultStrip(const std::vector<Insert>& modules) override;

private:
    Track channel_, midi_;
    int index_;
};

// Aims a MIDI track at an instrument channel (invalidId: none) on a MIDI channel.
class SetMidiTarget final : public Command
{
public:
    SetMidiTarget(Id trackId, Id target, int midiChannel);
    std::string name() const override { return "Set MIDI target"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, target_;
    int channel_;
    Id previousTarget_ = invalidId;
    int previousChannel_ = 1;
};

// What a removed channel leaves pointing at it, detached on removal and put
// back on undo: MIDI tracks aimed at it, output tracks it fed and key inputs
// (sidechains) it supplied.
struct DetachedReferences
{
    std::vector<Id> midiTracks;                // their midiTarget was the channel
    std::vector<Id> auxTracks;                 // their auxTrack was the channel
    std::vector<std::pair<Id, Id>> sidechains; // channel id, insert id
    void detach(Project& p, Id removed);
    void restore(Project& p, Id removed) const;
};

class RemoveTrack final : public Command
{
public:
    explicit RemoveTrack(Id trackId);
    bool capturesInsertState() const noexcept override { return true; }
    std::string name() const override { return "Remove track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    Track removed_;
    int index_ = -1;
    DetachedReferences detached_;
};

// Moves a track to another position in the list (the arrange view's reorder).
class MoveTrack final : public Command
{
public:
    MoveTrack(Id trackId, int newIndex);
    std::string name() const override { return "Move track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    int newIndex_, oldIndex_ = -1;
};

class AddBus final : public Command
{
public:
    AddBus(Bus bus, int index = -1);
    std::string name() const override { return "Add bus"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id busId() const noexcept { return bus_.id; }
    void setDefaultStrip(const std::vector<Insert>& modules) override;

private:
    Bus bus_;
    int index_;
};

// Removing a bus also detaches everything routed or sent to it (restored on undo).
class RemoveBus final : public Command
{
public:
    explicit RemoveBus(Id busId);
    bool capturesInsertState() const noexcept override { return true; }
    Id busId() const noexcept { return busId_; }
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
    DetachedReferences detached_; // key inputs it supplied
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
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
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

// ----- MIDI ----------------------------------------------------------------------------------

class SetTrackMidiInput final : public Command
{
public:
    SetTrackMidiInput(Id trackId, std::string deviceName);
    std::string name() const override { return "Set MIDI input"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    std::string value_, previous_;
};

// Whether a MIDI track plays the MIDI output device, and on which channel (1..16).
class SetTrackMidiOutput final : public Command
{
public:
    SetTrackMidiOutput(Id trackId, bool on, int channel);
    std::string name() const override { return "Set MIDI output"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_;
    bool on_, previousOn_ = false;
    int channel_, previousChannel_ = 1;
};

class AddMidiClip final : public Command
{
public:
    AddMidiClip(Id trackId, MidiClip clip); // ids (clip and notes) allocated on apply if invalid
    std::string name() const override { return "Add MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id clipId() const noexcept { return clip_.id; }

private:
    Id trackId_;
    MidiClip clip_;
};

class RemoveMidiClip final : public Command
{
public:
    RemoveMidiClip(Id trackId, Id clipId);
    std::string name() const override { return "Remove MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    MidiClip removed_;
};

class MoveMidiClip final : public Command
{
public:
    MoveMidiClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart);
    std::string name() const override { return "Move MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id fromTrackId_, clipId_, toTrackId_;
    SamplePosition newStart_, previousStart_ = 0;
};

// New start and length; the notes keep their place on the timeline (a start
// change shifts their clip-relative offsets the other way).
class TrimMidiClip final : public Command
{
public:
    TrimMidiClip(Id trackId, Id clipId, SamplePosition start, SamplePosition length);
    std::string name() const override { return "Trim MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    SamplePosition start_, length_;
    MidiClip previous_;
};

// Splits at a timeline position inside the clip; a note across the split is cut
// at it (the left part keeps it, shortened).
class SplitMidiClip final : public Command
{
public:
    SplitMidiClip(Id trackId, Id clipId, SamplePosition at);
    std::string name() const override { return "Split MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id rightClipId() const noexcept { return right_.id; }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    MidiClip original_, right_;
};

class DuplicateMidiClip final : public Command
{
public:
    DuplicateMidiClip(Id trackId, Id clipId, SamplePosition at = -1);
    std::string name() const override { return "Duplicate MIDI clip"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id newClipId() const noexcept { return copy_.id; }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    MidiClip copy_;
};

class AddNote final : public Command
{
public:
    AddNote(Id trackId, Id clipId, MidiNote note); // id allocated on apply if invalid
    std::string name() const override { return "Add note"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id noteId() const noexcept { return note_.id; }

private:
    Id trackId_, clipId_;
    MidiNote note_;
};

class RemoveNote final : public Command
{
public:
    RemoveNote(Id trackId, Id clipId, Id noteId);
    std::string name() const override { return "Remove note"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_, noteId_;
    MidiNote removed_;
};

// Pitch, velocity, start and length together (a move, a resize, a velocity edit).
class SetNote final : public Command
{
public:
    SetNote(Id trackId, Id clipId, MidiNote value);
    std::string name() const override { return "Edit note"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    MidiNote value_, previous_;
};

// Moves note starts towards the nearest grid line (timeline grid, anchored at
// zero) by `strength` (1 = exactly on the grid); lengths are kept.
class QuantiseNotes final : public Command
{
public:
    QuantiseNotes(Id trackId, Id clipId, std::vector<Id> noteIds, SamplePosition grid, float strength = 1.0f);
    // To a grid on the tempo track (bars and beats follow tempo and meter changes).
    QuantiseNotes(
        Id trackId, Id clipId, std::vector<Id> noteIds, TempoMap map, GridStep step, float strength = 1.0f);
    std::string name() const override { return "Quantise"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    std::vector<Id> noteIds_;
    SamplePosition grid_ = 1;
    std::optional<std::pair<TempoMap, GridStep>> tempoGrid_;
    float strength_;
    std::vector<std::pair<Id, SamplePosition>> previous_;
};

// ----- markers -------------------------------------------------------------------------------

class AddMarker final : public Command
{
public:
    explicit AddMarker(Marker marker); // the id is allocated on apply if invalid
    std::string name() const override { return "Add marker"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id markerId() const noexcept { return marker_.id; }

private:
    Marker marker_;
};

class RemoveMarker final : public Command
{
public:
    explicit RemoveMarker(Id markerId);
    std::string name() const override { return "Remove marker"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id markerId_;
    Marker removed_;
};

// Position, length and name together (a move, a resize or a rename).
class SetMarker final : public Command
{
public:
    SetMarker(Id markerId, SamplePosition position, SamplePosition length, std::string name);
    std::string name() const override { return "Edit marker"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id markerId_;
    Marker value_, previous_;
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
    // A new send or a pre/post switch changes the graph; a level is a live value.
    // (Known once applied: the session asks after.)
    ChangeKind changeKind() const noexcept override
    {
        return existed_ && previous_.preFader == value_.preFader ? ChangeKind::values : ChangeKind::structure;
    }
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

// A track's (or FX track's) row height in the arrange view; 0 returns it to
// the view's default.
class SetChannelHeight final : public Command
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    SetChannelHeight(Id channelId, int height);
    std::string name() const override { return "Resize track"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    int value_, previous_ = 0;
};

class SetMasterGain final : public Command
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
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
    bool capturesInsertState() const noexcept override { return true; }
    RemoveInsert(Id channelId, Id insertId);
    std::string name() const override { return "Remove insert"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    Insert removed_;
    int index_ = -1;
};

// Sets, replaces or removes (an empty type) a track's instrument. A replacement
// keeps the slot's id, so the output tracks reading its extra outputs stay
// connected; the automation lanes of a replaced or removed instrument's
// parameters go with it (and come back on undo).
class SetInstrument final : public Command
{
public:
    bool capturesInsertState() const noexcept override { return true; }
    SetInstrument(Id trackId, Insert instrument);
    std::string name() const override
    {
        return instrument_.type.empty() ? "Remove instrument" : "Set instrument";
    }
    void apply(Project&) override;
    void revert(Project&) override;
    Id instrumentId() const noexcept { return instrument_.id; }

private:
    Id trackId_;
    Insert instrument_, previous_;
    std::vector<AutomationLane> previousLanes_;
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
    ChangeKind changeKind() const noexcept override { return ChangeKind::parameters; }
    SetInsertParameter(Id channelId, Id insertId, int parameter, float value);
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{channelId_, AutomationTarget::insertParameter, insertId_, parameter_, value_};
    }
    std::string name() const override { return "Set parameter"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id channelId() const noexcept { return channelId_; }
    Id insertId() const noexcept { return insertId_; }
    int parameter() const noexcept { return parameter_; }
    float value() const noexcept { return value_; }
    void touchedParameters(std::vector<ParameterTouch>& out) const override
    {
        out.push_back({insertId_, parameter_});
    }

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
// for none, tapped before or after the source's fader. Resolved at compile
// time; an impossible source is ignored there.
class SetInsertSidechain final : public Command
{
public:
    SetInsertSidechain(Id channelId, Id insertId, Id sourceId, bool postFader = false);
    std::string name() const override { return "Set insert sidechain"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_, value_, previous_ = invalidId;
    bool postFader_, previousPostFader_ = false;
};

class SetInsertBypass final : public Command
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    SetInsertBypass(Id channelId, Id insertId, bool bypass);
    std::string name() const override { return "Bypass insert"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, insertId_;
    bool value_, previous_ = false;
};

// Moves a strip module to another place in the strip (processing order).
class MoveStripModule final : public Command
{
public:
    MoveStripModule(Id channelId, Id moduleId, int newIndex);
    std::string name() const override { return "Move strip module"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, moduleId_;
    int newIndex_, previousIndex_ = -1;
};

// Whether the channel strip runs before the inserts or after them.
class SetStripPosition final : public Command
{
public:
    SetStripPosition(Id channelId, bool preInserts);
    std::string name() const override { return "Move channel strip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    bool value_, previous_ = false;
};

// Sets the strip's modules from a preset: for each given module, the channel's
// module of the same type takes its on/off state and parameters (ids and the
// strip's order stay). One undo step.
class SetStripState final : public Command
{
public:
    SetStripState(Id channelId, std::vector<Insert> modules, std::string name = "Apply strip preset");
    std::string name() const override { return name_; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    std::vector<Insert> modules_;
    std::string name_;
    struct Previous
    {
        Id id;
        bool bypass;
        std::vector<float> params;
    };
    std::vector<Previous> previous_;
};

// ----- automation -----------------------------------------------------------------------

// Adds a lane (its id allocated on apply when invalid). A channel has at most
// one lane per target.
class AddAutomationLane final : public Command
{
public:
    AddAutomationLane(Id channelId, AutomationLane lane);
    std::string name() const override { return "Add automation"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id laneId() const noexcept { return lane_.id; }

private:
    Id channelId_;
    AutomationLane lane_;
};

class RemoveAutomationLane final : public Command
{
public:
    RemoveAutomationLane(Id channelId, Id laneId);
    std::string name() const override { return "Remove automation"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, laneId_;
    AutomationLane removed_;
    int index_ = -1;
};

// Replaces a lane's points (sorted on construction): every point edit and
// every recorded pass is one of these.
class SetAutomationPoints final : public Command
{
public:
    SetAutomationPoints(Id channelId,
                        Id laneId,
                        std::vector<AutomationPoint> points,
                        std::string name = "Edit automation");
    std::string name() const override { return name_; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_, laneId_;
    std::vector<AutomationPoint> points_, previous_;
    std::string name_;
};

class SetAutomationMode final : public Command
{
public:
    SetAutomationMode(Id channelId, AutomationMode mode);
    std::string name() const override { return "Set automation mode"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id channelId_;
    AutomationMode value_, previous_ = AutomationMode::read;
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
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    SetTrackGain(Id trackId, float gain);
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{trackId_, AutomationTarget::gain, invalidId, 0, value_};
    }
    std::string name() const override { return "Set track gain"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id channelId() const noexcept { return trackId_; }
    float value() const noexcept { return value_; }

private:
    Id trackId_;
    float value_, previous_ = 1.0f;
};

class SetTrackPan final : public Command
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    SetTrackPan(Id trackId, float pan);
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{trackId_, AutomationTarget::pan, invalidId, 0, value_};
    }
    std::string name() const override { return "Set track pan"; }
    void apply(Project&) override;
    void revert(Project&) override;
    Id channelId() const noexcept { return trackId_; }
    float value() const noexcept { return value_; }

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

// Points every clip using a media file at another path (consolidating media
// into the project folder): old path -> new path.
class SetMediaPaths final : public Command
{
public:
    explicit SetMediaPaths(std::vector<std::pair<std::string, std::string>> renames);
    std::string name() const override { return "Consolidate media"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    std::vector<std::pair<std::string, std::string>> renames_;
    struct Changed
    {
        Id track, clip;
        std::string previous;
    };
    std::vector<Changed> changed_;
};

// Replaces a clip's vocal edit (every pitch, breath and note edit, and entering
// or leaving pitch editing). Notes and segments without an id get one on apply
// (kept for redo).
class SetVocalEdit final : public Command
{
public:
    SetVocalEdit(Id trackId, Id clipId, VocalEdit edit, std::string name = "Edit pitch");
    std::string name() const override { return name_; }
    void apply(Project&) override;
    void revert(Project&) override;
    const VocalEdit& edit() const noexcept { return value_; }

private:
    Id trackId_, clipId_;
    VocalEdit value_, previous_;
    std::string name_;
};

class SetProjectKey final : public Command
{
public:
    explicit SetProjectKey(MusicalKey key);
    std::string name() const override { return "Set key"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    MusicalKey value_, previous_;
};

// What a tempo edit changes and restores: the tempo track, and every position
// that follows it (MIDI clips, clips that follow the tempo, automation, markers),
// captured whole so undo puts them back exactly rather than remapping again.
struct TempoState
{
    double bpm = 120.0;
    TimeSignature signature;
    std::vector<TempoChange> tempoChanges;
    std::vector<MeterChange> meterChanges;
    bool operator==(const TempoState&) const = default;
    static TempoState of(const Project& p);
};

class MusicalPositions
{
public:
    void capture(const Project& p);
    void restore(Project& p) const;

private:
    std::vector<std::vector<MidiClip>> midi_;
    std::vector<std::vector<AudioClip>> audio_;
    std::vector<std::vector<AutomationLane>> trackLanes_, busLanes_;
    std::vector<Marker> markers_;
};

// Replaces the tempo track (start tempo, signature, changes); musical content
// keeps its bars and beats (core/TempoMap.h remapMusicalTime).
class SetTempoMap final : public Command
{
public:
    explicit SetTempoMap(TempoState state, std::string name = "Edit tempo track");
    std::string name() const override { return name_; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    TempoState value_, previous_;
    MusicalPositions positions_;
    std::string name_;
};

// A clip's time-stretch and transposition (and its length, which a stretch
// changes, and whether it follows the tempo).
class SetClipStretch final : public Command
{
public:
    SetClipStretch(
        Id trackId, Id clipId, double stretch, double transpose, SamplePosition length, bool followTempo);
    std::string name() const override { return "Stretch clip"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    Id trackId_, clipId_;
    double stretch_, transpose_;
    SamplePosition length_;
    bool followTempo_;
    AudioClip previous_;
};

class SetTimeSignature final : public Command
{
public:
    SetTimeSignature(int numerator, int denominator);
    std::string name() const override { return "Set time signature"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    TimeSignature value_, previous_;
};

// The tempo at the song's start (what the header sets when there are no tempo
// changes); musical content keeps its bars and beats.
class SetTempo final : public Command
{
public:
    explicit SetTempo(double bpm);
    std::string name() const override { return "Set tempo"; }
    void apply(Project&) override;
    void revert(Project&) override;

private:
    double value_, previous_ = 120.0;
    MusicalPositions positions_;
};

} // namespace aural::core
