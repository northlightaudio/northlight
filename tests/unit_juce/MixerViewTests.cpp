#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "ui/MixerView.h"
#include "ui/components/Fader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
void collectFaders(juce::Component& parent, std::vector<ui::Fader*>& out)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* fader = dynamic_cast<ui::Fader*>(child))
            out.push_back(fader);
        collectFaders(*child, out);
    }
}

// The channel strips' faders (not the master's), sorted by the gain they show.
std::vector<float> channelFaderGains(ui::MixerView& mixer)
{
    std::vector<ui::Fader*> faders;
    for (auto* child : mixer.getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*>(child))
            collectFaders(*viewport, faders);
    std::vector<float> gains;
    for (auto* fader : faders)
        gains.push_back(fader->gain());
    std::sort(gains.begin(), gains.end());
    return gains;
}
} // namespace

TEST_CASE("Moving one fader leaves an automated fader on its played value", "[ui][mixer][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track a, b;
    a.name = "Drums";
    b.name = "Vox";
    core::AddTrack addA(a), addB(b);
    addA.apply(project);
    addB.apply(project);
    const core::Id moved = addA.trackId();
    const core::Id automated = addB.trackId();

    rt::MeterBlock meters;
    ui::MeterReadings readings(meters);
    ui::MixerView mixer(readings, nullptr, nullptr);
    mixer.automatedValues = [automated](core::Id id)
    {
        ui::MixerView::Automated played;
        if (id == automated)
        {
            played.gain = true;
            played.gainValue = 0.25f;
        }
        return played;
    };
    mixer.setBounds(0, 0, 1200, 700);
    mixer.setProject(&project);
    const auto before = channelFaderGains(mixer);
    REQUIRE(before.size() == 2);
    CHECK(before[0] == Approx(0.25f));
    CHECK(before[1] == Approx(1.0f));

    // A drag on the other strip: the model changes and the strips refresh their values.
    core::SetTrackGain drag(moved, 0.5f);
    drag.apply(project);
    mixer.setProject(&project, true);

    const auto gains = channelFaderGains(mixer);
    REQUIRE(gains.size() == 2);
    CHECK(gains[0] == Approx(0.25f)); // the automated strip, not its static 1.0
    CHECK(gains[1] == Approx(0.5f));  // the dragged strip follows the model
}

namespace
{
// Tooltips of everything under `parent` outside the channel strips' viewport: the
// master strip's insert slots carry their insert's name.
void collectMasterTooltips(juce::Component& parent, std::vector<juce::String>& out)
{
    for (auto* child : parent.getChildren())
    {
        if (dynamic_cast<juce::Viewport*>(child) != nullptr && &parent == child->getParentComponent() &&
            dynamic_cast<ui::MixerView*>(&parent) != nullptr)
            continue; // the channel strips
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child))
            out.push_back(tip->getTooltip());
        collectMasterTooltips(*child, out);
    }
}
} // namespace

TEST_CASE("The master strip lists the master's inserts and follows their changes", "[ui][mixer][master]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track a;
    core::AddTrack add(a);
    add.apply(project);
    core::Insert glue;
    glue.type = "northlight.glue";
    core::AddInsert insert(core::masterId, glue);
    insert.apply(project);

    rt::MeterBlock meters;
    ui::MeterReadings readings(meters);
    ui::MixerView mixer(readings, nullptr, nullptr);
    mixer.setBounds(0, 0, 1200, 700);
    mixer.setProject(&project);
    std::vector<juce::String> tips;
    collectMasterTooltips(mixer, tips);
    CHECK(std::count(tips.begin(), tips.end(), juce::String("Glue Compressor")) == 1);
    CHECK(std::count(tips.begin(),
                     tips.end(),
                     juce::String("Add an insert on the whole mix, before the master fader")) == 1);

    // Bypassed, the slot says so; removed, it goes.
    core::SetInsertBypass bypass(core::masterId, project.master.inserts[0].id, true);
    bypass.apply(project);
    mixer.setProject(&project);
    tips.clear();
    collectMasterTooltips(mixer, tips);
    CHECK(std::count(tips.begin(), tips.end(), juce::String("Glue Compressor (bypassed)")) == 1);
    core::RemoveInsert remove(core::masterId, project.master.inserts[0].id);
    remove.apply(project);
    mixer.setProject(&project);
    tips.clear();
    collectMasterTooltips(mixer, tips);
    CHECK(std::count(tips.begin(), tips.end(), juce::String("Glue Compressor")) == 0);
}
