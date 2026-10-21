#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/components/Fader.h"
#include "ui/panels/EditorPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace northlight;

namespace
{
void findFaders(juce::Component& parent, std::vector<ui::Fader*>& out)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* fader = dynamic_cast<ui::Fader*>(child))
            out.push_back(fader);
        findFaders(*child, out);
    }
}
} // namespace

// Two channels that look alike (no inserts, no strip, a send to the same FX
// track): switching between them must still aim the editor's controls at the
// one now shown.
TEST_CASE("The Editor panel follows a switch between look-alike channels", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    for (core::Id id : {1, 2})
    {
        core::Track t;
        t.id = id;
        t.name = id == 1 ? "One" : "Two";
        t.sends.push_back({9, 0.5f, false});
        project.tracks.push_back(t);
    }
    core::Bus fx;
    fx.id = 9;
    fx.name = "Hall";
    fx.fx = true;
    project.buses.push_back(fx);
    project.nextId = 10;

    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.setSize(900, 300);
    std::unique_ptr<core::Command> last;
    panel.onCommand = [&](std::unique_ptr<core::Command> c) { last = std::move(c); };
    panel.setProject(&project);

    for (core::Id id : {core::Id{1}, core::Id{2}, core::Id{1}})
    {
        panel.setChannel(id);
        CHECK(panel.channelId() == id);
        std::vector<ui::Fader*> faders;
        findFaders(panel, faders);
        REQUIRE(faders.size() == 1); // the one send
        REQUIRE(faders[0]->onGainChanged);
        faders[0]->onGainChanged(0.25f);
        REQUIRE(last != nullptr);
        auto copy = project;
        last->apply(copy);
        CHECK(copy.findTrack(id)->sends[0].level == 0.25f);
        CHECK(copy.findTrack(id == 1 ? 2 : 1)->sends[0].level == 0.5f);
    }
}
