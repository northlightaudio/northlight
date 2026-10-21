#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "core/ProjectJson.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/Theme.h"
#include "ui/strip/ChannelStripView.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;
namespace ps = plugins::strip;

namespace
{
class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return settings_; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { settings_ = settings; }

private:
    audio::AudioSettings settings_;
};

void runBlocks(ProjectSession& session, int blocks, float value)
{
    std::vector<float> in(64, value);
    const float* inputs[] = {in.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    for (int i = 0; i < blocks; ++i)
        session.engine().process(inputs, 1, outputs, 2, 64);
}
} // namespace

TEST_CASE("A new track has the channel strip; a module switched on runs and reports", "[strip][session]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    ProjectSession session(audio);
    core::Track t;
    t.name = "Vocal";
    t.inputChannel = 0;
    t.monitor = core::MonitorMode::on; // the input runs through the chain
    session.execute(std::make_unique<core::AddTrack>(t));
    const auto trackId = session.project().tracks.front().id;
    const auto strip = session.project().tracks.front().strip; // a copy
    REQUIRE(strip.size() == ps::moduleTypes().size());
    const auto& comp = strip[2];
    REQUIRE(comp.type == ps::types::compressor);
    CHECK(comp.bypass);

    session.engine().prepare(48000.0, 64);
    auto live = session.liveInsert(comp.id);
    REQUIRE(live != nullptr);
    REQUIRE(live->feedback() != nullptr);
    CHECK(live->isBypassed());

    // Switching it on is a live command (no recompile, the same instance).
    session.execute(std::make_unique<core::SetInsertBypass>(trackId, comp.id, false));
    session.execute(
        std::make_unique<core::SetInsertParameter>(trackId, comp.id, ps::compressor::threshold, -30.0f));
    CHECK(session.liveInsert(comp.id) == live);
    CHECK_FALSE(live->isBypassed());
    CHECK(live->parameter(ps::compressor::threshold) == -30.0f);
    runBlocks(session, 200, 0.5f); // -6 dBFS: 24 dB over the threshold
    CHECK(live->feedback()->latest().reduction > 10.0f);

    // Undo switches it off again.
    session.undo();
    session.undo();
    CHECK(live->isBypassed());
    CHECK(live->parameter(ps::compressor::threshold) == -18.0f);
}

TEST_CASE("A project saved before the strip existed gets one when opened", "[strip][session]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("northlight-strip-tests")
                            .getChildFile(juce::Uuid().toString());
    REQUIRE(folder.createDirectory().wasOk());
    core::Project old;
    core::Track t;
    t.id = old.allocateId();
    t.name = "Old";
    old.tracks.push_back(t);
    core::Bus b;
    b.id = old.allocateId();
    old.buses.push_back(b);
    const auto file = folder.getChildFile("old.northlight");
    REQUIRE(file.replaceWithText(juce::String(core::projectToJson(old))));

    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    ProjectSession session(audio);
    REQUIRE(session.open(file).empty());
    CHECK(session.project().tracks[0].strip.size() == ps::moduleTypes().size());
    CHECK(session.project().buses[0].strip.size() == ps::moduleTypes().size());
    CHECK_FALSE(session.canUndo()); // not an edit
    folder.deleteRecursively();
}

TEST_CASE("The strip view shows a card per module, off modules as tabs", "[strip][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddTrack>(core::Track{});
    add->setDefaultStrip(ps::defaultStrip());
    undo.execute(p, std::move(add));
    // The deliberately unusual order checks the cards follow the model.
    undo.execute(p, std::make_unique<core::MoveStripModule>(p.tracks[0].id, p.tracks[0].strip[5].id, 0));

    ui::strip::ChannelStripView view(theme);
    view.setBounds(0, 0, 2400, 420);
    view.setChannel(&p.tracks[0]);
    CHECK(view.moduleCount() == static_cast<int>(ps::moduleTypes().size()));
    const int n = view.moduleCount();
    const int allTabs = n * ui::strip::ModuleCard::collapsedWidth + (n - 1) * theme.spacing.sm;
    CHECK(view.preferredWidth() == allTabs);

    // Switching the EQ on opens its card in place.
    auto& eq = p.tracks[0].strip[2];
    REQUIRE(eq.type == ps::types::eq);
    eq.bypass = false;
    view.setChannel(&p.tracks[0]);
    CHECK(view.preferredWidth() > allTabs + 300);

    // Clearing the channel removes the cards.
    view.setChannel(nullptr);
    CHECK(view.moduleCount() == 0);
}

TEST_CASE("Swapping the compressor for the opto replaces the live processor and the card", "[strip][opto]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    ProjectSession session(audio);
    core::Track t;
    t.inputChannel = 0;
    t.monitor = core::MonitorMode::on;
    session.execute(std::make_unique<core::AddTrack>(t));
    const auto trackId = session.project().tracks.front().id;
    const auto compId = session.project().tracks.front().strip[2].id;
    session.engine().prepare(48000.0, 64);
    session.execute(std::make_unique<core::SetInsertBypass>(trackId, compId, false));
    session.execute(std::make_unique<core::SetStripModuleType>(
        trackId, compId, ps::types::opto, plugins::defaultParameters(ps::types::opto), "Swap to opto"));
    auto live = session.liveInsert(compId);
    REQUIRE(live != nullptr);
    CHECK(live->typeName() == ps::types::opto);
    CHECK_FALSE(live->isBypassed());
    runBlocks(session, 400, 0.5f);
    CHECK(live->feedback()->latest().reduction > 3.0f);

    // The view rebuilds the card as the opto's, in the same place.
    const auto& theme = ui::Theme::dark();
    ui::strip::ChannelStripView view(theme);
    view.setBounds(0, 0, 2400, 420);
    const auto cardTypes = [&view]
    {
        std::vector<std::string> types;
        for (auto* child : view.getChildren())
            if (auto* card = dynamic_cast<ui::strip::ModuleCard*>(child))
                types.push_back(card->type());
        return types;
    };
    view.setChannel(&session.project().tracks.front());
    REQUIRE(cardTypes().size() == ps::moduleTypes().size());
    CHECK(cardTypes()[2] == ps::types::opto);
    session.undo();
    CHECK(session.liveInsert(compId)->typeName() == ps::types::compressor);
    view.setChannel(&session.project().tracks.front());
    CHECK(cardTypes()[2] == ps::types::compressor);
}

#include "ui/strip/UserStripPresets.h"

TEST_CASE("A user strip preset saves and loads back", "[strip][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("northlight-strip-presets")
                            .getChildFile(juce::Uuid().toString());
    auto modules = ps::defaultStrip();
    modules[2].bypass = false;
    modules[2].params[ps::compressor::threshold] = -31.5f;
    CHECK_FALSE(ui::strip::saveUserPreset(folder, "  ", modules).empty());
    REQUIRE(ui::strip::saveUserPreset(folder, "Gritty bass", modules).empty());
    const auto loaded = ui::strip::loadUserPresets(folder);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].name == "Gritty bass");
    CHECK(loaded[0].category == "Yours");
    REQUIRE(loaded[0].modules.size() == modules.size());
    CHECK(loaded[0].modules[2].type == ps::types::compressor);
    CHECK_FALSE(loaded[0].modules[2].bypass);
    CHECK(loaded[0].modules[2].params[ps::compressor::threshold] == -31.5f);
    CHECK(loaded[0].modules[0].bypass);
    folder.deleteRecursively();
}

#include "plugins/BuiltIns.h"
#include "ui/strip/BuiltInCards.h"

TEST_CASE("A card follows automation on its processor through refreshes that repeat the model",
          "[strip][ui][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(plugins::types::delay);
    live->prepare(48000.0, 64);
    core::Insert insert;
    insert.id = 7;
    insert.type = plugins::types::delay;
    insert.params = plugins::defaultParameters(insert.type);
    insert.params[plugins::delay::time] = 375.0f;
    live->setParameter(plugins::delay::time, 375.0f);
    ui::strip::CardHost host;
    host.liveInsert = [&](core::Id) { return live; };
    auto card = ui::strip::createBuiltInCard(theme, host, 1, insert);
    REQUIRE(card != nullptr);
    card->setBounds(0, 0, 600, 300);

    // A lane plays the time: the card follows the processor.
    live->setParameter(plugins::delay::time, 200.0f);
    card->tick(16.0);
    CHECK(card->param(plugins::delay::time) == 200.0f);
    // The views refresh with the model as it was: the card stays with the lane
    // (it used to flip back to the model's value every refresh).
    card->refresh(insert);
    CHECK(card->param(plugins::delay::time) == 200.0f);
    card->tick(16.0);
    card->refresh(insert);
    CHECK(card->param(plugins::delay::time) == 200.0f);
    // A change to the model (an edit, an undo) still comes in.
    insert.params[plugins::delay::time] = 500.0f;
    card->refresh(insert);
    CHECK(card->param(plugins::delay::time) == 500.0f);
}

#include "ui/strip/Graphs.h"

TEST_CASE("A peak scrolling off the level graph's left keeps its falling tail", "[strip][ui]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live =
        plugins::BuiltInFactory().create(plugins::types::compressor);
    live->prepare(48000.0, 240);
    core::Insert insert;
    insert.id = 9;
    insert.type = plugins::types::compressor;
    insert.params = plugins::defaultParameters(insert.type);
    for (size_t i = 0; i < insert.params.size(); ++i)
        live->setParameter(static_cast<int>(i), insert.params[i]);
    ui::strip::CardHost host;
    host.liveInsert = [&](core::Id) { return live; };
    auto card = ui::strip::createBuiltInCard(theme, host, 1, insert);
    REQUIRE(card != nullptr);
    card->setBounds(0, 0, 600, 300);
    card->tick(16.0); // starts watching

    ui::strip::HistoryGraph graph(*card, ui::strip::HistoryGraph::Style{});
    graph.setBounds(0, 0, 302, 100); // 300 columns over three seconds: 2 frames each
    juce::Image image(juce::Image::ARGB, 302, 100, true);
    // `seconds` of audio at `level`, the card reading it as it goes.
    std::vector<float> l(240), r(240);
    const auto play = [&](double seconds, float level)
    {
        for (int b = 0; b < static_cast<int>(seconds * 200.0); ++b) // 5 ms blocks
        {
            std::fill(l.begin(), l.end(), level);
            std::fill(r.begin(), r.end(), level);
            live->process(l.data(), r.data(), 240);
            if (b % 10 == 0)
                card->tick(50.0);
        }
        card->tick(50.0);
    };
    play(0.5, 0.0f);
    play(0.05, 1.0f); // the peak
    play(3.1, 0.0f);  // now just off the left edge
    {
        juce::Graphics g(image);
        graph.paintContent(g);
    }
    // Still falling from the peak at 30 dB a second (about 3 dB down), not silent.
    const float left = graph.levelColumnForTesting(0);
    CHECK(left > 0.5f);
    CHECK(left < 1.0f);
}
