#include "core/Model.h"
#include "ui/InputChoices.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace northlight;

namespace
{
std::vector<ui::InputChoice> choices()
{
    return {{"Vocals", "In 1 + In 2", -1, "Vocals"}, {"In 1", {}, 0, {}}, {"In 2", {}, 1, {}}};
}
} // namespace

TEST_CASE("The input menu selects a track's bus or channel and hands back what was picked", "[ui][inputs]")
{
    juce::ComboBox box;
    const auto list = choices();
    core::Track track;

    track.inputBus = "Vocals";
    ui::inputs::fill(box, list, track);
    CHECK(box.getText() == "Vocals");
    CHECK(box.getTooltip().contains("In 1 + In 2"));

    track.inputBus.clear();
    track.inputChannel = 1;
    ui::inputs::fill(box, list, track);
    CHECK(box.getText() == "In 2");

    track.inputChannel = -1;
    ui::inputs::fill(box, list, track);
    CHECK(box.getText() == "No input");
    const auto none = ui::inputs::selected(box, list);
    REQUIRE(none.has_value());
    CHECK(none->channel == -1);
    CHECK(none->bus.empty());

    box.setSelectedId(ui::inputs::firstChoiceId, juce::dontSendNotification);
    const auto bus = ui::inputs::selected(box, list);
    REQUIRE(bus.has_value());
    CHECK(bus->bus == "Vocals");
}

TEST_CASE("A bus the device does not offer stays in the input menu, marked missing", "[ui][inputs]")
{
    juce::ComboBox box;
    const auto list = choices();
    core::Track track;
    track.inputBus = "Drum overheads";
    ui::inputs::fill(box, list, track);
    CHECK(box.getText() == "Drum overheads (missing)");
    CHECK(box.getTooltip().contains("Audio Setup"));
    // Choosing it again changes nothing.
    CHECK_FALSE(ui::inputs::selected(box, list).has_value());
}
