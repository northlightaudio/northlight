#pragma once

#include "core/Model.h"

#include <type_traits>

// Every id a project holds, in one walk. Adding an id-carrying field to the model
// means adding it here, and the load pass that keeps ids unique (ProjectJson.cpp)
// follows.
namespace northlight::core
{

namespace detail
{
template <class ClipT, class F> void forEachIdInAudioClip(ClipT& clip, F& f)
{
    f(clip.id);
    for (auto& n : clip.vocal.notes)
        f(n.id);
    for (auto& s : clip.vocal.segments)
        f(s.id);
}

template <class ChannelT, class F> void forEachIdInChain(ChannelT& channel, F& f)
{
    if (channel.hasInstrument())
        f(channel.instrument.id);
    for (auto& i : channel.inserts)
        f(i.id);
    for (auto& i : channel.strip)
        f(i.id);
    for (auto& l : channel.automation)
        f(l.id);
}
} // namespace detail

// Calls f(id) with every id in the project: each track and what it holds (clips and
// their vocal notes and segments, take groups and their takes, MIDI clips and their
// notes and controller events, the instrument, inserts, strip modules and
// automation lanes), each bus, the master's chain, the groups and the markers.
// `ProjectT` is Project or const Project; f takes Id& or Id.
template <class ProjectT, class F> void forEachId(ProjectT& project, F&& f)
{
    static_assert(std::is_same_v<std::remove_const_t<ProjectT>, Project>);
    for (auto& t : project.tracks)
    {
        f(t.id);
        for (auto& c : t.clips)
            detail::forEachIdInAudioClip(c, f);
        for (auto& g : t.takeGroups)
        {
            f(g.id);
            for (auto& take : g.takes)
                detail::forEachIdInAudioClip(take.clip, f);
        }
        for (auto& c : t.midiClips)
        {
            f(c.id);
            for (auto& n : c.notes)
                f(n.id);
            for (auto& e : c.controllers)
                f(e.id);
        }
        detail::forEachIdInChain(t, f);
    }
    for (auto& b : project.buses)
    {
        f(b.id);
        detail::forEachIdInChain(b, f);
    }
    detail::forEachIdInChain(project.master, f);
    for (auto& g : project.groups)
        f(g.id);
    for (auto& m : project.markers)
        f(m.id);
}

} // namespace northlight::core
