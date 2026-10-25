// Pictures of every card, hidden from the default run (northlight_juce_tests
// "[.card-snapshots]"): each built-in's card and each channel strip module, played
// into as the work budgets do (so the same frames every run), saved at 100% and
// 200% (and a rendered face at its smallest too) as PNGs in the folder
// NORTHLIGHT_SNAPSHOT_DIR names. Saved before and after a refactor of the cards,
// the two folders should match pixel for pixel.

#include "UiPerf.h"
#include "plugins/ChannelStrip.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/EffectPresetLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
void save(const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out(file);
    REQUIRE(out.openedOk());
    CHECK(juce::PNGImageFormat().writeImageToStream(image, out));
}

bool hasRenderedFace(juce::Component& card)
{
    for (auto* c : card.getChildren())
        if (dynamic_cast<ui::RenderedFace*>(c) != nullptr)
            return true;
    return false;
}
} // namespace

TEST_CASE("Every card, saved as pictures", "[.card-snapshots]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_SNAPSHOT_DIR", {});
    REQUIRE(folder.isNotEmpty());
    const juce::File dir(folder);
    REQUIRE(dir.createDirectory());
    // An empty presets folder: the user's own presets never reach the pictures.
    const auto presets =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("northlight-snapshot-presets");
    presets.deleteRecursively();
    ui::strip::EffectPresetLibrary::instance().setRoot(presets);

    auto types = uiperf::builtInTypes();
    for (const auto& module : plugins::strip::moduleTypes())
        types.push_back(module);
    types.push_back(plugins::strip::types::opto);
    for (const auto& type : types)
    {
        uiperf::CardUnderTest t(type);
        if (t.card() == nullptr)
            continue;
        const auto name = juce::String(type).fromLastOccurrenceOf(".", false, false);
        const auto prefix = juce::String(plugins::strip::isStripType(type) ? "strip-" : "") + name;
        auto& card = *t.card();
        for (const float scale : {1.0f, 2.0f})
            save(card.createComponentSnapshot(card.getLocalBounds(), true, scale),
                 dir.getChildFile(prefix + "-" + juce::String(juce::roundToInt(scale * 100)) + ".png"));
        if (hasRenderedFace(card))
        {
            const int body = card.minimumBodyHeight();
            card.setSize(card.preferredWidthAt(body), body + ui::strip::ModuleCard::headerHeight);
            for (const float scale : {1.0f, 2.0f})
                save(card.createComponentSnapshot(card.getLocalBounds(), true, scale),
                     dir.getChildFile(prefix + "-compact-" + juce::String(juce::roundToInt(scale * 100)) +
                                      ".png"));
        }
    }
}
