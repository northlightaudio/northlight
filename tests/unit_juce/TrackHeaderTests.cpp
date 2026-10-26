#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace northlight;

namespace
{
juce::ComboBox* outputSelect(ui::TrackHeader& header)
{
    for (auto* child : header.getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(child); box != nullptr && box->getTooltip() == "Output")
            return box;
    return nullptr;
}
} // namespace

TEST_CASE("A tall header's output select fits its name, and the sends summary shows only when whole",
          "[ui][arrange]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ui::Theme theme = ui::Theme::defaults();
    core::Project project;
    core::Track track;
    track.id = 1;
    track.name = "Vocal";
    track.sends.push_back(core::Send{});
    project.tracks.push_back(track);
    ui::TrackHeader header(theme);
    header.setTrack(project.tracks[0], 0, project, {}, {});

    for (const int width : {theme.sizes.trackHeaderMinWidth, theme.sizes.trackHeaderWidth})
    {
        INFO("header width " << width);
        header.setSize(width, theme.sizes.trackRowLarge);
        const auto* output = outputSelect(header);
        REQUIRE(output != nullptr);
        REQUIRE(output->isVisible());
        const int textW = juce::GlyphArrangement::getStringWidthInt(theme.body(), output->getText());
        // Room for the name, its padding and the chevron: never an ellipsis.
        CHECK(output->getWidth() >= textW + 2 * theme.spacing.sm + theme.sizes.iconSmall);
        CHECK(output->getRight() <= header.getWidth());
        const auto summary = header.sendsSummaryBounds();
        if (!summary.isEmpty())
        {
            CHECK(summary.getX() > output->getRight());
            CHECK(summary.getRight() <= header.getWidth());
        }
    }
    // The default width has room for "1 send"; the narrowest does not.
    header.setSize(theme.sizes.trackHeaderWidth, theme.sizes.trackRowLarge);
    CHECK_FALSE(header.sendsSummaryBounds().isEmpty());
    header.setSize(theme.sizes.trackHeaderMinWidth, theme.sizes.trackRowLarge);
    CHECK(header.sendsSummaryBounds().isEmpty());
}

TEST_CASE("A narrow header keeps the name and a whole input select, with no control overlapping another",
          "[ui][arrange]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ui::Theme theme = ui::Theme::defaults();
    core::Project project;
    core::Track audio;
    audio.id = 1;
    audio.name = "Vocal";
    core::Track midi;
    midi.id = 2;
    midi.name = "Keys";
    midi.kind = core::TrackKind::midi;
    project.tracks = {audio, midi};

    for (const auto& track : project.tracks)
        for (const int height : {theme.sizes.trackRow, theme.sizes.trackRowLarge})
            for (int width = theme.sizes.trackHeaderMinWidth; width <= theme.sizes.trackHeaderMaxWidth;
                 width += 20)
            {
                INFO(track.name << " at " << width << " x " << height);
                ui::TrackHeader header(theme);
                header.setTrack(track, 0, project, {}, {});
                header.setSize(width, height);
                std::vector<juce::Component*> shown;
                const juce::Label* name = nullptr;
                const juce::ComboBox* input = nullptr;
                for (auto* child : header.getChildren())
                {
                    if (!child->isVisible() || child->getBounds().isEmpty())
                        continue;
                    shown.push_back(child);
                    if (auto* label = dynamic_cast<juce::Label*>(child))
                        name = label;
                    if (auto* box = dynamic_cast<juce::ComboBox*>(child);
                        box != nullptr && box->getTooltip() == "Input")
                        input = box;
                }
                REQUIRE(name != nullptr);
                CHECK(name->getWidth() >= 56);
                REQUIRE(input != nullptr);
                const int textW = juce::GlyphArrangement::getStringWidthInt(theme.body(), input->getText());
                CHECK(input->getWidth() >= textW + 2 * theme.spacing.sm + theme.sizes.iconSmall);
                for (size_t i = 0; i < shown.size(); ++i)
                {
                    CHECK(header.getLocalBounds().contains(shown[i]->getBounds()));
                    for (size_t j = i + 1; j < shown.size(); ++j)
                        CHECK_FALSE(shown[i]->getBounds().intersects(shown[j]->getBounds()));
                }
            }
}
