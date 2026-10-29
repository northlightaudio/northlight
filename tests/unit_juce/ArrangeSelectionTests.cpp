#include "audio/PeakStore.h"
#include "ui/ArrangeView.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
core::Project twoClips()
{
    core::Project p;
    core::Track t;
    t.id = 1;
    for (core::Id id : {10, 11})
    {
        core::AudioClip c;
        c.id = id;
        c.mediaPath = "missing.wav";
        c.start = static_cast<core::SamplePosition>(id - 10) * 96000;
        c.length = 48000;
        t.clips.push_back(c);
    }
    p.tracks.push_back(t);
    p.nextId = 20;
    return p;
}
} // namespace

TEST_CASE("Every change to the arrange view's clip selection is reported once", "[ui][arrange][selection]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(900, 600);
    auto project = twoClips();
    view.setProject(&project);
    int reports = 0;
    view.onSelectionChanged = [&reports] { ++reports; };

    view.selectAll();
    CHECK(view.selection().size() == 2);
    CHECK(reports == 1);
    view.selectAll(); // nothing changed
    CHECK(reports == 1);

    // A clip that goes away leaves the selection, and the owner hears it.
    project.tracks[0].clips.pop_back();
    view.setProject(&project);
    CHECK(view.selection().size() == 1);
    CHECK(reports == 2);

    view.clearSelection();
    CHECK(view.selection().empty());
    CHECK(reports == 3);
}

TEST_CASE("Ctrl+Left and Ctrl+Right step through the selected track's clips",
          "[ui][arrange][selection][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(900, 600);
    auto project = twoClips();
    view.setProject(&project);
    view.setSelectedTrack(1);
    using Key = juce::KeyPress;
    const auto ctrl = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto first = std::vector<ui::ArrangeView::ClipRef>{{1, 10}};
    const auto second = std::vector<ui::ArrangeView::ClipRef>{{1, 11}};

    // From no selection: the first clip that ends after the playhead (at 0).
    CHECK(view.handleEditKey(Key(Key::rightKey, ctrl, 0)));
    CHECK(view.selection() == first);
    CHECK(view.handleEditKey(Key(Key::rightKey, ctrl, 0)));
    CHECK(view.selection() == second);
    CHECK(view.handleEditKey(Key(Key::rightKey, ctrl, 0))); // the last stays
    CHECK(view.selection() == second);
    CHECK(view.handleEditKey(Key(Key::leftKey, juce::ModifierKeys(ctrl | shift), 0)));
    CHECK(view.selection().size() == 2);
    CHECK(view.handleEditKey(Key(Key::leftKey, ctrl, 0)));
    CHECK(view.selection() == first);
    // Before the first clip there is nothing to the left of the playhead.
    view.clearSelection();
    CHECK_FALSE(view.stepClipSelection(-1, false));
    // A track with no clips leaves the key to others.
    view.setSelectedTrack(99);
    CHECK_FALSE(view.handleEditKey(Key(Key::rightKey, ctrl, 0)));
}

TEST_CASE("The arrange view shows the shared selection's track", "[ui][arrange]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(1000, 600);
    core::Project p;
    for (core::Id id : {1, 2})
    {
        core::Track t;
        t.id = id;
        p.tracks.push_back(t);
    }
    p.nextId = 10;
    view.setProject(&p);

    // On its own the view selects.
    view.setSelectedTrack(2);
    CHECK(view.selectedTrack() == 2);

    // With a shared selection it shows that one: it keeps no copy of its own.
    ui::Selection selection;
    selection.select(1);
    view.setSelection(&selection);
    CHECK(view.selectedTrack() == 1);
    view.setSelectedTrack(2); // the owner has not selected it: still the owner's
    CHECK(view.selectedTrack() == 1);
    selection.select(2);
    CHECK(view.selectedTrack() == 2);
}
