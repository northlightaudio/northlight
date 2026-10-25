#include "core/Command.h"

#include "core/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace northlight::core
{

// ----- undo stack -------------------------------------------------------------------------

class UndoStack::Group final : public Command
{
public:
    explicit Group(std::string name) : name_(std::move(name)) {}
    std::string name() const override { return name_; }
    // All or nothing: a member that fails has the members before it put back,
    // so the project is as it was and the group stays where it is.
    bool apply(Project& p) override
    {
        for (size_t done = 0; done < commands_.size(); ++done)
            if (!commands_[done]->apply(p))
            {
                while (done > 0)
                    commands_[--done]->revert(p);
                return false;
            }
        return true;
    }
    bool revert(Project& p) override
    {
        for (size_t done = 0; done < commands_.size(); ++done)
            if (!commands_[commands_.size() - 1 - done]->revert(p))
            {
                while (done > 0)
                    commands_[commands_.size() - done--]->apply(p);
                return false;
            }
        return true;
    }
    // A member that sets what the last one set (a drag's next tick) folds
    // into it, so a gesture keeps one command per field, not one per tick.
    void add(std::unique_ptr<Command> c)
    {
        if (!commands_.empty() && commands_.back()->mergeWith(*c))
            return;
        commands_.push_back(std::move(c));
    }
    bool empty() const noexcept { return commands_.empty(); }
    ChangeKind changeKind() const noexcept override
    {
        auto kind = ChangeKind::cosmetic;
        for (const auto& c : commands_)
            kind = std::max(kind, c->changeKind());
        return kind;
    }
    bool rebuildsViews() const noexcept override
    {
        return std::any_of(
            commands_.begin(), commands_.end(), [](const auto& c) { return c->rebuildsViews(); });
    }
    Id liveChannel() const noexcept override
    {
        // One channel when every member names the same one (a fader gesture).
        if (commands_.empty())
            return invalidId;
        const Id first = commands_.front()->liveChannel();
        for (const auto& c : commands_)
            if (c->liveChannel() != first)
                return invalidId;
        return first;
    }
    void touchedParameters(std::vector<ParameterTouch>& out) const override
    {
        for (const auto& c : commands_)
            c->touchedParameters(out);
    }
    std::size_t footprint() const noexcept override
    {
        std::size_t bytes = sizeof(*this) + commands_.capacity() * sizeof(commands_[0]);
        for (const auto& c : commands_)
            bytes += c->footprint();
        return bytes;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

UndoStack::UndoStack() = default;
UndoStack::~UndoStack() = default;

bool UndoStack::execute(Project& project, std::unique_ptr<Command> command)
{
    if (!command->apply(project))
        return false;
    if (openGroup_ != nullptr)
        openGroup_->add(std::move(command));
    else
        push(std::move(command));
    return true;
}

void UndoStack::push(std::unique_ptr<Command> command)
{
    // The save point was on the redo side: it goes with the redo history.
    if (savedDepth_ > static_cast<std::ptrdiff_t>(undo_.size()))
        savedDepth_ = -1;
    for (const auto& step : redo_)
        bytes_ -= step.bytes;
    redo_.clear();
    const auto bytes = command->footprint();
    undo_.push_back({std::move(command), bytes});
    bytes_ += bytes;
    trim();
}

void UndoStack::trim()
{
    size_t drop = 0;
    for (size_t kept = undo_.size(); kept > 1 && (kept > maxSteps_ || bytes_ > maxBytes_); --kept)
        bytes_ -= undo_[drop++].bytes;
    if (drop == 0)
        return;
    undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(drop));
    // The save point moves down with the history, or goes with the dropped steps.
    savedDepth_ = savedDepth_ >= static_cast<std::ptrdiff_t>(drop)
                      ? savedDepth_ - static_cast<std::ptrdiff_t>(drop)
                      : -1;
}

void UndoStack::setLimits(std::size_t maxSteps, std::size_t maxBytes)
{
    maxSteps_ = std::max<std::size_t>(1, maxSteps);
    maxBytes_ = maxBytes;
    trim();
}

void UndoStack::markSaved() noexcept
{
    savedDepth_ = static_cast<std::ptrdiff_t>(undo_.size());
}

bool UndoStack::atSavePoint() const noexcept
{
    return savedDepth_ == static_cast<std::ptrdiff_t>(undo_.size()) &&
           (openGroup_ == nullptr || openGroup_->empty());
}

bool UndoStack::undo(Project& project)
{
    if (undo_.empty())
        return false;
    // Revert first: a command that fails stays on the undo side.
    if (!undo_.back().command->revert(project))
        return false;
    redo_.push_back(std::move(undo_.back()));
    undo_.pop_back();
    return true;
}

bool UndoStack::redo(Project& project)
{
    if (redo_.empty())
        return false;
    if (!redo_.back().command->apply(project))
        return false;
    undo_.push_back(std::move(redo_.back()));
    redo_.pop_back();
    return true;
}

std::string UndoStack::undoName() const
{
    return undo_.empty() ? std::string{} : undo_.back().command->name();
}

std::string UndoStack::redoName() const
{
    return redo_.empty() ? std::string{} : redo_.back().command->name();
}

void UndoStack::clear()
{
    undo_.clear();
    redo_.clear();
    bytes_ = 0;
    openGroup_.reset();
    groupDepth_ = 0;
    savedDepth_ = 0;
}

void UndoStack::beginGroup(std::string name)
{
    if (openGroup_ == nullptr)
        openGroup_ = std::make_unique<Group>(std::move(name));
    ++groupDepth_;
}

void UndoStack::endGroup()
{
    if (openGroup_ == nullptr)
        return;
    if (--groupDepth_ > 0)
        return; // an inner group: the outer one is still open
    groupDepth_ = 0;
    auto group = std::move(openGroup_);
    if (!group->empty())
        push(std::move(group));
}

// ----- helpers ----------------------------------------------------------------------------

std::size_t heapBytes(const VocalEdit& edit) noexcept
{
    return heapBytes(edit.notes) + heapBytes(edit.segments);
}

std::size_t heapBytes(const AudioClip& clip) noexcept
{
    return heapBytes(clip.name) + heapBytes(clip.mediaPath) + heapBytes(clip.vocal);
}

std::size_t heapBytes(const MidiClip& clip) noexcept
{
    return heapBytes(clip.name) + heapBytes(clip.notes) + heapBytes(clip.controllers);
}

std::size_t heapBytes(const AutomationLane& lane) noexcept
{
    return heapBytes(lane.points) + heapBytes(lane.parameterKey);
}

std::size_t heapBytes(const Insert& insert) noexcept
{
    return heapBytes(insert.type) + heapBytes(insert.params) + heapBytes(insert.state) +
           heapBytes(insert.paramKeys);
}

std::size_t heapBytes(const TakeGroup& group) noexcept
{
    std::size_t bytes =
        heapBytes(group.comp) + heapBytes(group.clips) + group.takes.capacity() * sizeof(Take);
    for (const auto& take : group.takes)
        bytes += heapBytes(take.clip);
    return bytes;
}

std::size_t heapBytes(const Channel& channel) noexcept
{
    return heapBytes(channel.name) + heapBytes(channel.sends) + heapBytes(channel.instrument) +
           heapBytes(channel.inserts) + heapBytes(channel.strip) + heapBytes(channel.automation);
}

std::size_t heapBytes(const Track& track) noexcept
{
    return heapBytes(static_cast<const Channel&>(track)) + heapBytes(track.inputBus) +
           heapBytes(track.midiInput) + heapBytes(track.clips) + heapBytes(track.midiClips) +
           heapBytes(track.takeGroups) + heapBytes(track.frozenMedia);
}

namespace
{
// Two-level lookups (null when either is gone).
AudioClip* findClip(Project& p, Id track, Id clip) noexcept
{
    return OnClip{track, clip}.find(p);
}

MidiClip* findMidiClip(Project& p, Id track, Id clip) noexcept
{
    auto* t = p.findTrack(track);
    return t != nullptr ? t->findMidiClip(clip) : nullptr;
}

Insert* findInsert(Project& p, Id channel, Id insert) noexcept
{
    return OnInsert{channel, insert}.find(p);
}

AutomationLane* findLane(Project& p, Id channel, Id lane) noexcept
{
    return OnLane{channel, lane}.find(p);
}

// Gives a new channel, and each of its parts that has none, an id. Ids
// allocated here stay in the command, so a redo brings back the same ones
// (later commands in the history refer to them); a caller-chosen id is never
// handed out again.
void allocateChannelId(Project& p, Channel& c)
{
    if (c.id == invalidId)
        c.id = p.allocateId();
    p.nextId = std::max(p.nextId, c.id + 1);
}

void allocateProcessorIds(Project& p, Channel& c)
{
    if (c.hasInstrument() && c.instrument.id == invalidId)
        c.instrument.id = p.allocateId();
    for (auto* list : {&c.inserts, &c.strip})
        for (auto& i : *list)
            if (i.id == invalidId)
                i.id = p.allocateId();
}

void allocateIds(Project& p, Bus& b)
{
    allocateChannelId(p, b);
    allocateProcessorIds(p, b);
}

void allocateIds(Project& p, Track& t)
{
    allocateChannelId(p, t);
    for (auto& c : t.clips)
        if (c.id == invalidId)
            c.id = p.allocateId();
    for (auto& c : t.midiClips)
        if (c.id == invalidId)
            c.id = p.allocateId();
    allocateProcessorIds(p, t);
}
} // namespace

// ----- tracks -----------------------------------------------------------------------------

AddTrack::AddTrack(Track track, int index) : track_(std::move(track)), index_(index) {}

void AddTrack::setDefaultStrip(const std::vector<Insert>& modules)
{
    if (track_.strip.empty() && track_.hasAudio()) // a MIDI track makes no audio to process
        track_.strip = modules;
}

bool AddTrack::apply(Project& p)
{
    allocateIds(p, track_);
    const int at =
        index_ < 0 || index_ > static_cast<int>(p.tracks.size()) ? static_cast<int>(p.tracks.size()) : index_;
    index_ = at;
    p.tracks.insert(p.tracks.begin() + at, track_);
    return true;
}

bool AddTrack::revert(Project& p)
{
    const int i = p.trackIndex(track_.id);
    if (i >= 0)
        p.tracks.erase(p.tracks.begin() + i);
    return true;
}

AddInstrumentTrack::AddInstrumentTrack(Track instrumentChannel, Track midiTrack, int index)
    : channel_(std::move(instrumentChannel)), midi_(std::move(midiTrack)), index_(index)
{
    channel_.kind = TrackKind::instrument;
    midi_.kind = TrackKind::midi;
}

void AddInstrumentTrack::setDefaultStrip(const std::vector<Insert>& modules)
{
    if (channel_.strip.empty())
        channel_.strip = modules;
}

bool AddInstrumentTrack::apply(Project& p)
{
    allocateIds(p, midi_);
    allocateIds(p, channel_);
    midi_.midiTarget = channel_.id;
    const int at =
        index_ < 0 || index_ > static_cast<int>(p.tracks.size()) ? static_cast<int>(p.tracks.size()) : index_;
    index_ = at;
    p.tracks.insert(p.tracks.begin() + at, channel_);
    p.tracks.insert(p.tracks.begin() + at, midi_); // the MIDI track above its instrument
    return true;
}

bool AddInstrumentTrack::revert(Project& p)
{
    std::erase_if(p.tracks, [this](const Track& t) { return t.id == midi_.id || t.id == channel_.id; });
    return true;
}

SetMidiTarget::SetMidiTarget(Id trackId, Id target, int midiChannel)
    : trackId_(trackId), target_(target), channel_(std::clamp(midiChannel, 1, 16))
{
}

bool SetMidiTarget::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr || !t->isMidi())
        return false;
    previousTarget_ = t->midiTarget;
    previousChannel_ = t->midiChannel;
    t->midiTarget = target_;
    t->midiChannel = channel_;
    return true;
}

bool SetMidiTarget::revert(Project& p)
{
    if (auto* t = p.findTrack(trackId_))
    {
        t->midiTarget = previousTarget_;
        t->midiChannel = previousChannel_;
    }
    return true;
}

RemoveTrack::RemoveTrack(Id trackId) : trackId_(trackId) {}

void DetachedReferences::detach(Project& p, Id removed)
{
    midiTracks.clear();
    auxTracks.clear();
    sidechains.clear();
    for (auto& t : p.tracks)
    {
        if (t.isMidi() && t.midiTarget == removed)
        {
            midiTracks.push_back(t.id);
            t.midiTarget = invalidId;
        }
        if (t.isAux() && t.auxTrack == removed)
        {
            auxTracks.push_back(t.id);
            t.auxTrack = invalidId;
        }
    }
    const auto keys = [&](Channel& c)
    {
        for (auto* list : {&c.inserts, &c.strip})
            for (auto& insert : *list)
                if (insert.sidechain == removed)
                {
                    sidechains.emplace_back(c.id, insert.id);
                    insert.sidechain = invalidId;
                }
    };
    for (auto& t : p.tracks)
        keys(t);
    for (auto& b : p.buses)
        keys(b);
    keys(p.master);
}

void DetachedReferences::restore(Project& p, Id removed) const
{
    for (const Id id : midiTracks)
        if (auto* t = p.findTrack(id))
            t->midiTarget = removed;
    for (const Id id : auxTracks)
        if (auto* t = p.findTrack(id))
            t->auxTrack = removed;
    for (const auto& [channelId, insertId] : sidechains)
        if (auto* c = p.findChannel(channelId))
            if (auto* insert = c->findInsert(insertId))
                insert->sidechain = removed;
}

bool RemoveTrack::apply(Project& p)
{
    index_ = p.trackIndex(trackId_);
    if (index_ < 0)
        return false;
    removed_ = p.tracks[static_cast<size_t>(index_)];
    p.tracks.erase(p.tracks.begin() + index_);
    detached_.detach(p, trackId_);
    return true;
}

bool RemoveTrack::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.tracks.size()));
    p.tracks.insert(p.tracks.begin() + at, removed_);
    detached_.restore(p, trackId_);
    return true;
}

MoveTrack::MoveTrack(Id trackId, int newIndex) : trackId_(trackId), newIndex_(newIndex) {}

bool MoveTrack::apply(Project& p)
{
    oldIndex_ = p.trackIndex(trackId_);
    if (oldIndex_ < 0)
        return false;
    newIndex_ = std::clamp(newIndex_, 0, static_cast<int>(p.tracks.size()) - 1);
    Track moved = std::move(p.tracks[static_cast<size_t>(oldIndex_)]);
    p.tracks.erase(p.tracks.begin() + oldIndex_);
    p.tracks.insert(p.tracks.begin() + newIndex_, std::move(moved));
    return true;
}

bool MoveTrack::revert(Project& p)
{
    Track moved = std::move(p.tracks[static_cast<size_t>(newIndex_)]);
    p.tracks.erase(p.tracks.begin() + newIndex_);
    p.tracks.insert(p.tracks.begin() + oldIndex_, std::move(moved));
    return true;
}

AddBus::AddBus(Bus bus, int index) : bus_(std::move(bus)), index_(index) {}

void AddBus::setDefaultStrip(const std::vector<Insert>& modules)
{
    if (bus_.strip.empty())
        bus_.strip = modules;
}

bool AddBus::apply(Project& p)
{
    allocateIds(p, bus_);
    const int at =
        index_ < 0 || index_ > static_cast<int>(p.buses.size()) ? static_cast<int>(p.buses.size()) : index_;
    index_ = at;
    p.buses.insert(p.buses.begin() + at, bus_);
    return true;
}

bool AddBus::revert(Project& p)
{
    const int i = p.busIndex(bus_.id);
    if (i >= 0)
        p.buses.erase(p.buses.begin() + i);
    return true;
}

RemoveBus::RemoveBus(Id busId) : busId_(busId) {}

bool RemoveBus::apply(Project& p)
{
    index_ = p.busIndex(busId_);
    if (index_ < 0)
        return false;
    removed_ = p.buses[static_cast<size_t>(index_)];
    detachedOutputs_.clear();
    detachedSends_.clear();
    const auto detach = [&](Channel& c)
    {
        if (c.outputBus == busId_)
        {
            detachedOutputs_.emplace_back(c.id, c.outputBus);
            c.outputBus = invalidId;
        }
        for (size_t i = 0; i < c.sends.size();)
        {
            if (c.sends[i].bus == busId_)
            {
                detachedSends_.push_back({c.id, c.sends[i], i});
                c.sends.erase(c.sends.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
                ++i;
        }
    };
    for (auto& t : p.tracks)
        detach(t);
    for (auto& b : p.buses)
        if (b.id != busId_)
            detach(b);
    p.buses.erase(p.buses.begin() + index_);
    detached_.detach(p, busId_);
    return true;
}

bool RemoveBus::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.buses.size()));
    p.buses.insert(p.buses.begin() + at, removed_);
    detached_.restore(p, busId_);
    for (const auto& [channelId, output] : detachedOutputs_)
        if (auto* c = p.findChannel(channelId))
            c->outputBus = output;
    // In recorded order the indices are ascending per channel, so inserting at
    // each one restores the original order.
    for (const auto& d : detachedSends_)
        if (auto* c = p.findChannel(d.channel))
            c->sends.insert(
                c->sends.begin() + static_cast<std::ptrdiff_t>(std::min(d.index, c->sends.size())), d.send);
    return true;
}

// ----- MIDI ----------------------------------------------------------------------------------

namespace
{
void clampNote(MidiNote& n)
{
    n.note = std::clamp(n.note, 0, 127);
    n.velocity = std::clamp(n.velocity, 1, 127);
    n.start = std::max<SamplePosition>(0, n.start);
    n.length = std::max<SamplePosition>(1, n.length);
}

void clampController(MidiControllerEvent& e)
{
    e.number = std::clamp(e.number, 0, 127);
    e.value = std::clamp(e.value, midiControllerMinimum(e.type), midiControllerMaximum(e.type));
    e.time = std::max<SamplePosition>(0, e.time);
}
} // namespace

AddMidiClip::AddMidiClip(Id trackId, MidiClip clip) : trackId_(trackId), clip_(std::move(clip)) {}
bool AddMidiClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (clip_.id == invalidId)
        clip_.id = p.allocateId();
    for (auto& n : clip_.notes)
    {
        if (n.id == invalidId)
            n.id = p.allocateId();
        clampNote(n);
    }
    for (auto& e : clip_.controllers)
    {
        if (e.id == invalidId)
            e.id = p.allocateId();
        clampController(e);
    }
    clip_.start = std::max<SamplePosition>(0, clip_.start);
    clip_.length = std::max<SamplePosition>(0, clip_.length);
    clip_.sortNotes();
    clip_.sortControllers();
    t->midiClips.push_back(clip_);
    t->sortMidiClips();
    return true;
}
bool AddMidiClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    std::erase_if(t->midiClips, [this](const MidiClip& c) { return c.id == clip_.id; });
    return true;
}

RemoveMidiClip::RemoveMidiClip(Id trackId, Id clipId) : trackId_(trackId), clipId_(clipId) {}
bool RemoveMidiClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    const auto* found = t->findMidiClip(clipId_);
    if (found == nullptr)
        return false;
    removed_ = *found;
    std::erase_if(t->midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
    return true;
}
bool RemoveMidiClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->midiClips.push_back(removed_);
    t->sortMidiClips();
    return true;
}

MoveMidiClip::MoveMidiClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart)
    : fromTrackId_(fromTrackId), clipId_(clipId), toTrackId_(toTrackId), newStart_(newStart)
{
}
bool MoveMidiClip::apply(Project& p)
{
    auto* from = p.findTrack(fromTrackId_);
    auto* to = p.findTrack(toTrackId_);
    if (from == nullptr || to == nullptr || !to->isMidi()) // a MIDI clip only lives on a MIDI track
        return false;
    const auto* found = from->findMidiClip(clipId_);
    if (found == nullptr)
        return false;
    MidiClip clip = *found;
    previousStart_ = clip.start;
    clip.start = std::max<SamplePosition>(0, newStart_);
    std::erase_if(from->midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
    to->midiClips.push_back(clip);
    to->sortMidiClips();
    return true;
}
bool MoveMidiClip::revert(Project& p)
{
    auto* to = p.findTrack(toTrackId_);
    auto* from = p.findTrack(fromTrackId_);
    const auto* found = to != nullptr ? to->findMidiClip(clipId_) : nullptr;
    if (found == nullptr || from == nullptr)
        return false;
    MidiClip clip = *found;
    clip.start = previousStart_;
    std::erase_if(to->midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
    from->midiClips.push_back(clip);
    from->sortMidiClips();
    return true;
}

TrimMidiClip::TrimMidiClip(Id trackId, Id clipId, SamplePosition start, SamplePosition length)
    : trackId_(trackId), clipId_(clipId), start_(start), length_(length)
{
}
bool TrimMidiClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findMidiClip(clipId_);
    if (c == nullptr)
        return false;
    previous_ = *c;
    const auto newStart = std::max<SamplePosition>(0, start_);
    const auto shift = c->start - newStart; // notes move the other way to stay put
    for (auto& n : c->notes)
        n.start += shift;
    for (auto& e : c->controllers) // before the start (negative) they still set the opening value
        e.time += shift;
    c->start = newStart;
    c->length = std::max<SamplePosition>(0, length_);
    t->sortMidiClips();
    return true;
}
bool TrimMidiClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findMidiClip(clipId_);
    if (found == nullptr)
        return false;
    *found = previous_;
    t->sortMidiClips();
    return true;
}

SplitMidiClip::SplitMidiClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
bool SplitMidiClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findMidiClip(clipId_);
    if (c == nullptr)
        return false;
    if (at_ <= c->start || at_ >= c->end())
        return false;
    original_ = *c;
    const auto offset = at_ - c->start; // the split, clip-relative
    if (right_.id == invalidId)
        right_.id = p.allocateId();
    right_.name = c->name;
    right_.start = at_;
    right_.length = c->end() - at_;
    right_.notes.clear();
    right_.controllers.clear();
    // The right part opens with each controller's value at the split, unless an
    // event of that controller falls exactly on it.
    std::vector<MidiControllerEvent> opening;
    for (const auto& e : c->controllers)
    {
        if (e.time >= offset)
            break; // sorted by time
        auto it = std::find_if(opening.begin(),
                               opening.end(),
                               [&](const MidiControllerEvent& o) { return o.sameLane(e.type, e.number); });
        if (it == opening.end())
            opening.push_back(e);
        else
            *it = e;
    }
    std::erase_if(opening,
                  [&](const MidiControllerEvent& o)
                  {
                      return o.type == MidiControllerType::polyPressure ||
                             std::any_of(c->controllers.begin(),
                                         c->controllers.end(),
                                         [&](const MidiControllerEvent& e)
                                         { return e.time == offset && e.sameLane(o.type, o.number); });
                  });
    for (size_t i = 0; i < opening.size(); ++i)
    {
        if (i >= chaseIds_.size())
            chaseIds_.push_back(p.allocateId());
        auto e = opening[i];
        e.id = chaseIds_[i];
        e.time = 0;
        right_.controllers.push_back(e);
    }
    std::vector<MidiControllerEvent> leftControllers;
    for (const auto& e : c->controllers)
    {
        if (e.time >= offset)
        {
            auto moved = e;
            moved.time -= offset;
            right_.controllers.push_back(moved);
        }
        else
            leftControllers.push_back(e);
    }
    c->controllers = std::move(leftControllers);
    std::vector<MidiNote> left;
    for (const auto& n : c->notes)
    {
        if (n.start >= offset)
        {
            MidiNote moved = n;
            moved.start -= offset;
            right_.notes.push_back(moved);
        }
        else
        {
            MidiNote kept = n;
            kept.length = std::min(kept.length, offset - kept.start); // cut at the split
            left.push_back(kept);
        }
    }
    c->notes = std::move(left);
    c->length = offset;
    t->midiClips.push_back(right_);
    t->sortMidiClips();
    return true;
}
bool SplitMidiClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findMidiClip(clipId_);
    if (found == nullptr)
        return false;
    std::erase_if(t->midiClips, [this](const MidiClip& c) { return c.id == right_.id; });
    *found = original_;
    t->sortMidiClips();
    return true;
}

DuplicateMidiClip::DuplicateMidiClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
bool DuplicateMidiClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    const auto* c = t->findMidiClip(clipId_);
    if (c == nullptr)
        return false;
    const bool fresh = copy_.id == invalidId;
    const auto keep = copy_;
    copy_ = *c;
    copy_.id = fresh ? p.allocateId() : keep.id;
    for (size_t i = 0; i < copy_.notes.size(); ++i)
        copy_.notes[i].id = fresh || i >= keep.notes.size() ? p.allocateId() : keep.notes[i].id;
    for (size_t i = 0; i < copy_.controllers.size(); ++i)
        copy_.controllers[i].id =
            fresh || i >= keep.controllers.size() ? p.allocateId() : keep.controllers[i].id;
    copy_.start = at_ >= 0 ? at_ : c->end();
    t->midiClips.push_back(copy_);
    t->sortMidiClips();
    return true;
}
bool DuplicateMidiClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    std::erase_if(t->midiClips, [this](const MidiClip& c) { return c.id == copy_.id; });
    return true;
}

bool JoinMidiClips::canJoin(const MidiClip& left, const MidiClip& right) noexcept
{
    return left.id != right.id && right.start >= left.start && left.muted == right.muted;
}
JoinMidiClips::JoinMidiClips(Id trackId, Id leftClipId, Id rightClipId)
    : trackId_(trackId), leftId_(leftClipId), rightId_(rightClipId)
{
}
bool JoinMidiClips::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* left = t->findMidiClip(leftId_);
    if (left == nullptr)
        return false;
    const auto* right = t->findMidiClip(rightId_);
    if (right == nullptr)
        return false;
    if (!canJoin(*left, *right))
        return false;
    previousLeft_ = *left;
    removedRight_ = *right;
    // The right's times are relative to its own start; they move to the left's.
    const auto offset = right->start - left->start;
    for (auto n : right->notes)
    {
        n.start += offset;
        left->notes.push_back(n);
    }
    for (auto e : right->controllers)
    {
        e.time += offset;
        left->controllers.push_back(e);
    }
    left->length = std::max(left->end(), right->end()) - left->start;
    left->sortNotes();
    left->sortControllers();
    std::erase_if(t->midiClips, [this](const MidiClip& c) { return c.id == rightId_; });
    return true;
}
bool JoinMidiClips::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findMidiClip(leftId_);
    if (found == nullptr)
        return false;
    *found = previousLeft_;
    t->midiClips.push_back(removedRight_);
    t->sortMidiClips();
    return true;
}

AddNote::AddNote(Id trackId, Id clipId, MidiNote note) : trackId_(trackId), clipId_(clipId), note_(note) {}
bool AddNote::apply(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    if (note_.id == invalidId)
        note_.id = p.allocateId();
    clampNote(note_);
    c->notes.push_back(note_);
    c->sortNotes();
    return true;
}
bool AddNote::revert(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    std::erase_if(c->notes, [this](const MidiNote& n) { return n.id == note_.id; });
    return true;
}

RemoveNote::RemoveNote(Id trackId, Id clipId, Id noteId) : trackId_(trackId), clipId_(clipId), noteId_(noteId)
{
}
bool RemoveNote::apply(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    const auto* found = c->findNote(noteId_);
    if (found == nullptr)
        return false;
    removed_ = *found;
    std::erase_if(c->notes, [this](const MidiNote& n) { return n.id == noteId_; });
    return true;
}
bool RemoveNote::revert(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    c->notes.push_back(removed_);
    c->sortNotes();
    return true;
}

SetNote::SetNote(Id trackId, Id clipId, MidiNote value) : trackId_(trackId), clipId_(clipId), value_(value) {}
bool SetNote::apply(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    auto* n = c->findNote(value_.id);
    if (n == nullptr)
        return false;
    previous_ = *n;
    *n = value_;
    clampNote(*n);
    c->sortNotes();
    return true;
}
bool SetNote::revert(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    auto* found = c->findNote(value_.id);
    if (found == nullptr)
        return false;
    *found = previous_;
    c->sortNotes();
    return true;
}

EditMidiControllers::EditMidiControllers(
    Id trackId, Id clipId, std::vector<MidiControllerEvent> events, std::vector<Id> remove, std::string name)
    : trackId_(trackId), clipId_(clipId), events_(std::move(events)), remove_(std::move(remove)),
      name_(std::move(name))
{
}
bool EditMidiControllers::apply(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    previous_ = c->controllers;
    if (!remove_.empty())
        std::erase_if(c->controllers,
                      [this](const MidiControllerEvent& e)
                      { return std::find(remove_.begin(), remove_.end(), e.id) != remove_.end(); });
    for (auto& e : events_)
    {
        clampController(e);
        if (e.id != invalidId)
            if (auto* existing = c->findController(e.id))
            {
                *existing = e;
                continue;
            }
        if (e.id == invalidId)
            e.id = p.allocateId();
        c->controllers.push_back(e);
    }
    c->sortControllers();
    return true;
}
bool EditMidiControllers::revert(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    c->controllers = previous_;
    return true;
}

QuantiseNotes::QuantiseNotes(
    Id trackId, Id clipId, std::vector<Id> noteIds, SamplePosition grid, float strength)
    : trackId_(trackId), clipId_(clipId), noteIds_(std::move(noteIds)), grid_(grid), strength_(strength)
{
}
QuantiseNotes::QuantiseNotes(
    Id trackId, Id clipId, std::vector<Id> noteIds, TempoMap map, GridStep step, float strength)
    : trackId_(trackId), clipId_(clipId), noteIds_(std::move(noteIds)),
      tempoGrid_(std::make_pair(std::move(map), step)), strength_(strength)
{
}
bool QuantiseNotes::apply(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    previous_.clear();
    const float strength = std::clamp(strength_, 0.0f, 1.0f);
    for (auto& n : c->notes)
    {
        if (!noteIds_.empty() && std::find(noteIds_.begin(), noteIds_.end(), n.id) == noteIds_.end())
            continue;
        previous_.emplace_back(n.id, n.start);
        const auto absolute = c->start + n.start;
        const auto target =
            tempoGrid_ ? tempoGrid_->first.snap(absolute, tempoGrid_->second) : snapToGrid(absolute, grid_);
        const auto moved =
            absolute + static_cast<SamplePosition>(std::llround((target - absolute) * strength));
        n.start = std::max<SamplePosition>(0, moved - c->start);
    }
    c->sortNotes();
    return true;
}
bool QuantiseNotes::revert(Project& p)
{
    auto* c = findMidiClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    for (const auto& [id, start] : previous_)
        if (auto* n = c->findNote(id))
            n->start = start;
    c->sortNotes();
    return true;
}

// ----- markers -------------------------------------------------------------------------------

AddMarker::AddMarker(Marker marker) : marker_(std::move(marker)) {}
bool AddMarker::apply(Project& p)
{
    if (marker_.id == invalidId)
        marker_.id = p.allocateId();
    marker_.position = std::max<SamplePosition>(0, marker_.position);
    marker_.length = std::max<SamplePosition>(0, marker_.length);
    p.markers.push_back(marker_);
    p.sortMarkers();
    return true;
}
bool AddMarker::revert(Project& p)
{
    std::erase_if(p.markers, [this](const Marker& m) { return m.id == marker_.id; });
    return true;
}

RemoveMarker::RemoveMarker(Id markerId) : markerId_(markerId) {}
bool RemoveMarker::apply(Project& p)
{
    const auto* found = p.findMarker(markerId_);
    if (found == nullptr)
        return false;
    removed_ = *found;
    std::erase_if(p.markers, [this](const Marker& m) { return m.id == markerId_; });
    return true;
}
bool RemoveMarker::revert(Project& p)
{
    p.markers.push_back(removed_);
    p.sortMarkers();
    return true;
}

SetMarker::SetMarker(Id markerId, SamplePosition position, SamplePosition length, std::string name)
    : markerId_(markerId)
{
    value_.id = markerId;
    value_.position = std::max<SamplePosition>(0, position);
    value_.length = std::max<SamplePosition>(0, length);
    value_.name = std::move(name);
}
bool SetMarker::apply(Project& p)
{
    auto* m = p.findMarker(markerId_);
    if (m == nullptr)
        return false;
    previous_ = *m;
    *m = value_;
    p.sortMarkers();
    return true;
}
bool SetMarker::revert(Project& p)
{
    auto* found = p.findMarker(markerId_);
    if (found == nullptr)
        return false;
    *found = previous_;
    p.sortMarkers();
    return true;
}

// ----- fader groups ----------------------------------------------------------------------------

AddGroup::AddGroup(Group group, std::vector<Id> members)
    : group_(std::move(group)), members_(std::move(members))
{
}

bool AddGroup::apply(Project& p)
{
    if (std::any_of(members_.begin(), members_.end(), [&p](Id id) { return p.findChannel(id) == nullptr; }))
        return false;
    if (group_.id == invalidId)
        group_.id = p.allocateId();
    p.groups.push_back(group_);
    previousGroups_.clear();
    for (const auto id : members_)
    {
        auto* c = p.findChannel(id);
        previousGroups_.emplace_back(id, c->group);
        c->group = group_.id;
    }
    return true;
}

bool AddGroup::revert(Project& p)
{
    for (const auto& [id, previous] : previousGroups_)
        if (auto* c = p.findChannel(id))
            c->group = previous;
    const int i = p.groupIndex(group_.id);
    if (i >= 0)
        p.groups.erase(p.groups.begin() + i);
    return true;
}

RemoveGroup::RemoveGroup(Id groupId) : groupId_(groupId) {}

bool RemoveGroup::apply(Project& p)
{
    index_ = p.groupIndex(groupId_);
    if (index_ < 0)
        return false;
    removed_ = p.groups[static_cast<size_t>(index_)];
    members_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        members_.push_back(c->id);
        c->group = invalidId;
    }
    p.groups.erase(p.groups.begin() + index_);
    return true;
}

bool RemoveGroup::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.groups.size()));
    p.groups.insert(p.groups.begin() + at, removed_);
    for (const auto id : members_)
        if (auto* c = p.findChannel(id))
            c->group = groupId_;
    return true;
}

SetChannelGroup::SetChannelGroup(Id channelId, Id groupId) : channelId_(channelId), value_(groupId) {}

bool SetChannelGroup::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (value_ != invalidId && p.findGroup(value_) == nullptr)
        return false;
    previous_ = c->group;
    c->group = value_;
    return true;
}

bool SetChannelGroup::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    c->group = previous_;
    return true;
}

SetGroupGain::SetGroupGain(Id groupId, float ratio) : groupId_(groupId), ratio_(ratio) {}

bool SetGroupGain::apply(Project& p)
{
    if (p.findGroup(groupId_) == nullptr)
        return false;
    previous_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        previous_.emplace_back(c->id, c->gain);
        c->gain = std::clamp(c->gain * ratio_, 0.0f, maxChannelGain);
    }
    return true;
}

bool SetGroupGain::revert(Project& p)
{
    for (const auto& [id, gain] : previous_)
        if (auto* c = p.findChannel(id))
            c->gain = gain;
    return true;
}

SetGroupSwitch::SetGroupSwitch(Id groupId, bool Channel::* field, bool value)
    : groupId_(groupId), field_(field), value_(value)
{
}

bool SetGroupSwitch::apply(Project& p)
{
    if (p.findGroup(groupId_) == nullptr)
        return false;
    previous_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        previous_.emplace_back(c->id, c->*field_);
        c->*field_ = value_;
    }
    return true;
}

bool SetGroupSwitch::revert(Project& p)
{
    for (const auto& [id, value] : previous_)
        if (auto* c = p.findChannel(id))
            c->*field_ = value;
    return true;
}

SetOutputBus::SetOutputBus(Id channelId, Id busId) : channelId_(channelId), value_(busId) {}
bool SetOutputBus::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (value_ != invalidId && p.findBus(value_) == nullptr)
        return false;
    if (p.wouldCycle(channelId_, value_))
        return false;
    previous_ = c->outputBus;
    c->outputBus = value_;
    return true;
}
bool SetOutputBus::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    c->outputBus = previous_;
    return true;
}

SetSend::SetSend(Id channelId, Id busId, float level, bool preFader)
    : channelId_(channelId), value_{busId, level, preFader}
{
}
bool SetSend::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (p.findBus(value_.bus) == nullptr)
        return false;
    if (p.wouldCycle(channelId_, value_.bus))
        return false;
    if (auto* existing = c->findSend(value_.bus))
    {
        existed_ = true;
        previous_ = *existing;
        *existing = value_;
    }
    else
    {
        existed_ = false;
        c->sends.push_back(value_);
    }
    return true;
}
bool SetSend::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (existed_)
    {
        if (auto* existing = c->findSend(value_.bus))
            *existing = previous_;
    }
    else
        std::erase_if(c->sends, [this](const Send& s) { return s.bus == value_.bus; });
    return true;
}

RemoveSend::RemoveSend(Id channelId, Id busId) : channelId_(channelId), busId_(busId) {}
bool RemoveSend::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    existed_ = false;
    if (auto* existing = c->findSend(busId_))
    {
        existed_ = true;
        previous_ = *existing;
        std::erase_if(c->sends, [this](const Send& s) { return s.bus == busId_; });
    }
    return true;
}
bool RemoveSend::revert(Project& p)
{
    if (!existed_)
        return true;
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    c->sends.push_back(previous_);
    return true;
}

AddInsert::AddInsert(Id channelId, Insert insert, int index)
    : channelId_(channelId), insert_(std::move(insert)), index_(index)
{
}
bool AddInsert::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (insert_.id == invalidId)
        insert_.id = p.allocateId();
    else
    {
        // A caller-supplied id must be unused: two inserts sharing one would share
        // one processor instance.
        for (const auto& t : p.tracks)
            if (t.findInsert(insert_.id) != nullptr)
                return false;
        for (const auto& b : p.buses)
            if (b.findInsert(insert_.id) != nullptr)
                return false;
        if (p.master.findInsert(insert_.id) != nullptr)
            return false;
    }
    const int at = index_ < 0 || index_ > static_cast<int>(c->inserts.size())
                       ? static_cast<int>(c->inserts.size())
                       : index_;
    index_ = at;
    c->inserts.insert(c->inserts.begin() + at, insert_);
    return true;
}
bool AddInsert::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    std::erase_if(c->inserts, [this](const Insert& i) { return i.id == insert_.id; });
    return true;
}

RemoveInsert::RemoveInsert(Id channelId, Id insertId) : channelId_(channelId), insertId_(insertId) {}
bool RemoveInsert::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    index_ = c->insertIndex(insertId_);
    if (index_ < 0)
        return false;
    removed_ = c->inserts[static_cast<size_t>(index_)];
    c->inserts.erase(c->inserts.begin() + index_);
    return true;
}
bool RemoveInsert::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int at = std::clamp(index_, 0, static_cast<int>(c->inserts.size()));
    c->inserts.insert(c->inserts.begin() + at, removed_);
    return true;
}

SetInstrument::SetInstrument(Id trackId, Insert instrument)
    : trackId_(trackId), instrument_(std::move(instrument))
{
}
bool SetInstrument::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    previous_ = t->instrument;
    if (!instrument_.type.empty())
    {
        // The slot keeps its id (a new one for an empty slot; the same on redo).
        if (previous_.id != invalidId && !previous_.type.empty())
            instrument_.id = previous_.id;
        else if (instrument_.id == invalidId)
            instrument_.id = p.allocateId();
    }
    // A different processor (or none) has other parameters: its lanes go.
    previousLanes_ = t->automation;
    if (!previous_.type.empty() && previous_.type != instrument_.type)
    {
        std::erase_if(
            t->automation,
            [this](const AutomationLane& lane)
            { return lane.target == AutomationTarget::insertParameter && lane.insert == previous_.id; });
    }
    t->instrument = instrument_.type.empty() ? Insert{} : instrument_;
    return true;
}
bool SetInstrument::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return true;
    t->instrument = previous_;
    t->automation = previousLanes_;
    return true;
}

MoveInsert::MoveInsert(Id channelId, Id insertId, int newIndex)
    : channelId_(channelId), insertId_(insertId), newIndex_(newIndex)
{
}
bool MoveInsert::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    previousIndex_ = c->insertIndex(insertId_);
    if (previousIndex_ < 0)
        return false;
    Insert moved = c->inserts[static_cast<size_t>(previousIndex_)];
    c->inserts.erase(c->inserts.begin() + previousIndex_);
    const int at = std::clamp(newIndex_, 0, static_cast<int>(c->inserts.size()));
    c->inserts.insert(c->inserts.begin() + at, moved);
    return true;
}
bool MoveInsert::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int current = c->insertIndex(insertId_);
    if (current < 0)
        return true;
    Insert moved = c->inserts[static_cast<size_t>(current)];
    c->inserts.erase(c->inserts.begin() + current);
    const int at = std::clamp(previousIndex_, 0, static_cast<int>(c->inserts.size()));
    c->inserts.insert(c->inserts.begin() + at, moved);
    return true;
}

SetInsertParameter::SetInsertParameter(Id channelId, Id insertId, int parameter, float value)
    : channelId_(channelId), insertId_(insertId), parameter_(parameter), value_(value)
{
}
bool SetInsertParameter::apply(Project& p)
{
    auto* i = findInsert(p, channelId_, insertId_);
    if (i == nullptr)
        return false;
    if (parameter_ < 0)
        return false;
    previousSize_ = i->params.size();
    if (static_cast<size_t>(parameter_) >= i->params.size())
        i->params.resize(static_cast<size_t>(parameter_) + 1, 0.0f);
    previous_ = i->params[static_cast<size_t>(parameter_)];
    i->params[static_cast<size_t>(parameter_)] = value_;
    return true;
}
bool SetInsertParameter::mergeWith(Command& next)
{
    // The same parameter again: the first previous value (and list size) stay.
    const auto* later = dynamic_cast<const SetInsertParameter*>(&next);
    if (later == nullptr || later->channelId_ != channelId_ || later->insertId_ != insertId_ ||
        later->parameter_ != parameter_)
        return false;
    value_ = later->value_;
    return true;
}
bool SetInsertParameter::revert(Project& p)
{
    auto* i = findInsert(p, channelId_, insertId_);
    if (i == nullptr)
        return false;
    if (static_cast<size_t>(parameter_) < i->params.size())
        i->params[static_cast<size_t>(parameter_)] = previous_;
    if (i->params.size() > previousSize_)
        i->params.resize(previousSize_);
    return true;
}

SetInsertSidechain::SetInsertSidechain(Id channelId, Id insertId, Id sourceId, bool postFader)
    : channelId_(channelId), insertId_(insertId), value_(sourceId), postFader_(postFader)
{
}
bool SetInsertSidechain::apply(Project& p)
{
    auto* i = findInsert(p, channelId_, insertId_);
    if (i == nullptr)
        return false;
    if (value_ != invalidId && p.findChannel(value_) == nullptr)
        return false;
    if (p.keyWouldCycle(channelId_, value_))
        return false;
    previous_ = i->sidechain;
    previousPostFader_ = i->sidechainPostFader;
    i->sidechain = value_;
    i->sidechainPostFader = value_ != invalidId && postFader_;
    return true;
}
bool SetInsertSidechain::revert(Project& p)
{
    auto* i = findInsert(p, channelId_, insertId_);
    if (i == nullptr)
        return false;
    i->sidechain = previous_;
    i->sidechainPostFader = previousPostFader_;
    return true;
}

MoveStripModule::MoveStripModule(Id channelId, Id moduleId, int newIndex)
    : channelId_(channelId), moduleId_(moduleId), newIndex_(newIndex)
{
}
bool MoveStripModule::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    previousIndex_ = c->stripIndex(moduleId_);
    if (previousIndex_ < 0)
        return false;
    Insert moved = c->strip[static_cast<size_t>(previousIndex_)];
    c->strip.erase(c->strip.begin() + previousIndex_);
    const int at = std::clamp(newIndex_, 0, static_cast<int>(c->strip.size()));
    c->strip.insert(c->strip.begin() + at, moved);
    return true;
}
bool MoveStripModule::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int current = c->stripIndex(moduleId_);
    if (current < 0)
        return true;
    Insert moved = c->strip[static_cast<size_t>(current)];
    c->strip.erase(c->strip.begin() + current);
    const int at = std::clamp(previousIndex_, 0, static_cast<int>(c->strip.size()));
    c->strip.insert(c->strip.begin() + at, moved);
    return true;
}

SetStripModuleType::SetStripModuleType(
    Id channelId, Id moduleId, std::string type, std::vector<float> params, std::string name)
    : channelId_(channelId), moduleId_(moduleId), type_(std::move(type)), params_(std::move(params)),
      name_(std::move(name))
{
}
bool SetStripModuleType::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int index = c->stripIndex(moduleId_);
    if (index < 0)
        return false;
    auto& module = c->strip[static_cast<size_t>(index)];
    previous_ = module;
    previousLanes_ = c->automation;
    if (module.type != type_)
        std::erase_if(
            c->automation,
            [this](const AutomationLane& lane)
            { return lane.target == AutomationTarget::insertParameter && lane.insert == moduleId_; });
    module.type = type_;
    module.params = params_;
    module.paramKeys.clear();
    module.state.clear();
    return true;
}
bool SetStripModuleType::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int index = c->stripIndex(moduleId_);
    if (index < 0)
        return true;
    c->strip[static_cast<size_t>(index)] = previous_;
    c->automation = previousLanes_;
    return true;
}

SetStripState::SetStripState(Id channelId, std::vector<Insert> modules, std::string name)
    : channelId_(channelId), modules_(std::move(modules)), name_(std::move(name))
{
}
bool SetStripState::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    previous_.clear();
    for (const auto& wanted : modules_)
        for (auto& module : c->strip)
            if (module.type == wanted.type)
            {
                previous_.push_back({module.id, module.bypass, module.params});
                module.bypass = wanted.bypass;
                module.params = wanted.params;
                break;
            }
    return true;
}
bool SetStripState::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    for (const auto& before : previous_)
        if (auto* module = c->findInsert(before.id))
        {
            module->bypass = before.bypass;
            module->params = before.params;
        }
    return true;
}

// ----- automation -------------------------------------------------------------------------

namespace
{
void sortPoints(std::vector<AutomationPoint>& points)
{
    std::stable_sort(points.begin(),
                     points.end(),
                     [](const AutomationPoint& a, const AutomationPoint& b)
                     { return a.position < b.position; });
}

} // namespace

AddAutomationLane::AddAutomationLane(Id channelId, AutomationLane lane)
    : channelId_(channelId), lane_(std::move(lane))
{
    sortPoints(lane_.points);
}
bool AddAutomationLane::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (c->findLane(lane_.target, lane_.insert, lane_.parameter) != nullptr)
        return false;
    if (lane_.id == invalidId)
        lane_.id = p.allocateId();
    p.nextId = std::max(p.nextId, lane_.id + 1);
    c->automation.push_back(lane_);
    previousOpen_ = c->automationOpen;
    c->automationOpen = true;
    return true;
}
bool AddAutomationLane::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    std::erase_if(c->automation, [this](const AutomationLane& l) { return l.id == lane_.id; });
    c->automationOpen = previousOpen_;
    return true;
}

RemoveAutomationLane::RemoveAutomationLane(Id channelId, Id laneId) : channelId_(channelId), laneId_(laneId)
{
}
bool RemoveAutomationLane::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    for (size_t i = 0; i < c->automation.size(); ++i)
        if (c->automation[i].id == laneId_)
        {
            removed_ = c->automation[i];
            index_ = static_cast<int>(i);
            c->automation.erase(c->automation.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    return false;
}
bool RemoveAutomationLane::revert(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    const int at = std::clamp(index_, 0, static_cast<int>(c->automation.size()));
    c->automation.insert(c->automation.begin() + at, removed_);
    return true;
}

SetAutomationLaneTarget::SetAutomationLaneTarget(
    Id channelId, Id laneId, AutomationTarget target, Id insert, int parameter)
    : channelId_(channelId), laneId_(laneId), target_(target),
      insert_(target == AutomationTarget::insertParameter ? insert : invalidId),
      parameter_(target == AutomationTarget::insertParameter ? parameter : 0)
{
}
bool SetAutomationLaneTarget::apply(Project& p)
{
    auto* c = p.findChannel(channelId_);
    if (c == nullptr)
        return false;
    if (const auto* other = c->findLane(target_, insert_, parameter_);
        other != nullptr && other->id != laneId_)
        return false;
    auto* lane = c->findLane(laneId_);
    if (lane == nullptr)
        return false;
    previous_ = *lane;
    lane->target = target_;
    lane->insert = insert_;
    lane->parameter = parameter_;
    lane->parameterKey.clear();
    return true;
}
bool SetAutomationLaneTarget::revert(Project& p)
{
    auto* lane = findLane(p, channelId_, laneId_);
    if (lane == nullptr)
        return false;
    lane->target = previous_.target;
    lane->insert = previous_.insert;
    lane->parameter = previous_.parameter;
    lane->parameterKey = previous_.parameterKey;
    return true;
}

SetAutomationPoints::SetAutomationPoints(Id channelId,
                                         Id laneId,
                                         std::vector<AutomationPoint> points,
                                         std::string name)
    : SetField({channelId, laneId}, std::move(points)), name_(std::move(name))
{
    sortPoints(value_);
}

SetMediaPaths::SetMediaPaths(std::vector<std::pair<std::string, std::string>> renames)
    : renames_(std::move(renames))
{
}
bool SetMediaPaths::apply(Project& p)
{
    changed_.clear();
    const auto repoint = [this](Id track, AudioClip& c)
    {
        for (const auto& [from, to] : renames_)
            if (c.mediaPath == from)
            {
                changed_.push_back({track, c.id, c.mediaPath});
                c.mediaPath = to;
                break;
            }
    };
    for (auto& t : p.tracks)
    {
        for (auto& c : t.clips)
            repoint(t.id, c);
        for (auto& g : t.takeGroups)
            for (auto& take : g.takes)
                repoint(t.id, take.clip);
    }
    return true;
}
bool SetMediaPaths::revert(Project& p)
{
    for (const auto& change : changed_)
        if (auto* t = p.findTrack(change.track))
            if (auto* c = t->findClip(change.clip) != nullptr ? t->findClip(change.clip)
                                                              : t->findTakeClip(change.clip))
                c->mediaPath = change.previous;
    return true;
}

SetTrackMidiOutput::SetTrackMidiOutput(Id trackId, bool on, int channel)
    : trackId_(trackId), on_(on), channel_(std::clamp(channel, 1, 16))
{
}
bool SetTrackMidiOutput::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    previousOn_ = t->midiOut;
    previousChannel_ = t->midiOutChannel;
    t->midiOut = on_;
    t->midiOutChannel = channel_;
    return true;
}
bool SetTrackMidiOutput::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->midiOut = previousOn_;
    t->midiOutChannel = previousChannel_;
    return true;
}

SetTrackInput::SetTrackInput(Id trackId, int inputChannel, std::string inputBus)
    : trackId_(trackId), value_(inputBus.empty() ? inputChannel : -1), bus_(std::move(inputBus))
{
}
bool SetTrackInput::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    previous_ = t->inputChannel;
    previousBus_ = t->inputBus;
    t->inputChannel = value_;
    t->inputBus = bus_;
    return true;
}
bool SetTrackInput::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->inputChannel = previous_;
    t->inputBus = previousBus_;
    return true;
}

// ----- clips ------------------------------------------------------------------------------

AddClip::AddClip(Id trackId, AudioClip clip) : trackId_(trackId), clip_(std::move(clip)) {}

bool AddClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (clip_.id == invalidId)
        clip_.id = p.allocateId();
    t->clips.push_back(clip_);
    t->sortClips();
    return true;
}

bool AddClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    std::erase_if(t->clips, [this](const AudioClip& c) { return c.id == clip_.id; });
    return true;
}

RemoveClip::RemoveClip(Id trackId, Id clipId) : trackId_(trackId), clipId_(clipId) {}

bool RemoveClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    const auto* found = t->findClip(clipId_);
    if (found == nullptr)
        return false;
    removed_ = *found;
    std::erase_if(t->clips, [this](const AudioClip& c) { return c.id == clipId_; });
    return true;
}

bool RemoveClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->clips.push_back(removed_);
    t->sortClips();
    return true;
}

MoveClip::MoveClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart)
    : fromTrackId_(fromTrackId), clipId_(clipId), toTrackId_(toTrackId), newStart_(newStart)
{
}

bool MoveClip::apply(Project& p)
{
    auto* from = p.findTrack(fromTrackId_);
    auto* to = p.findTrack(toTrackId_);
    const auto* found = from != nullptr ? from->findClip(clipId_) : nullptr;
    if (found == nullptr || to == nullptr)
        return false;
    AudioClip clip = *found;
    previousStart_ = clip.start;
    clip.start = newStart_;
    std::erase_if(from->clips, [this](const AudioClip& c) { return c.id == clipId_; });
    to->clips.push_back(clip);
    to->sortClips();
    return true;
}

bool MoveClip::revert(Project& p)
{
    auto* to = p.findTrack(toTrackId_);
    auto* from = p.findTrack(fromTrackId_);
    const auto* found = to != nullptr ? to->findClip(clipId_) : nullptr;
    if (found == nullptr || from == nullptr)
        return false;
    AudioClip clip = *found;
    clip.start = previousStart_;
    std::erase_if(to->clips, [this](const AudioClip& c) { return c.id == clipId_; });
    from->clips.push_back(clip);
    from->sortClips();
    return true;
}

TrimClip::TrimClip(
    Id trackId, Id clipId, SamplePosition start, SamplePosition length, SamplePosition sourceOffset)
    : trackId_(trackId), clipId_(clipId), start_(start), length_(length), sourceOffset_(sourceOffset)
{
}

bool TrimClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findClip(clipId_);
    if (c == nullptr)
        return false;
    previousStart_ = c->start;
    previousLength_ = c->length;
    previousOffset_ = c->sourceOffset;
    c->start = start_;
    c->length = std::max<SamplePosition>(0, length_);
    c->sourceOffset = std::max<SamplePosition>(0, sourceOffset_);
    t->sortClips();
    return true;
}

bool TrimClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findClip(clipId_);
    if (c == nullptr)
        return false;
    c->start = previousStart_;
    c->length = previousLength_;
    c->sourceOffset = previousOffset_;
    t->sortClips();
    return true;
}

SetClipFades::SetClipFades(Id trackId,
                           Id clipId,
                           SamplePosition fadeIn,
                           SamplePosition fadeOut,
                           std::optional<float> fadeInCurve,
                           std::optional<float> fadeOutCurve)
    : trackId_(trackId), clipId_(clipId), fadeIn_(fadeIn), fadeOut_(fadeOut), inCurve_(fadeInCurve),
      outCurve_(fadeOutCurve)
{
}
bool SetClipFades::apply(Project& p)
{
    auto* c = findClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    previousIn_ = c->fadeIn;
    previousOut_ = c->fadeOut;
    previousInCurve_ = c->fadeInCurve;
    previousOutCurve_ = c->fadeOutCurve;
    c->fadeIn = std::clamp<SamplePosition>(fadeIn_, 0, c->length);
    c->fadeOut = std::clamp<SamplePosition>(fadeOut_, 0, c->length);
    if (inCurve_)
        c->fadeInCurve = std::clamp(*inCurve_, -1.0f, 1.0f);
    if (outCurve_)
        c->fadeOutCurve = std::clamp(*outCurve_, -1.0f, 1.0f);
    return true;
}
bool SetClipFades::revert(Project& p)
{
    auto* c = findClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    c->fadeIn = previousIn_;
    c->fadeOut = previousOut_;
    c->fadeInCurve = previousInCurve_;
    c->fadeOutCurve = previousOutCurve_;
    return true;
}

SplitClip::SplitClip(Id trackId, Id clipId, SamplePosition at) : trackId_(trackId), clipId_(clipId), at_(at)
{
}
bool SplitClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findClip(clipId_);
    if (c == nullptr)
        return false;
    if (at_ <= c->start || at_ >= c->end())
        return false;
    original_ = *c;
    if (right_.id == invalidId)
        right_.id = p.allocateId();
    right_.name = c->name;
    right_.mediaPath = c->mediaPath;
    right_.start = at_;
    right_.length = c->end() - at_;
    right_.sourceOffset = c->sourceAt(at_ - c->start);
    right_.stretch = c->stretch;
    right_.transpose = c->transpose;
    right_.followTempo = c->followTempo;
    right_.gain = c->gain;
    right_.fadeIn = 0;
    right_.fadeOut = std::min(c->fadeOut, right_.length);
    right_.fadeOutCurve = c->fadeOutCurve;
    right_.muted = c->muted;
    right_.vocal = c->vocal; // in source samples: both halves keep the notes they play
    c->length = at_ - c->start;
    c->fadeOut = 0;
    c->fadeIn = std::min(c->fadeIn, c->length);
    t->clips.push_back(right_);
    t->sortClips();
    return true;
}
bool SplitClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findClip(clipId_);
    if (found == nullptr)
        return false;
    std::erase_if(t->clips, [this](const AudioClip& c) { return c.id == right_.id; });
    *found = original_;
    t->sortClips();
    return true;
}

bool JoinClips::canJoin(const AudioClip& left, const AudioClip& right) noexcept
{
    const auto expected = left.sourceAt(left.length);
    return left.mediaPath == right.mediaPath && right.start == left.end() &&
           std::abs(right.sourceOffset - expected) <= (left.stretch == 1.0 ? 0 : 1) &&
           left.gain == right.gain && left.vocal == right.vocal && left.stretch == right.stretch &&
           left.transpose == right.transpose && left.muted == right.muted;
}
JoinClips::JoinClips(Id trackId, Id leftClipId, Id rightClipId)
    : trackId_(trackId), leftId_(leftClipId), rightId_(rightClipId)
{
}
bool JoinClips::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* left = t->findClip(leftId_);
    if (left == nullptr)
        return false;
    const auto* right = t->findClip(rightId_);
    if (right == nullptr)
        return false;
    if (!canJoin(*left, *right))
        return false;
    previousLeft_ = *left;
    removedRight_ = *right;
    left->length += right->length;
    left->fadeOut = right->fadeOut;
    left->fadeOutCurve = right->fadeOutCurve;
    std::erase_if(t->clips, [this](const AudioClip& c) { return c.id == rightId_; });
    return true;
}
bool JoinClips::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findClip(leftId_);
    if (found == nullptr)
        return false;
    *found = previousLeft_;
    t->clips.push_back(removedRight_);
    t->sortClips();
    return true;
}

DuplicateClip::DuplicateClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
bool DuplicateClip::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    const auto* c = t->findClip(clipId_);
    if (c == nullptr)
        return false;
    const Id keep = copy_.id;
    copy_ = *c;
    copy_.id = keep == invalidId ? p.allocateId() : keep;
    copy_.start = at_ >= 0 ? at_ : c->end();
    t->clips.push_back(copy_);
    t->sortClips();
    return true;
}
bool DuplicateClip::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    std::erase_if(t->clips, [this](const AudioClip& c) { return c.id == copy_.id; });
    return true;
}

CrossfadeClips::CrossfadeClips(Id trackId, Id leftClipId, Id rightClipId)
    : trackId_(trackId), leftId_(leftClipId), rightId_(rightClipId)
{
}
bool CrossfadeClips::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* left = t->findClip(leftId_);
    if (left == nullptr)
        return false;
    auto* right = t->findClip(rightId_);
    if (right == nullptr)
        return false;
    const auto overlap = std::min(left->end(), right->end()) - std::max(left->start, right->start);
    if (right->start <= left->start || overlap <= 0)
        return false;
    previousOut_ = left->fadeOut;
    previousIn_ = right->fadeIn;
    left->fadeOut = std::min(overlap, left->length);
    right->fadeIn = std::min(overlap, right->length);
    return true;
}
bool CrossfadeClips::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* left = t->findClip(leftId_);
    auto* right = t->findClip(rightId_);
    if (left == nullptr || right == nullptr)
        return false;
    left->fadeOut = previousOut_;
    right->fadeIn = previousIn_;
    return true;
}

ShiftClips::ShiftClips(Id trackId, SamplePosition from, SamplePosition delta)
    : trackId_(trackId), from_(from), delta_(delta)
{
}
bool ShiftClips::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    previous_.clear();
    for (auto& c : t->clips)
        if (c.start >= from_)
        {
            previous_.emplace_back(c.id, c.start);
            c.start = std::max<SamplePosition>(0, c.start + delta_);
        }
    t->sortClips();
    // A take group that starts at or after the position moves with its clips.
    shiftedGroups_.clear();
    for (auto& g : t->takeGroups)
        if (g.start() >= from_)
        {
            const auto delta = std::max(delta_, -g.start());
            shiftedGroups_.emplace_back(g.id, delta);
            for (auto& take : g.takes)
                take.clip.start += delta;
            for (auto& s : g.comp)
            {
                s.start += delta;
                s.end += delta;
            }
        }
    return true;
}
bool ShiftClips::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    for (const auto& [id, start] : previous_)
        if (auto* c = t->findClip(id))
            c->start = start;
    t->sortClips();
    for (const auto& [id, delta] : shiftedGroups_)
        if (auto* g = t->findTakeGroup(id))
        {
            for (auto& take : g->takes)
                take.clip.start -= delta;
            for (auto& s : g->comp)
            {
                s.start -= delta;
                s.end -= delta;
            }
        }
    return true;
}

SetClipMute::SetClipMute(Id trackId, Id clipId, bool midi, bool muted)
    : trackId_(trackId), clipId_(clipId), midi_(midi), value_(muted)
{
}
bool* SetClipMute::flag(Project& p) const
{
    if (midi_)
    {
        auto* c = findMidiClip(p, trackId_, clipId_);
        return c != nullptr ? &c->muted : nullptr;
    }
    auto* c = findClip(p, trackId_, clipId_);
    return c != nullptr ? &c->muted : nullptr;
}
bool SetClipMute::apply(Project& p)
{
    auto* muted = flag(p);
    if (muted == nullptr)
        return false;
    previous_ = *muted;
    *muted = value_;
    return true;
}
bool SetClipMute::revert(Project& p)
{
    auto* muted = flag(p);
    if (muted == nullptr)
        return false;
    *muted = previous_;
    return true;
}

// ----- vocal pitch editing ----------------------------------------------------------------

SetVocalEdit::SetVocalEdit(Id trackId, Id clipId, VocalEdit edit, std::string name)
    : trackId_(trackId), clipId_(clipId), value_(std::move(edit)), name_(std::move(name))
{
    std::sort(value_.notes.begin(),
              value_.notes.end(),
              [](const VocalNote& a, const VocalNote& b) { return a.start < b.start; });
    std::sort(value_.segments.begin(),
              value_.segments.end(),
              [](const VocalSegment& a, const VocalSegment& b) { return a.start < b.start; });
}
bool SetVocalEdit::apply(Project& p)
{
    auto* c = findClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    previous_ = c->vocal;
    for (auto& n : value_.notes)
        if (n.id == invalidId)
            n.id = p.allocateId();
    for (auto& s : value_.segments)
        if (s.id == invalidId)
            s.id = p.allocateId();
    c->vocal = value_;
    return true;
}
bool SetVocalEdit::revert(Project& p)
{
    auto* c = findClip(p, trackId_, clipId_);
    if (c == nullptr)
        return false;
    c->vocal = previous_;
    return true;
}

// ----- project ----------------------------------------------------------------------------

SetTempo::SetTempo(double bpm) : value_(bpm) {}

bool SetTempo::apply(Project& p)
{
    previous_ = p.tempoBpm;
    positions_.capture(p);
    const auto before = tempoMapOf(p);
    p.tempoBpm = std::clamp(value_, 1.0, 999.0);
    remapMusicalTime(p, before, tempoMapOf(p));
    return true;
}
bool SetTempo::revert(Project& p)
{
    p.tempoBpm = previous_;
    positions_.restore(p);
    return true;
}

AddFolderTrack::AddFolderTrack(std::string name, std::vector<Id> members, int index)
    : members_(std::move(members)), index_(index)
{
    folder_.name = std::move(name);
    folder_.kind = TrackKind::folder;
    folder_.inputChannel = -1;
}
bool AddFolderTrack::apply(Project& p)
{
    allocateIds(p, folder_);
    previousOrder_ = p.tracks;
    // The folder takes the place of its first member (or `index`), its members follow.
    int at =
        index_ < 0 ? static_cast<int>(p.tracks.size()) : std::min(index_, static_cast<int>(p.tracks.size()));
    for (const Id id : members_)
        if (const int i = p.trackIndex(id); i >= 0)
            at = std::min(at, i);
    std::vector<Track> moved;
    for (const auto& t : p.tracks)
        if (std::find(members_.begin(), members_.end(), t.id) != members_.end() && t.id != folder_.id)
            moved.push_back(t);
    const auto before = static_cast<int>(std::count_if(
        p.tracks.begin(),
        p.tracks.begin() + at,
        [&](const Track& t) { return std::find(members_.begin(), members_.end(), t.id) != members_.end(); }));
    std::erase_if(p.tracks,
                  [&](const Track& t)
                  { return std::find(members_.begin(), members_.end(), t.id) != members_.end(); });
    at -= before;
    const Id outer = moved.empty() ? invalidId : moved.front().folder; // nest where the first member was
    folder_.folder = outer;
    for (auto& t : moved)
        t.folder = folder_.id;
    p.tracks.insert(p.tracks.begin() + at, folder_);
    p.tracks.insert(p.tracks.begin() + at + 1, moved.begin(), moved.end());
    return true;
}
bool AddFolderTrack::revert(Project& p)
{
    p.tracks = previousOrder_;
    return true;
}

SetTrackFolder::SetTrackFolder(Id trackId, Id folderId) : trackId_(trackId), folderId_(folderId) {}
bool SetTrackFolder::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (folderId_ != invalidId)
    {
        const auto* f = p.findTrack(folderId_);
        if (f == nullptr)
            return false;
        if (!f->isFolder() || p.insideFolder(folderId_, trackId_))
            return false;
    }
    previous_ = t->folder;
    t->folder = folderId_;
    return true;
}
bool SetTrackFolder::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->folder = previous_;
    return true;
}

SetTrackFrozen::SetTrackFrozen(Id trackId, std::string media) : trackId_(trackId), media_(std::move(media)) {}
bool SetTrackFrozen::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (!t->hasAudio() && !media_.empty())
        return false;
    previous_ = t->frozenMedia;
    t->frozenMedia = media_;
    return true;
}
bool SetTrackFrozen::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    t->frozenMedia = previous_;
    return true;
}

TempoState TempoState::of(const Project& p)
{
    return {p.tempoBpm, p.timeSignature, p.tempoChanges, p.meterChanges};
}

void MusicalPositions::capture(const Project& p)
{
    midi_.clear();
    audio_.clear();
    trackLanes_.clear();
    busLanes_.clear();
    for (const auto& t : p.tracks)
    {
        midi_.push_back(t.midiClips);
        audio_.push_back(t.clips);
        trackLanes_.push_back(t.automation);
    }
    for (const auto& b : p.buses)
        busLanes_.push_back(b.automation);
    markers_ = p.markers;
}

std::size_t MusicalPositions::footprint() const noexcept
{
    return sizeof(*this) + heapBytes(midi_) + heapBytes(audio_) + heapBytes(trackLanes_) +
           heapBytes(busLanes_) + heapBytes(markers_);
}

void MusicalPositions::restore(Project& p) const
{
    for (size_t i = 0; i < p.tracks.size() && i < midi_.size(); ++i)
    {
        p.tracks[i].midiClips = midi_[i];
        p.tracks[i].clips = audio_[i];
        p.tracks[i].automation = trackLanes_[i];
    }
    for (size_t i = 0; i < p.buses.size() && i < busLanes_.size(); ++i)
        p.buses[i].automation = busLanes_[i];
    p.markers = markers_;
}

SetTempoMap::SetTempoMap(TempoState state, std::string name)
    : value_(std::move(state)), name_(std::move(name))
{
}
bool SetTempoMap::apply(Project& p)
{
    previous_ = TempoState::of(p);
    positions_.capture(p);
    const auto before = tempoMapOf(p);
    p.tempoBpm = std::clamp(value_.bpm, 1.0, 999.0);
    p.timeSignature = {std::clamp(value_.signature.numerator, 1, 32),
                       std::clamp(value_.signature.denominator, 1, 32)};
    p.tempoChanges = value_.tempoChanges;
    std::erase_if(p.tempoChanges, [](const TempoChange& t) { return !(t.quarter > 0.0); });
    std::stable_sort(p.tempoChanges.begin(),
                     p.tempoChanges.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.quarter < b.quarter; });
    for (auto& t : p.tempoChanges)
        t.bpm = std::clamp(t.bpm, 1.0, 999.0);
    p.meterChanges = value_.meterChanges;
    std::erase_if(p.meterChanges, [](const MeterChange& m) { return m.bar <= 0; });
    std::stable_sort(p.meterChanges.begin(),
                     p.meterChanges.end(),
                     [](const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
    remapMusicalTime(p, before, tempoMapOf(p));
    return true;
}
bool SetTempoMap::revert(Project& p)
{
    p.tempoBpm = previous_.bpm;
    p.timeSignature = previous_.signature;
    p.tempoChanges = previous_.tempoChanges;
    p.meterChanges = previous_.meterChanges;
    positions_.restore(p);
    return true;
}

SetClipStretch::SetClipStretch(
    Id trackId, Id clipId, double stretch, double transpose, SamplePosition length, bool followTempo)
    : trackId_(trackId), clipId_(clipId), stretch_(std::clamp(stretch, 0.05, 20.0)),
      transpose_(std::clamp(transpose, -48.0, 48.0)), length_(length), followTempo_(followTempo)
{
}
bool SetClipStretch::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* c = t->findClip(clipId_);
    if (c == nullptr)
        return false;
    previous_ = *c;
    c->stretch = stretch_;
    c->transpose = transpose_;
    c->followTempo = followTempo_;
    c->length = std::max<SamplePosition>(1, length_);
    c->fadeIn = std::min(c->fadeIn, c->length);
    c->fadeOut = std::min(c->fadeOut, c->length);
    return true;
}
bool SetClipStretch::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    auto* found = t->findClip(clipId_);
    if (found == nullptr)
        return false;
    *found = previous_;
    return true;
}

} // namespace northlight::core
