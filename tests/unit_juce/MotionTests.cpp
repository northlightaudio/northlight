#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "plugins/ChannelStrip.h"
#include "ui/ArrangeView.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"
#include "ui/components/Sheet.h"
#include "ui/components/Toast.h"
#include "ui/panels/EditorPanel.h"
#include "ui/strip/ChannelStripView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using Catch::Approx;

TEST_CASE("The easing decelerates from 0 to 1", "[ui][motion]")
{
    CHECK(ui::motion::ease(0.0f) == 0.0f);
    CHECK(ui::motion::ease(1.0f) == 1.0f);
    CHECK(ui::motion::ease(0.5f) > 0.75f); // most of the way there by half time
    float last = 0.0f;
    for (int i = 1; i <= 20; ++i)
    {
        const float v = ui::motion::ease(static_cast<float>(i) / 20.0f);
        CHECK(v >= last);
        last = v;
    }
}

TEST_CASE("A menu window fades in and slides 2 px into place, unless motion is reduced", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    auto theme = ui::Theme::dark();
    // The default follows Windows' animation setting, which is off on build
    // machines; the test sets it either way rather than inherit the machine's.
    theme.motion.reduceMotion = false;
    ui::NorthlightLookAndFeel lookAndFeel(theme);
    juce::Component window;
    window.setBounds(100, 200, 160, 120);
    lookAndFeel.preparePopupMenuWindow(window);
    CHECK(window.getAlpha() == Approx(0.0f).margin(0.01f));
    CHECK(window.getY() == 198);
    ui::motion::Tween::finishAllForTesting();
    CHECK(window.getAlpha() == Approx(1.0f));
    CHECK(window.getPosition() == juce::Point<int>(100, 200));

    theme.motion.reduceMotion = true;
    ui::NorthlightLookAndFeel still(theme);
    juce::Component other;
    other.setBounds(10, 20, 50, 50);
    still.preparePopupMenuWindow(other);
    CHECK(other.getAlpha() == 1.0f);
    CHECK(other.getY() == 20);
}

namespace
{
// Every component under `root` (itself included) that is not fully opaque.
int fadingComponents(juce::Component& root)
{
    int count = root.getAlpha() < 1.0f ? 1 : 0;
    for (auto* child : root.getChildren())
        count += fadingComponents(*child);
    return count;
}

ui::TrackHeader* headerFor(ui::ArrangeView& view, core::Id id)
{
    for (auto* child : view.getChildren())
        if (auto* header = dynamic_cast<ui::TrackHeader*>(child);
            header != nullptr && header->trackId() == id)
            return header;
    return nullptr;
}
} // namespace

TEST_CASE("A toast slides up and fades in; the oldest of too many fades out", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    if (theme.motion.reduceMotion)
        SKIP("the OS asks for reduced motion");
    ui::ToastStack stack(theme);
    stack.setBounds(0, 0, 400, 600);
    stack.show(ui::ToastStack::Kind::info, "Saved");
    auto* toast = stack.getChildComponent(0);
    REQUIRE(toast != nullptr);
    CHECK(toast->getAlpha() == Approx(0.0f).margin(0.01f));
    const int restingY = 600 - toast->getHeight();
    CHECK(toast->getY() == restingY + 8);
    ui::motion::Tween::finishAllForTesting();
    CHECK(toast->getAlpha() == Approx(1.0f));
    CHECK(toast->getY() == restingY);

    for (int i = 0; i < ui::ToastStack::maxShown; ++i)
        stack.show(ui::ToastStack::Kind::info, "More");
    CHECK(stack.getNumChildComponents() == ui::ToastStack::maxShown + 1); // one leaving
    ui::motion::Tween::finishAllForTesting();
    CHECK(stack.getNumChildComponents() == ui::ToastStack::maxShown);
}

TEST_CASE("A sheet fades in with its panel rising into place", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    if (theme.motion.reduceMotion)
        SKIP("the OS asks for reduced motion");
    juce::Component host;
    host.setBounds(0, 0, 800, 600);
    auto* sheet = ui::Sheet::show(host,
                                  theme,
                                  "Remove bus?",
                                  "Tracks routed to it go to the master.",
                                  {{"Cancel"}, {"Remove", false, true}},
                                  [](int) {});
    CHECK(sheet->getAlpha() == Approx(0.0f).margin(0.01f));
    ui::motion::Tween::finishAllForTesting();
    CHECK(sheet->getAlpha() == Approx(1.0f));
    delete sheet;
}

TEST_CASE("A new track's row grows in; a removed track's space closes", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    if (ui::Theme::dark().motion.reduceMotion)
        SKIP("the OS asks for reduced motion");
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(900, 600);
    core::Project p;
    for (core::Id id : {1, 2})
    {
        core::Track t;
        t.id = id;
        p.tracks.push_back(t);
    }
    view.setProject(&p);
    const int full = view.currentRowHeight();
    const int firstTop = headerFor(view, 1)->getY();

    core::Track added;
    added.id = 3;
    p.tracks.push_back(added);
    view.setProject(&p);
    CHECK(headerFor(view, 3)->getHeight() < full); // growing
    ui::motion::Tween::finishAllForTesting();
    CHECK(headerFor(view, 3)->getHeight() == full);

    p.tracks.erase(p.tracks.begin()); // track 1 goes: 2 and 3 move up
    view.setProject(&p);
    CHECK(headerFor(view, 2)->getY() > firstTop); // still where it was, gliding up
    ui::motion::Tween::finishAllForTesting();
    CHECK(headerFor(view, 2)->getY() == firstTop);
    CHECK(headerFor(view, 3)->getY() == firstTop + full);
}

TEST_CASE("A new insert's card fades in; switching channels does not animate", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    if (theme.motion.reduceMotion)
        SKIP("the OS asks for reduced motion");
    core::Project p;
    for (core::Id id : {1, 2})
    {
        core::Track t;
        t.id = id;
        p.tracks.push_back(t);
    }
    p.nextId = 10;
    ui::EditorPanel panel(theme);
    panel.setSize(900, 300);
    panel.setProject(&p);
    panel.setChannel(1);
    ui::motion::Tween::finishAllForTesting();
    CHECK(fadingComponents(panel) == 0);

    core::Insert gain;
    gain.id = 5;
    gain.type = "northlight.gain";
    gain.params = {0.0f};
    p.tracks[0].inserts.push_back(gain);
    panel.setProject(&p);
    CHECK(fadingComponents(panel) > 0); // the new card
    ui::motion::Tween::finishAllForTesting();
    CHECK(fadingComponents(panel) == 0);

    panel.setChannel(2);
    CHECK(fadingComponents(panel) == 0);
}

TEST_CASE("Strip cards glide when one opens, and appear at once for a new channel", "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    if (theme.motion.reduceMotion)
        SKIP("the OS asks for reduced motion");
    core::Channel channel;
    channel.id = 1;
    channel.strip = plugins::strip::defaultStrip(); // every module off: collapsed
    core::Id next = 10;
    for (auto& m : channel.strip)
        m.id = next++;
    ui::strip::ChannelStripView view(theme);
    view.setSize(2000, 300);
    view.setChannel(&channel);
    CHECK(fadingComponents(view) == 0);
    std::vector<int> before;
    for (auto* child : view.getChildren())
        before.push_back(child->getWidth());

    channel.strip[0].bypass = false; // the gate switches on: its card opens
    view.setChannel(&channel);
    const auto* gate = view.getChildComponent(view.getNumChildComponents() - view.moduleCount());
    REQUIRE(gate != nullptr);
    const int opening = gate->getWidth();
    ui::motion::Tween::finishAllForTesting();
    CHECK(gate->getWidth() > opening);                       // it was still on its way
    ui::strip::ChannelStripView::forgetExpandedForTesting(); // the open gate is remembered per session
}

TEST_CASE("A tooltip's box fits its text as drawn, without wrapping it again", "[ui][tooltip]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::NorthlightLookAndFeel lookAndFeel(theme);
    const juce::Rectangle<int> screen(0, 0, 1920, 1080);
    for (const juce::String text : {"UADx Topline Vocal Tune",
                                    "UADx Pultec EQP-1A",
                                    "Remove this send",
                                    "Volume",
                                    "Choose the instrument this track's MIDI plays",
                                    "Pre-fader: the send ignores the channel's fader and follows its pan"})
    {
        INFO(text);
        const auto box = lookAndFeel.getTooltipBounds(text, {200, 200}, screen);
        const auto lines = [&](float width)
        {
            juce::TextLayout layout;
            juce::AttributedString attributed(text);
            attributed.setFont(theme.small());
            layout.createLayout(attributed, width);
            return std::pair{layout.getNumLines(), layout.getHeight()};
        };
        const auto measured = lines(static_cast<float>(theme.sizes.tooltipMaxWidth - 2 * theme.spacing.sm));
        const auto drawn = lines(static_cast<float>(box.getWidth() - 2 * theme.spacing.sm)); // as drawTooltip
        CHECK(drawn.first == measured.first);
        CHECK(drawn.second + 2 * theme.spacing.xs <= static_cast<float>(box.getHeight()));
    }
}

namespace
{
// A mouse event on the arrange view at x, y (no real input: the handlers are
// called directly).
juce::MouseEvent mouseAt(ui::ArrangeView& view, int x, int y, int clicks = 1, bool dragged = false)
{
    const juce::Point<float> at(static_cast<float>(x), static_cast<float>(y));
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            at,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            &view,
                            &view,
                            juce::Time::getCurrentTime(),
                            at,
                            juce::Time::getCurrentTime(),
                            clicks,
                            dragged);
}

struct DrawFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    audio::PeakStore peaks;
    ui::ArrangeView view{peaks};
    core::Project project;
    core::UndoStack undo;
    DrawFixture()
    {
        core::Track midi;
        midi.id = 1;
        midi.kind = core::TrackKind::midi;
        core::Track audioTrack;
        audioTrack.id = 2;
        project.tracks = {midi, audioTrack};
        project.nextId = 10;
        view.setSize(1200, 500);
        view.setProject(&project);
        view.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string&)
        {
            for (auto& c : commands)
                undo.execute(project, std::move(c));
            view.setProject(&project);
        };
    }
    int rowY(int row)
    {
        return headerFor(view, project.tracks[static_cast<size_t>(row)].id)->getBounds().getCentreY();
    }
};
} // namespace

TEST_CASE("The pencil draws a MIDI clip on a MIDI track, snapped and selected", "[ui][pencil]")
{
    DrawFixture f;
    f.view.setTool(ui::ArrangeView::Tool::pencil);
    const int y = f.rowY(0);
    const int x0 = 400, x1 = 700;
    f.view.mouseDown(mouseAt(f.view, x0, y));
    f.view.mouseDrag(mouseAt(f.view, x1, y, 1, true));
    f.view.mouseUp(mouseAt(f.view, x1, y, 1, true));
    REQUIRE(f.project.tracks[0].midiClips.size() == 1);
    const auto& clip = f.project.tracks[0].midiClips[0];
    CHECK(clip.notes.empty());
    CHECK(clip.length > 0);
    REQUIRE(f.view.selection().size() == 1);
    CHECK(f.view.selection().front().clip == clip.id); // ready to open or move

    // On an audio track the pencil draws nothing.
    f.view.mouseDown(mouseAt(f.view, x0, f.rowY(1)));
    f.view.mouseUp(mouseAt(f.view, x0, f.rowY(1)));
    CHECK(f.project.tracks[1].clips.empty());
    CHECK(f.project.tracks[1].midiClips.empty());
}

TEST_CASE("A click with the pencil makes a bar-long clip; double-click on empty MIDI space does too",
          "[ui][pencil]")
{
    DrawFixture f;
    f.view.setTool(ui::ArrangeView::Tool::pencil);
    f.view.mouseDown(mouseAt(f.view, 600, f.rowY(0)));
    f.view.mouseUp(mouseAt(f.view, 600, f.rowY(0)));
    REQUIRE(f.project.tracks[0].midiClips.size() == 1);
    const auto bar = f.project.tracks[0].midiClips[0].length;
    CHECK(bar == 4 * 24000); // 4/4 at 120 bpm, 48 kHz

    f.view.setTool(ui::ArrangeView::Tool::select);
    static_cast<juce::Component&>(f.view).mouseDoubleClick(mouseAt(f.view, 1100, f.rowY(0), 2));
    REQUIRE(f.project.tracks[0].midiClips.size() == 2);
    CHECK(f.project.tracks[0].midiClips[1].length == bar);
    CHECK(f.project.tracks[0].midiClips[1].start % bar == 0); // on a bar line
}

TEST_CASE("A tooltip's shortcut is told apart from its text", "[ui][tooltip]")
{
    using Laf = ui::NorthlightLookAndFeel;
    auto parts = Laf::splitShortcut("Redo  Ctrl+Shift+Z");
    CHECK(parts.text == "Redo");
    CHECK(parts.keys == juce::StringArray{"Ctrl", "Shift", "Z"});
    CHECK(Laf::splitShortcut("Select tool: move, trim and select clips  1").keys == juce::StringArray{"1"});
    CHECK(Laf::splitShortcut("Go to start  Enter").keys == juce::StringArray{"Enter"});
    CHECK(Laf::splitShortcut("Mixer  F2").keys == juce::StringArray{"F2"});
    CHECK(Laf::splitShortcut("Snap: Adaptive  N").text == "Snap: Adaptive");
    // Not shortcuts: words, a modifier alone, a label with a middle dot.
    CHECK(Laf::splitShortcut("Volume").keys.isEmpty());
    CHECK(Laf::splitShortcut("Pad  Shift").keys.isEmpty());
    CHECK(Laf::splitShortcut("Notes  pitched").keys.isEmpty());
    CHECK(Laf::splitShortcut(juce::String::fromUTF8("CHIP SYNTH  \xc2\xb7  NORTHLIGHT")).keys.isEmpty());
    CHECK(Laf::splitShortcut("Two words  Ctrl+Click here").keys.isEmpty());
}

TEST_CASE("A tooltip with a shortcut is wide enough for its text and key caps", "[ui][tooltip]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::NorthlightLookAndFeel lookAndFeel(theme);
    const auto plain = lookAndFeel.getTooltipBounds("Redo", {200, 200}, {0, 0, 1920, 1080});
    const auto withKeys = lookAndFeel.getTooltipBounds("Redo  Ctrl+Shift+Z", {200, 200}, {0, 0, 1920, 1080});
    CHECK(withKeys.getWidth() > plain.getWidth() + 3 * 16); // three caps beside the word
    CHECK(withKeys.getHeight() >= plain.getHeight());
}

TEST_CASE("A progress sheet waits out its delay unless hurried, and says when it is fully shown",
          "[ui][motion]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    juce::Component host;
    host.setBounds(0, 0, 800, 600);
    ui::SheetOptions options;
    options.blocking = true;
    options.appearAfterMs = theme.motion.progressDelayMs;
    auto* sheet = ui::Sheet::show(
        host, theme, "Opening Big", {}, {{"Cancel", false, false, true}}, [](int) {}, nullptr, 0, options);
    CHECK(sheet->getAlpha() == Approx(0.0f).margin(0.01f));
    CHECK_FALSE(sheet->hasAppeared());
    // Nothing runs the delay's timer here: only appearNow starts the entrance.
    ui::motion::Tween::finishAllForTesting();
    CHECK_FALSE(sheet->hasAppeared());
    sheet->appearNow();
    ui::motion::Tween::finishAllForTesting();
    CHECK(sheet->hasAppeared());
    CHECK(sheet->getAlpha() == Approx(1.0f));
    // A blocking sheet keeps every key from the window behind.
    CHECK(sheet->keyPressed(juce::KeyPress(' ')));
    delete sheet;
}

namespace
{
// An audio track with two clips cut from one recording (bars 2-3 and 3-4, a
// split undone by the glue) and a MIDI track with a clip on bars 2-3.
struct ToolFixture : DrawFixture
{
    ToolFixture()
    {
        project.tracks[1].clips.push_back({20, "a", "a.wav", 96000, 96000, 0, 1.0f});
        project.tracks[1].clips.push_back({21, "b", "a.wav", 192000, 96000, 96000, 1.0f});
        core::MidiClip clip;
        clip.id = 22;
        clip.start = 96000;
        clip.length = 96000;
        clip.notes = {{23, 60, 100, 0, 1000}};
        project.tracks[0].midiClips.push_back(clip);
        project.nextId = 30;
        view.setProject(&project);
    }
    juce::Point<int> at(int row, double bar) { return view.pointOnRow(row, bar); }
    int rowTop(int row)
    {
        return headerFor(view, project.tracks[static_cast<size_t>(row)].id)->getBounds().getY();
    }
    void click(juce::Point<int> p)
    {
        view.mouseDown(mouseAt(view, p.x, p.y));
        view.mouseUp(mouseAt(view, p.x, p.y));
    }
    void drag(juce::Point<int> from, juce::Point<int> to)
    {
        view.mouseDown(mouseAt(view, from.x, from.y));
        view.mouseDrag(mouseAt(view, to.x, to.y, 1, true));
        view.mouseUp(mouseAt(view, to.x, to.y, 1, true));
    }
};
} // namespace

TEST_CASE("Dragging a clip's fade handle sets its fade, one undo step", "[ui][arrange]")
{
    ToolFixture f;
    const auto clipStart = f.at(1, 2.0);
    const int top = f.rowTop(1) + 3; // the handle at the clip's top left corner
    f.drag({clipStart.x + 2, top}, {f.at(1, 2.5).x, top});
    const auto& clip = *f.project.tracks[1].findClip(20);
    CHECK(clip.fadeIn > 40000); // about half a bar (48000 samples)
    CHECK(clip.fadeIn < 56000);
    CHECK(clip.fadeOut == 0);
    REQUIRE(f.undo.undo(f.project));
    CHECK(f.project.tracks[1].findClip(20)->fadeIn == 0);

    // And the fade out from the top right corner.
    f.view.setProject(&f.project);
    const auto clipEnd = f.at(1, 3.0);
    f.drag({clipEnd.x - 3, top}, {f.at(1, 2.75).x, top});
    CHECK(f.project.tracks[1].findClip(20)->fadeOut > 16000);
    CHECK(f.project.tracks[1].findClip(20)->fadeIn == 0);
}

TEST_CASE("The split tool cuts the clip under the pointer on the grid; Shift cuts every track",
          "[ui][arrange][tools]")
{
    ToolFixture f;
    f.view.setTool(ui::ArrangeView::Tool::split);
    f.click(f.at(1, 2.51)); // snaps to the grid line at 2.5 (a beat at this zoom, at least)
    REQUIRE(f.project.tracks[1].clips.size() == 3);
    CHECK(f.project.tracks[1].clips[0].length + f.project.tracks[1].clips[1].length == 96000);
    CHECK(f.project.tracks[0].midiClips.size() == 1); // the other track is untouched

    // Shift+click: every clip under the line, on every track.
    const auto p = f.at(1, 2.25);
    const auto e = mouseAt(f.view, p.x, p.y);
    const juce::MouseEvent shifted(
        e.source,
        e.position,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier),
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &f.view,
        &f.view,
        e.eventTime,
        e.position,
        e.eventTime,
        1,
        false);
    f.view.mouseDown(shifted);
    f.view.mouseUp(shifted);
    CHECK(f.project.tracks[1].clips.size() == 4);
    CHECK(f.project.tracks[0].midiClips.size() == 2);
}

TEST_CASE("The erase tool deletes the clips a sweep passes over as one undo step", "[ui][arrange][tools]")
{
    ToolFixture f;
    // One edit (one undo step in the app) for the whole sweep.
    std::vector<std::string> edits;
    f.view.onEdit = [&](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    {
        edits.push_back(name);
        for (auto& c : commands)
            f.undo.execute(f.project, std::move(c));
        f.view.setProject(&f.project);
    };
    f.view.setTool(ui::ArrangeView::Tool::erase);
    f.view.mouseDown(mouseAt(f.view, f.at(1, 2.5).x, f.at(1, 2.5).y));
    f.view.mouseDrag(mouseAt(f.view, f.at(1, 3.5).x, f.at(1, 3.5).y, 1, true));
    CHECK(f.project.tracks[1].clips.size() == 2); // nothing goes until the release
    f.view.mouseUp(mouseAt(f.view, f.at(1, 3.5).x, f.at(1, 3.5).y, 1, true));
    CHECK(f.project.tracks[1].clips.empty());
    CHECK(edits == std::vector<std::string>{"Delete clips"});
}

TEST_CASE("The glue tool joins a clip with the next; the mute tool mutes and unmutes", "[ui][arrange][tools]")
{
    ToolFixture f;
    f.view.setTool(ui::ArrangeView::Tool::glue);
    f.click(f.at(1, 2.5));
    REQUIRE(f.project.tracks[1].clips.size() == 1);
    CHECK(f.project.tracks[1].clips[0].length == 192000);

    f.view.setTool(ui::ArrangeView::Tool::mute);
    f.click(f.at(0, 2.5));
    CHECK(f.project.tracks[0].midiClips[0].muted);
    f.click(f.at(0, 2.5));
    CHECK_FALSE(f.project.tracks[0].midiClips[0].muted);
}

TEST_CASE("A click that selects a clip reports the new selection", "[ui][arrange]")
{
    ToolFixture f;
    int reports = 0;
    f.view.onSelectionChanged = [&] { ++reports; };
    f.click(f.at(0, 2.5)); // the MIDI clip
    CHECK(reports == 1);
    REQUIRE(f.view.selection().size() == 1);
    CHECK(f.view.selection().front() == ui::ArrangeView::ClipRef{1, 22, true});
    f.click(f.at(0, 2.5)); // the same clip again: nothing changed
    CHECK(reports == 1);
    f.click(f.at(1, 2.5)); // an audio clip
    CHECK(reports == 2);
}

TEST_CASE("Dragging a fade handle up bends the fade fast, down slow; a small move stays linear",
          "[ui][arrange]")
{
    ToolFixture f;
    const auto clipStart = f.at(1, 2.0);
    const int top = f.rowTop(1) + 3;
    const int x = f.at(1, 2.5).x;
    f.view.mouseDown(mouseAt(f.view, clipStart.x + 2, top));
    f.view.mouseDrag(mouseAt(f.view, x, top - 40, 1, true));
    f.view.mouseUp(mouseAt(f.view, x, top - 40, 1, true));
    CHECK(f.project.tracks[1].findClip(20)->fadeInCurve == Catch::Approx(0.5f));
    CHECK(f.project.tracks[1].findClip(20)->fadeIn > 40000);

    f.view.setProject(&f.project);
    const auto clipEnd = f.at(1, 3.0);
    f.view.mouseDown(mouseAt(f.view, clipEnd.x - 3, top));
    f.view.mouseDrag(mouseAt(f.view, f.at(1, 2.75).x, top + 3, 1, true)); // within the detent
    f.view.mouseUp(mouseAt(f.view, f.at(1, 2.75).x, top + 3, 1, true));
    CHECK(f.project.tracks[1].findClip(20)->fadeOutCurve == 0.0f);
    CHECK(f.project.tracks[1].findClip(20)->fadeOut > 16000);
    CHECK(f.project.tracks[1].findClip(20)->fadeInCurve == Catch::Approx(0.5f)); // untouched
}

TEST_CASE("Every bus and the master have a row in the arrange view, the master last", "[ui][arrange]")
{
    DrawFixture f;
    core::Bus group;
    group.id = 5;
    group.name = "Drums";
    core::Bus fx;
    fx.id = 6;
    fx.name = "Hall";
    fx.fx = true;
    f.project.buses = {group, fx};
    f.view.setProject(&f.project);
    const auto* groupHeader = headerFor(f.view, 5);
    const auto* fxHeader = headerFor(f.view, 6);
    const auto* masterHeader = headerFor(f.view, core::masterId);
    REQUIRE(groupHeader != nullptr);
    REQUIRE(fxHeader != nullptr);
    REQUIRE(masterHeader != nullptr);
    CHECK(groupHeader->getY() < fxHeader->getY());
    CHECK(fxHeader->getY() < masterHeader->getY());
}
