// UI paint-time budgets (docs/ui-performance.md): how long each card and the
// mixer take to paint at 200%, whole and for the repaints a user causes most
// (a hover, a live frame). Timed, so not run by default ([.painttime]); CTest
// runs it as the "ui_paint_time" test (label perf) in a Release build only.

#include "UiPerf.h"
#include "core/Command.h"
#include "engine/Meters.h"
#include "ui/MixerView.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace northlight;
using uiperf::CardUnderTest;

namespace
{
// The median of `runs` paints of `c` at `scale`, clipped to `region`, in ms.
double medianPaintMs(juce::Component& c, const juce::RectangleList<int>& region, int runs, float scale = 2.0f)
{
    juce::Image image(juce::Image::ARGB,
                      juce::roundToInt(static_cast<float>(c.getWidth()) * scale),
                      juce::roundToInt(static_cast<float>(c.getHeight()) * scale),
                      true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(scale));
    const auto once = [&]
    {
        g.saveState();
        g.reduceClipRegion(region);
        c.paintEntireComponent(g, false);
        g.restoreState();
    };
    once(); // caches (a face's art) at this scale
    std::vector<double> ms;
    for (int i = 0; i < runs; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        once();
        ms.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}

struct Times
{
    double full = 0.0, hover = 0.0, frame = 0.0;
};

Times timeCard(const std::string& type)
{
    Times out;
    CardUnderTest t(type);
    if (t.card() == nullptr)
        return out;
    auto& card = *t.card();
    out.full = medianPaintMs(card, juce::RectangleList<int>(card.getLocalBounds()), 15);
    // A hover: what entering the busiest-looking knob (the first) repaints.
    const auto knobs = t.knobs();
    if (!knobs.empty())
    {
        auto& rec = t.record();
        knobs.front()->mouseEnter(uiperf::mouseOver(*knobs.front()));
        const auto region = rec.area;
        knobs.front()->mouseExit(uiperf::mouseOver(*knobs.front()));
        out.hover = medianPaintMs(card, region, 31);
    }
    // A live frame: what one display frame repaints.
    {
        auto& rec = t.record();
        t.frame();
        const auto region = rec.area;
        out.frame = region.isEmpty() ? 0.0 : medianPaintMs(card, region, 31);
    }
    return out;
}
} // namespace

TEST_CASE("Every card and the mixer paint within their time budgets", "[.painttime]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("%-22s %9s %9s %9s   (ms at 200%%, median)\n", "surface", "full", "hover", "frame");
    for (const auto& type : uiperf::builtInTypes())
    {
        const auto t = timeCard(type);
        std::printf("%-22s %9.2f %9.2f %9.2f\n", type.c_str(), t.full, t.hover, t.frame);
        INFO(type);
        CHECK(t.full <= 12.0);
        CHECK(t.hover <= 1.5);
        CHECK(t.frame <= 6.0);
    }

    // The mixer with twelve tracks.
    core::Project project;
    for (int i = 0; i < 12; ++i)
    {
        core::Track track;
        track.name = "Track " + std::to_string(i + 1);
        core::AddTrack(track).apply(project);
    }
    rt::MeterBlock meters;
    ui::MixerView mixer(meters, nullptr, nullptr);
    mixer.setBounds(0, 0, 1400, 700);
    mixer.setProject(&project);
    const double full = medianPaintMs(mixer, juce::RectangleList<int>(mixer.getLocalBounds()), 15);
    std::printf("%-22s %9.2f\n", "mixer (12 tracks)", full);
    CHECK(full <= 12.0);
}
