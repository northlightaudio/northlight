// UI work budgets (docs/ui-performance.md): what each card repaints, and the
// words its face sets painting that, when a user hovers a knob, turns one, and
// while it plays. Counted, not timed, so they hold on any machine and run in
// CI. "Measure" prints the numbers for every card ([.measure], not run by default).

#include "UiPerf.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/RenderedCard.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>

using namespace northlight;
using uiperf::CardUnderTest;
using uiperf::coverage;

namespace
{
// The worst of one kind of interaction: the share of the card it repainted, the
// words painting that set, whether it asked for the whole card, and the art
// rebuilds it caused.
struct Worst
{
    double area = 0.0;
    std::uint64_t words = 0;
    bool everything = false;
    std::uint64_t rebuilds = 0;
};

struct Work
{
    bool rendered = false; // a face built from renders
    Worst hover, compactHover, turn, frame;
};

bool hasRenderedFace(juce::Component& card)
{
    for (auto* c : card.getChildren())
        if (dynamic_cast<ui::RenderedFace*>(c) != nullptr)
            return true;
    return false;
}

// Hovers every knob (in and out), turns every knob a tenth of its range and
// back, and plays ten frames, measuring each.
Work measure(const std::string& type)
{
    Work work;
    CardUnderTest t(type);
    if (t.card() == nullptr)
        return work;
    work.rendered = hasRenderedFace(*t.card());
    const auto run = [&](Worst& w, const std::function<void()>& act)
    {
        auto& rec = t.record();
        const auto rebuilds = ui::paintstats::counters().artRebuilds;
        act();
        const auto bounds = t.card()->getLocalBounds();
        const double area = coverage(rec.area, bounds);
        const bool everything = rec.everything;
        const auto region = rec.area;
        w.area = std::max(w.area, area);
        w.everything = w.everything || everything;
        w.words = std::max(w.words, t.paint(region, 1.0f));
        w.rebuilds += ui::paintstats::counters().artRebuilds - rebuilds;
        rec.clear();
    };
    const auto hoverAll = [&](Worst& w)
    {
        for (auto* k : t.knobs())
        {
            run(w, [&] { k->mouseEnter(uiperf::mouseOver(*k)); });
            run(w, [&] { k->mouseExit(uiperf::mouseOver(*k)); });
        }
    };

    t.paintAll(1.0f); // the art at this scale
    hoverAll(work.hover);
    for (auto* k : t.knobs())
    {
        const double v = k->value();
        const auto [lo, hi] = k->accessibleRange();
        run(work.turn, [&] { k->setValue(v + (hi - lo) * 0.1, juce::sendNotification); });
        run(work.turn, [&] { k->setValue(v, juce::sendNotification); });
    }
    for (int i = 0; i < 10; ++i)
        run(work.frame, [&] { t.frame(); });

    // In the Editor dock: a rendered face at its smallest, compact.
    if (work.rendered)
    {
        const int body = t.card()->minimumBodyHeight();
        t.card()->setSize(t.card()->preferredWidthAt(body), body + ui::strip::ModuleCard::headerHeight);
        t.paintAll(1.0f);
        hoverAll(work.compactHover);
    }
    return work;
}
} // namespace

TEST_CASE("Measure: UI work per interaction for every card", "[.measure]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("%-22s %14s %14s %14s %14s\n", "card", "hover", "compact hover", "turn", "frame");
    const auto cell = [](const Worst& w)
    {
        return juce::String(w.area * 100.0, 1) + "% " + juce::String(static_cast<juce::int64>(w.words)) + "w";
    };
    for (const auto& type : uiperf::builtInTypes())
    {
        const auto w = measure(type);
        std::printf("%-22s %14s %14s %14s %14s\n",
                    type.c_str(),
                    cell(w.hover).toRawUTF8(),
                    w.rendered ? cell(w.compactHover).toRawUTF8() : "-",
                    cell(w.turn).toRawUTF8(),
                    cell(w.frame).toRawUTF8());
    }
}

// The budgets (docs/ui-performance.md has the table and how to change one).
TEST_CASE("Every card keeps hovering, turning and playing within its repaint budget", "[ui][perf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto& type : uiperf::builtInTypes())
    {
        INFO(type);
        const auto w = measure(type);

        // Hovering repaints the knob, not the card.
        CHECK(w.hover.area <= 0.06);
        CHECK(w.hover.words <= 30);
        // Turning a knob repaints round it (and, on a card with a display, the
        // display); playing repaints what moves.
        CHECK(w.turn.area <= (w.rendered ? 0.25 : 0.65));
        CHECK(w.turn.words <= 64);
        CHECK(w.frame.area <= (w.rendered ? 0.30 : 0.75));
        CHECK(w.frame.words <= 24);
        if (w.rendered)
        {
            // Compact, hovering shows the knob's value where its name is: that alone.
            CHECK(w.compactHover.area <= 0.12);
            CHECK(w.compactHover.words <= 30);
        }
        // Nothing asks for the whole card, and nothing redraws a face's art.
        for (const auto* x : {&w.hover, &w.compactHover, &w.turn, &w.frame})
        {
            CHECK(!x->everything);
            CHECK(x->rebuilds == 0);
        }
    }
}

TEST_CASE("An undo on a rendered card repaints round the knob that moved, not the card", "[ui][perf]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    for (const auto& type : uiperf::builtInTypes())
    {
        uiperf::CardUnderTest t(type);
        if (t.card() == nullptr || dynamic_cast<ui::strip::RenderedCard*>(t.card()) == nullptr)
            continue;
        INFO(type);
        const auto info = plugins::BuiltInFactory().create(type)->parameters();
        // The first continuous parameter, a tenth of its range away.
        for (size_t i = 0; i < info.size(); ++i)
        {
            if (!info[i].choices.empty())
                continue;
            const float range = info[i].maximum - info[i].minimum;
            auto& rec = t.record();
            t.modelChange(static_cast<int>(i),
                          std::min(info[i].maximum, t.insert().params[i] + range * 0.1f));
            // What the face repaints is its own call (a parameter it prints widely
            // may repaint most of it); the card is never repainted whole.
            CHECK_FALSE(rec.everything);
            break;
        }
    }
}
