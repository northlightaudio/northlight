// The arrange view's drags, driven through its own mouse handlers (no window):
// each gesture previews and commits what screens/arrange.md describes.

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "ui/ArrangeView.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
// At 1200 samples a pixel (the view's first zoom) a 120 BPM beat is 20 px: the
// adaptive grid snaps to beats. One audio track, one clip at a beat in, two beats long.
struct Fixture
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view{peaks};
    core::Project project;
    core::UndoStack stack;
    std::vector<std::string> edits;

    Fixture()
    {
        core::Track t;
        t.id = 1;
        core::AudioClip c;
        c.id = 10;
        c.mediaPath = "missing.wav";
        c.start = 24000;
        c.length = 48000;
        t.clips.push_back(c);
        project.tracks.push_back(t);
        project.nextId = 100;
        view.setSize(1400, 600);
        view.setProject(&project);
        view.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
        {
            for (auto& c : commands)
                stack.execute(project, std::move(c));
            edits.push_back(name);
            view.setProject(&project);
        };
    }

    const core::AudioClip& clip() const { return project.tracks[0].clips[0]; }

    juce::MouseEvent event(juce::Point<int> at, juce::Point<int> down, juce::ModifierKeys mods, bool dragged)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                at.toFloat(),
                                mods,
                                1.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f,
                                &view,
                                &view,
                                juce::Time::getCurrentTime(),
                                down.toFloat(),
                                juce::Time::getCurrentTime(),
                                1,
                                dragged);
    }

    // Presses at `from`, drags to `to` and lets go, with `mods` held.
    void drag(juce::Point<int> from, juce::Point<int> to, juce::ModifierKeys mods = {})
    {
        const auto held = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
        view.mouseDown(event(from, from, held, false));
        view.mouseDrag(event(to, from, held, true));
        view.mouseUp(event(to, from, mods, true));
    }
};

// The clip's body (below its fade handles) and its right edge, row 0.
constexpr int bodyY = 70;
constexpr int clipLeft = 260 + 20, clipRight = 260 + 60;
} // namespace

TEST_CASE("Dragging a clip's body moves it, snapped to the grid, in one step", "[ui][arrange][gesture]")
{
    Fixture f;
    f.drag({clipLeft + 10, bodyY}, {clipLeft + 10 + 41, bodyY}); // two beats and a pixel
    CHECK(f.edits == std::vector<std::string>{"Move clip"});
    CHECK(f.clip().start == 24000 + 48000);
    CHECK(f.view.selection().size() == 1);
}

TEST_CASE("Dragging a clip's right edge trims it to the grid", "[ui][arrange][gesture]")
{
    Fixture f;
    f.drag({clipRight - 2, bodyY}, {clipRight - 2 + 40, bodyY});
    CHECK(f.edits == std::vector<std::string>{"Trim clip"});
    CHECK(f.clip().start == 24000);
    CHECK(f.clip().length == 96000); // its end from 72 000 to 120 000
}

TEST_CASE("Ctrl+Shift+dragging a clip's body up sets its gain", "[ui][arrange][gesture]")
{
    Fixture f;
    f.drag({clipLeft + 10, bodyY},
           {clipLeft + 10, bodyY - 24},
           juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier));
    CHECK(f.edits == std::vector<std::string>{"Set clip gain"});
    CHECK(f.clip().gain > 1.9f); // +6 dB
    CHECK(f.clip().gain < 2.1f);
}

TEST_CASE("Ctrl+dragging a clip's body copies it there and selects the copy", "[ui][arrange][gesture]")
{
    Fixture f;
    f.drag({clipLeft + 10, bodyY}, {clipLeft + 10 + 41, bodyY}, juce::ModifierKeys::ctrlModifier);
    CHECK(f.edits == std::vector<std::string>{"Copy clip"});
    REQUIRE(f.project.tracks[0].clips.size() == 2);
    CHECK(f.project.tracks[0].clips[0].start == 24000); // the original stays
    CHECK(f.project.tracks[0].clips[1].start == 24000 + 48000);
    REQUIRE(f.view.selection().size() == 1);
    CHECK(f.view.selection()[0].clip == f.project.tracks[0].clips[1].id);
}

TEST_CASE("The range tool selects time over a clip, and Delete removes that range", "[ui][arrange][gesture]")
{
    Fixture f;
    f.view.setTool(ui::ArrangeView::Tool::range);
    f.drag({clipLeft + 11, bodyY}, {clipLeft + 41, bodyY}); // from beat 2 to beat 3, over the clip
    REQUIRE(f.view.hasTimeSelection());
    CHECK(f.view.timeSelectionStart() == 48000);
    CHECK(f.view.timeSelectionEnd() == 72000);
    f.view.deleteTimeRange(); // (what Delete does with a time selection)
    CHECK(f.edits == std::vector<std::string>{"Delete range"});
    REQUIRE(f.project.tracks[0].clips.size() == 1);
    CHECK(f.clip().start == 24000);
    CHECK(f.clip().length == 24000);
}

TEST_CASE("Mute from the keys acts on every selected track", "[ui][arrange]")
{
    Fixture f;
    core::Track second;
    second.id = 2;
    f.project.tracks.push_back(second);
    f.view.setProject(&f.project);
    f.view.selectTracks({1, 2});
    CHECK(f.view.selectedTracks() == std::vector<core::Id>{1, 2});
    f.view.toggleMuteSelectedTracks();
    CHECK(f.edits == std::vector<std::string>{"Mute tracks"});
    CHECK(f.project.tracks[0].mute);
    CHECK(f.project.tracks[1].mute);
    // A track chosen elsewhere (not among the selected) is selected alone.
    f.view.selectTracks({2});
    f.view.setSelectedTrack(1);
    CHECK(f.view.selectedTracks() == std::vector<core::Id>{1});
}

TEST_CASE("Dragging a fade handle sets the fade", "[ui][arrange][gesture]")
{
    Fixture f;
    // The handle is the 8 px square at the clip's top left corner (the block starts 2 px under the row).
    f.drag({clipLeft + 2, 32 + 2 + 3}, {clipLeft + 12, 32 + 2 + 3});
    CHECK(f.edits == std::vector<std::string>{"Set fade"});
    CHECK(f.clip().fadeIn > 0);
}

TEST_CASE("A lasso over empty space selects the clips it touches", "[ui][arrange][gesture]")
{
    Fixture f;
    f.drag({clipRight + 60, 40}, {clipLeft + 5, 80});
    CHECK(f.edits.empty());
    REQUIRE(f.view.selection().size() == 1);
    CHECK(f.view.selection()[0].clip == 10);
}

TEST_CASE("A drag in the ruler's bars row sets the loop region on the grid", "[ui][arrange][gesture]")
{
    Fixture f;
    core::SamplePosition from = -1, to = -1;
    f.view.onRegionChanged = [&](core::SamplePosition a, core::SamplePosition b)
    {
        from = a;
        to = b;
    };
    f.drag({260 + 41, 24}, {260 + 99, 24});
    CHECK(from == 48000);
    CHECK(to == 120000);
}

TEST_CASE("The erase tool removes the clip it is pressed on, on release", "[ui][arrange][gesture]")
{
    Fixture f;
    f.view.setTool(ui::ArrangeView::Tool::erase);
    f.drag({clipLeft + 10, bodyY}, {clipLeft + 11, bodyY});
    CHECK(f.project.tracks[0].clips.empty());
    CHECK(f.edits.size() == 1);
}
