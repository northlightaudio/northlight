#include "UiPerf.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"
#include "ui/components/Readout.h"
#include "ui/shell/HeaderBar.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace northlight;

namespace
{
// The readout's fields (bars, time, tempo, signature), left to right.
std::vector<juce::Component*> readoutFields(ui::HeaderBar& bar)
{
    std::vector<juce::Component*> fields;
    for (auto* child : bar.getChildren())
        if (child->isVisible() && (dynamic_cast<ui::TimeDisplay*>(child) != nullptr ||
                                   dynamic_cast<ui::NumericField*>(child) != nullptr ||
                                   dynamic_cast<juce::ComboBox*>(child) != nullptr))
            fields.push_back(child);
    std::sort(fields.begin(), fields.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
    return fields;
}

// The gaps between the fields' text, and from the well's ends to the text.
std::vector<int> textGaps(ui::HeaderBar& bar, const ui::Theme& theme)
{
    const int inset = ui::readout::inset(theme);
    const auto well = bar.readoutBounds();
    std::vector<int> gaps;
    int previousRight = well.getX();
    for (auto* field : readoutFields(bar))
    {
        gaps.push_back(field->getX() + inset - previousRight);
        previousRight = field->getRight() - inset;
    }
    gaps.push_back(well.getRight() - previousRight);
    return gaps;
}

// The lowest row in `area` with a pixel bright enough to be text on the well.
int lowestInkRow(const juce::Image& image, juce::Rectangle<int> area)
{
    for (int y = area.getBottom() - 1; y >= area.getY(); --y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto p = image.getPixelAt(x, y);
            if (std::max({p.getRed(), p.getGreen(), p.getBlue()}) > 0x50)
                return y;
        }
    return -1;
}
} // namespace

TEST_CASE("The transport readout's text sits md from the well's ends and lg apart", "[ui][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::HeaderBar bar(theme);
    bar.setBounds(0, 0, 1600, theme.sizes.headerBar);

    REQUIRE(readoutFields(bar).size() == 4);
    const auto gaps = textGaps(bar, theme);
    CHECK(gaps.front() == theme.spacing.md);
    CHECK(gaps.back() == theme.spacing.md);
    for (size_t i = 1; i + 1 < gaps.size(); ++i)
        CHECK(gaps[i] == theme.spacing.lg);
}

TEST_CASE("A longer position widens the transport readout and keeps its spacing", "[ui][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::HeaderBar bar(theme);
    bar.setBounds(0, 0, 1600, theme.sizes.headerBar);
    const int before = bar.readoutBounds().getWidth();

    auto state = bar.state();
    state.barsText = "128.3.240";
    state.timeText = "4:14.480";
    bar.setState(state);

    const int grown = ui::readout::textWidth(theme.timeLarge(), "128.3.240") -
                      ui::readout::textWidth(theme.timeLarge(), "1.1.000");
    CHECK(grown > 0);
    CHECK(bar.readoutBounds().getWidth() == before + grown);
    const auto gaps = textGaps(bar, theme);
    CHECK(gaps.front() == theme.spacing.md);
    CHECK(gaps.back() == theme.spacing.md);
    for (size_t i = 1; i + 1 < gaps.size(); ++i)
        CHECK(gaps[i] == theme.spacing.lg);
}

TEST_CASE("The transport readout's fields share one baseline", "[ui][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::NorthlightLookAndFeel lookAndFeel(theme);
    ui::HeaderBar bar(theme);
    bar.setLookAndFeel(&lookAndFeel);
    bar.setBounds(0, 0, 1600, theme.sizes.headerBar);
    const auto image = bar.createComponentSnapshot(bar.getLocalBounds(), true, 1.0f);

    const auto fields = readoutFields(bar);
    REQUIRE(fields.size() == 4);
    const int inset = ui::readout::inset(theme);
    // Bars "1.1.000", time "0:00.000" and tempo "120.00 BPM" end on the baseline;
    // of the signature, only its first figure (the slash dips below).
    std::vector<int> bottoms;
    for (size_t i = 0; i < 3; ++i)
        bottoms.push_back(lowestInkRow(image, fields[i]->getBounds()));
    const int figure = ui::readout::textWidth(theme.timeSmall(), "4");
    bottoms.push_back(lowestInkRow(image, fields[3]->getBounds().withTrimmedLeft(inset).withWidth(figure)));
    for (const int bottom : bottoms)
    {
        CHECK(bottom > 0);
        CHECK(std::abs(bottom - bottoms.front()) <= 1);
    }
    bar.setLookAndFeel(nullptr);
}

TEST_CASE("The header repaints nothing when the state it is given has not changed", "[ui][perf][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto theme = ui::Theme::defaults();
    ui::HeaderBar bar(theme);
    bar.setSize(1400, 44);
    bar.setVisible(true);
    auto state = bar.state();
    state.projectName = "Song";
    state.playing = true;
    bar.setState(state);

    auto* recorder = new uiperf::RepaintRecorder();
    recorder->setBounds(bar.getLocalBounds());
    bar.setCachedComponentImage(recorder);
    recorder->clear();           // attaching it asks for a full repaint
    for (int i = 0; i < 30; ++i) // a second of ticks, nothing changing
        bar.setState(state);
    CHECK(recorder->area.isEmpty());

    // A new position repaints the readout alone.
    state.timeText = "0:01.000";
    bar.setState(state);
    CHECK_FALSE(recorder->area.isEmpty());
    CHECK(uiperf::coverage(recorder->area, bar.getLocalBounds()) < 0.2);
    bar.setCachedComponentImage(nullptr);
}

TEST_CASE("The count-in toggle steps Off, 1, 2 and 4 bars, as Preferences offers", "[ui][transport]")
{
    CHECK(ui::HeaderBar::nextCountIn(0) == 1);
    CHECK(ui::HeaderBar::nextCountIn(1) == 2);
    CHECK(ui::HeaderBar::nextCountIn(2) == 4);
    CHECK(ui::HeaderBar::nextCountIn(4) == 0);
    CHECK(ui::HeaderBar::nextCountIn(3) == 4); // a value from elsewhere moves on to the next choice
}

TEST_CASE("While playing, Play is Pause: its icon and tooltip say so", "[ui][transport]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto theme = ui::Theme::defaults();
    ui::HeaderBar bar(theme);
    bar.setSize(1400, 44);
    const auto tooltipOf = [&](const juce::String& name)
    {
        for (auto* child : bar.getChildren())
            if (auto* button = dynamic_cast<ui::IconButton*>(child);
                button != nullptr && button->getName() == name)
                return button->getTooltip();
        return juce::String("(none)");
    };
    auto state = bar.state();
    state.deviceReady = true;
    bar.setState(state);
    CHECK(tooltipOf(ui::Icons::name(ui::Icon::play)) == "Play  Space");
    state.playing = true;
    bar.setState(state);
    CHECK(tooltipOf(ui::Icons::name(ui::Icon::pause)) == "Pause  Shift+Space"); // the pause icon
    CHECK(tooltipOf(ui::Icons::name(ui::Icon::stop)) == "Stop  Space");
}
