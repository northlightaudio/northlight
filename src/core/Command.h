#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "core/ParameterTouch.h"
#include "core/TempoMap.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

// Every edit to the Project is a Command with apply/revert. UndoStack owns the
// history. Commands capture whatever they need to revert at apply time, so they can
// be constructed before the model state they revert to is known.

namespace northlight::core
{

// Rough heap memory a model object holds (beyond its own sizeof), for
// Command::footprint: what an undo step costs to keep.
inline std::size_t heapBytes(const std::string& s) noexcept
{
    return s.capacity();
}
template <class T> std::size_t heapBytes(const std::vector<T>& v) noexcept
{
    std::size_t bytes = v.capacity() * sizeof(T);
    if constexpr (requires(const T& x) { heapBytes(x); })
        for (const auto& x : v)
            bytes += heapBytes(x);
    return bytes;
}
std::size_t heapBytes(const VocalEdit& edit) noexcept;
std::size_t heapBytes(const AudioClip& clip) noexcept;
std::size_t heapBytes(const MidiClip& clip) noexcept;
std::size_t heapBytes(const AutomationLane& lane) noexcept;
std::size_t heapBytes(const Insert& insert) noexcept;
std::size_t heapBytes(const TakeGroup& group) noexcept;
std::size_t heapBytes(const Channel& channel) noexcept;
std::size_t heapBytes(const Track& track) noexcept;

class Command
{
public:
    virtual ~Command() = default;
    virtual std::string name() const = 0;
    // Both return false, having changed nothing, when the command cannot run:
    // what it refers to is gone (a stale command: a menu opened on a track an
    // undo has since removed) or the edit is impossible (a routing cycle, a
    // split outside the clip).
    virtual bool apply(Project& project) = 0;
    virtual bool revert(Project& project) = 0;
    // A live command changes a value the engine takes from the model without a
    // recompile (gains, pans, insert parameters and bypass): the session pushes
    // the new values instead of publishing a new snapshot.
    // What a command changes, from the least costly to the most: the session
    // picks the engine work from it (and from a group's widest member).
    //   cosmetic:   only what the views show (a row height, a name, a colour); no
    //               engine work.
    //   parameters: insert parameters; pushed to their processors.
    //   values:     fader, pan, send level and bypass values the running graph
    //               reads without a recompile; every channel's values are pushed.
    //               (Mute and solo are resolved at compile: structure.)
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
    // Whether the views rebuild from the model (a name, a colour, a marker),
    // whatever the engine needs. Values-only refreshes do not redraw labels.
    virtual bool rebuildsViews() const noexcept { return changeKind() == ChangeKind::structure; }
    // A values change that touches one channel's live values only (a fader, a
    // pan, a send level, an insert's bypass or mix): its id, so only that channel's
    // values are pushed. invalidId: any or every channel.
    virtual Id liveChannel() const noexcept { return invalidId; }
    // A command that copies inserts for undo (removing or replacing them) needs
    // the live plugin state pulled into the model first, so undo brings back
    // what the user had rather than what the last save held.
    virtual bool capturesInsertState() const noexcept { return false; }
    // A command that changed the opaque state of an insert whose processor owns
    // it (a hosted plugin's preset undone): the insert's id, so the session hands
    // the model's state to the live instance. invalidId: none.
    virtual Id pushesInsertState() const noexcept { return invalidId; }
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
    using ParameterTouch = core::ParameterTouch;
    virtual void touchedParameters(std::vector<ParameterTouch>&) const {}
    // Folds `next`, applied right after this command, into this one: the same
    // field of the same target set again (a fader drag's ticks inside one
    // gesture). This command then reverts to its own previous value and applies
    // next's value, so undoing it does what undoing both did. False: left apart.
    virtual bool mergeWith(Command& /*next*/) { return false; }
    // Roughly how much memory the command keeps once applied (what it copied to
    // revert or redo), so the history can be capped by size as well as by count.
    // A command that copies tracks, clips or lanes says so; the rest are small.
    virtual std::size_t footprint() const noexcept { return 128; }
    // False for a change to how a track is set up for recording (arming, input
    // monitoring, the input): the session applies it outside the undo history,
    // as other DAWs do, and the project is still modified and saves it.
    virtual bool undoable() const noexcept { return true; }
};

class UndoStack
{
public:
    UndoStack();
    ~UndoStack();
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    // Applies the command and pushes it; clears the redo history. A command that
    // fails (it references something that is gone) is not pushed: false.
    bool execute(Project& project, std::unique_ptr<Command> command);
    // False when there is nothing to undo or redo, or the command fails: it
    // stays where it was, and a group puts back the members it had already
    // done, so a failure changes nothing.
    bool undo(Project& project);
    bool redo(Project& project);

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    // The command a successful undo() moved to the redo side, and vice versa.
    const Command* redoTop() const noexcept { return redo_.empty() ? nullptr : redo_.back().command.get(); }
    const Command* undoTop() const noexcept { return undo_.empty() ? nullptr : undo_.back().command.get(); }

    // The history keeps at most this many steps and about this much memory
    // (Command::footprint); a new step past either drops the oldest ones, though
    // never the newest. A save point dropped with them can no longer be reached.
    static constexpr std::size_t defaultMaxSteps = 1000;
    static constexpr std::size_t defaultMaxBytes = std::size_t{256} << 20;
    void setLimits(std::size_t maxSteps, std::size_t maxBytes);
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t historyBytes() const noexcept { return bytes_; }
    std::string undoName() const;
    std::string redoName() const;
    // Empties both histories; the empty history is the save point.
    void clear();

    // The save point: the place in the history the saved file matches. Undoing or
    // redoing back to it makes the project unmodified again; a new command after
    // undoing past it makes it unreachable.
    void markSaved() noexcept;
    bool atSavePoint() const noexcept;

    // Groups the next `execute` calls into one undoable step until endGroup().
    // Groups nest: an inner begin/end pair joins the open group (keeping its
    // name) and only the outermost endGroup() commits it. Inside a group, a
    // command that sets what the one before it set merges into it
    // (Command::mergeWith).
    void beginGroup(std::string name);
    void endGroup();
    bool groupOpen() const noexcept { return openGroup_ != nullptr; }

private:
    class Group;
    struct Step
    {
        std::unique_ptr<Command> command;
        std::size_t bytes = 0; // its footprint when it was pushed
    };
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::unique_ptr<Group> openGroup_;
    int groupDepth_ = 0;
    // undo_.size() at the save point; -1 once it can no longer be reached.
    std::ptrdiff_t savedDepth_ = 0;
    std::size_t bytes_ = 0; // of every step on both sides
    std::size_t maxSteps_ = defaultMaxSteps;
    std::size_t maxBytes_ = defaultMaxBytes;
    void push(std::unique_ptr<Command> command);
    void trim();
};

// ----- one-field setters ------------------------------------------------------------------

// What a setter command addresses, found by id each time it runs (null: gone).
struct OnChannel // a track, a bus or the master
{
    Id channel = invalidId;
    Channel* find(Project& p) const noexcept { return p.findChannel(channel); }
    bool operator==(const OnChannel&) const = default;
};

struct OnTrack
{
    Id track = invalidId;
    Track* find(Project& p) const noexcept { return p.findTrack(track); }
    bool operator==(const OnTrack&) const = default;
};

struct OnInsert // an insert, a strip module or the instrument
{
    Id channel = invalidId;
    Id insert = invalidId;
    Insert* find(Project& p) const noexcept
    {
        auto* c = p.findChannel(channel);
        return c != nullptr ? c->findInsert(insert) : nullptr;
    }
    bool operator==(const OnInsert&) const = default;
};

struct OnLane
{
    Id channel = invalidId;
    Id lane = invalidId;
    AutomationLane* find(Project& p) const noexcept
    {
        auto* c = p.findChannel(channel);
        return c != nullptr ? c->findLane(lane) : nullptr;
    }
    bool operator==(const OnLane&) const = default;
};

struct OnClip // an audio clip
{
    Id track = invalidId;
    Id clip = invalidId;
    AudioClip* find(Project& p) const noexcept
    {
        auto* t = p.findTrack(track);
        return t != nullptr ? t->findClip(clip) : nullptr;
    }
    bool operator==(const OnClip&) const = default;
};

struct OnProject
{
    Project* find(Project& p) const noexcept { return &p; }
    bool operator==(const OnProject&) const = default;
};

namespace detail
{
template <class> struct FieldType;
template <class Object, class Value> struct FieldType<Value Object::*>
{
    using type = Value;
};
} // namespace detail

// Sets one field of one object (a channel's gain, an insert's bypass) and puts
// back the value it replaced on revert. Each setter is a thin subclass that
// names itself and says what it changes (changeKind, liveChannel, ...).
template <class Target, auto Field> class SetField : public Command
{
public:
    using Value = typename detail::FieldType<decltype(Field)>::type;
    bool apply(Project& p) override
    {
        auto* object = target_.find(p);
        if (object == nullptr)
            return false;
        previous_ = object->*Field;
        object->*Field = value_;
        return true;
    }
    bool revert(Project& p) override
    {
        auto* object = target_.find(p);
        if (object == nullptr)
            return false;
        object->*Field = previous_;
        return true;
    }
    // The same setter on the same target: keep the first previous value and
    // take the later value.
    bool mergeWith(Command& next) override
    {
        if (typeid(next) != typeid(*this))
            return false;
        auto& later = static_cast<SetField&>(next);
        if (!(later.target_ == target_))
            return false;
        value_ = std::move(later.value_);
        return true;
    }
    std::size_t footprint() const noexcept override
    {
        if constexpr (requires(const Value& v) { heapBytes(v); })
            return sizeof(*this) + heapBytes(value_) + heapBytes(previous_);
        else
            return sizeof(*this);
    }
    const Target& target() const noexcept { return target_; }
    const Value& value() const noexcept { return value_; }

protected:
    SetField(Target target, Value value) : target_(target), value_(std::move(value)) {}
    Target target_;
    Value value_;
    Value previous_{};
};

// ----- concrete commands ------------------------------------------------------------------

class AddTrack final : public Command
{
public:
    // Inserts a copy of `track` at `index` (or at the end when index < 0). The track's
    // id is allocated on apply if it is invalidId.
    AddTrack(Track track, int index = -1);
    std::string name() const override { return "Add track"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id trackId() const noexcept { return track_.id; }
    // Gives the track these strip modules when it has none (the session supplies
    // the default strip; ids are allocated on apply). Before apply only.
    void setDefaultStrip(const std::vector<Insert>& modules) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(track_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id channelId() const noexcept { return channel_.id; }
    Id midiTrackId() const noexcept { return midi_.id; }
    // The channel strip for the instrument channel (the MIDI track has none).
    void setDefaultStrip(const std::vector<Insert>& modules) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(channel_) + heapBytes(midi_);
    }

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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(removed_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_;
    int newIndex_, oldIndex_ = -1;
};

class AddBus final : public Command
{
public:
    AddBus(Bus bus, int index = -1);
    std::string name() const override { return "Add bus"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id busId() const noexcept { return bus_.id; }
    void setDefaultStrip(const std::vector<Insert>& modules) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(bus_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(removed_) + detachedSends_.capacity() * sizeof(DetachedSend);
    }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id groupId_;
    float ratio_;
    std::vector<std::pair<Id, float>> previous_;
};

// Sets one switch (mute or solo) on every member; undo restores each member's
// own state.
class SetGroupSwitch : public Command
{
public:
    bool apply(Project&) override;
    bool revert(Project&) override;

protected:
    SetGroupSwitch(Id groupId, bool Channel::* field, bool value);

private:
    Id groupId_;
    bool Channel::* field_;
    bool value_;
    std::vector<std::pair<Id, bool>> previous_;
};

class SetGroupMute final : public SetGroupSwitch
{
public:
    SetGroupMute(Id groupId, bool mute) : SetGroupSwitch(groupId, &Channel::mute, mute) {}
    std::string name() const override { return "Set group mute"; }
};

class SetGroupSolo final : public SetGroupSwitch
{
public:
    SetGroupSolo(Id groupId, bool solo) : SetGroupSwitch(groupId, &Channel::solo, solo) {}
    std::string name() const override { return "Set group solo"; }
};

// ----- MIDI ----------------------------------------------------------------------------------

class SetTrackMidiInput final : public SetField<OnTrack, &Track::midiInput>
{
public:
    SetTrackMidiInput(Id trackId, std::string deviceName) : SetField({trackId}, std::move(deviceName)) {}
    std::string name() const override { return "Set MIDI input"; }
    bool undoable() const noexcept override { return false; }
};

// Whether a MIDI track plays the MIDI output device, and on which channel (1..16).
class SetTrackMidiOutput final : public Command
{
public:
    SetTrackMidiOutput(Id trackId, bool on, int channel);
    std::string name() const override { return "Set MIDI output"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id clipId() const noexcept { return clip_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(clip_); }

private:
    Id trackId_;
    MidiClip clip_;
};

class RemoveMidiClip final : public Command
{
public:
    RemoveMidiClip(Id trackId, Id clipId);
    std::string name() const override { return "Remove MIDI clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(removed_); }

private:
    Id trackId_, clipId_;
    MidiClip removed_;
};

class MoveMidiClip final : public Command
{
public:
    MoveMidiClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart);
    std::string name() const override { return "Move MIDI clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(previous_); }

private:
    Id trackId_, clipId_;
    SamplePosition start_, length_;
    MidiClip previous_;
};

// Splits at a timeline position inside the clip; a note across the split is cut
// at it (the left part keeps it, shortened). Controller events after the split
// go right, and the right part opens with each controller's value at the split
// (so it sounds the same when moved on its own).
class SplitMidiClip final : public Command
{
public:
    SplitMidiClip(Id trackId, Id clipId, SamplePosition at);
    std::string name() const override { return "Split MIDI clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id rightClipId() const noexcept { return right_.id; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(original_) + heapBytes(right_);
    }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    MidiClip original_, right_;
    std::vector<Id> chaseIds_; // the right part's opening controller values, kept for redo
};

class DuplicateMidiClip final : public Command
{
public:
    DuplicateMidiClip(Id trackId, Id clipId, SamplePosition at = -1);
    std::string name() const override { return "Duplicate MIDI clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id newClipId() const noexcept { return copy_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(copy_); }

private:
    Id trackId_, clipId_;
    SamplePosition at_;
    MidiClip copy_;
};

// Joins a MIDI clip with a later one on the same track (the glue tool): the left
// grows to the right's end, across any gap, and takes the right's notes and
// controller events; the right goes.
class JoinMidiClips final : public Command
{
public:
    JoinMidiClips(Id trackId, Id leftClipId, Id rightClipId);
    std::string name() const override { return "Join MIDI clips"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    static bool canJoin(const MidiClip& left, const MidiClip& right) noexcept;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(previousLeft_) + heapBytes(removedRight_);
    }

private:
    Id trackId_, leftId_, rightId_;
    MidiClip previousLeft_, removedRight_;
};

class AddNote final : public Command
{
public:
    AddNote(Id trackId, Id clipId, MidiNote note); // id allocated on apply if invalid
    std::string name() const override { return "Add note"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_, clipId_;
    MidiNote value_, previous_;
};

// Adds, changes and removes controller events in one clip, as one step (a
// drawn line, a drag of several points, a delete). `events` with an id the clip
// has replace that event; the rest are added (an invalid id is allocated on
// apply and kept for redo). Values are clamped to their type's range.
class EditMidiControllers final : public Command
{
public:
    EditMidiControllers(Id trackId,
                        Id clipId,
                        std::vector<MidiControllerEvent> events,
                        std::vector<Id> remove = {},
                        std::string name = "Edit controllers");
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    const std::vector<MidiControllerEvent>& events() const noexcept { return events_; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(events_) + heapBytes(previous_);
    }

private:
    Id trackId_, clipId_;
    std::vector<MidiControllerEvent> events_;
    std::vector<Id> remove_;
    std::string name_;
    std::vector<MidiControllerEvent> previous_;
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
    // To a division of the beat (2 = eighths in 4/4), each note's beat counted in
    // the signature of the bar it falls in (the piano roll's grid).
    QuantiseNotes(Id trackId,
                  Id clipId,
                  std::vector<Id> noteIds,
                  TempoMap map,
                  int divisionsPerBeat,
                  float strength = 1.0f);
    std::string name() const override { return "Quantise"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(noteIds_) + heapBytes(previous_);
    }

private:
    Id trackId_, clipId_;
    std::vector<Id> noteIds_;
    SamplePosition grid_ = 1;
    std::optional<std::pair<TempoMap, GridStep>> tempoGrid_;
    int divisionsPerBeat_ = 0; // > 0: the step follows each note's signature
    float strength_;
    std::vector<std::pair<Id, SamplePosition>> previous_;
};

// ----- markers -------------------------------------------------------------------------------

class AddMarker final : public Command
{
public:
    explicit AddMarker(Marker marker); // the id is allocated on apply if invalid
    std::string name() const override { return "Add marker"; }
    // What the views show: no engine work, but the views rebuild.
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id markerId() const noexcept { return marker_.id; }

private:
    Marker marker_;
};

class RemoveMarker final : public Command
{
public:
    explicit RemoveMarker(Id markerId);
    std::string name() const override { return "Remove marker"; }
    // What the views show: no engine work, but the views rebuild.
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    // What the views show: no engine work, but the views rebuild.
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id markerId_;
    Marker value_, previous_;
};

// Routes a track or bus to a bus (invalidId = master), leaving any hardware
// output it played to.
class SetOutputBus final : public Command
{
public:
    SetOutputBus(Id channelId, Id busId);
    std::string name() const override { return "Set output"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id channelId_, value_, previous_ = invalidId;
    int previousHardware_ = -1;
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
    Id liveChannel() const noexcept override { return channelId_; }
    std::string name() const override { return "Set send"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id channelId_, busId_;
    bool existed_ = false;
    Send previous_;
};

class SetPhaseInvert final : public SetField<OnChannel, &Channel::phaseInvert>
{
public:
    SetPhaseInvert(Id channelId, bool invert) : SetField({channelId}, invert) {}
    std::string name() const override { return "Invert phase"; }
};

class SetColour final : public SetField<OnChannel, &Channel::colour>
{
public:
    SetColour(Id channelId, std::uint32_t colour) : SetField({channelId}, colour) {}
    std::string name() const override { return "Set colour"; }
    // What the views show: no engine work, but the views rebuild.
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
};

// A track's (or FX track's) row height in the arrange view; 0 returns it to
// the view's default.
class SetChannelHeight final : public SetField<OnChannel, &Channel::height>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    SetChannelHeight(Id channelId, int height) : SetField({channelId}, std::max(0, height)) {}
    std::string name() const override { return "Resize track"; }
};

class SetMasterGain final : public SetField<OnChannel, &Channel::gain>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return masterId; }
    explicit SetMasterGain(float gain) : SetField({masterId}, gain) {}
    std::string name() const override { return "Set master gain"; }
    // While playing in a writing mode, the master's fader writes its volume lane.
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{masterId, AutomationTarget::gain, invalidId, 0, value_};
    }
};

class AddInsert final : public Command
{
public:
    // Inserts at `index` (or the end when index < 0); the id is allocated on apply.
    AddInsert(Id channelId, Insert insert, int index = -1);
    std::string name() const override { return "Add insert"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(removed_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id instrumentId() const noexcept { return instrument_.id; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(instrument_) + heapBytes(previous_) + heapBytes(previousLanes_);
    }

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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id channelId() const noexcept { return channelId_; }
    Id insertId() const noexcept { return insertId_; }
    int parameter() const noexcept { return parameter_; }
    float value() const noexcept { return value_; }
    void touchedParameters(std::vector<ParameterTouch>& out) const override
    {
        out.push_back({insertId_, parameter_});
    }
    bool mergeWith(Command& next) override;

private:
    Id channelId_, insertId_;
    int parameter_;
    float value_, previous_ = 0.0f;
    size_t previousSize_ = 0; // the params vector may have grown on apply
};

// Every parameter of an insert at once (a preset): one command, so one undo
// step and one update of the engine and the views. Sets params[0..n) to the
// given values (growing the list if it is shorter); undo restores the list as
// it was. A preset sets the static values, so it never writes automation.
class SetInsertParameters final : public Command
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::parameters; }
    SetInsertParameters(Id channelId, Id insertId, std::vector<float> values, std::string name);
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id channelId() const noexcept { return channelId_; }
    Id insertId() const noexcept { return insertId_; }
    const std::vector<float>& values() const noexcept { return values_; }
    void touchedParameters(std::vector<ParameterTouch>& out) const override
    {
        for (size_t i = 0; i < values_.size(); ++i)
            out.push_back({insertId_, static_cast<int>(i)});
    }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(values_) + heapBytes(previous_) + heapBytes(name_);
    }

private:
    Id channelId_, insertId_;
    std::vector<float> values_, previous_;
    std::string name_;
};

// Wet/dry mix of one insert slot (0..1).
class SetInsertMix final : public SetField<OnInsert, &Insert::mix>
{
public:
    SetInsertMix(Id channelId, Id insertId, float mix)
        : SetField({channelId, insertId}, std::clamp(mix, 0.0f, 1.0f))
    {
    }
    std::string name() const override { return "Set insert mix"; }
    // A live value: the renderer glides to it (a latent slot always keeps its
    // dry line), so a drag never recompiles.
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
};

// The sidechain (key) source of one insert slot: a track or bus id, invalidId
// for none, tapped before or after the source's fader. Resolved at compile
// time; an impossible source is ignored there.
class SetInsertSidechain final : public Command
{
public:
    SetInsertSidechain(Id channelId, Id insertId, Id sourceId, bool postFader = false);
    std::string name() const override { return "Set insert sidechain"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id channelId_, insertId_, value_, previous_ = invalidId;
    bool postFader_, previousPostFader_ = false;
};

// The opaque state of a built-in that keeps state beyond its parameters (the
// Sampler's file): the engine passes the new state to the live instance.
class SetInsertState final : public SetField<OnInsert, &Insert::state>
{
public:
    SetInsertState(Id channelId, Id insertId, std::string state, std::string name = "Set insert state")
        : SetField({channelId, insertId}, std::move(state)), name_(std::move(name))
    {
    }
    std::string name() const override { return name_; }

private:
    std::string name_;
};

class SetInsertBypass final : public SetField<OnInsert, &Insert::bypass>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
    SetInsertBypass(Id channelId, Id insertId, bool bypass) : SetField({channelId, insertId}, bypass) {}
    std::string name() const override { return "Bypass insert"; }
};

// Moves a strip module to another place in the strip (processing order).
class MoveStripModule final : public Command
{
public:
    MoveStripModule(Id channelId, Id moduleId, int newIndex);
    std::string name() const override { return "Move strip module"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id channelId_, moduleId_;
    int newIndex_, previousIndex_ = -1;
};

// Swaps a strip module for another model of the same slot (the compressor for
// the opto): the module keeps its id, place and on/off state and takes the new
// type and parameters. The automation lanes of the old model's parameters go
// with it (and come back on undo).
class SetStripModuleType final : public Command
{
public:
    SetStripModuleType(
        Id channelId, Id moduleId, std::string type, std::vector<float> params, std::string name);
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(params_) + heapBytes(previous_) + heapBytes(previousLanes_);
    }

private:
    Id channelId_, moduleId_;
    std::string type_;
    std::vector<float> params_;
    std::string name_;
    Insert previous_;
    std::vector<AutomationLane> previousLanes_;
};

// Whether the channel strip runs before the inserts or after them.
class SetStripPosition final : public SetField<OnChannel, &Channel::stripPreInserts>
{
public:
    SetStripPosition(Id channelId, bool preInserts) : SetField({channelId}, preInserts) {}
    std::string name() const override { return "Move channel strip"; }
};

// Sets the strip's modules from a preset: for each given module, the channel's
// module of the same type takes its on/off state and parameters (ids and the
// strip's order stay). One undo step.
class SetStripState final : public Command
{
public:
    SetStripState(Id channelId, std::vector<Insert> modules, std::string name = "Apply strip preset");
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(modules_) + previous_.capacity() * sizeof(Previous);
    }

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
// one lane per target. The channel's lanes open so the new one is seen.
class AddAutomationLane final : public Command
{
public:
    AddAutomationLane(Id channelId, AutomationLane lane);
    std::string name() const override { return "Add automation"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id laneId() const noexcept { return lane_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(lane_); }

private:
    Id channelId_;
    AutomationLane lane_;
    bool previousOpen_ = false;
};

class RemoveAutomationLane final : public Command
{
public:
    RemoveAutomationLane(Id channelId, Id laneId);
    std::string name() const override { return "Remove automation"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(removed_); }

private:
    Id channelId_, laneId_;
    AutomationLane removed_;
    int index_ = -1;
};

// Replaces a lane's points (sorted on construction): every point edit and
// every recorded pass is one of these.
class SetAutomationPoints final : public SetField<OnLane, &AutomationLane::points>
{
public:
    SetAutomationPoints(Id channelId,
                        Id laneId,
                        std::vector<AutomationPoint> points,
                        std::string name = "Edit automation");
    std::string name() const override { return name_; }

private:
    std::string name_;
};

class SetAutomationMode final : public SetField<OnChannel, &Channel::automationMode>
{
public:
    SetAutomationMode(Id channelId, AutomationMode mode) : SetField({channelId}, mode) {}
    std::string name() const override { return "Set automation mode"; }
};

// Opens or closes a channel's automation lanes in the arrange view.
class SetAutomationOpen final : public SetField<OnChannel, &Channel::automationOpen>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    SetAutomationOpen(Id channelId, bool open) : SetField({channelId}, open) {}
    std::string name() const override { return value_ ? "Show automation" : "Hide automation"; }
};

// How a lane shows in the arrange view: its height, collapsed, hidden.
class SetAutomationLaneView final : public SetField<OnLane, &AutomationLane::view>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    SetAutomationLaneView(Id channelId,
                          Id laneId,
                          AutomationLaneView view,
                          std::string name = "Change automation lane")
        : SetField({channelId, laneId}, view), name_(std::move(name))
    {
        value_.height = std::max(0, value_.height);
    }
    std::string name() const override { return name_; }

private:
    std::string name_;
};

// Points a lane at another parameter. The points keep their values, so the
// arrange view only does this to a lane that has none.
class SetAutomationLaneTarget final : public Command
{
public:
    SetAutomationLaneTarget(Id channelId, Id laneId, AutomationTarget target, Id insert, int parameter);
    std::string name() const override { return "Change automation parameter"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id channelId_, laneId_;
    AutomationTarget target_;
    Id insert_;
    int parameter_;
    AutomationLane previous_; // its target, insert, parameter and key
};

// The fader-section commands below take a track or bus id.
class SetTrackName final : public SetField<OnChannel, &Channel::name>
{
public:
    SetTrackName(Id trackId, std::string name) : SetField({trackId}, std::move(name)) {}
    std::string name() const override { return "Rename track"; }
    // What the views show: no engine work, but the views rebuild.
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
};

class SetTrackGain final : public SetField<OnChannel, &Channel::gain>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
    SetTrackGain(Id trackId, float gain) : SetField({trackId}, gain) {}
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{target_.channel, AutomationTarget::gain, invalidId, 0, value_};
    }
    std::string name() const override { return "Set track gain"; }
    Id channelId() const noexcept { return target_.channel; }
};

class SetTrackPan final : public SetField<OnChannel, &Channel::pan>
{
public:
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
    SetTrackPan(Id trackId, float pan) : SetField({trackId}, pan) {}
    std::optional<AutomationWrite> automationWrite() const noexcept override
    {
        return AutomationWrite{target_.channel, AutomationTarget::pan, invalidId, 0, value_};
    }
    std::string name() const override { return "Set track pan"; }
    Id channelId() const noexcept { return target_.channel; }
};

class SetTrackMute final : public SetField<OnChannel, &Channel::mute>
{
public:
    SetTrackMute(Id trackId, bool mute) : SetField({trackId}, mute) {}
    std::string name() const override { return "Mute track"; }
};

class SetTrackInput final : public Command
{
public:
    // A single device input channel (-1 = none), or a stereo input bus by name,
    // which takes the place of the channel.
    SetTrackInput(Id trackId, int inputChannel, std::string inputBus = {});
    std::string name() const override { return "Set track input"; }
    bool undoable() const noexcept override { return false; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_;
    int value_, previous_ = -1;
    std::string bus_, previousBus_;
};

class SetTrackArmed final : public SetField<OnTrack, &Track::armed>
{
public:
    SetTrackArmed(Id trackId, bool armed) : SetField({trackId}, armed) {}
    std::string name() const override { return "Arm track"; }
    bool undoable() const noexcept override { return false; }
};

class SetTrackMonitor final : public SetField<OnTrack, &Track::monitor>
{
public:
    SetTrackMonitor(Id trackId, MonitorMode mode) : SetField({trackId}, mode) {}
    std::string name() const override { return "Set track monitoring"; }
    bool undoable() const noexcept override { return false; }
};

class SetTrackSolo final : public SetField<OnChannel, &Channel::solo>
{
public:
    SetTrackSolo(Id trackId, bool solo) : SetField({trackId}, solo) {}
    std::string name() const override { return "Solo track"; }
};

class AddClip final : public Command
{
public:
    // The clip's id is allocated on apply if it is invalidId.
    AddClip(Id trackId, AudioClip clip);
    std::string name() const override { return "Add clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id clipId() const noexcept { return clip_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(clip_); }

private:
    Id trackId_;
    AudioClip clip_;
};

class RemoveClip final : public Command
{
public:
    RemoveClip(Id trackId, Id clipId);
    std::string name() const override { return "Remove clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(removed_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_, clipId_;
    SamplePosition start_, length_, sourceOffset_;
    SamplePosition previousStart_ = 0, previousLength_ = 0, previousOffset_ = 0;
};

// Fade lengths in samples, clamped to the clip, and (when given) their curves
// from -1 to 1; a curve not given stays as it is.
class SetClipFades final : public Command
{
public:
    SetClipFades(Id trackId,
                 Id clipId,
                 SamplePosition fadeIn,
                 SamplePosition fadeOut,
                 std::optional<float> fadeInCurve = std::nullopt,
                 std::optional<float> fadeOutCurve = std::nullopt);
    std::string name() const override { return "Set clip fades"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_, clipId_;
    SamplePosition fadeIn_, fadeOut_, previousIn_ = 0, previousOut_ = 0;
    std::optional<float> inCurve_, outCurve_;
    float previousInCurve_ = 0.0f, previousOutCurve_ = 0.0f;
};

// Splits a clip at a timeline position strictly inside it. The left part keeps
// the id (and loses its fade-out); the right part gets a new id (and the fade-out).
class SplitClip final : public Command
{
public:
    SplitClip(Id trackId, Id clipId, SamplePosition at);
    std::string name() const override { return "Split clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id rightClipId() const noexcept { return right_.id; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(original_) + heapBytes(right_);
    }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    static bool canJoin(const AudioClip& left, const AudioClip& right) noexcept;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(previousLeft_) + heapBytes(removedRight_);
    }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id newClipId() const noexcept { return copy_.id; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(copy_); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_, leftId_, rightId_;
    SamplePosition previousOut_ = 0, previousIn_ = 0;
};

// Moves every clip starting at or after `from` by `delta` (ripple editing);
// clips that would start before zero are pinned at zero. Undo moves them back.
// Take groups starting at or after `from` move too (their takes and comp).
class ShiftClips final : public Command
{
public:
    ShiftClips(Id trackId, SamplePosition from, SamplePosition delta);
    std::string name() const override { return "Shift clips"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(previous_) + heapBytes(shiftedGroups_);
    }

private:
    Id trackId_;
    SamplePosition from_, delta_;
    std::vector<std::pair<Id, SamplePosition>> previous_;      // clip id, start before
    std::vector<std::pair<Id, SamplePosition>> shiftedGroups_; // take group id, delta applied
};

class SetClipGain final : public SetField<OnClip, &AudioClip::gain>
{
public:
    SetClipGain(Id trackId, Id clipId, float gain) : SetField({trackId, clipId}, gain) {}
    std::string name() const override { return "Set clip gain"; }
};

// Mutes or unmutes an audio clip (`midi` false) or a MIDI clip: it stays on the
// timeline and plays nothing.
class SetClipMute final : public Command
{
public:
    SetClipMute(Id trackId, Id clipId, bool midi, bool muted);
    std::string name() const override { return value_ ? "Mute clip" : "Unmute clip"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    bool* flag(Project& p) const; // null when the clip is gone
    Id trackId_, clipId_;
    bool midi_, value_, previous_ = false;
};

// Points every clip, take and frozen track using a media file at another path
// (consolidating media into the project folder, relinking a missing file):
// old path -> new path.
class SetMediaPaths final : public Command
{
public:
    explicit SetMediaPaths(std::vector<std::pair<std::string, std::string>> renames,
                           std::string name = "Consolidate media");
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    std::vector<std::pair<std::string, std::string>> renames_;
    std::string name_;
    struct Changed
    {
        Id track, clip; // clip invalidId: the track's frozen render
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
    bool apply(Project&) override;
    bool revert(Project&) override;
    const VocalEdit& edit() const noexcept { return value_; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(value_) + heapBytes(previous_);
    }

private:
    Id trackId_, clipId_;
    VocalEdit value_, previous_;
    std::string name_;
};

class SetProjectKey final : public SetField<OnProject, &Project::key>
{
public:
    explicit SetProjectKey(MusicalKey key) : SetField({}, key) {}
    std::string name() const override { return "Set key"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
};

// ----- folders and freezing ------------------------------------------------------------------

// A new folder track at `index` (end when < 0) holding `members` (moved into it,
// keeping their order, right after it). Its id is allocated on apply.
class AddFolderTrack final : public Command
{
public:
    AddFolderTrack(std::string name, std::vector<Id> members, int index = -1);
    std::string name() const override { return "Add folder track"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id folderId() const noexcept { return folder_.id; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(folder_) + heapBytes(previousOrder_);
    }

private:
    Track folder_;
    std::vector<Id> members_;
    int index_;
    std::vector<Track> previousOrder_;
};

// Moves a track into a folder (invalidId: out of any). A folder cannot go into
// itself or a folder inside it.
class SetTrackFolder final : public Command
{
public:
    SetTrackFolder(Id trackId, Id folderId);
    std::string name() const override
    {
        return folderId_ == invalidId ? "Remove from folder" : "Move into folder";
    }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_, folderId_, previous_ = invalidId;
};

// Opens or closes a folder (what the arrange view shows; no engine work).
class SetFolderOpen final : public SetField<OnTrack, &Track::folderOpen>
{
public:
    SetFolderOpen(Id folderId, bool open) : SetField({folderId}, open) {}
    std::string name() const override { return value_ ? "Open folder" : "Close folder"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
};

// Freezes a track to a render of its output (`media`), or unfreezes it (empty).
class SetTrackFrozen final : public Command
{
public:
    SetTrackFrozen(Id trackId, std::string media);
    std::string name() const override { return media_.empty() ? "Unfreeze track" : "Freeze track"; }
    bool apply(Project&) override;
    bool revert(Project&) override;

private:
    Id trackId_;
    std::string media_, previous_;
};

// What a tempo edit changes and restores: the tempo track (core/TempoMap.h
// TempoState), and every position that follows it (MIDI clips, clips that
// follow the tempo, automation, markers), captured whole so undo puts them back
// exactly rather than remapping again.
class MusicalPositions
{
public:
    void capture(const Project& p);
    void restore(Project& p) const;
    std::size_t footprint() const noexcept;

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + positions_.footprint(); }

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
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(previous_); }

private:
    Id trackId_, clipId_;
    double stretch_, transpose_;
    SamplePosition length_;
    bool followTempo_;
    AudioClip previous_;
};

class SetTimeSignature final : public SetField<OnProject, &Project::timeSignature>
{
public:
    SetTimeSignature(int numerator, int denominator)
        : SetField({}, TimeSignature{std::clamp(numerator, 1, 32), std::clamp(denominator, 1, 32)})
    {
    }
    std::string name() const override { return "Set time signature"; }
};

// The tempo at the song's start (what the header sets when there are no tempo
// changes); musical content keeps its bars and beats.
class SetTempo final : public Command
{
public:
    explicit SetTempo(double bpm);
    std::string name() const override { return "Set tempo"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + positions_.footprint(); }

private:
    double value_, previous_ = 120.0;
    MusicalPositions positions_;
};

// ----- mixer: routing, solo, sends, insert chains -------------------------------------------

// A channel's send to one bus, found by id each time it runs.
struct OnSend
{
    Id channel = invalidId;
    Id bus = invalidId;
    Send* find(Project& p) const noexcept
    {
        auto* c = p.findChannel(channel);
        return c != nullptr ? c->findSend(bus) : nullptr;
    }
    bool operator==(const OnSend&) const = default;
};

// A send's balance (-1..1): a live value, ramped like its level.
class SetSendPan final : public SetField<OnSend, &Send::pan>
{
public:
    SetSendPan(Id channelId, Id busId, float pan) : SetField({channelId, busId}, std::clamp(pan, -1.0f, 1.0f))
    {
    }
    std::string name() const override { return "Set send pan"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
};

// Mutes a send without losing its level: the level ramps to silence and back.
class SetSendMute final : public SetField<OnSend, &Send::mute>
{
public:
    SetSendMute(Id channelId, Id busId, bool mute) : SetField({channelId, busId}, mute) {}
    std::string name() const override { return value_ ? "Mute send" : "Unmute send"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::values; }
    Id liveChannel() const noexcept override { return target_.channel; }
};

// Plays a track or bus straight to a device output pair (Channel::hardwareOutput;
// -1 returns it to its bus or the master).
class SetHardwareOutput final : public SetField<OnChannel, &Channel::hardwareOutput>
{
public:
    SetHardwareOutput(Id channelId, int pair) : SetField({channelId}, std::max(-1, pair)) {}
    std::string name() const override { return "Set output"; }
    bool apply(Project& p) override
    {
        if (target_.channel == masterId)
            return false; // the master always plays to the first pair
        return SetField::apply(p);
    }
};

// Keeps a channel audible whatever else is soloed (resolved at compile).
class SetSoloSafe final : public SetField<OnChannel, &Channel::soloSafe>
{
public:
    SetSoloSafe(Id channelId, bool safe) : SetField({channelId}, safe) {}
    std::string name() const override { return value_ ? "Make solo safe" : "Clear solo safe"; }
};

// Leaves a channel's strip out of the mixer (or brings it back); it still plays.
class SetMixerHidden final : public SetField<OnChannel, &Channel::mixerHidden>
{
public:
    SetMixerHidden(Id channelId, bool hidden) : SetField({channelId}, hidden) {}
    std::string name() const override { return value_ ? "Hide strip" : "Show strip"; }
    ChangeKind changeKind() const noexcept override { return ChangeKind::cosmetic; }
    bool rebuildsViews() const noexcept override { return true; }
};

// Solos one channel alone (Ctrl+click on a solo button): every other track and
// bus is unsoloed. When it already is the only one soloed, the solo is cleared.
class SetExclusiveSolo final : public Command
{
public:
    explicit SetExclusiveSolo(Id channelId) : channelId_(channelId) {}
    std::string name() const override { return "Solo exclusively"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override { return sizeof(*this) + previous_.capacity() * 16; }

private:
    Id channelId_;
    std::vector<std::pair<Id, bool>> previous_; // every track's and bus's solo before
};

// Moves an insert from one channel's chain to another's at `index` (or the end
// when index < 0). It keeps its id, so the live instance and its state travel
// with it, and its automation lanes go along. A key source the new channel
// cannot use (itself, a loop, the master) is cleared.
class MoveInsertToChannel final : public Command
{
public:
    MoveInsertToChannel(Id fromChannel, Id insertId, Id toChannel, int index = -1);
    std::string name() const override { return "Move insert"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override;

private:
    Id from_, insertId_, to_;
    int index_;
    // Both chains and their lanes as they were, for revert.
    std::vector<Insert> fromInserts_, toInserts_;
    std::vector<AutomationLane> fromLanes_, toLanes_;
};

// Adds copies of inserts (a paste, a Ctrl+drag, a copied chain) at `index` (or
// the end when index < 0), each with a new id, as one step. A key source the
// channel cannot use is cleared; the parameters and state are the copies'.
class AddInserts final : public Command
{
public:
    AddInserts(Id channelId, std::vector<Insert> inserts, int index = -1, std::string name = "Paste inserts");
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    // The ids the copies got (known once applied).
    const std::vector<Insert>& inserts() const noexcept { return inserts_; }
    std::size_t footprint() const noexcept override { return sizeof(*this) + heapBytes(inserts_); }

private:
    Id channelId_;
    std::vector<Insert> inserts_;
    int index_;
    std::string name_;
    bool idsAssigned_ = false;
};

// Replaces an effect in place with another type: it keeps its id, place, mix
// and key and takes the replacement's type, parameters and state. The old
// effect's parameter lanes go with it (and come back on undo).
class ReplaceInsert final : public Command
{
public:
    bool capturesInsertState() const noexcept override { return true; }
    ReplaceInsert(Id channelId, Id insertId, Insert replacement);
    std::string name() const override { return "Replace insert"; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(replacement_) + heapBytes(previous_) + heapBytes(previousLanes_);
    }

private:
    Id channelId_, insertId_;
    Insert replacement_, previous_;
    std::vector<AutomationLane> previousLanes_;
};

// A hosted plugin's state changed by a preset or a program (loaded through the
// plugin window or its card): `before` and `after` are the plugin's own state
// around the load. The first apply only records it (the plugin already has it);
// undo and redo hand the state back to the live instance (pushesInsertState).
class SetPluginState final : public Command
{
public:
    SetPluginState(Id channelId, Id insertId, std::string before, std::string after, std::string name);
    std::string name() const override { return name_; }
    bool apply(Project&) override;
    bool revert(Project&) override;
    Id pushesInsertState() const noexcept override { return replayed_ ? insertId_ : invalidId; }
    std::size_t footprint() const noexcept override
    {
        return sizeof(*this) + heapBytes(before_) + heapBytes(after_) + heapBytes(name_);
    }

private:
    Id channelId_, insertId_;
    std::string before_, after_, name_;
    bool replayed_ = false; // undone at least once: redo must push the state too
};

} // namespace northlight::core
