// The shared drawing for rendered hardware faces: images shrunk to the size
// they land at, the fit arithmetic, and RenderedFace's cached art and live words.
#include "ui/Theme.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/Knob.h"
#include "ui/components/RenderedFace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace northlight;

namespace
{
// A face that counts its passes and prints one word.
class CountingFace final : public ui::RenderedFace
{
public:
    CountingFace() : RenderedFace(200.0f, 100.0f) {}
    mutable int artPasses = 0, wordPasses = 0, livePaints = 0;
    int key = 0;

private:
    int cacheKey() const override { return key; }
    void paintStatic(juce::Graphics& g) const override
    {
        if (artPass())
        {
            ++artPasses;
            g.fillAll(juce::Colours::black);
        }
        else
            ++wordPasses;
        g.setColour(juce::Colours::white);
        print(g, "WORD", {0.0f, 0.0f, 200.0f, 100.0f});
    }
    void paintLive(juce::Graphics&) const override { ++livePaints; }
};

int whitePixels(const juce::Image& image)
{
    int n = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            n += image.getPixelAt(x, y).getBrightness() > 0.5f ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE("A rendered part is shrunk to the size it is drawn at, once", "[ui][hardware]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Image source(juce::Image::ARGB, 640, 960, true);
    {
        juce::Graphics g(source);
        g.fillAll(juce::Colours::orange);
    }
    const auto& small = ui::hardware::scaled(source, 101, 152);
    CHECK(small.getWidth() == 101);
    CHECK(small.getHeight() == 152);
    CHECK(small.getPixelAt(50, 76).getRed() > 200); // still orange
    // Asked again, the same pixels come back (kept, not made again).
    CHECK(ui::hardware::scaled(source, 101, 152).getPixelData() == small.getPixelData());
    // Never enlarged: a size at or above the source's gives the source.
    CHECK(ui::hardware::scaled(source, 640, 960).getPixelData() == source.getPixelData());
}

TEST_CASE("A scaled image stays valid while its cache lets older sizes go", "[ui][hardware]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Image source(juce::Image::ARGB, 640, 640, true);
    const auto held = ui::hardware::scaled(source, 300, 300);
    for (int size = 10; size < 600; ++size) // more sizes than the cache keeps
        (void)ui::hardware::scaled(source, size, size);
    CHECK(held.isValid());
    CHECK(held.getWidth() == 300);
}

TEST_CASE("A face fits its body by scaling, never above its drawn size", "[ui][hardware]")
{
    CHECK(ui::hardware::fitScale(1160.0f, 600.0f, 1160.0f, 600.0f) == 1.0f);
    CHECK(ui::hardware::fitScale(2000.0f, 900.0f, 1160.0f, 600.0f) == 1.0f);
    CHECK(ui::hardware::fitScale(1160.0f, 300.0f, 1160.0f, 600.0f) == Catch::Approx(0.5f));
    CHECK(ui::hardware::fitScale(0.0f, 300.0f, 1160.0f, 600.0f) == 0.0f);
    CHECK(ui::hardware::widthForHeight(300, 1160.0f, 600.0f) == 580);
    CHECK(ui::hardware::widthForHeight(900, 1160.0f, 600.0f) == 1160);
    const auto p = ui::hardware::polar({10.0f, 10.0f}, 5.0f, 90.0f); // 3 o'clock
    CHECK(p.x == Catch::Approx(15.0f));
    CHECK(p.y == Catch::Approx(10.0f).margin(1.0e-4));
}

TEST_CASE("A rendered face caches its art and prints its words live", "[ui][hardware]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    CountingFace face;
    juce::Image screen(juce::Image::RGB, 200, 100, true);
    {
        juce::Graphics g(screen);
        face.paintEntireComponent(g, false);
    }
    CHECK(face.artPasses == 1);
    CHECK(face.wordPasses == 1);
    CHECK(face.livePaints == 1);
    CHECK(whitePixels(screen) > 0); // the word reached the screen

    // Painted again: the art comes from the cache, the words are drawn again.
    {
        juce::Graphics g(screen);
        face.paintEntireComponent(g, false);
    }
    CHECK(face.artPasses == 1);
    CHECK(face.wordPasses == 2);

    // A new cache key or invalidate() draws the art again.
    face.key = 1;
    {
        juce::Graphics g(screen);
        face.paintEntireComponent(g, false);
    }
    CHECK(face.artPasses == 2);
    face.invalidate();
    {
        juce::Graphics g(screen);
        face.paintEntireComponent(g, false);
    }
    CHECK(face.artPasses == 3);
}

TEST_CASE("A rendered face placed in a smaller body scales and centres", "[ui][hardware]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    CountingFace face;
    face.placeIn({0, 0, 300, 50});
    CHECK(face.isVisible());
    CHECK(face.getTransform().mat00 == Catch::Approx(0.5f));
    CHECK(face.getTransform().mat02 == Catch::Approx(100.0f)); // (300 - 100) / 2
    CHECK(face.widthForHeight(50) == 100);
    face.placeIn({0, 0, 0, 0});
    CHECK_FALSE(face.isVisible());
}

TEST_CASE("A rendered face goes compact below two thirds of its size", "[ui][hardware]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    CountingFace face;
    face.placeIn({0, 0, 200, 100});
    CHECK_FALSE(face.isCompact());
    face.placeIn({0, 0, 200, 70}); // 70%
    CHECK_FALSE(face.isCompact());
    face.placeIn({0, 0, 200, 60}); // 60%
    CHECK(face.isCompact());
    face.placeIn({0, 0, 200, 100});
    CHECK_FALSE(face.isCompact());
    face.setActiveControl(4);
    CHECK(face.activeControl() == 4);
}

TEST_CASE("A knob's watchers hear it after its owner, whenever either was set", "[ui][knob]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::Knob knob(ui::Theme::dark());
    knob.setRange(0.0, 10.0, 5.0);
    std::string heard;
    // A watcher first (as a face places a knob), then the owner's callbacks.
    ui::Knob::Watcher watcher;
    watcher.dragStart = [&] { heard += "W<"; };
    watcher.valueChanged = [&](double) { heard += "Wv"; };
    watcher.dragEnd = [&] { heard += "W>"; };
    knob.addWatcher(std::move(watcher));
    knob.onDragStart = [&] { heard += "O<"; };
    knob.onValueChanged = [&](double) { heard += "Ov"; };
    knob.onDragEnd = [&] { heard += "O>"; };

    knob.setValue(2.0, juce::dontSendNotification);
    knob.mouseDoubleClick(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                           {},
                                           {},
                                           0.0f,
                                           0.0f,
                                           0.0f,
                                           0.0f,
                                           0.0f,
                                           &knob,
                                           &knob,
                                           juce::Time(),
                                           {},
                                           juce::Time(),
                                           2,
                                           false)); // back to its default: one gesture
    CHECK(heard == "O<W<OvWvO>W>");
    CHECK(knob.value() == 5.0);
}
