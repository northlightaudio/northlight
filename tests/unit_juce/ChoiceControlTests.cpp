#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Vesper.h"
#include "ui/InsertEditor.h"
#include "ui/Theme.h"
#include "ui/components/ChoiceControl.h"
#include "ui/panels/EditorPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace northlight;
namespace vesper = plugins::vesper;

namespace
{
template <typename T> void findAll(juce::Component& parent, std::vector<T*>& out)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* t = dynamic_cast<T*>(child))
            out.push_back(t);
        findAll(*child, out);
    }
}

// How many of Vesper's parameters are stepped (Space, Auto pre-delay, the syncs,
// Lookahead, Catch, ...): each gets a choice control.
size_t vesperChoices()
{
    size_t n = 0;
    for (const auto& info : plugins::parameterInfo(vesper::type))
        n += info.choices.empty() ? 0 : 1;
    return n;
}
// A GUI-less hosted plugin with Vesper's parameters (stepped ones among them):
// what the Editor panel shows as a generic grid.
class FakeClap final : public engine::InsertProcessor
{
public:
    FakeClap() : infos_(plugins::parameterInfo(vesper::type)), values_(infos_.size(), 0.0f) {}
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return infos_; }
    void prepare(double, int) override {}
    void setParameter(int index, float value) noexcept override
    {
        if (index >= 0 && static_cast<size_t>(index) < values_.size())
            values_[static_cast<size_t>(index)] = value;
    }
    float parameter(int index) const noexcept override
    {
        return index >= 0 && static_cast<size_t>(index) < values_.size() ? values_[static_cast<size_t>(index)]
                                                                         : 0.0f;
    }
    void process(float*, float*, int) noexcept override {}

private:
    std::string type_ = "clap:fake";
    std::vector<engine::ParameterInfo> infos_;
    std::vector<float> values_;
};
} // namespace

TEST_CASE("A choice control is segmented up to four choices and a menu past that", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    ui::ChoiceControl few(theme, {"Plate", "Chamber", "Hall"}, "Space");
    ui::ChoiceControl many(theme, {"Off", "1/64", "1/32", "1/16", "1/8", "1/4"}, "Sync");
    CHECK(few.isSegmented());
    CHECK_FALSE(many.isSegmented());
    CHECK(few.preferredWidth() > 0);
    CHECK(many.preferredWidth() > 0);

    // A value shows its nearest choice, quietly; a pick reports the index as a value.
    float picked = -1.0f;
    many.onChange = [&](float v) { picked = v; };
    many.setValue(2.4f);
    CHECK(many.selected() == 2);
    CHECK(picked == -1.0f);
    std::vector<juce::ComboBox*> combos;
    findAll(many, combos);
    REQUIRE(combos.size() == 1);
    combos[0]->setSelectedId(5, juce::sendNotificationSync);
    CHECK(picked == 4.0f);
    few.setValue(9.0f); // past the end: the last choice
    CHECK(few.selected() == 2);
}

TEST_CASE("The insert window's generic editor shows choices as choices", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto processor = plugins::BuiltInFactory().create(vesper::type);
    REQUIRE(processor != nullptr);
    ui::InsertEditor editor(1, 5, *processor, "", false);
    core::Insert insert;
    insert.id = 5;
    insert.type = vesper::type;
    insert.params = plugins::defaultParameters(vesper::type);
    insert.params[vesper::space] = 2.0f;
    editor.setInsert(insert);
    editor.setSize(ui::InsertEditor::preferredWidth, 600);

    std::vector<ui::ChoiceControl*> choices;
    findAll(editor, choices);
    REQUIRE(choices.size() == vesperChoices());
    CHECK(choices[0]->isSegmented());
    CHECK(choices[0]->selected() == 2); // Hall
    for (auto* c : choices)
    {
        CHECK(c->getWidth() > 0);
        CHECK(c->getRight() <= c->getParentWidth());
    }

    std::unique_ptr<core::Command> last;
    editor.onCommand = [&](std::unique_ptr<core::Command> c) { last = std::move(c); };
    REQUIRE(choices[0]->onChange);
    choices[0]->onChange(1.0f);
    REQUIRE(last != nullptr);
    core::Project project;
    core::Track t;
    t.id = 1;
    t.inserts.push_back(insert);
    project.tracks.push_back(t);
    last->apply(project);
    CHECK(project.findTrack(1)->inserts[0].params[vesper::space] == 1.0f);
}

TEST_CASE("The Editor panel's generic card shows choices and grows wide rather than spill", "[ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track t;
    t.id = 1;
    t.name = "Vox";
    core::Insert insert;
    insert.id = 5;
    insert.type = "clap:fake";
    insert.params = plugins::defaultParameters(vesper::type);
    t.inserts.push_back(insert);
    project.tracks.push_back(t);
    project.nextId = 6;

    std::shared_ptr<engine::InsertProcessor> live = std::make_shared<FakeClap>();
    const auto& theme = ui::Theme::dark();
    ui::EditorPanel panel(theme);
    panel.liveInsert = [&](core::Id id) { return id == 5 ? live : nullptr; };
    std::unique_ptr<core::Command> last;
    panel.onCommand = [&](std::unique_ptr<core::Command> c) { last = std::move(c); };
    panel.setSize(3000, 320); // short: the grid must spread sideways
    panel.setProject(&project);
    panel.setChannel(1);

    std::vector<ui::ChoiceControl*> choices;
    findAll(panel, choices);
    REQUIRE(choices.size() == vesperChoices());
    CHECK(choices[0]->isSegmented());

    // Every control of the card inside the card.
    auto* card = choices[0]->getParentComponent();
    REQUIRE(card != nullptr);
    std::vector<ui::Knob*> knobs;
    findAll(*card, knobs);
    REQUIRE_FALSE(knobs.empty());
    for (auto* k : knobs)
        CHECK(card->getLocalBounds().contains(k->getBounds()));
    for (auto* c : choices)
        CHECK(card->getLocalBounds().contains(c->getBounds()));

    REQUIRE(choices[0]->onChange);
    choices[0]->onChange(2.0f);
    REQUIRE(last != nullptr);
    auto copy = project;
    last->apply(copy);
    CHECK(copy.findTrack(1)->inserts[0].params[vesper::space] == 2.0f);
}
