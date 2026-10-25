#include "SyntheticSinger.h"
#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Vesper.h"
#include "ui/Theme.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/SynthParts.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <vector>

using namespace northlight;
namespace pv = plugins::vesper;

namespace
{
// A live Vesper and its card, sung into: the card ticks as the audio plays.
struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(pv::type);
    core::Insert insert;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    Rig()
    {
        live->prepare(48000.0, block);
        live->setBypassed(false);
        insert.id = 12;
        insert.type = pv::type;
        insert.params = plugins::defaultParameters(insert.type);
        for (size_t i = 0; i < insert.params.size(); ++i)
            live->setParameter(static_cast<int>(i), insert.params[i]);
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [](std::unique_ptr<core::Command>) {};
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
        REQUIRE(card != nullptr);
        card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight() + 40);
        REQUIRE(card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f).isValid());
        card->tick(16.0);
    }

    // The audio through the processor in 5 ms blocks, the card ticking every 20 ms.
    void sing(const std::vector<float>& audio)
    {
        std::vector<float> l(block), r(block);
        int n = 0;
        for (size_t at = 0; at + block <= audio.size(); at += block, ++n)
        {
            std::copy(audio.begin() + static_cast<std::ptrdiff_t>(at),
                      audio.begin() + static_cast<std::ptrdiff_t>(at + block),
                      l.begin());
            r = l;
            live->process(l.data(), r.data(), block);
            if (n % 4 == 0)
                card->tick(20.0);
        }
    }

    template <typename T> T* find(const juce::String& id) const
    {
        return dynamic_cast<T*>(card->findChildWithID(id));
    }

    static constexpr int block = 240;
};

// Two sung lines with a pause between and after them.
std::vector<float> phrase()
{
    testing::Singer singer(48000.0);
    singer.rest(0.2);
    for (int line = 0; line < 2; ++line)
        singer.vowel(0.35, 220.0f).s(0.08).vowel(0.3, 247.0f).t().vowel(0.4, 262.0f).rest(0.9);
    return singer.audio();
}
} // namespace

TEST_CASE("Vesper's card watches its processor: what was heard and each line end arrive", "[ui][vesper]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    const auto* telemetry = pv::telemetryOf(rig.live.get());
    REQUIRE(telemetry != nullptr);
    rig.sing(phrase());
    // The processor published while the card watched: about 2.9 s of 5 ms frames.
    CHECK(telemetry->framesWritten() > 400);
    std::uint64_t cursor = 0;
    std::vector<pv::Telemetry::Frame> frames(pv::Telemetry::length);
    const int n = telemetry->read(cursor, frames.data(), static_cast<int>(frames.size()));
    int voiced = 0, kept = 0, ends = 0;
    for (int i = 0; i < n; ++i)
    {
        voiced += frames[static_cast<size_t>(i)][pv::Telemetry::voiced] > 0.5f ? 1 : 0;
        kept += frames[static_cast<size_t>(i)][pv::Telemetry::keptOutDb] > 1.5f ? 1 : 0;
        ends += frames[static_cast<size_t>(i)][pv::Telemetry::lineEnd] > 0.5f ? 1 : 0;
    }
    CHECK(voiced > 100);
    CHECK(kept > 5);  // the s and the t, held out of the send
    CHECK(ends == 2); // one per line
    REQUIRE(rig.card->findChildWithID("evensong") != nullptr);
    CHECK(rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 2.0f).isValid());
}

TEST_CASE("Vesper's card: the star catches, the pills choose the space, the chips set the modes",
          "[ui][vesper]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto& card = *rig.card;

    auto* star = rig.find<juce::Button>("catch");
    REQUIRE(star != nullptr);
    REQUIRE(star->onClick);
    star->onClick();
    CHECK(card.param(pv::catchTail) == 1.0f);
    star->onClick();
    CHECK(card.param(pv::catchTail) == 0.0f);

    auto* space = rig.find<ui::strip::PillRow>("space");
    REQUIRE(space != nullptr);
    REQUIRE(space->onChange);
    space->onChange(2);
    CHECK(card.param(pv::space) == 2.0f);

    auto* preDelay = rig.find<ui::strip::CornerChoice>("predelay-mode");
    REQUIRE(preDelay != nullptr);
    CHECK(preDelay->selected == 0); // Auto, the default
    preDelay->onChoose(1);          // Free: the time in ms
    CHECK(card.param(pv::preDelayAuto) == 0.0f);
    CHECK(card.param(pv::preDelaySync) == 0.0f);
    preDelay->onChoose(11); // the tenth note value
    CHECK(card.param(pv::preDelayAuto) == 0.0f);
    CHECK(card.param(pv::preDelaySync) == 10.0f);
    CHECK(preDelay->selected == 11);
    preDelay->onChoose(0);
    CHECK(card.param(pv::preDelayAuto) == 1.0f);

    auto* release = rig.find<ui::strip::CornerChoice>("release-mode");
    REQUIRE(release != nullptr);
    CHECK(release->selected == 8); // 1/8, the default
    release->onChoose(0);
    CHECK(card.param(pv::releaseSync) == 0.0f);
}

// A picture, hidden from the default run (northlight_juce_tests "[.vesper-snapshot]"):
// the card after two sung lines at 200%, saved to NORTHLIGHT_SNAPSHOT (a .png path),
// for checking the face against its spec with real phrasing.
TEST_CASE("Vesper's card, sung into", "[.vesper-snapshot]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    rig.live->setParameter(pv::lookahead, 2.0f);
    rig.sing(phrase());
    const auto image = rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 2.0f);
    const char* path = std::getenv("NORTHLIGHT_SNAPSHOT");
    REQUIRE(path != nullptr);
    juce::File file(juce::String::fromUTF8(path));
    file.deleteFile();
    juce::FileOutputStream out(file);
    REQUIRE(out.openedOk());
    CHECK(juce::PNGImageFormat().writeImageToStream(image, out));
}
