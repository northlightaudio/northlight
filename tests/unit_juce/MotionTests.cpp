#include "audio/PeakStore.h"
#include "core/Model.h"
#include "ui/ArrangeView.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/Motion.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"
#include "ui/components/Sheet.h"
#include "ui/components/Toast.h"
#include "ui/panels/EditorPanel.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace aural;
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
    ui::AuralLookAndFeel lookAndFeel(theme);
    juce::Component window;
    window.setBounds(100, 200, 160, 120);
    lookAndFeel.preparePopupMenuWindow(window);
    CHECK(window.getAlpha() == Approx(0.0f).margin(0.01f));
    CHECK(window.getY() == 198);
    ui::motion::Tween::finishAllForTesting();
    CHECK(window.getAlpha() == Approx(1.0f));
    CHECK(window.getPosition() == juce::Point<int>(100, 200));

    theme.motion.reduceMotion = true;
    ui::AuralLookAndFeel still(theme);
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
    gain.type = "aural.gain";
    gain.params = {0.0f};
    p.tracks[0].inserts.push_back(gain);
    panel.setProject(&p);
    CHECK(fadingComponents(panel) > 0); // the new card
    ui::motion::Tween::finishAllForTesting();
    CHECK(fadingComponents(panel) == 0);

    panel.setChannel(2);
    CHECK(fadingComponents(panel) == 0);
}
