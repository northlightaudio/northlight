#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;
using Catch::Approx;

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

struct WriteFixture
{
    WriteFixture()
    {
        session = std::make_unique<ProjectSession>(audio);
        core::Track t;
        t.name = "Vox";
        session->execute(std::make_unique<core::AddTrack>(t));
        track = session->project().tracks.front().id;
        session->engine().prepare(48000.0, 64);
    }
    void blocks(int n)
    {
        std::vector<float> l(64), r(64);
        float* outputs[] = {l.data(), r.data()};
        for (int i = 0; i < n; ++i)
            session->engine().process(nullptr, 0, outputs, 2, 64);
    }
    void mode(core::AutomationMode m)
    {
        session->execute(std::make_unique<core::SetAutomationMode>(track, m));
    }
    const core::Track& model() const { return session->project().tracks.front(); }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    std::unique_ptr<ProjectSession> session;
    core::Id track = core::invalidId;
};
} // namespace

TEST_CASE("Touch writes a fader move into a new lane and leaves the static value", "[session][automation]")
{
    WriteFixture f;
    f.mode(core::AutomationMode::touch);
    // Stopped: a fader move is an ordinary edit.
    f.session->execute(std::make_unique<core::SetTrackGain>(f.track, 0.9f));
    CHECK(f.model().gain == Approx(0.9f));
    CHECK(f.model().automation.empty());

    f.session->play();
    f.blocks(10); // 640 samples in
    f.session->beginGesture("Fader");
    f.session->execute(std::make_unique<core::SetTrackGain>(f.track, 0.5f));
    f.blocks(100);
    f.session->execute(std::make_unique<core::SetTrackGain>(f.track, 0.25f));
    CHECK(f.session->isWritingAutomation());
    CHECK(f.model().gain == Approx(0.9f)); // the static value is untouched
    f.blocks(10);
    f.session->endGesture();
    CHECK_FALSE(f.session->isWritingAutomation());

    const auto* lane = f.model().findLane(core::AutomationTarget::gain);
    REQUIRE(lane != nullptr);
    CHECK(f.model().gain == Approx(0.9f));
    // Before the pass: the old value; the written values; after the release, back.
    CHECK(lane->valueAt(0) == Approx(0.9f));
    CHECK(lane->valueAt(700) == Approx(0.5f).margin(0.01f));
    CHECK(lane->valueAt(640 + 100 * 64) == Approx(0.25f));
    CHECK(lane->valueAt(48000 * 10) == Approx(0.9f));
    // One undo step removes the pass.
    CHECK(f.session->undoName() == "Write automation");
    f.session->undo();
    CHECK(f.model().automation.empty());
}

TEST_CASE("Latch holds the last value until the transport stops", "[session][automation]")
{
    WriteFixture f;
    f.mode(core::AutomationMode::latch);
    f.session->play();
    f.blocks(10);
    f.session->beginGesture("Pan");
    f.session->execute(std::make_unique<core::SetTrackPan>(f.track, -0.5f));
    f.session->endGesture(); // still latched
    CHECK(f.session->isWritingAutomation());
    f.blocks(200);
    const auto stopAt = f.session->position();
    f.session->stop();
    CHECK_FALSE(f.session->isWritingAutomation());
    const auto* lane = f.model().findLane(core::AutomationTarget::pan);
    REQUIRE(lane != nullptr);
    CHECK(lane->points.back().position == stopAt);
    CHECK(lane->valueAt(stopAt - 10) == Approx(-0.5f));
    CHECK(f.model().pan == Approx(0.0f));
}

TEST_CASE("Write mode rewrites the channel's lanes from where playback started", "[session][automation]")
{
    WriteFixture f;
    core::AutomationLane lane;
    lane.target = core::AutomationTarget::gain;
    lane.points = {{0, 0.1f}, {100000, 0.1f}};
    f.session->execute(std::make_unique<core::AddAutomationLane>(f.track, lane));
    f.mode(core::AutomationMode::write);
    f.session->play();
    f.blocks(10);
    f.session->execute(std::make_unique<core::SetTrackGain>(f.track, 0.7f));
    f.blocks(50);
    f.session->stop();
    const auto* written = f.model().findLane(core::AutomationTarget::gain);
    REQUIRE(written != nullptr);
    CHECK(written->valueAt(2000) == Approx(0.7f));
    // The part of the old lane past the stop is kept.
    CHECK(written->points.back().position == 100000);
}

TEST_CASE("A built-in parameter writes too; read mode does not write", "[session][automation]")
{
    WriteFixture f;
    const auto strip = f.model().strip; // a copy
    const auto& comp = strip[2];
    f.session->execute(std::make_unique<core::SetInsertBypass>(f.track, comp.id, false));
    f.session->play();
    f.blocks(5);
    // Read: an ordinary edit.
    f.session->execute(std::make_unique<core::SetInsertParameter>(f.track, comp.id, 0, -30.0f));
    CHECK_FALSE(f.session->isWritingAutomation());
    CHECK(f.model().findInsert(comp.id)->params[0] == -30.0f);

    f.mode(core::AutomationMode::touch);
    f.session->beginGesture("Threshold");
    f.session->execute(std::make_unique<core::SetInsertParameter>(f.track, comp.id, 0, -40.0f));
    CHECK(f.session->liveInsert(comp.id)->parameter(0) == -40.0f); // heard at once
    f.blocks(20);
    f.session->endGesture();
    const auto* lane = f.model().findLane(core::AutomationTarget::insertParameter, comp.id, 0);
    REQUIRE(lane != nullptr);
    CHECK(f.model().findInsert(comp.id)->params[0] == -30.0f);
    CHECK(lane->valueAt(f.session->position() - 64 * 30) == Approx(-40.0f));
}

TEST_CASE("The master fader writes its volume lane in Touch and leaves the static gain",
          "[session][automation]")
{
    WriteFixture f;
    f.session->execute(
        std::make_unique<core::SetAutomationMode>(core::masterId, core::AutomationMode::touch));
    f.session->play();
    f.blocks(10);
    f.session->beginGesture("Master fader");
    f.session->execute(std::make_unique<core::SetMasterGain>(0.5f));
    f.blocks(50);
    f.session->endGesture();
    const auto& master = f.session->project().master;
    CHECK(master.gain == Approx(1.0f));
    const auto* lane = master.findLane(core::AutomationTarget::gain);
    REQUIRE(lane != nullptr);
    CHECK(lane->valueAt(700) == Approx(0.5f).margin(0.01f));
    CHECK(master.automationOpen); // a new lane opens the master's lanes
}
