#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/components/Fader.h"
#include "ui/panels/EditorPanel.h"
#include "ui/strip/ModuleCard.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
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

namespace
{
juce::TextButton* buttonNamed(juce::Component& parent, const juce::String& text)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b != nullptr && b->getButtonText() == text)
            return b;
        if (auto* found = buttonNamed(*child, text))
            return found;
    }
    return nullptr;
}

} // namespace

// An insert open in its own window is edited there: its card shows a note and a
// button that brings the window forward, and its controls come back when the
// window closes.
TEST_CASE("An insert popped out into its window shows a note in the Editor panel", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track t;
    t.id = 1;
    t.name = "Gtr";
    core::Insert gain;
    gain.id = 5;
    gain.type = "northlight.gain";
    gain.params = {0.0f};
    t.inserts.push_back(gain);
    project.tracks.push_back(t);
    project.nextId = 6;

    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.setSize(1200, 480);
    bool open = false;
    core::Id shownChannel = core::invalidId, shownInsert = core::invalidId;
    panel.isEditorOpen = [&](core::Id id) { return open && id == 5; };
    panel.onOpenEditor = [&](core::Id channel, core::Id insert)
    {
        shownChannel = channel;
        shownInsert = insert;
    };
    panel.setProject(&project);
    panel.setChannel(1);

    // In the panel: the card's controls, no note.
    auto* show = buttonNamed(panel, "Show window");
    REQUIRE(show != nullptr);
    CHECK(!show->isVisible());
    const auto controlsShowing = [&]
    {
        bool any = false;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (dynamic_cast<ui::strip::ModuleCard*>(child) != nullptr && child->isVisible())
                    any = true;
                walk(*child);
            }
        };
        walk(panel);
        return any;
    };
    CHECK(controlsShowing());

    // Popped out: the note and its button instead of the controls.
    open = true;
    panel.editorWindowsChanged();
    CHECK(show->isVisible());
    CHECK(!controlsShowing());
    show->onClick();
    CHECK(shownChannel == 1);
    CHECK(shownInsert == 5);

    // The window closes: the controls are back.
    open = false;
    panel.editorWindowsChanged();
    CHECK(!show->isVisible());
    CHECK(controlsShowing());
}
