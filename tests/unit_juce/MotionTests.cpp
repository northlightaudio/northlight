#include "ui/AuralLookAndFeel.h"
#include "ui/Motion.h"
#include "ui/Theme.h"

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
