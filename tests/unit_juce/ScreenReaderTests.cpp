// Painted content for screen readers (design/accessibility-performance.md 11.3):
// the stand-ins ui::AccessibleItems keeps for clips, notes, markers, tempo
// changes, automation points and the mixer's painted parts. Handlers are made
// directly: JUCE only hands them out for components on the desktop.
#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "plugins/ChannelStrip.h"
#include "ui/ArrangeView.h"
#include "ui/MixerView.h"
#include "ui/PianoRoll.h"
#include "ui/TempoLane.h"
#include "ui/Theme.h"
#include "ui/components/AccessibleItems.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <vector>

using namespace northlight;

namespace
{
// As if a screen reader were running, for one test.
struct ReaderRunning
{
    ReaderRunning() { ui::forceAccessibilityClients(true); }
    ~ReaderRunning() { ui::forceAccessibilityClients(false); }
    ReaderRunning(const ReaderRunning&) = delete;
    ReaderRunning& operator=(const ReaderRunning&) = delete;
};

std::unique_ptr<juce::AccessibilityHandler> handlerOf(juce::Component* component)
{
    REQUIRE(component != nullptr);
    return component->createAccessibilityHandler();
}

// The layer named `title` anywhere under `root`.
ui::AccessibleItems* layerIn(juce::Component& root, const juce::String& title)
{
    for (auto* child : root.getChildren())
    {
        if (auto* layer = dynamic_cast<ui::AccessibleItems*>(child);
            layer != nullptr && layer->getTitle() == title)
            return layer;
        if (auto* found = layerIn(*child, title))
            return found;
    }
    return nullptr;
}

void visit(juce::Component& root, const std::function<void(juce::Component&)>& f)
{
    f(root);
    for (auto* child : root.getChildren())
        visit(*child, f);
}

// 120 BPM at 48 kHz: 24 000 samples a beat.
constexpr core::SamplePosition beat = 24000;
} // namespace

TEST_CASE("The stand-in layer holds nothing until a screen reader listens", "[ui][accessibility][reader]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Component owner;
    owner.setSize(400, 200);
    int pressed = 0, toggled = 0;
    bool second = true;
    ui::AccessibleItems layer(owner,
                              "Lanes",
                              [&]
                              {
                                  ui::AccessibleItem lane;
                                  lane.key = "lane";
                                  lane.role = juce::AccessibilityRole::list;
                                  lane.title = "Vocals lane";
                                  lane.bounds = {0, 0, 400, 50};
                                  // Given later-first: the reading order is the list's, not the position's.
                                  ui::AccessibleItem late;
                                  late.key = "b";
                                  late.title = "Chorus";
                                  late.bounds = {200, 10, 50, 30};
                                  late.selectable = late.selected = true;
                                  late.press = [&pressed] { ++pressed; };
                                  ui::AccessibleItem early;
                                  early.key = "a";
                                  early.title = "Verse";
                                  early.bounds = {20, 10, 50, 30};
                                  ui::AccessibleItem gone;
                                  gone.key = "c";
                                  gone.bounds = {600, 10, 50, 30}; // off screen: not listed
                                  lane.children = {late, early, gone};
                                  ui::AccessibleItem cell;
                                  cell.key = "cell";
                                  cell.role = juce::AccessibilityRole::toggleButton;
                                  cell.title = "Compressor";
                                  cell.bounds = {0, 60, 40, 20};
                                  cell.checkable = cell.checked = true;
                                  cell.press = [&toggled] { ++toggled; };
                                  if (!second)
                                      lane.children.erase(lane.children.begin());
                                  return std::vector<ui::AccessibleItem>{lane, cell};
                              });
    owner.addAndMakeVisible(layer);
    layer.setBounds(owner.getLocalBounds());

    layer.rebuild();
    CHECK(layer.count() == 0); // no reader: nothing made, nothing to paint

    const ReaderRunning reader;
    layer.rebuild();
    CHECK(layer.count() == 4); // the lane, its two clips on screen, the cell
    auto* lane = layer.find({"lane"});
    auto* late = layer.find({"lane", "b"});
    auto* early = layer.find({"lane", "a"});
    REQUIRE(lane != nullptr);
    REQUIRE(late != nullptr);
    REQUIRE(early != nullptr);
    CHECK(layer.find({"lane", "c"}) == nullptr);
    CHECK(late->getParentComponent() == lane);
    CHECK(late->getBounds() == juce::Rectangle<int>(200, 10, 50, 30)); // in the lane's coordinates
    CHECK(lane->isFocusContainer());
    CHECK(late->getExplicitFocusOrder() < early->getExplicitFocusOrder());
    bool clicks = true, childClicks = true;
    late->getInterceptsMouseClicks(clicks, childClicks);
    CHECK_FALSE(clicks);                        // the owner still gets every click
    CHECK_FALSE(late->getWantsKeyboardFocus()); // and Tab never stops on one

    {
        // Handlers made here go before the stand-ins they read.
        const auto laneHandler = handlerOf(lane);
        CHECK(laneHandler->getRole() == juce::AccessibilityRole::list);
        CHECK(laneHandler->getTitle() == "Vocals lane");
        const auto clip = handlerOf(late);
        CHECK(clip->getRole() == juce::AccessibilityRole::listItem);
        CHECK(clip->getTitle() == "Chorus");
        CHECK(clip->getCurrentState().isSelectable());
        CHECK(clip->getCurrentState().isSelected());
        CHECK(clip->getActions().invoke(juce::AccessibilityActionType::press));
        CHECK(pressed == 1);
        CHECK_FALSE(handlerOf(early)->getActions().contains(juce::AccessibilityActionType::press));

        const auto cell = handlerOf(layer.find({"cell"}));
        CHECK(cell->getCurrentState().isCheckable());
        CHECK(cell->getCurrentState().isChecked());
        CHECK(cell->getActions().invoke(juce::AccessibilityActionType::toggle));
        CHECK(toggled == 1);
    }

    // A rebuild keeps the stand-ins of what stayed and drops the rest.
    second = false;
    layer.rebuild();
    CHECK(layer.find({"lane", "a"}) == early);
    CHECK(layer.find({"lane", "b"}) == nullptr);
    CHECK(layer.count() == 3);

    // The reader stops: the layer empties.
    ui::forceAccessibilityClients(false);
    layer.rebuild();
    CHECK(layer.count() == 0);
}

TEST_CASE("A song position reads as bars, beats and ticks", "[ui][accessibility][reader]")
{
    const core::TempoMap map; // 120 BPM, 4/4, 48 kHz
    CHECK(ui::spokenPosition(map, 0) == "bar 1 beat 1");
    CHECK(ui::spokenPosition(map, 5 * beat) == "bar 2 beat 2");
    CHECK(ui::spokenPosition(map, beat / 2) == "bar 1 beat 1 tick 480");
    CHECK(ui::spokenPosition(map, 4 * beat - 1) == "bar 2 beat 1"); // an end, one sample short
}

TEST_CASE("A screen reader finds the arrange view's clips, markers and automation points",
          "[ui][accessibility][reader][arrange]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ReaderRunning reader;
    audio::PeakStore peaks;
    ui::ArrangeView view(peaks);
    view.setSize(900, 600);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.name = "Vocals";
    for (core::Id id : {10, 11})
    {
        core::AudioClip c;
        c.id = id;
        c.name = id == 10 ? "Verse" : "Chorus";
        c.start = static_cast<core::SamplePosition>(id - 10) * 4 * beat;
        c.length = 2 * beat;
        c.muted = id == 11;
        t.clips.push_back(c);
    }
    t.automationOpen = true;
    core::AutomationLane lane;
    lane.id = 30;
    lane.points = {{0, 1.0f}, {4 * beat, 0.5f}};
    t.automation.push_back(lane);
    p.tracks.push_back(t);
    core::Marker marker;
    marker.id = 40;
    marker.name = "Hook";
    marker.position = 4 * beat;
    p.markers.push_back(marker);
    p.nextId = 50;
    view.setProject(&p);

    auto* layer = layerIn(view, "Lanes");
    REQUIRE(layer != nullptr);
    layer->rebuild();
    CHECK(handlerOf(layer->find({"track:1"}))->getTitle() == "Vocals lane");
    auto* verse = layer->find({"track:1", "audio:10"});
    CHECK(handlerOf(verse)->getTitle() == "Verse, audio clip, bar 1 beat 1 to bar 1 beat 3");
    CHECK(handlerOf(layer->find({"track:1", "audio:11"}))->getTitle() ==
          "Chorus, audio clip, bar 2 beat 1 to bar 2 beat 3, muted");
    CHECK(handlerOf(layer->find({"markers", "40"}))->getTitle() == "Marker Hook, at bar 2 beat 1");

    // Pressing a clip selects it, as a click does.
    CHECK(handlerOf(verse)->getActions().invoke(juce::AccessibilityActionType::press));
    CHECK(view.selection() == std::vector<ui::ArrangeView::ClipRef>{{1, 10}});
    layer->rebuild();
    CHECK(handlerOf(layer->find({"track:1", "audio:10"}))->getCurrentState().isSelected());
    CHECK_FALSE(handlerOf(layer->find({"track:1", "audio:11"}))->getCurrentState().isSelected());

    auto* automation = layerIn(view, "Automation");
    REQUIRE(automation != nullptr);
    automation->rebuild();
    auto* points = automation->find({"lane:30"});
    REQUIRE(points != nullptr);
    CHECK(handlerOf(points)->getTitle().startsWith("Vocals Volume automation"));
    auto* second = automation->find({"lane:30", "1"});
    REQUIRE(second != nullptr);
    CHECK(handlerOf(second)->getTitle().endsWith(" at bar 2 beat 1"));
    CHECK(handlerOf(second)->getActions().invoke(juce::AccessibilityActionType::press));
    automation->rebuild();
    CHECK(handlerOf(automation->find({"lane:30", "1"}))->getCurrentState().isSelected());
}

TEST_CASE("A screen reader finds the tempo track's changes and time signatures",
          "[ui][accessibility][reader]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ReaderRunning reader;
    ui::TimelineViewport viewport{260, 0, 48000.0 / 40.0};
    ui::TempoLane lane(ui::Theme::dark(), viewport);
    lane.setBounds(0, 0, 1000, 52);
    core::Project p;
    p.tempoChanges.push_back({8.0, 140.0, true});
    lane.setProject(&p);
    auto* layer = layerIn(lane, "Tempo changes");
    REQUIRE(layer != nullptr);
    layer->rebuild();
    CHECK(handlerOf(layer->find({"tempo:-1"}))->getTitle() == "Tempo 120 BPM at the start");
    CHECK(handlerOf(layer->find({"tempo:0"}))->getTitle() ==
          "Tempo 140 BPM at bar 3 beat 1, ramps from the tempo before");
    CHECK(handlerOf(layer->find({"meter:-1"}))->getTitle() == "Time signature 4/4 at the start");
    CHECK(handlerOf(layer->find({"tempo:0"}))->getActions().invoke(juce::AccessibilityActionType::press));
    layer->rebuild();
    CHECK(handlerOf(layer->find({"tempo:0"}))->getCurrentState().isSelected());
}

TEST_CASE("A screen reader finds the piano roll's notes in time order",
          "[ui][accessibility][reader][pianoroll]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ReaderRunning reader;
    core::Project project;
    core::Track midi;
    midi.id = 1;
    midi.kind = core::TrackKind::midi;
    core::MidiClip clip;
    clip.id = 2;
    clip.name = "Riff";
    clip.length = 16 * beat;
    // Higher first in pitch, later in time: read by time.
    clip.notes = {{3, 60, 90, 0, beat / 2, false}, {4, 72, 100, beat, beat, true}};
    midi.midiClips.push_back(clip);
    project.tracks.push_back(midi);
    project.nextId = 10;
    ui::PianoRoll roll;
    roll.setSize(1000, 600);
    roll.setClip(&project, 1, 2);

    auto* layer = layerIn(roll, "Piano roll");
    REQUIRE(layer != nullptr);
    layer->rebuild();
    CHECK(handlerOf(layer->find({"notes"}))->getTitle() == "Riff notes");
    auto* first = layer->find({"notes", "3"});
    auto* second = layer->find({"notes", "4"});
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(handlerOf(first)->getTitle() == "C3, bar 1 beat 1 to bar 1 beat 1 tick 480, velocity 90");
    CHECK(handlerOf(second)->getTitle() == "C4, bar 1 beat 2 to bar 1 beat 3, velocity 100, muted");
    CHECK(first->getExplicitFocusOrder() < second->getExplicitFocusOrder());
    CHECK(handlerOf(second)->getActions().invoke(juce::AccessibilityActionType::press));
    CHECK(roll.selection() == std::vector<core::Id>{4});
}

TEST_CASE("The mixer's name caps and channel strip cells read as controls",
          "[ui][accessibility][reader][mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const ReaderRunning reader;
    core::Project project;
    core::Track bass;
    bass.name = "Bass";
    core::AddTrack add(bass);
    add.setDefaultStrip(plugins::strip::defaultStrip());
    add.apply(project);
    rt::MeterBlock meters;
    ui::MeterReadings readings(meters);
    ui::MixerView mixer(readings, nullptr, nullptr);
    mixer.setBounds(0, 0, 1200, 700);
    mixer.setProject(&project);

    // The name cap: a button named for the channel (its label stays out of the way).
    int caps = 0;
    visit(mixer,
          [&caps](juce::Component& c)
          {
              if (const auto h = c.createAccessibilityHandler();
                  h != nullptr && h->getRole() == juce::AccessibilityRole::button &&
                  h->getTitle() == "Bass" &&
                  h->getActions().contains(juce::AccessibilityActionType::showMenu))
                  ++caps;
          });
    CHECK(caps == 1);

    // The channel strip's cells: one switch per module, on as the module is.
    const auto& strip = project.tracks[0].strip;
    REQUIRE_FALSE(strip.empty());
    auto* cells = layerIn(mixer, "Channel strip modules");
    REQUIRE(cells != nullptr);
    cells->rebuild();
    const auto cell = handlerOf(cells->find({juce::String(strip.front().id)}));
    CHECK(cell->getRole() == juce::AccessibilityRole::toggleButton);
    CHECK(cell->getCurrentState().isCheckable());
    CHECK(cell->getCurrentState().isChecked() == !strip.front().bypass);
}
