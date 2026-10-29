#include "core/Command.h"
#include "engine/Midi.h"
#include "plugins/BuiltIns.h"
#include "plugins/midi/Chords.h"
#include "plugins/midi/TimeEffects.h"
#include "ui/Theme.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
// A face with its live effect, the effect fed notes through the engine's call.
struct FaceUnderTest
{
    std::shared_ptr<engine::InsertProcessor> live;
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;
    core::Project project;

    explicit FaceUnderTest(const std::string& type)
    {
        live = plugins::BuiltInFactory().create(type);
        live->prepare(48000.0, 256);
        insert.id = 30;
        insert.type = type;
        insert.params = plugins::defaultParameters(type);
        for (size_t i = 0; i < insert.params.size(); ++i)
            live->setParameter(static_cast<int>(i), insert.params[i]);
        project.key.root = 0; // C major
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [this](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
        host.project = [this] { return &project; };
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, 1, insert);
        if (card != nullptr)
        {
            card->setEmbedded(true);
            card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight());
        }
    }
    void play(std::vector<engine::MidiEvent> events)
    {
        engine::MidiEventList in(64), out(1024);
        for (const auto& e : events)
            in.push(e);
        live->processMidiEffect(in, out, 256);
        card->tick(16.0);
    }
    template <typename T> T* find(const juce::String& tooltip)
    {
        for (auto* child : card->getChildren())
            if (auto* c = dynamic_cast<T*>(child); c != nullptr && c->getTooltip() == tooltip)
                return c;
        return nullptr;
    }
};

engine::MidiEvent noteOn(int note)
{
    return {0, engine::MidiEvent::Type::noteOn, 0, static_cast<std::uint8_t>(note), 100};
}
} // namespace

TEST_CASE("The five MIDI faces build at a family width and draw at 100% and 200%, playing or not",
          "[ui][midifx][faces]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    for (const char* type : {"northlight.arpeggiator",
                             "northlight.chord",
                             "northlight.chordpads",
                             "northlight.velocity",
                             "northlight.scale"})
    {
        CAPTURE(type);
        FaceUnderTest f(type);
        REQUIRE(f.card != nullptr);
        const int w = f.card->preferredWidth();
        CHECK((w == 224 || w == 320 || w == 416)); // the family's widths
        CHECK(f.card->colour() == ui::strip::paletteColour(ui::Theme::dark(), "Heather"));
        CHECK(f.card->createComponentSnapshot(f.card->getLocalBounds(), true, 1.0f).isValid());
        f.play({noteOn(60), noteOn(64), noteOn(67)});
        CHECK(f.card->createComponentSnapshot(f.card->getLocalBounds(), true, 2.0f).isValid());
        // In a short dock the display gives way first; the controls keep their room.
        f.card->setBounds(0, 0, w, f.card->minimumBodyHeight());
        CHECK(f.card->createComponentSnapshot(f.card->getLocalBounds(), true, 1.0f).isValid());
    }
    // The other MIDI effects keep the settings rows for now.
    FaceUnderTest transpose("northlight.transpose");
    CHECK(transpose.card == nullptr);
}

TEST_CASE("A face's pills set their parameter's value, offset for octaves and a register",
          "[ui][midifx][faces]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    FaceUnderTest arp("northlight.arpeggiator");
    auto* octaves = arp.find<ui::strip::PillRow>("Octaves");
    REQUIRE(octaves != nullptr);
    CHECK(octaves->selected == 0); // one octave, the first pill
    octaves->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(arp.sent.size() == 1);                                   // one command
    CHECK(arp.card->param(plugins::arpeggiator::octaves) == 2.0f); // the second pill: two octaves

    FaceUnderTest pads("northlight.chordpads");
    auto* reg = pads.find<ui::strip::PillRow>("Register");
    REQUIRE(reg != nullptr);
    CHECK(reg->selected == 2); // octave 0 is the middle pill
    reg->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    CHECK(pads.sent.size() == 1);
    CHECK(pads.card->param(plugins::chordpads::octave) == -2.0f);
}
