#pragma once

#include "core/Model.h"

#include <functional>

namespace northlight::ui
{

// The one owner of what is selected: a channel (a track, an FX track or a bus)
// and the audio clip on it the inspector shows. Everything that selects goes
// through select(); one listener (the shell's) brings every view to it, so no
// view can be left showing another channel. The arrange view reads the selected
// channel from here (it keeps no copy); it keeps its own set of selected clips
// for editing, and reports the one in focus here.
class Selection
{
public:
    core::Id channel() const noexcept { return channel_; }
    core::Id clip() const noexcept { return clip_; }

    // Selects a channel and, optionally, a clip on it. Notifies when anything
    // changed.
    void select(core::Id channel, core::Id clip = core::invalidId)
    {
        if (channel == channel_ && clip == clip_)
            return;
        channel_ = channel;
        clip_ = clip;
        if (onChanged)
            onChanged();
    }

    // After the project changed: a channel that is gone falls back to the first
    // track (or nothing, with no tracks), a clip that is gone is dropped. While
    // the project has tracks, one is always selected.
    void validate(const core::Project& project)
    {
        auto channel = channel_;
        auto clip = clip_;
        if (project.findChannel(channel) == nullptr)
        {
            channel = project.tracks.empty() ? core::invalidId : project.tracks.front().id;
            clip = core::invalidId;
        }
        const auto* track = project.findTrack(channel);
        if (clip != core::invalidId && (track == nullptr || track->findClip(clip) == nullptr))
            clip = core::invalidId;
        select(channel, clip);
    }

    std::function<void()> onChanged;

private:
    core::Id channel_ = core::invalidId;
    core::Id clip_ = core::invalidId;
};

} // namespace northlight::ui
