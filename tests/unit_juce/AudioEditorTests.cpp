#include "audio/PeakStore.h"
#include "engine/AudioSource.h"
#include "engine/pitch/VocalService.h"
#include "ui/AudioEditor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using Catch::Approx;

namespace
{
class NoMedia final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media in this test";
        return nullptr;
    }
};

// A project with one clip at 10 000 that plays its source from 1 000 at half speed
// (stretch 2): 4 000 timeline samples hold 2 000 source samples.
core::Project stretchedClipProject()
{
    core::Project p;
    core::Track t;
    t.id = 1;
    core::AudioClip c;
    c.id = 2;
    c.mediaPath = "missing.wav";
    c.start = 10000;
    c.length = 4000;
    c.sourceOffset = 1000;
    c.stretch = 2.0;
    t.clips.push_back(c);
    p.tracks.push_back(t);
    return p;
}
} // namespace

TEST_CASE("The audio editor's playhead follows a stretched clip's source", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    const auto project = stretchedClipProject();
    editor.setClip(&project, 1, 2);

    editor.setPlayhead(12000); // 2 000 into the clip: 1 000 source samples in
    CHECK(editor.playheadSource() == 2000);
    editor.setPlayhead(9000); // before the clip
    CHECK(editor.playheadSource() == -1);
}

TEST_CASE("A linked audio editor converts the arrange zoom into the clip's source", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    const auto project = stretchedClipProject();
    editor.setClip(&project, 1, 2);
    editor.setZoomLinked(true);

    editor.setTimeView(100.0, 12000);
    CHECK(editor.sourceSamplesPerPixel() == Approx(50.0));
    CHECK(editor.firstVisibleSource() == Approx(2000.0));
}
