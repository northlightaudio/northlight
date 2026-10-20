#include "core/Command.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace aural::core
{

// ----- undo stack -------------------------------------------------------------------------

class UndoStack::Group final : public Command
{
public:
    explicit Group(std::string name) : name_(std::move(name)) {}
    std::string name() const override { return name_; }
    void apply(Project& p) override
    {
        for (auto& c : commands_)
            c->apply(p);
    }
    void revert(Project& p) override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            (*it)->revert(p);
    }
    void add(std::unique_ptr<Command> c) { commands_.push_back(std::move(c)); }
    bool empty() const noexcept { return commands_.empty(); }
    void touchedParameters(std::vector<ParameterTouch>& out) const override
    {
        for (const auto& c : commands_)
            c->touchedParameters(out);
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

UndoStack::UndoStack() = default;
UndoStack::~UndoStack() = default;

void UndoStack::execute(Project& project, std::unique_ptr<Command> command)
{
    command->apply(project);
    if (openGroup_ != nullptr)
    {
        openGroup_->add(std::move(command));
        return;
    }
    undo_.push_back(std::move(command));
    redo_.clear();
}

bool UndoStack::undo(Project& project)
{
    if (undo_.empty())
        return false;
    auto command = std::move(undo_.back());
    undo_.pop_back();
    command->revert(project);
    redo_.push_back(std::move(command));
    return true;
}

bool UndoStack::redo(Project& project)
{
    if (redo_.empty())
        return false;
    auto command = std::move(redo_.back());
    redo_.pop_back();
    command->apply(project);
    undo_.push_back(std::move(command));
    return true;
}

std::string UndoStack::undoName() const
{
    return undo_.empty() ? std::string{} : undo_.back()->name();
}

std::string UndoStack::redoName() const
{
    return redo_.empty() ? std::string{} : redo_.back()->name();
}

void UndoStack::clear()
{
    undo_.clear();
    redo_.clear();
    openGroup_.reset();
}

void UndoStack::beginGroup(std::string name)
{
    if (openGroup_ == nullptr)
        openGroup_ = std::make_unique<Group>(std::move(name));
}

void UndoStack::endGroup()
{
    if (openGroup_ == nullptr)
        return;
    auto group = std::move(openGroup_);
    if (!group->empty())
    {
        undo_.push_back(std::move(group));
        redo_.clear();
    }
}

// ----- helpers ----------------------------------------------------------------------------

namespace
{
Track& requireTrack(Project& p, Id id)
{
    auto* t = p.findTrack(id);
    if (t == nullptr)
        throw std::logic_error("command references a track that does not exist");
    return *t;
}

Channel& requireChannel(Project& p, Id id)
{
    auto* c = p.findChannel(id);
    if (c == nullptr)
        throw std::logic_error("command references a channel that does not exist");
    return *c;
}

Bus& requireBus(Project& p, Id id)
{
    auto* b = p.findBus(id);
    if (b == nullptr)
        throw std::logic_error("command references a bus that does not exist");
    return *b;
}

AudioClip& requireClip(Track& t, Id id)
{
    auto* c = t.findClip(id);
    if (c == nullptr)
        throw std::logic_error("command references a clip that does not exist");
    return *c;
}
} // namespace

// ----- tracks -----------------------------------------------------------------------------

AddTrack::AddTrack(Track track, int index) : track_(std::move(track)), index_(index) {}

void AddTrack::setDefaultStrip(const std::vector<Insert>& modules)
{
    if (track_.strip.empty())
        track_.strip = modules;
}

void AddTrack::apply(Project& p)
{
    if (track_.id == invalidId)
        track_.id = p.allocateId();
    p.nextId = std::max(p.nextId, track_.id + 1); // a caller-chosen id is never handed out again
    for (auto& c : track_.clips)
        if (c.id == invalidId)
            c.id = p.allocateId();
    // Ids allocated here stay in the command, so a redo brings back the same
    // ones (later commands in the history refer to them).
    for (auto& i : track_.inserts)
        if (i.id == invalidId)
            i.id = p.allocateId();
    for (auto& m : track_.strip)
        if (m.id == invalidId)
            m.id = p.allocateId();
    const int at =
        index_ < 0 || index_ > static_cast<int>(p.tracks.size()) ? static_cast<int>(p.tracks.size()) : index_;
    index_ = at;
    p.tracks.insert(p.tracks.begin() + at, track_);
}

void AddTrack::revert(Project& p)
{
    const int i = p.trackIndex(track_.id);
    if (i >= 0)
        p.tracks.erase(p.tracks.begin() + i);
}

RemoveTrack::RemoveTrack(Id trackId) : trackId_(trackId) {}

void RemoveTrack::apply(Project& p)
{
    index_ = p.trackIndex(trackId_);
    if (index_ < 0)
        throw std::logic_error("command references a track that does not exist");
    removed_ = p.tracks[static_cast<size_t>(index_)];
    p.tracks.erase(p.tracks.begin() + index_);
}

void RemoveTrack::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.tracks.size()));
    p.tracks.insert(p.tracks.begin() + at, removed_);
}

MoveTrack::MoveTrack(Id trackId, int newIndex) : trackId_(trackId), newIndex_(newIndex) {}

void MoveTrack::apply(Project& p)
{
    oldIndex_ = p.trackIndex(trackId_);
    if (oldIndex_ < 0)
        throw std::logic_error("command references a track that does not exist");
    newIndex_ = std::clamp(newIndex_, 0, static_cast<int>(p.tracks.size()) - 1);
    Track moved = std::move(p.tracks[static_cast<size_t>(oldIndex_)]);
    p.tracks.erase(p.tracks.begin() + oldIndex_);
    p.tracks.insert(p.tracks.begin() + newIndex_, std::move(moved));
}

void MoveTrack::revert(Project& p)
{
    Track moved = std::move(p.tracks[static_cast<size_t>(newIndex_)]);
    p.tracks.erase(p.tracks.begin() + newIndex_);
    p.tracks.insert(p.tracks.begin() + oldIndex_, std::move(moved));
}

AddBus::AddBus(Bus bus, int index) : bus_(std::move(bus)), index_(index) {}

void AddBus::setDefaultStrip(const std::vector<Insert>& modules)
{
    if (bus_.strip.empty())
        bus_.strip = modules;
}

void AddBus::apply(Project& p)
{
    if (bus_.id == invalidId)
        bus_.id = p.allocateId();
    p.nextId = std::max(p.nextId, bus_.id + 1);
    for (auto& i : bus_.inserts)
        if (i.id == invalidId)
            i.id = p.allocateId();
    for (auto& m : bus_.strip)
        if (m.id == invalidId)
            m.id = p.allocateId();
    const int at =
        index_ < 0 || index_ > static_cast<int>(p.buses.size()) ? static_cast<int>(p.buses.size()) : index_;
    index_ = at;
    p.buses.insert(p.buses.begin() + at, bus_);
}

void AddBus::revert(Project& p)
{
    const int i = p.busIndex(bus_.id);
    if (i >= 0)
        p.buses.erase(p.buses.begin() + i);
}

RemoveBus::RemoveBus(Id busId) : busId_(busId) {}

void RemoveBus::apply(Project& p)
{
    index_ = p.busIndex(busId_);
    if (index_ < 0)
        throw std::logic_error("command references a bus that does not exist");
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
}

void RemoveBus::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.buses.size()));
    p.buses.insert(p.buses.begin() + at, removed_);
    for (const auto& [channelId, output] : detachedOutputs_)
        if (auto* c = p.findChannel(channelId))
            c->outputBus = output;
    // In recorded order the indices are ascending per channel, so inserting at
    // each one restores the original order.
    for (const auto& d : detachedSends_)
        if (auto* c = p.findChannel(d.channel))
            c->sends.insert(
                c->sends.begin() + static_cast<std::ptrdiff_t>(std::min(d.index, c->sends.size())), d.send);
}

// ----- MIDI ----------------------------------------------------------------------------------

namespace
{
MidiClip& requireMidiClip(Track& t, Id id)
{
    auto* c = t.findMidiClip(id);
    if (c == nullptr)
        throw std::logic_error("command references a MIDI clip that does not exist");
    return *c;
}

MidiNote& requireNote(MidiClip& c, Id id)
{
    auto* n = c.findNote(id);
    if (n == nullptr)
        throw std::logic_error("command references a note that does not exist");
    return *n;
}

void clampNote(MidiNote& n)
{
    n.note = std::clamp(n.note, 0, 127);
    n.velocity = std::clamp(n.velocity, 1, 127);
    n.start = std::max<SamplePosition>(0, n.start);
    n.length = std::max<SamplePosition>(1, n.length);
}
} // namespace

SetTrackMidiInput::SetTrackMidiInput(Id trackId, std::string deviceName)
    : trackId_(trackId), value_(std::move(deviceName))
{
}
void SetTrackMidiInput::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.midiInput;
    t.midiInput = value_;
}
void SetTrackMidiInput::revert(Project& p)
{
    requireTrack(p, trackId_).midiInput = previous_;
}

AddMidiClip::AddMidiClip(Id trackId, MidiClip clip) : trackId_(trackId), clip_(std::move(clip)) {}
void AddMidiClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    if (clip_.id == invalidId)
        clip_.id = p.allocateId();
    for (auto& n : clip_.notes)
    {
        if (n.id == invalidId)
            n.id = p.allocateId();
        clampNote(n);
    }
    clip_.start = std::max<SamplePosition>(0, clip_.start);
    clip_.length = std::max<SamplePosition>(0, clip_.length);
    clip_.sortNotes();
    t.midiClips.push_back(clip_);
    t.sortMidiClips();
}
void AddMidiClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.midiClips, [this](const MidiClip& c) { return c.id == clip_.id; });
}

RemoveMidiClip::RemoveMidiClip(Id trackId, Id clipId) : trackId_(trackId), clipId_(clipId) {}
void RemoveMidiClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    removed_ = requireMidiClip(t, clipId_);
    std::erase_if(t.midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
}
void RemoveMidiClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    t.midiClips.push_back(removed_);
    t.sortMidiClips();
}

MoveMidiClip::MoveMidiClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart)
    : fromTrackId_(fromTrackId), clipId_(clipId), toTrackId_(toTrackId), newStart_(newStart)
{
}
void MoveMidiClip::apply(Project& p)
{
    auto& from = requireTrack(p, fromTrackId_);
    auto& to = requireTrack(p, toTrackId_);
    if (!to.isMidi())
        throw std::logic_error("a MIDI clip can only live on a MIDI track");
    MidiClip clip = requireMidiClip(from, clipId_);
    previousStart_ = clip.start;
    clip.start = std::max<SamplePosition>(0, newStart_);
    std::erase_if(from.midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
    to.midiClips.push_back(clip);
    to.sortMidiClips();
}
void MoveMidiClip::revert(Project& p)
{
    auto& to = requireTrack(p, toTrackId_);
    MidiClip clip = requireMidiClip(to, clipId_);
    clip.start = previousStart_;
    std::erase_if(to.midiClips, [this](const MidiClip& c) { return c.id == clipId_; });
    auto& from = requireTrack(p, fromTrackId_);
    from.midiClips.push_back(clip);
    from.sortMidiClips();
}

TrimMidiClip::TrimMidiClip(Id trackId, Id clipId, SamplePosition start, SamplePosition length)
    : trackId_(trackId), clipId_(clipId), start_(start), length_(length)
{
}
void TrimMidiClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& c = requireMidiClip(t, clipId_);
    previous_ = c;
    const auto newStart = std::max<SamplePosition>(0, start_);
    const auto shift = c.start - newStart; // notes move the other way to stay put
    for (auto& n : c.notes)
        n.start += shift;
    c.start = newStart;
    c.length = std::max<SamplePosition>(0, length_);
    t.sortMidiClips();
}
void TrimMidiClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    requireMidiClip(t, clipId_) = previous_;
    t.sortMidiClips();
}

SplitMidiClip::SplitMidiClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
void SplitMidiClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& c = requireMidiClip(t, clipId_);
    if (at_ <= c.start || at_ >= c.end())
        throw std::logic_error("split point is not inside the clip");
    original_ = c;
    const auto offset = at_ - c.start; // the split, clip-relative
    if (right_.id == invalidId)
        right_.id = p.allocateId();
    right_.name = c.name;
    right_.start = at_;
    right_.length = c.end() - at_;
    right_.notes.clear();
    std::vector<MidiNote> left;
    for (const auto& n : c.notes)
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
    c.notes = std::move(left);
    c.length = offset;
    t.midiClips.push_back(right_);
    t.sortMidiClips();
}
void SplitMidiClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.midiClips, [this](const MidiClip& c) { return c.id == right_.id; });
    requireMidiClip(t, clipId_) = original_;
    t.sortMidiClips();
}

DuplicateMidiClip::DuplicateMidiClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
void DuplicateMidiClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    const auto& c = requireMidiClip(t, clipId_);
    const bool fresh = copy_.id == invalidId;
    const auto keep = copy_;
    copy_ = c;
    copy_.id = fresh ? p.allocateId() : keep.id;
    for (size_t i = 0; i < copy_.notes.size(); ++i)
        copy_.notes[i].id = fresh || i >= keep.notes.size() ? p.allocateId() : keep.notes[i].id;
    copy_.start = at_ >= 0 ? at_ : c.end();
    t.midiClips.push_back(copy_);
    t.sortMidiClips();
}
void DuplicateMidiClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.midiClips, [this](const MidiClip& c) { return c.id == copy_.id; });
}

AddNote::AddNote(Id trackId, Id clipId, MidiNote note) : trackId_(trackId), clipId_(clipId), note_(note) {}
void AddNote::apply(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    if (note_.id == invalidId)
        note_.id = p.allocateId();
    clampNote(note_);
    c.notes.push_back(note_);
    c.sortNotes();
}
void AddNote::revert(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    std::erase_if(c.notes, [this](const MidiNote& n) { return n.id == note_.id; });
}

RemoveNote::RemoveNote(Id trackId, Id clipId, Id noteId) : trackId_(trackId), clipId_(clipId), noteId_(noteId)
{
}
void RemoveNote::apply(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    removed_ = requireNote(c, noteId_);
    std::erase_if(c.notes, [this](const MidiNote& n) { return n.id == noteId_; });
}
void RemoveNote::revert(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    c.notes.push_back(removed_);
    c.sortNotes();
}

SetNote::SetNote(Id trackId, Id clipId, MidiNote value) : trackId_(trackId), clipId_(clipId), value_(value) {}
void SetNote::apply(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    auto& n = requireNote(c, value_.id);
    previous_ = n;
    n = value_;
    clampNote(n);
    c.sortNotes();
}
void SetNote::revert(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    requireNote(c, value_.id) = previous_;
    c.sortNotes();
}

QuantiseNotes::QuantiseNotes(
    Id trackId, Id clipId, std::vector<Id> noteIds, SamplePosition grid, float strength)
    : trackId_(trackId), clipId_(clipId), noteIds_(std::move(noteIds)), grid_(grid), strength_(strength)
{
}
void QuantiseNotes::apply(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    previous_.clear();
    const float strength = std::clamp(strength_, 0.0f, 1.0f);
    for (auto& n : c.notes)
    {
        if (!noteIds_.empty() && std::find(noteIds_.begin(), noteIds_.end(), n.id) == noteIds_.end())
            continue;
        previous_.emplace_back(n.id, n.start);
        const auto absolute = c.start + n.start;
        const auto target = snapToGrid(absolute, grid_);
        const auto moved =
            absolute + static_cast<SamplePosition>(std::llround((target - absolute) * strength));
        n.start = std::max<SamplePosition>(0, moved - c.start);
    }
    c.sortNotes();
}
void QuantiseNotes::revert(Project& p)
{
    auto& c = requireMidiClip(requireTrack(p, trackId_), clipId_);
    for (const auto& [id, start] : previous_)
        if (auto* n = c.findNote(id))
            n->start = start;
    c.sortNotes();
}

// ----- markers -------------------------------------------------------------------------------

namespace
{
Marker& requireMarker(Project& p, Id id)
{
    auto* m = p.findMarker(id);
    if (m == nullptr)
        throw std::logic_error("command references a marker that does not exist");
    return *m;
}
} // namespace

AddMarker::AddMarker(Marker marker) : marker_(std::move(marker)) {}
void AddMarker::apply(Project& p)
{
    if (marker_.id == invalidId)
        marker_.id = p.allocateId();
    marker_.position = std::max<SamplePosition>(0, marker_.position);
    marker_.length = std::max<SamplePosition>(0, marker_.length);
    p.markers.push_back(marker_);
    p.sortMarkers();
}
void AddMarker::revert(Project& p)
{
    std::erase_if(p.markers, [this](const Marker& m) { return m.id == marker_.id; });
}

RemoveMarker::RemoveMarker(Id markerId) : markerId_(markerId) {}
void RemoveMarker::apply(Project& p)
{
    removed_ = requireMarker(p, markerId_);
    std::erase_if(p.markers, [this](const Marker& m) { return m.id == markerId_; });
}
void RemoveMarker::revert(Project& p)
{
    p.markers.push_back(removed_);
    p.sortMarkers();
}

SetMarker::SetMarker(Id markerId, SamplePosition position, SamplePosition length, std::string name)
    : markerId_(markerId)
{
    value_.id = markerId;
    value_.position = std::max<SamplePosition>(0, position);
    value_.length = std::max<SamplePosition>(0, length);
    value_.name = std::move(name);
}
void SetMarker::apply(Project& p)
{
    auto& m = requireMarker(p, markerId_);
    previous_ = m;
    m = value_;
    p.sortMarkers();
}
void SetMarker::revert(Project& p)
{
    requireMarker(p, markerId_) = previous_;
    p.sortMarkers();
}

// ----- fader groups ----------------------------------------------------------------------------

namespace
{
Group& requireGroup(Project& p, Id id)
{
    auto* g = p.findGroup(id);
    if (g == nullptr)
        throw std::logic_error("command references a group that does not exist");
    return *g;
}
} // namespace

AddGroup::AddGroup(Group group, std::vector<Id> members)
    : group_(std::move(group)), members_(std::move(members))
{
}

void AddGroup::apply(Project& p)
{
    for (const auto id : members_)
        requireChannel(p, id);
    if (group_.id == invalidId)
        group_.id = p.allocateId();
    p.groups.push_back(group_);
    previousGroups_.clear();
    for (const auto id : members_)
    {
        auto& c = requireChannel(p, id);
        previousGroups_.emplace_back(id, c.group);
        c.group = group_.id;
    }
}

void AddGroup::revert(Project& p)
{
    for (const auto& [id, previous] : previousGroups_)
        if (auto* c = p.findChannel(id))
            c->group = previous;
    const int i = p.groupIndex(group_.id);
    if (i >= 0)
        p.groups.erase(p.groups.begin() + i);
}

RemoveGroup::RemoveGroup(Id groupId) : groupId_(groupId) {}

void RemoveGroup::apply(Project& p)
{
    index_ = p.groupIndex(groupId_);
    if (index_ < 0)
        throw std::logic_error("command references a group that does not exist");
    removed_ = p.groups[static_cast<size_t>(index_)];
    members_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        members_.push_back(c->id);
        c->group = invalidId;
    }
    p.groups.erase(p.groups.begin() + index_);
}

void RemoveGroup::revert(Project& p)
{
    const int at = std::clamp(index_, 0, static_cast<int>(p.groups.size()));
    p.groups.insert(p.groups.begin() + at, removed_);
    for (const auto id : members_)
        if (auto* c = p.findChannel(id))
            c->group = groupId_;
}

SetChannelGroup::SetChannelGroup(Id channelId, Id groupId) : channelId_(channelId), value_(groupId) {}

void SetChannelGroup::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (value_ != invalidId)
        requireGroup(p, value_);
    previous_ = c.group;
    c.group = value_;
}

void SetChannelGroup::revert(Project& p)
{
    requireChannel(p, channelId_).group = previous_;
}

SetGroupGain::SetGroupGain(Id groupId, float ratio) : groupId_(groupId), ratio_(ratio) {}

void SetGroupGain::apply(Project& p)
{
    requireGroup(p, groupId_);
    previous_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        previous_.emplace_back(c->id, c->gain);
        c->gain = std::clamp(c->gain * ratio_, 0.0f, maxChannelGain);
    }
}

void SetGroupGain::revert(Project& p)
{
    for (const auto& [id, gain] : previous_)
        if (auto* c = p.findChannel(id))
            c->gain = gain;
}

SetGroupMute::SetGroupMute(Id groupId, bool mute) : groupId_(groupId), value_(mute) {}

void SetGroupMute::apply(Project& p)
{
    requireGroup(p, groupId_);
    previous_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        previous_.emplace_back(c->id, c->mute);
        c->mute = value_;
    }
}

void SetGroupMute::revert(Project& p)
{
    for (const auto& [id, mute] : previous_)
        if (auto* c = p.findChannel(id))
            c->mute = mute;
}

SetGroupSolo::SetGroupSolo(Id groupId, bool solo) : groupId_(groupId), value_(solo) {}

void SetGroupSolo::apply(Project& p)
{
    requireGroup(p, groupId_);
    previous_.clear();
    for (auto* c : p.groupMembers(groupId_))
    {
        previous_.emplace_back(c->id, c->solo);
        c->solo = value_;
    }
}

void SetGroupSolo::revert(Project& p)
{
    for (const auto& [id, solo] : previous_)
        if (auto* c = p.findChannel(id))
            c->solo = solo;
}

SetOutputBus::SetOutputBus(Id channelId, Id busId) : channelId_(channelId), value_(busId) {}
void SetOutputBus::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (value_ != invalidId)
        requireBus(p, value_);
    if (p.wouldCycle(channelId_, value_))
        throw std::logic_error("routing would form a cycle");
    previous_ = c.outputBus;
    c.outputBus = value_;
}
void SetOutputBus::revert(Project& p)
{
    requireChannel(p, channelId_).outputBus = previous_;
}

SetSend::SetSend(Id channelId, Id busId, float level, bool preFader)
    : channelId_(channelId), value_{busId, level, preFader}
{
}
void SetSend::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    requireBus(p, value_.bus);
    if (p.wouldCycle(channelId_, value_.bus))
        throw std::logic_error("send would form a cycle");
    if (auto* existing = c.findSend(value_.bus))
    {
        existed_ = true;
        previous_ = *existing;
        *existing = value_;
    }
    else
    {
        existed_ = false;
        c.sends.push_back(value_);
    }
}
void SetSend::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (existed_)
    {
        if (auto* existing = c.findSend(value_.bus))
            *existing = previous_;
    }
    else
        std::erase_if(c.sends, [this](const Send& s) { return s.bus == value_.bus; });
}

RemoveSend::RemoveSend(Id channelId, Id busId) : channelId_(channelId), busId_(busId) {}
void RemoveSend::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    existed_ = false;
    if (auto* existing = c.findSend(busId_))
    {
        existed_ = true;
        previous_ = *existing;
        std::erase_if(c.sends, [this](const Send& s) { return s.bus == busId_; });
    }
}
void RemoveSend::revert(Project& p)
{
    if (existed_)
        requireChannel(p, channelId_).sends.push_back(previous_);
}

SetPhaseInvert::SetPhaseInvert(Id channelId, bool invert) : channelId_(channelId), value_(invert) {}
void SetPhaseInvert::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previous_ = c.phaseInvert;
    c.phaseInvert = value_;
}
void SetPhaseInvert::revert(Project& p)
{
    requireChannel(p, channelId_).phaseInvert = previous_;
}

SetColour::SetColour(Id channelId, std::uint32_t colour) : channelId_(channelId), value_(colour) {}
void SetColour::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previous_ = c.colour;
    c.colour = value_;
}
void SetColour::revert(Project& p)
{
    requireChannel(p, channelId_).colour = previous_;
}

SetMasterGain::SetMasterGain(float gain) : value_(gain) {}
void SetMasterGain::apply(Project& p)
{
    previous_ = p.master.gain;
    p.master.gain = value_;
}
void SetMasterGain::revert(Project& p)
{
    p.master.gain = previous_;
}

namespace
{
Insert& requireInsert(Channel& c, Id id)
{
    auto* i = c.findInsert(id);
    if (i == nullptr)
        throw std::logic_error("command references an insert that does not exist");
    return *i;
}
} // namespace

AddInsert::AddInsert(Id channelId, Insert insert, int index)
    : channelId_(channelId), insert_(std::move(insert)), index_(index)
{
}
void AddInsert::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (insert_.id == invalidId)
        insert_.id = p.allocateId();
    else
    {
        // A caller-supplied id must be unused: two inserts sharing one would share
        // one processor instance.
        for (const auto& t : p.tracks)
            if (t.findInsert(insert_.id) != nullptr)
                throw std::logic_error("insert id already in use");
        for (const auto& b : p.buses)
            if (b.findInsert(insert_.id) != nullptr)
                throw std::logic_error("insert id already in use");
    }
    const int at = index_ < 0 || index_ > static_cast<int>(c.inserts.size())
                       ? static_cast<int>(c.inserts.size())
                       : index_;
    index_ = at;
    c.inserts.insert(c.inserts.begin() + at, insert_);
}
void AddInsert::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    std::erase_if(c.inserts, [this](const Insert& i) { return i.id == insert_.id; });
}

RemoveInsert::RemoveInsert(Id channelId, Id insertId) : channelId_(channelId), insertId_(insertId) {}
void RemoveInsert::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    index_ = c.insertIndex(insertId_);
    if (index_ < 0)
        throw std::logic_error("command references an insert that does not exist");
    removed_ = c.inserts[static_cast<size_t>(index_)];
    c.inserts.erase(c.inserts.begin() + index_);
}
void RemoveInsert::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    const int at = std::clamp(index_, 0, static_cast<int>(c.inserts.size()));
    c.inserts.insert(c.inserts.begin() + at, removed_);
}

MoveInsert::MoveInsert(Id channelId, Id insertId, int newIndex)
    : channelId_(channelId), insertId_(insertId), newIndex_(newIndex)
{
}
void MoveInsert::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previousIndex_ = c.insertIndex(insertId_);
    if (previousIndex_ < 0)
        throw std::logic_error("command references an insert that does not exist");
    Insert moved = c.inserts[static_cast<size_t>(previousIndex_)];
    c.inserts.erase(c.inserts.begin() + previousIndex_);
    const int at = std::clamp(newIndex_, 0, static_cast<int>(c.inserts.size()));
    c.inserts.insert(c.inserts.begin() + at, moved);
}
void MoveInsert::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    const int current = c.insertIndex(insertId_);
    if (current < 0)
        return;
    Insert moved = c.inserts[static_cast<size_t>(current)];
    c.inserts.erase(c.inserts.begin() + current);
    const int at = std::clamp(previousIndex_, 0, static_cast<int>(c.inserts.size()));
    c.inserts.insert(c.inserts.begin() + at, moved);
}

SetInsertParameter::SetInsertParameter(Id channelId, Id insertId, int parameter, float value)
    : channelId_(channelId), insertId_(insertId), parameter_(parameter), value_(value)
{
}
void SetInsertParameter::apply(Project& p)
{
    auto& i = requireInsert(requireChannel(p, channelId_), insertId_);
    if (parameter_ < 0)
        throw std::logic_error("negative parameter index");
    previousSize_ = i.params.size();
    if (static_cast<size_t>(parameter_) >= i.params.size())
        i.params.resize(static_cast<size_t>(parameter_) + 1, 0.0f);
    previous_ = i.params[static_cast<size_t>(parameter_)];
    i.params[static_cast<size_t>(parameter_)] = value_;
}
void SetInsertParameter::revert(Project& p)
{
    auto& i = requireInsert(requireChannel(p, channelId_), insertId_);
    if (static_cast<size_t>(parameter_) < i.params.size())
        i.params[static_cast<size_t>(parameter_)] = previous_;
    if (i.params.size() > previousSize_)
        i.params.resize(previousSize_);
}

SetInsertMix::SetInsertMix(Id channelId, Id insertId, float mix)
    : channelId_(channelId), insertId_(insertId), value_(std::clamp(mix, 0.0f, 1.0f))
{
}
void SetInsertMix::apply(Project& p)
{
    auto& i = requireInsert(requireChannel(p, channelId_), insertId_);
    previous_ = i.mix;
    i.mix = value_;
}
void SetInsertMix::revert(Project& p)
{
    requireInsert(requireChannel(p, channelId_), insertId_).mix = previous_;
}

SetInsertSidechain::SetInsertSidechain(Id channelId, Id insertId, Id sourceId)
    : channelId_(channelId), insertId_(insertId), value_(sourceId)
{
}
void SetInsertSidechain::apply(Project& p)
{
    auto& i = requireInsert(requireChannel(p, channelId_), insertId_);
    if (value_ != invalidId && p.findChannel(value_) == nullptr)
        throw std::logic_error("sidechain source does not exist");
    if (p.keyWouldCycle(channelId_, value_))
        throw std::logic_error("sidechain source would form a feedback loop");
    previous_ = i.sidechain;
    i.sidechain = value_;
}
void SetInsertSidechain::revert(Project& p)
{
    requireInsert(requireChannel(p, channelId_), insertId_).sidechain = previous_;
}

SetInsertBypass::SetInsertBypass(Id channelId, Id insertId, bool bypass)
    : channelId_(channelId), insertId_(insertId), value_(bypass)
{
}
void SetInsertBypass::apply(Project& p)
{
    auto& i = requireInsert(requireChannel(p, channelId_), insertId_);
    previous_ = i.bypass;
    i.bypass = value_;
}
void SetInsertBypass::revert(Project& p)
{
    requireInsert(requireChannel(p, channelId_), insertId_).bypass = previous_;
}

MoveStripModule::MoveStripModule(Id channelId, Id moduleId, int newIndex)
    : channelId_(channelId), moduleId_(moduleId), newIndex_(newIndex)
{
}
void MoveStripModule::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previousIndex_ = c.stripIndex(moduleId_);
    if (previousIndex_ < 0)
        throw std::logic_error("command references a strip module that does not exist");
    Insert moved = c.strip[static_cast<size_t>(previousIndex_)];
    c.strip.erase(c.strip.begin() + previousIndex_);
    const int at = std::clamp(newIndex_, 0, static_cast<int>(c.strip.size()));
    c.strip.insert(c.strip.begin() + at, moved);
}
void MoveStripModule::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    const int current = c.stripIndex(moduleId_);
    if (current < 0)
        return;
    Insert moved = c.strip[static_cast<size_t>(current)];
    c.strip.erase(c.strip.begin() + current);
    const int at = std::clamp(previousIndex_, 0, static_cast<int>(c.strip.size()));
    c.strip.insert(c.strip.begin() + at, moved);
}

SetStripState::SetStripState(Id channelId, std::vector<Insert> modules, std::string name)
    : channelId_(channelId), modules_(std::move(modules)), name_(std::move(name))
{
}
void SetStripState::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previous_.clear();
    for (const auto& wanted : modules_)
        for (auto& module : c.strip)
            if (module.type == wanted.type)
            {
                previous_.push_back({module.id, module.bypass, module.params});
                module.bypass = wanted.bypass;
                module.params = wanted.params;
                break;
            }
}
void SetStripState::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    for (const auto& before : previous_)
        if (auto* module = c.findInsert(before.id))
        {
            module->bypass = before.bypass;
            module->params = before.params;
        }
}

SetStripPosition::SetStripPosition(Id channelId, bool preInserts) : channelId_(channelId), value_(preInserts)
{
}
void SetStripPosition::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previous_ = c.stripPreInserts;
    c.stripPreInserts = value_;
}
void SetStripPosition::revert(Project& p)
{
    requireChannel(p, channelId_).stripPreInserts = previous_;
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

AutomationLane& requireLane(Channel& c, Id laneId)
{
    auto* lane = c.findLane(laneId);
    if (lane == nullptr)
        throw std::logic_error("command references an automation lane that does not exist");
    return *lane;
}
} // namespace

AddAutomationLane::AddAutomationLane(Id channelId, AutomationLane lane)
    : channelId_(channelId), lane_(std::move(lane))
{
    sortPoints(lane_.points);
}
void AddAutomationLane::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (c.findLane(lane_.target, lane_.insert, lane_.parameter) != nullptr)
        throw std::logic_error("the channel already has a lane for that parameter");
    if (lane_.id == invalidId)
        lane_.id = p.allocateId();
    p.nextId = std::max(p.nextId, lane_.id + 1);
    c.automation.push_back(lane_);
}
void AddAutomationLane::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    std::erase_if(c.automation, [this](const AutomationLane& l) { return l.id == lane_.id; });
}

RemoveAutomationLane::RemoveAutomationLane(Id channelId, Id laneId) : channelId_(channelId), laneId_(laneId)
{
}
void RemoveAutomationLane::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    for (size_t i = 0; i < c.automation.size(); ++i)
        if (c.automation[i].id == laneId_)
        {
            removed_ = c.automation[i];
            index_ = static_cast<int>(i);
            c.automation.erase(c.automation.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    throw std::logic_error("command references an automation lane that does not exist");
}
void RemoveAutomationLane::revert(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    const int at = std::clamp(index_, 0, static_cast<int>(c.automation.size()));
    c.automation.insert(c.automation.begin() + at, removed_);
}

SetAutomationPoints::SetAutomationPoints(Id channelId,
                                         Id laneId,
                                         std::vector<AutomationPoint> points,
                                         std::string name)
    : channelId_(channelId), laneId_(laneId), points_(std::move(points)), name_(std::move(name))
{
    sortPoints(points_);
}
void SetAutomationPoints::apply(Project& p)
{
    auto& lane = requireLane(requireChannel(p, channelId_), laneId_);
    previous_ = lane.points;
    lane.points = points_;
}
void SetAutomationPoints::revert(Project& p)
{
    requireLane(requireChannel(p, channelId_), laneId_).points = previous_;
}

SetAutomationMode::SetAutomationMode(Id channelId, AutomationMode mode) : channelId_(channelId), value_(mode)
{
}
void SetAutomationMode::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    previous_ = c.automationMode;
    c.automationMode = value_;
}
void SetAutomationMode::revert(Project& p)
{
    requireChannel(p, channelId_).automationMode = previous_;
}

SetMediaPaths::SetMediaPaths(std::vector<std::pair<std::string, std::string>> renames)
    : renames_(std::move(renames))
{
}
void SetMediaPaths::apply(Project& p)
{
    changed_.clear();
    for (auto& t : p.tracks)
        for (auto& c : t.clips)
            for (const auto& [from, to] : renames_)
                if (c.mediaPath == from)
                {
                    changed_.push_back({t.id, c.id, c.mediaPath});
                    c.mediaPath = to;
                    break;
                }
}
void SetMediaPaths::revert(Project& p)
{
    for (const auto& change : changed_)
        if (auto* t = p.findTrack(change.track))
            if (auto* c = t->findClip(change.clip))
                c->mediaPath = change.previous;
}

SetTrackMidiOutput::SetTrackMidiOutput(Id trackId, bool on, int channel)
    : trackId_(trackId), on_(on), channel_(std::clamp(channel, 1, 16))
{
}
void SetTrackMidiOutput::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previousOn_ = t.midiOut;
    previousChannel_ = t.midiOutChannel;
    t.midiOut = on_;
    t.midiOutChannel = channel_;
}
void SetTrackMidiOutput::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    t.midiOut = previousOn_;
    t.midiOutChannel = previousChannel_;
}

SetTrackName::SetTrackName(Id trackId, std::string name) : trackId_(trackId), value_(std::move(name)) {}
void SetTrackName::apply(Project& p)
{
    auto& t = requireChannel(p, trackId_);
    previous_ = t.name;
    t.name = value_;
}
void SetTrackName::revert(Project& p)
{
    requireChannel(p, trackId_).name = previous_;
}

SetTrackGain::SetTrackGain(Id trackId, float gain) : trackId_(trackId), value_(gain) {}
void SetTrackGain::apply(Project& p)
{
    auto& t = requireChannel(p, trackId_);
    previous_ = t.gain;
    t.gain = value_;
}
void SetTrackGain::revert(Project& p)
{
    requireChannel(p, trackId_).gain = previous_;
}

SetTrackPan::SetTrackPan(Id trackId, float pan) : trackId_(trackId), value_(pan) {}
void SetTrackPan::apply(Project& p)
{
    auto& t = requireChannel(p, trackId_);
    previous_ = t.pan;
    t.pan = value_;
}
void SetTrackPan::revert(Project& p)
{
    requireChannel(p, trackId_).pan = previous_;
}

SetTrackMute::SetTrackMute(Id trackId, bool mute) : trackId_(trackId), value_(mute) {}
void SetTrackMute::apply(Project& p)
{
    auto& t = requireChannel(p, trackId_);
    previous_ = t.mute;
    t.mute = value_;
}
void SetTrackMute::revert(Project& p)
{
    requireChannel(p, trackId_).mute = previous_;
}

SetTrackSolo::SetTrackSolo(Id trackId, bool solo) : trackId_(trackId), value_(solo) {}
void SetTrackSolo::apply(Project& p)
{
    auto& t = requireChannel(p, trackId_);
    previous_ = t.solo;
    t.solo = value_;
}
void SetTrackSolo::revert(Project& p)
{
    requireChannel(p, trackId_).solo = previous_;
}

SetTrackInput::SetTrackInput(Id trackId, int inputChannel) : trackId_(trackId), value_(inputChannel) {}
void SetTrackInput::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.inputChannel;
    t.inputChannel = value_;
}
void SetTrackInput::revert(Project& p)
{
    requireTrack(p, trackId_).inputChannel = previous_;
}

SetTrackArmed::SetTrackArmed(Id trackId, bool armed) : trackId_(trackId), value_(armed) {}
void SetTrackArmed::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.armed;
    t.armed = value_;
}
void SetTrackArmed::revert(Project& p)
{
    requireTrack(p, trackId_).armed = previous_;
}

SetTrackMonitor::SetTrackMonitor(Id trackId, MonitorMode mode) : trackId_(trackId), value_(mode) {}
void SetTrackMonitor::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.monitor;
    t.monitor = value_;
}
void SetTrackMonitor::revert(Project& p)
{
    requireTrack(p, trackId_).monitor = previous_;
}

// ----- clips ------------------------------------------------------------------------------

AddClip::AddClip(Id trackId, AudioClip clip) : trackId_(trackId), clip_(std::move(clip)) {}

void AddClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    if (clip_.id == invalidId)
        clip_.id = p.allocateId();
    t.clips.push_back(clip_);
    t.sortClips();
}

void AddClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.clips, [this](const AudioClip& c) { return c.id == clip_.id; });
}

RemoveClip::RemoveClip(Id trackId, Id clipId) : trackId_(trackId), clipId_(clipId) {}

void RemoveClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    removed_ = requireClip(t, clipId_);
    std::erase_if(t.clips, [this](const AudioClip& c) { return c.id == clipId_; });
}

void RemoveClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    t.clips.push_back(removed_);
    t.sortClips();
}

MoveClip::MoveClip(Id fromTrackId, Id clipId, Id toTrackId, SamplePosition newStart)
    : fromTrackId_(fromTrackId), clipId_(clipId), toTrackId_(toTrackId), newStart_(newStart)
{
}

void MoveClip::apply(Project& p)
{
    auto& from = requireTrack(p, fromTrackId_);
    AudioClip clip = requireClip(from, clipId_);
    previousStart_ = clip.start;
    clip.start = newStart_;
    std::erase_if(from.clips, [this](const AudioClip& c) { return c.id == clipId_; });
    auto& to = requireTrack(p, toTrackId_);
    to.clips.push_back(clip);
    to.sortClips();
}

void MoveClip::revert(Project& p)
{
    auto& to = requireTrack(p, toTrackId_);
    AudioClip clip = requireClip(to, clipId_);
    clip.start = previousStart_;
    std::erase_if(to.clips, [this](const AudioClip& c) { return c.id == clipId_; });
    auto& from = requireTrack(p, fromTrackId_);
    from.clips.push_back(clip);
    from.sortClips();
}

TrimClip::TrimClip(
    Id trackId, Id clipId, SamplePosition start, SamplePosition length, SamplePosition sourceOffset)
    : trackId_(trackId), clipId_(clipId), start_(start), length_(length), sourceOffset_(sourceOffset)
{
}

void TrimClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& c = requireClip(t, clipId_);
    previousStart_ = c.start;
    previousLength_ = c.length;
    previousOffset_ = c.sourceOffset;
    c.start = start_;
    c.length = std::max<SamplePosition>(0, length_);
    c.sourceOffset = std::max<SamplePosition>(0, sourceOffset_);
    t.sortClips();
}

void TrimClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& c = requireClip(t, clipId_);
    c.start = previousStart_;
    c.length = previousLength_;
    c.sourceOffset = previousOffset_;
    t.sortClips();
}

SetClipFades::SetClipFades(Id trackId, Id clipId, SamplePosition fadeIn, SamplePosition fadeOut)
    : trackId_(trackId), clipId_(clipId), fadeIn_(fadeIn), fadeOut_(fadeOut)
{
}
void SetClipFades::apply(Project& p)
{
    auto& c = requireClip(requireTrack(p, trackId_), clipId_);
    previousIn_ = c.fadeIn;
    previousOut_ = c.fadeOut;
    c.fadeIn = std::clamp<SamplePosition>(fadeIn_, 0, c.length);
    c.fadeOut = std::clamp<SamplePosition>(fadeOut_, 0, c.length);
}
void SetClipFades::revert(Project& p)
{
    auto& c = requireClip(requireTrack(p, trackId_), clipId_);
    c.fadeIn = previousIn_;
    c.fadeOut = previousOut_;
}

SplitClip::SplitClip(Id trackId, Id clipId, SamplePosition at) : trackId_(trackId), clipId_(clipId), at_(at)
{
}
void SplitClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& c = requireClip(t, clipId_);
    if (at_ <= c.start || at_ >= c.end())
        throw std::logic_error("split point is not inside the clip");
    original_ = c;
    if (right_.id == invalidId)
        right_.id = p.allocateId();
    right_.name = c.name;
    right_.mediaPath = c.mediaPath;
    right_.start = at_;
    right_.length = c.end() - at_;
    right_.sourceOffset = c.sourceOffset + (at_ - c.start);
    right_.gain = c.gain;
    right_.fadeIn = 0;
    right_.fadeOut = std::min(c.fadeOut, right_.length);
    c.length = at_ - c.start;
    c.fadeOut = 0;
    c.fadeIn = std::min(c.fadeIn, c.length);
    t.clips.push_back(right_);
    t.sortClips();
}
void SplitClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.clips, [this](const AudioClip& c) { return c.id == right_.id; });
    requireClip(t, clipId_) = original_;
    t.sortClips();
}

bool JoinClips::canJoin(const AudioClip& left, const AudioClip& right) noexcept
{
    return left.mediaPath == right.mediaPath && right.start == left.end() &&
           right.sourceOffset == left.sourceOffset + left.length && left.gain == right.gain;
}
JoinClips::JoinClips(Id trackId, Id leftClipId, Id rightClipId)
    : trackId_(trackId), leftId_(leftClipId), rightId_(rightClipId)
{
}
void JoinClips::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& left = requireClip(t, leftId_);
    const auto& right = requireClip(t, rightId_);
    if (!canJoin(left, right))
        throw std::logic_error("clips are not contiguous in the same source");
    previousLeft_ = left;
    removedRight_ = right;
    left.length += right.length;
    left.fadeOut = right.fadeOut;
    std::erase_if(t.clips, [this](const AudioClip& c) { return c.id == rightId_; });
}
void JoinClips::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    requireClip(t, leftId_) = previousLeft_;
    t.clips.push_back(removedRight_);
    t.sortClips();
}

DuplicateClip::DuplicateClip(Id trackId, Id clipId, SamplePosition at)
    : trackId_(trackId), clipId_(clipId), at_(at)
{
}
void DuplicateClip::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    const auto& c = requireClip(t, clipId_);
    const Id keep = copy_.id;
    copy_ = c;
    copy_.id = keep == invalidId ? p.allocateId() : keep;
    copy_.start = at_ >= 0 ? at_ : c.end();
    t.clips.push_back(copy_);
    t.sortClips();
}
void DuplicateClip::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    std::erase_if(t.clips, [this](const AudioClip& c) { return c.id == copy_.id; });
}

CrossfadeClips::CrossfadeClips(Id trackId, Id leftClipId, Id rightClipId)
    : trackId_(trackId), leftId_(leftClipId), rightId_(rightClipId)
{
}
void CrossfadeClips::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    auto& left = requireClip(t, leftId_);
    auto& right = requireClip(t, rightId_);
    const auto overlap = std::min(left.end(), right.end()) - std::max(left.start, right.start);
    if (right.start <= left.start || overlap <= 0)
        throw std::logic_error("clips do not overlap left to right");
    previousOut_ = left.fadeOut;
    previousIn_ = right.fadeIn;
    left.fadeOut = std::min(overlap, left.length);
    right.fadeIn = std::min(overlap, right.length);
}
void CrossfadeClips::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    requireClip(t, leftId_).fadeOut = previousOut_;
    requireClip(t, rightId_).fadeIn = previousIn_;
}

ShiftClips::ShiftClips(Id trackId, SamplePosition from, SamplePosition delta)
    : trackId_(trackId), from_(from), delta_(delta)
{
}
void ShiftClips::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_.clear();
    for (auto& c : t.clips)
        if (c.start >= from_)
        {
            previous_.emplace_back(c.id, c.start);
            c.start = std::max<SamplePosition>(0, c.start + delta_);
        }
    t.sortClips();
}
void ShiftClips::revert(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    for (const auto& [id, start] : previous_)
        if (auto* c = t.findClip(id))
            c->start = start;
    t.sortClips();
}

SetClipGain::SetClipGain(Id trackId, Id clipId, float gain) : trackId_(trackId), clipId_(clipId), value_(gain)
{
}
void SetClipGain::apply(Project& p)
{
    auto& c = requireClip(requireTrack(p, trackId_), clipId_);
    previous_ = c.gain;
    c.gain = value_;
}
void SetClipGain::revert(Project& p)
{
    requireClip(requireTrack(p, trackId_), clipId_).gain = previous_;
}

// ----- project ----------------------------------------------------------------------------

SetTempo::SetTempo(double bpm) : value_(bpm) {}
SetTimeSignature::SetTimeSignature(int numerator, int denominator)
    : value_{std::clamp(numerator, 1, 32), std::clamp(denominator, 1, 32)}
{
}
void SetTimeSignature::apply(Project& p)
{
    previous_ = p.timeSignature;
    p.timeSignature = value_;
}
void SetTimeSignature::revert(Project& p)
{
    p.timeSignature = previous_;
}

void SetTempo::apply(Project& p)
{
    previous_ = p.tempoBpm;
    p.tempoBpm = value_;
}
void SetTempo::revert(Project& p)
{
    p.tempoBpm = previous_;
}

} // namespace aural::core
