// Screen reader names, roles and values, and keyboard focus behaviour
// (docs/design/accessibility-performance.md 11.2 and 11.3). Handlers are made
// directly: JUCE only hands them out for components on the desktop.
#include "core/Model.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"
#include "ui/components/Accessibility.h"
#include "ui/components/Fader.h"
#include "ui/components/Knob.h"
#include "ui/components/Meter.h"
#include "ui/components/Sheet.h"
#include "ui/shell/HeaderBar.h"
#include "ui/strip/FaceParts.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
std::unique_ptr<juce::AccessibilityHandler> handlerOf(juce::Component& component)
{
    return component.createAccessibilityHandler();
}

juce::Component* childTitled(juce::Component& root, const juce::String& title)
{
    for (auto* child : root.getChildren())
        if (child->getTitle() == title)
            return child;
    return nullptr;
}
} // namespace

TEST_CASE("A button whose state comes from the model reads as a checkable toggle", "[ui][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::IconButton loop(theme, ui::Icon::loop, "Loop  L");
    CHECK_FALSE(handlerOf(loop)->getCurrentState().isCheckable()); // a plain button until shown as a toggle

    ui::showToggle(loop, true);
    auto handler = handlerOf(loop);
    CHECK(handler->getActions().contains(juce::AccessibilityActionType::toggle)); // UIA's toggle pattern
    CHECK(handler->getCurrentState().isCheckable());
    CHECK(handler->getCurrentState().isChecked());
    CHECK(handler->getTitle() == "Loop");

    ui::showToggle(loop, false);
    CHECK_FALSE(handlerOf(loop)->getCurrentState().isChecked());
}

TEST_CASE("Faders and knobs take their spoken name from their title, else their tooltip",
          "[ui][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::Fader fader(theme);
    fader.setTooltip("Gain  (double-click for 0 dB)");
    CHECK(handlerOf(fader)->getTitle() == "Gain");
    fader.setTitle("Gain Bass");
    CHECK(handlerOf(fader)->getTitle() == "Gain Bass");
    CHECK(handlerOf(fader)->getHelp() == "Gain  (double-click for 0 dB)");

    ui::Knob knob(theme);
    knob.setTooltip("Pan");
    CHECK(handlerOf(knob)->getTitle() == "Pan");
    CHECK(handlerOf(knob)->getHelp().isEmpty()); // no repeat of the name

    ui::Meter meter(theme, 2, false);
    CHECK(handlerOf(meter)->getTitle() == "Level");
}

TEST_CASE("A track header is a group whose controls name their track", "[ui][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ui::Theme theme = ui::Theme::defaults();
    core::Project project;
    core::Track track;
    track.id = 1;
    track.name = "Bass";
    project.tracks.push_back(track);
    ui::TrackHeader header(theme);
    header.setTrack(project.tracks[0], 0, project, {}, {});

    CHECK(handlerOf(header)->getRole() == juce::AccessibilityRole::group);
    CHECK(header.getTitle() == "Track 1: Bass");
    for (const auto* name : {"Mute Bass", "Solo Bass", "Arm Bass", "Pan Bass", "Level Bass", "Volume Bass"})
    {
        INFO(name);
        CHECK(childTitled(header, name) != nullptr);
    }
    auto* mute = childTitled(header, "Mute Bass");
    REQUIRE(mute != nullptr);
    CHECK(handlerOf(*mute)->getCurrentState().isCheckable());
}

TEST_CASE("The position counters read their text and start typing when pressed", "[ui][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::TimeDisplay bars(theme, true);
    bars.setTitle("Position in bars and beats");
    bars.setText("12.3.240");
    auto handler = handlerOf(bars);
    CHECK(handler->getRole() == juce::AccessibilityRole::staticText);
    REQUIRE(handler->getValueInterface() != nullptr);
    CHECK(handler->getValueInterface()->isReadOnly());
    CHECK(handler->getValueInterface()->getCurrentValueAsString() == "12.3.240");
    CHECK(handler->getActions().contains(juce::AccessibilityActionType::press));
}

TEST_CASE("A VU needle reads its reading as read-only text", "[ui][accessibility]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::strip::VuNeedleMeter vu(theme, {});
    vu.setReading(-3.0f);
    auto handler = handlerOf(vu);
    CHECK(handler->getTitle() == "VU meter");
    REQUIRE(handler->getValueInterface() != nullptr);
    CHECK(handler->getValueInterface()->isReadOnly());
    CHECK(handler->getValueInterface()->getCurrentValueAsString() == "-3.0 VU");
}

TEST_CASE("A sheet keeps Tab to itself and is named by its title", "[ui][accessibility][sheet]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    juce::Component host;
    host.setBounds(0, 0, 800, 600);
    for (const bool blocking : {false, true})
    {
        INFO("blocking " << blocking);
        ui::SheetOptions options;
        options.blocking = blocking;
        auto* sheet = ui::Sheet::show(
            host,
            theme,
            "Remove bus?",
            "Tracks routed to it go to the master.",
            {{"Cancel"}, {"Remove", false, true}},
            [](int) {},
            nullptr,
            0,
            options);
        CHECK(sheet->getTitle() == "Remove bus?");
        CHECK(sheet->isKeyboardFocusContainer());
        // Tab moves among the sheet's buttons and never falls through to the window.
        CHECK(sheet->keyPressed(juce::KeyPress(juce::KeyPress::tabKey)));
        CHECK(
            sheet->keyPressed(juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0)));
        delete sheet;
    }
}
