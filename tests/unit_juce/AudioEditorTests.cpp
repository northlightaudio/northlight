#include "audio/PeakStore.h"
#include "core/Command.h"
#include "engine/AudioSource.h"
#include "engine/pitch/VocalService.h"
#include "ui/AudioEditor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using Catch::Approx;

namespace
{
juce::MouseEvent wheelAt(juce::Component& c, float x, float y, juce::ModifierKeys mods)
{
    const juce::Point<float> at(x, y);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            at,
                            mods,
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            &c,
                            &c,
                            juce::Time::getCurrentTime(),
                            at,
                            juce::Time::getCurrentTime(),
                            1,
                            false);
}

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

TEST_CASE("Zooming a linked audio editor moves the arrange view's zoom with it", "[ui][audioeditor]")
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
    double timelinePerPixel = 0.0;
    core::SamplePosition first = -1;
    editor.onTimeViewChanged = [&](double spp, core::SamplePosition at)
    {
        timelinePerPixel = spp;
        first = at;
    };
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 1.0f; // in
    editor.mouseWheelMove(
        wheelAt(editor, 400.0f, 200.0f, juce::ModifierKeys(juce::ModifierKeys::ctrlModifier)), wheel);
    // The editor zoomed in (by the wheel factor every timeline view shares, from 50
    // source samples a pixel) and reported it in timeline samples (stretch 2).
    const double zoomed = 50.0 * ui::TimelineViewport::wheelZoomFactor(1.0);
    CHECK(editor.sourceSamplesPerPixel() == Approx(zoomed));
    CHECK(timelinePerPixel == Approx(2.0 * zoomed));
    CHECK(first >= 10000);
}

TEST_CASE("Pitch mode edits a stretched clip (its stretch plays the edits)", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    auto project = stretchedClipProject();
    project.tracks[0].clips[0].vocal.analysisVersion = 1; // it has a vocal edit
    editor.setClip(&project, 1, 2);
    int edits = 0;
    editor.onEdit = [&edits](std::vector<std::unique_ptr<core::Command>>, const std::string&) { ++edits; };
    editor.setMode(ui::AudioEditor::Mode::pitch);
    editor.setBreaths(-12.0f, false);
    CHECK(edits == 1);
    CHECK(editor.keyPressed(juce::KeyPress('2')));
    CHECK(editor.tool() == ui::AudioEditor::Tool::split);
}

namespace
{
// A clip of three vocal notes (no media: the edit alone), not stretched.
core::Project vocalProject(double transpose = 0.0)
{
    auto p = stretchedClipProject();
    auto& c = p.tracks[0].clips[0];
    c.stretch = 1.0;
    c.transpose = transpose;
    c.length = 40000;
    c.vocal.analysisVersion = 1;
    for (int i = 0; i < 3; ++i)
    {
        core::VocalNote n;
        n.id = static_cast<core::Id>(10 + i);
        n.start = 1000 + i * 12000;
        n.end = n.start + 10000; // long enough for a full correction
        n.centre = n.target = 6000.0f + 200.0f * static_cast<float>(i);
        c.vocal.notes.push_back(n);
    }
    return p;
}
} // namespace

TEST_CASE("Left and Right step the audio editor's note selection", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    const auto project = vocalProject();
    editor.setClip(&project, 1, 2);
    editor.setMode(ui::AudioEditor::Mode::pitch);
    int changes = 0;
    editor.onSelectionChanged = [&changes] { ++changes; };
    CHECK(editor.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(editor.selection() == std::vector<core::Id>{10});
    CHECK(editor.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(editor.selection() == std::vector<core::Id>{11});
    CHECK(editor.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK(editor.selection().size() == 2);
    CHECK(editor.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    CHECK(editor.selection() == std::vector<core::Id>{10});
    CHECK(changes == 4);
}

TEST_CASE("The audio editor auditions by default and follows the playhead when asked", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    const auto project = stretchedClipProject();
    editor.setClip(&project, 1, 2);
    CHECK(editor.audition()); // as the piano roll's
    editor.zoomTo(1.0, 0.0);  // a sample a pixel: 2 000 source samples is wider than the view
    const auto first = editor.firstVisibleSource();
    editor.setPlayhead(10000 + 3000);            // 1 500 source samples in: off the right edge
    CHECK(editor.firstVisibleSource() == first); // not following: the view stays
    editor.setFollowPlayhead(true);
    editor.setPlayhead(10000 + 3200);
    CHECK(editor.firstVisibleSource() > first); // the page turned
}

TEST_CASE("Ctrl+Shift+wheel zooms the audio editor's pitches", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    const auto project = vocalProject();
    editor.setClip(&project, 1, 2);
    editor.setMode(ui::AudioEditor::Mode::pitch);
    const float before = editor.semitoneHeight();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.5f;
    editor.mouseWheelMove(
        wheelAt(editor,
                400.0f,
                200.0f,
                juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier)),
        wheel);
    CHECK(editor.semitoneHeight() > before);
}

TEST_CASE("Correct to key aims at the pitch a transposed clip plays", "[ui][audioeditor]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    NoMedia loader;
    engine::VocalService vocal(loader, true);
    ui::AudioEditor editor(peaks, vocal);
    editor.setSize(800, 400);
    // Sung C, D, E (a little flat) and played a semitone up: C# D# F, which C major does not hold.
    auto project = vocalProject(1.0);
    for (auto& n : project.tracks[0].clips[0].vocal.notes)
        n.centre = n.target = n.target - 30.0f;
    project.key = core::MusicalKey{1, core::ScaleKind::major}; // C# major holds them
    editor.setClip(&project, 1, 2);
    std::vector<std::unique_ptr<core::Command>> got;
    editor.onEdit = [&](std::vector<std::unique_ptr<core::Command>> commands, const std::string&)
    {
        for (auto& c : commands)
            got.push_back(std::move(c));
    };
    editor.correctToKey({});
    REQUIRE(got.size() == 1);
    core::UndoStack undo;
    REQUIRE(undo.execute(project, std::move(got.front())));
    // Each moved towards its own semitone (by 85%), not to C major's neighbours.
    const auto& notes = project.tracks[0].clips[0].vocal.notes;
    CHECK(notes[0].target == Approx(6000.0f - 30.0f * 0.15f).margin(1.0f));
    CHECK(notes[2].target == Approx(6400.0f - 30.0f * 0.15f).margin(1.0f));
}
