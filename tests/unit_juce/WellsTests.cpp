#include "ui/Theme.h"
#include "ui/Wells.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>

using namespace northlight;

namespace
{
juce::Image framed(bool cached, float scale)
{
    const auto theme = ui::Theme::defaults();
    juce::Image image(juce::Image::ARGB, juce::roundToInt(200 * scale), juce::roundToInt(100 * scale), true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    g.fillAll(theme.colours.surface1);
    g.setColour(theme.colours.accent);
    g.fillRect(30, 30, 120, 40); // content under the frame
    const juce::Rectangle<float> area(20.0f, 20.0f, 160.0f, 60.0f);
    if (cached)
        ui::drawWellFrame(g, theme, area);
    else
        ui::paintWellFrame(g, theme, area);
    return image;
}
} // namespace

TEST_CASE("A well's frame drawn from its cache matches the frame drawn directly", "[ui][wells]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    for (const float scale : {1.0f, 2.0f})
    {
        INFO(scale);
        const auto direct = framed(false, scale);
        const auto cached = framed(true, scale);
        int worst = 0;
        for (int y = 0; y < direct.getHeight(); ++y)
            for (int x = 0; x < direct.getWidth(); ++x)
            {
                const auto a = direct.getPixelAt(x, y), b = cached.getPixelAt(x, y);
                worst = std::max({worst,
                                  std::abs(a.getRed() - b.getRed()),
                                  std::abs(a.getGreen() - b.getGreen()),
                                  std::abs(a.getBlue() - b.getBlue())});
            }
        CHECK(worst <= 3); // rounding in the alpha compositing only
    }
}

TEST_CASE("A well's frame is drawn into its cache once per size and scale", "[ui][wells][perf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    (void)framed(true, 1.5f);
    const int before = ui::wellFrameRenders();
    for (int i = 0; i < 10; ++i)
        (void)framed(true, 1.5f);
    CHECK(ui::wellFrameRenders() == before);
}
