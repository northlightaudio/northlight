// Work budgets for the timeline views (docs/ui-performance.md): a playhead frame
// repaints a column, and painting that column draws only what is in it, however
// many clips and notes the project has.

#include "UiPerf.h"
#include "audio/PeakStore.h"
#include "ui/ArrangeView.h"
#include "ui/PaintStats.h"
#include "ui/PianoRoll.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
constexpr int trackCount = 16;
constexpr int clipsPerTrack = 20;

core::Project busyArrangement()
{
    core::Project p;
    for (int t = 0; t < trackCount; ++t)
    {
        core::Track track;
        track.id = static_cast<core::Id>(1 + t);
        for (int k = 0; k < clipsPerTrack; ++k)
        {
            core::AudioClip clip;
            clip.id = static_cast<core::Id>(1000 + t * 100 + k);
            clip.mediaPath = "missing.wav";
            clip.start = static_cast<core::SamplePosition>(k) * 48000;
            clip.length = 40000;
            track.clips.push_back(clip);
        }
        p.tracks.push_back(track);
    }
    p.nextId = 10000;
    return p;
}

// Paints `c` clipped to `region` into a throwaway image.
void paintRegion(juce::Component& c, const juce::RectangleList<int>& region)
{
    juce::Image image(juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    juce::Graphics g(image);
    g.reduceClipRegion(region);
    c.paintEntireComponent(g, false);
}

// What `move` asks `c` to repaint.
juce::RectangleList<int> repaintsOf(juce::Component& c, const std::function<void()>& move)
{
    auto* recorder = new uiperf::RepaintRecorder();
    recorder->setBounds(c.getLocalBounds());
    c.setCachedComponentImage(recorder); // the component owns it
    move();
    auto area = recorder->area;
    c.setCachedComponentImage(nullptr);
    return area;
}
} // namespace

TEST_CASE("A playhead frame in the arrange view draws only the clips in its columns", "[ui][perf][arrange]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(1400, 900);
    auto project = busyArrangement();
    view.setProject(&project);
    view.setPlayhead(48000, true, false);

    const auto area = repaintsOf(view, [&] { view.setPlayhead(48000 + 2400, true, false); });
    CHECK(uiperf::coverage(area, view.getLocalBounds()) < 0.02);

    ui::paintstats::reset();
    paintRegion(view, area);
    // Each column crosses at most one clip per track.
    CHECK(ui::paintstats::counters().clipsDrawn <= 2 * trackCount);

    // A full paint still draws every clip it shows (the count is not just zero).
    ui::paintstats::reset();
    paintRegion(view, juce::RectangleList<int>(view.getLocalBounds()));
    CHECK(ui::paintstats::counters().clipsDrawn > 2 * trackCount);
}

TEST_CASE("A playhead frame in the piano roll draws only its columns and the keys that change",
          "[ui][perf][midi]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project p;
    core::Track track;
    track.id = 1;
    core::MidiClip clip;
    clip.id = 2;
    clip.length = 48000 * 32;
    for (int i = 0; i < 400; ++i)
    {
        core::MidiNote n;
        n.id = static_cast<core::Id>(100 + i);
        n.note = 48 + (i * 7) % 24;
        n.start = static_cast<core::SamplePosition>(i) * 3600;
        n.length = 3000;
        clip.notes.push_back(n);
    }
    track.midiClips.push_back(clip);
    p.tracks.push_back(track);
    p.nextId = 1000;

    ui::PianoRoll roll;
    roll.setSize(1200, 700);
    roll.setClip(&p, 1, 2);
    roll.setPlayhead(100);

    // Inside one note: the playhead moves, no key changes.
    const auto area = repaintsOf(roll, [&] { roll.setPlayhead(400); });
    CHECK(uiperf::coverage(area, roll.getLocalBounds()) < 0.02);

    ui::paintstats::reset();
    paintRegion(roll, area);
    CHECK(ui::paintstats::counters().notesDrawn <= 4);
}
