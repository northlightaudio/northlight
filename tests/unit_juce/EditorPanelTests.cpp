#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "plugins/BuiltIns.h"
#include "ui/Theme.h"
#include "ui/components/Fader.h"
#include "ui/panels/EditorPanel.h"
#include "ui/strip/ModuleCard.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

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

// A round "+" sits in each gap between two insert cards, out of sight until the
// pointer is in that gap, and adds at that position.
TEST_CASE("The Editor panel shows an add button in the gap the pointer is in", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track t;
    t.id = 1;
    t.name = "Vox";
    core::Id next = 5;
    for (const char* type : {"northlight.gain", "northlight.eq", "northlight.limiter"})
    {
        core::Insert insert;
        insert.id = next++;
        insert.type = type;
        insert.params = plugins::defaultParameters(type);
        t.inserts.push_back(insert);
    }
    project.tracks.push_back(t);
    project.nextId = next;

    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.displayName = [](const std::string& type) { return type.substr(type.find('.') + 1); };
    panel.setSize(1600, 480);
    panel.setProject(&project);
    panel.setChannel(1);

    const auto& gaps = panel.gapButtons();
    REQUIRE(gaps.size() == 2); // between three cards; the "+" card adds at the end
    CHECK(gaps[0]->index() == 1);
    CHECK(gaps[1]->index() == 2);
    CHECK(gaps[0]->getTooltip() == "Add an insert between gain and eq");
    CHECK(gaps[1]->getTooltip() == "Add an insert between eq and limiter");
    // At rest none shows, and none takes a click.
    for (const auto& gap : gaps)
    {
        CHECK_FALSE(gap->isShown());
        CHECK_FALSE(gap->hitTest(gap->getWidth() / 2, gap->getHeight() / 2));
    }
    // The pointer in the second gap shows that one only.
    panel.pointerAt(gaps[1]->getBounds().getCentre());
    CHECK_FALSE(gaps[0]->isShown());
    CHECK(gaps[1]->isShown());
    CHECK(gaps[1]->hitTest(gaps[1]->getWidth() / 2, gaps[1]->getHeight() / 2));
    // Up the gap, level with the cards' headers, it still shows.
    panel.pointerAt(gaps[1]->getBounds().getCentre().withY(gaps[1]->getY() - 100));
    CHECK(gaps[1]->isShown());
    // Well inside a card, or gone from the chain: none.
    panel.pointerAt(gaps[1]->getBounds().getCentre().translated(-60, 0));
    CHECK_FALSE(gaps[1]->isShown());
    panel.pointerAt(gaps[0]->getBounds().getCentre());
    CHECK(gaps[0]->isShown());
    panel.pointerAt(std::nullopt);
    CHECK_FALSE(gaps[0]->isShown());
}

namespace
{
// A hosted plugin with its own GUI, as far as the card can tell: a VST3 type
// with a live instance that reports some latency.
class FakePlugin final : public engine::InsertProcessor
{
public:
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return params_; }
    void prepare(double, int) override {}
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    void process(float*, float*, int) noexcept override {}
    int latencySamples() const noexcept override { return 31; }

private:
    std::string type_ = "vst3:Fake";
    std::vector<engine::ParameterInfo> params_;
};

core::Project hostedProject()
{
    core::Project project;
    core::Track t;
    t.id = 1;
    t.name = "Vox";
    core::Insert plugin;
    plugin.id = 5;
    plugin.type = "vst3:Fake";
    t.inserts.push_back(plugin);
    project.tracks.push_back(t);
    project.nextId = 6;
    return project;
}
} // namespace

// A hosted plugin with its own window shows a faceplate: Open editor opens the
// window, and popped out the same place brings it forward, at the same width.
TEST_CASE("A hosted plugin's card opens its window and keeps its width popped out", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    auto project = hostedProject();
    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.setSize(1200, 320);
    const auto live = std::make_shared<FakePlugin>();
    panel.liveInsert = [&](core::Id id)
    { return id == 5 ? std::shared_ptr<engine::InsertProcessor>(live) : nullptr; };
    panel.displayName = [](const std::string&) { return std::string("Fake Channel Strip"); };
    bool popped = false;
    panel.isEditorOpen = [&](core::Id id) { return popped && id == 5; };
    core::Id opened = core::invalidId;
    panel.onOpenEditor = [&](core::Id, core::Id insert) { opened = insert; };
    panel.setProject(&project);
    panel.setChannel(1);

    auto* open = buttonNamed(panel, "Open editor");
    auto* show = buttonNamed(panel, "Show window");
    REQUIRE(open != nullptr);
    REQUIRE(show != nullptr);
    CHECK(open->isVisible());
    CHECK(!show->isVisible());
    auto* card = open->getParentComponent();
    REQUIRE(card != nullptr);
    const int width = card->getWidth();
    CHECK(width == theme.sizes.controlLarge * 7);
    // The button sits inside the card, under its header and above its Mix.
    CHECK(card->getLocalBounds().contains(open->getBounds()));
    CHECK(open->getY() > theme.sizes.control);
    open->onClick();
    CHECK(opened == 5);

    popped = true;
    panel.editorWindowsChanged();
    CHECK(!open->isVisible());
    CHECK(show->isVisible());
    CHECK(show->getBounds() == open->getBounds());
    CHECK(card->getWidth() == width);
}

// A plugin that is not installed shows its faceplate without a button to open.
TEST_CASE("A missing plugin's card offers nothing to open", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    auto project = hostedProject();
    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.setSize(1200, 320);
    panel.liveInsert = [](core::Id) { return std::shared_ptr<engine::InsertProcessor>(); };
    panel.setProject(&project);
    panel.setChannel(1);
    auto* open = buttonNamed(panel, "Open editor");
    REQUIRE(open != nullptr);
    CHECK(!open->isVisible());
    CHECK(open->getParentComponent()->getWidth() == theme.sizes.controlLarge * 7);
}
