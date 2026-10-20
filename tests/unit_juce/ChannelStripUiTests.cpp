#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "core/ProjectJson.h"
#include "plugins/ChannelStrip.h"
#include "ui/Theme.h"
#include "ui/strip/ChannelStripView.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace aural;
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
                            .getChildFile("aural-strip-tests")
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
    const auto file = folder.getChildFile("old.aural");
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

#include "ui/strip/UserStripPresets.h"

TEST_CASE("A user strip preset saves and loads back", "[strip][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("aural-strip-presets")
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
