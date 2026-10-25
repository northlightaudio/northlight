#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/ParamBinder.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;
namespace pc = plugins::strip::compressor;

TEST_CASE("A card's values hold their ranges and take only the model's changes", "[ui][strip]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& type = plugins::strip::types::compressor;
    auto model = plugins::defaultParameters(type);
    ui::strip::ParamBinder binder(type, model);
    ui::Knob knob(ui::Theme::dark());
    const auto& info = binder.info(pc::threshold);
    knob.setRange(info.minimum, info.maximum, info.defaultValue);
    binder.bind({.index = pc::threshold, .knob = &knob});

    // An edit is held to its range and sets its control, quietly.
    CHECK(binder.set(pc::threshold, -1000.0f));
    CHECK(binder.value(pc::threshold) == info.minimum);
    CHECK(knob.value() == static_cast<double>(info.minimum));
    CHECK_FALSE(binder.set(pc::threshold, info.minimum)); // already there
    CHECK_FALSE(binder.updating());

    // The model's value comes in once; repeating it leaves what the card shows
    // (automation may have moved it since).
    model[pc::threshold] = -30.0f;
    CHECK(binder.takeModel(model));
    CHECK(binder.value(pc::threshold) == -30.0f);
    CHECK(binder.set(pc::threshold, -12.0f));
    CHECK_FALSE(binder.takeModel(model));
    CHECK(binder.value(pc::threshold) == -12.0f);

    // A whole set at once, each held to its range.
    auto preset = model;
    preset[pc::ratio] = 1.0e6f;
    CHECK(binder.setAll(preset));
    CHECK(binder.value(pc::ratio) == binder.info(pc::ratio).maximum);
    CHECK(knob.value() == -30.0);
}
