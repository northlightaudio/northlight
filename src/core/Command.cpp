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

SetTrackName::SetTrackName(Id trackId, std::string name) : trackId_(trackId), value_(std::move(name)) {}
void SetTrackName::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.name;
    t.name = value_;
}
void SetTrackName::revert(Project& p)
{
    requireTrack(p, trackId_).name = previous_;
}

SetTrackGain::SetTrackGain(Id trackId, float gain) : trackId_(trackId), value_(gain) {}
void SetTrackGain::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.gain;
    t.gain = value_;
}
void SetTrackGain::revert(Project& p)
{
    requireTrack(p, trackId_).gain = previous_;
}

SetTrackPan::SetTrackPan(Id trackId, float pan) : trackId_(trackId), value_(pan) {}
void SetTrackPan::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.pan;
    t.pan = value_;
}
void SetTrackPan::revert(Project& p)
{
    requireTrack(p, trackId_).pan = previous_;
}

SetTrackMute::SetTrackMute(Id trackId, bool mute) : trackId_(trackId), value_(mute) {}
void SetTrackMute::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.mute;
    t.mute = value_;
}
void SetTrackMute::revert(Project& p)
{
    requireTrack(p, trackId_).mute = previous_;
}

SetTrackSolo::SetTrackSolo(Id trackId, bool solo) : trackId_(trackId), value_(solo) {}
void SetTrackSolo::apply(Project& p)
{
    auto& t = requireTrack(p, trackId_);
    previous_ = t.solo;
    t.solo = value_;
}
void SetTrackSolo::revert(Project& p)
{
    requireTrack(p, trackId_).solo = previous_;
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
