#include "ui/Theme.h"
#include "ui/components/Tooltips.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

TEST_CASE("Tooltips keep the app's scale whatever the control under the mouse is scaled by", "[ui][tooltips]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::Tooltips tips(ui::Theme::dark());
    CHECK(tips.getDesktopScaleFactor() == juce::Desktop::getInstance().getGlobalScaleFactor());
}
