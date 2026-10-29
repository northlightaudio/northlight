// The keyboard model (interaction.md 10.4, "Focus model"; accessibility-performance.md
// 11.2): the main window's Tab cycle, which track header is a Tab stop, and where
// Shift+F10 / the Menu key open a menu.
#include "audio/PeakStore.h"
#include "core/Model.h"
#include "ui/ArrangeView.h"
#include "ui/KeyboardMenu.h"
#include "ui/TrackHeader.h"
#include "ui/shell/FocusCycle.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace northlight;

namespace
{
struct Stop final : juce::Component
{
    Stop() { setWantsKeyboardFocus(true); }
};

struct MenuOwner final : juce::Component, ui::KeyboardMenu
{
    int opened = 0;
    bool has = true;
    bool showKeyboardMenu() override
    {
        ++opened;
        return has;
    }
};
} // namespace

TEST_CASE("The Tab cycle visits a region after its own controls, skips what it is told and wraps",
          "[ui][keys][focus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component window;
    Stop header, region, inner, hidden, dock;
    window.setBounds(0, 0, 400, 300);
    for (auto* c : {&header, &region, &dock})
        window.addAndMakeVisible(c);
    region.addAndMakeVisible(inner);
    region.addAndMakeVisible(hidden);
    header.setBounds(0, 0, 400, 20);
    region.setBounds(0, 20, 400, 200);
    inner.setBounds(0, 0, 100, 20);
    hidden.setBounds(0, 30, 100, 20);
    dock.setBounds(0, 220, 400, 80);
    // The explicit order wins over position: the dock comes before the region.
    header.setExplicitFocusOrder(1);
    dock.setExplicitFocusOrder(2);
    region.setExplicitFocusOrder(3);

    ui::FocusCycle cycle(window, {{&region}, [&](const juce::Component& c) { return &c == &hidden; }});
    CHECK(cycle.getAllComponents(&window) == std::vector<juce::Component*>{&header, &dock, &inner, &region});
    CHECK(cycle.getNextComponent(&header) == &dock);
    CHECK(cycle.getNextComponent(&inner) == &region);
    CHECK(cycle.getNextComponent(&region) == &header); // wraps
    CHECK(cycle.getPreviousComponent(&header) == &region);
    CHECK(cycle.getDefaultComponent(&window) == &header);
    // From a skipped component (a click focused it), the nearest stop on.
    CHECK(cycle.getNextComponent(&hidden) == &region);
    // From the window itself, the first stop.
    CHECK(cycle.getNextComponent(&window) == &header);
}

TEST_CASE("Only the selected track's header is a Tab stop", "[ui][keys][focus][arrange]")
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
    std::vector<ui::TrackHeader*> headers;
    for (auto* child : view.getChildren())
        if (auto* header = dynamic_cast<ui::TrackHeader*>(child))
            headers.push_back(header);
    REQUIRE(headers.size() == 3); // the two tracks', then the master's
    CHECK(view.getWantsKeyboardFocus());
    CHECK(headers[0]->getWantsKeyboardFocus());

    // With none selected, the first header is the stop.
    CHECK_FALSE(view.skippedByTab(*headers[0]));
    CHECK(view.skippedByTab(*headers[1]));
    view.setSelectedTrack(2);
    CHECK(view.skippedByTab(*headers[0]));
    CHECK_FALSE(view.skippedByTab(*headers[1]));
    // The header's controls go with it; the lanes are never skipped.
    for (auto* control : headers[0]->getChildren())
        CHECK(view.skippedByTab(*control));
    CHECK_FALSE(view.skippedByTab(view));
}

TEST_CASE("The menu key opens the nearest menu up from the focus, else the fallback's", "[ui][keys]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MenuOwner owner, fallback;
    juce::Component child, stray;
    owner.addChildComponent(child);

    CHECK(ui::KeyboardMenu::showFor(&child, &fallback));
    CHECK(owner.opened == 1);
    CHECK(fallback.opened == 0);

    // Nothing up from the focus has a menu: the fallback's.
    CHECK(ui::KeyboardMenu::showFor(&stray, &fallback));
    CHECK(fallback.opened == 1);
    // A view with nothing to show passes the key on.
    owner.has = false;
    fallback.has = false;
    CHECK_FALSE(ui::KeyboardMenu::showFor(&child, &fallback));
    CHECK_FALSE(ui::KeyboardMenu::showFor(nullptr, nullptr));
}
