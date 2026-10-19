#include "core/Command.h"

#include <algorithm>
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

void AddTrack::apply(Project& p)
{
    if (track_.id == invalidId)
        track_.id = p.allocateId();
    for (auto& c : track_.clips)
        if (c.id == invalidId)
            c.id = p.allocateId();
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

AddBus::AddBus(Bus bus, int index) : bus_(std::move(bus)), index_(index) {}

void AddBus::apply(Project& p)
{
    if (bus_.id == invalidId)
        bus_.id = p.allocateId();
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
        for (auto it = c.sends.begin(); it != c.sends.end();)
        {
            if (it->bus == busId_)
            {
                detachedSends_.emplace_back(c.id, *it);
                it = c.sends.erase(it);
            }
            else
                ++it;
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
    for (const auto& [channelId, send] : detachedSends_)
        if (auto* c = p.findChannel(channelId))
            c->sends.push_back(send);
}

SetOutputBus::SetOutputBus(Id channelId, Id busId) : channelId_(channelId), value_(busId) {}
void SetOutputBus::apply(Project& p)
{
    auto& c = requireChannel(p, channelId_);
    if (value_ != invalidId)
        requireBus(p, value_);
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
