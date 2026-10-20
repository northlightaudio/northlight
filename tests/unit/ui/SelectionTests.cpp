#include "ui/Selection.h"

#include <catch2/catch_test_macros.hpp>

using namespace aural;

namespace
{
core::Project twoTracks()
{
    core::Project p;
    core::Track a;
    a.id = 1;
    a.clips.push_back({10, "clip", "a.wav", 0, 100, 0, 1.0f});
    core::Track b;
    b.id = 2;
    core::Bus fx;
    fx.id = 3;
    fx.fx = true;
    p.tracks = {a, b};
    p.buses = {fx};
    return p;
}
} // namespace

TEST_CASE("The selection notifies its one listener only when it changes", "[ui][selection]")
{
    ui::Selection selection;
    int notified = 0;
    selection.onChanged = [&] { ++notified; };
    selection.select(1);
    CHECK(notified == 1);
    selection.select(1);
    CHECK(notified == 1); // nothing changed
    selection.select(1, 10);
    CHECK(notified == 2); // the inspector's clip is part of it
    CHECK(selection.channel() == 1);
    CHECK(selection.clip() == 10);
}

TEST_CASE("After a project change the selection falls back to what still exists", "[ui][selection]")
{
    auto project = twoTracks();
    ui::Selection selection;
    selection.validate(project);
    CHECK(selection.channel() == 1); // a project with tracks always has one selected

    selection.select(3);
    selection.validate(project);
    CHECK(selection.channel() == 3); // an FX track (a bus) is a valid selection

    selection.select(1, 10);
    project.tracks[0].clips.clear();
    selection.validate(project);
    CHECK(selection.channel() == 1);
    CHECK(selection.clip() == core::invalidId); // the clip went

    selection.select(2);
    project.tracks.erase(project.tracks.begin() + 1);
    selection.validate(project);
    CHECK(selection.channel() == 1); // the track went: the first one instead

    project.tracks.clear();
    selection.validate(project);
    CHECK(selection.channel() == core::invalidId);
}
