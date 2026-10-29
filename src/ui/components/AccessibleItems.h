#pragma once

#include "core/TempoMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace northlight::ui
{

// Whether a screen reader or another UI Automation client is listening (Narrator,
// NVDA, Inspect). Forced on by --screen-reader and by tests.
bool accessibilityClientsActive();
void forceAccessibilityClients(bool on);

// A song position as a reader says it: "bar 5 beat 2", with the ticks when it is
// off the beat ("bar 5 beat 2 tick 480"); within a millisecond before a beat, that beat.
juce::String spokenPosition(const core::TempoMap& map, core::SamplePosition at);

// One thing a view paints that a screen reader should find (a clip, a note, a
// marker, an automation point), or a group of them (a track's lane).
struct AccessibleItem
{
    juce::String key; // stable while the thing exists ("clip:3:12"), unique among its siblings
    juce::AccessibilityRole role = juce::AccessibilityRole::listItem;
    juce::String title;          // everything spoken: "Verse, audio clip, bar 5 beat 1 to bar 9 beat 1"
    juce::Rectangle<int> bounds; // the owner's coordinates; empty: not listed
    bool selectable = false, selected = false;
    // A switch (a strip module's cell): press toggles it.
    bool checkable = false, checked = false;
    // The item the keyboard is on (the last one selected by key): it takes the
    // screen reader's focus while the owner has the keyboard focus.
    bool current = false;
    std::function<void()> press;    // UI Automation's Invoke: what a click does (select it)
    std::function<void()> showMenu; // what a right-click opens
    std::vector<AccessibleItem> children;
};

// What UI Automation sees of painted content (design/accessibility-performance.md
// 11.3): a transparent layer over its owner holding one invisible, unclickable
// stand-in component per item on screen. It holds nothing, and costs nothing,
// unless a client is listening: it checks once a second, and rebuilds from
// `build` at most every 100 ms after invalidate(). Nothing is painted, and the
// mouse passes through to the owner. The layer is a group named `title`; a
// group item holds its children, so a reader can skip a track's clips at once.
class AccessibleItems final : public juce::Component, private juce::Timer
{
public:
    // The items in reading order (a lane's clips, a clip's notes: by time).
    using Build = std::function<std::vector<AccessibleItem>()>;
    // The owner adds the layer as its child and gives it its whole area.
    AccessibleItems(juce::Component& owner, juce::String title, Build build);
    ~AccessibleItems() override;

    // What the owner shows changed (a paint, a scroll, a selection): rebuild soon.
    void invalidate();
    // Rebuilds now (or empties the layer when no client listens). Tests call it.
    void rebuild();

    // The stand-in for an item (keys from the top level down), or null.
    juce::Component* find(const std::vector<juce::String>& path) const;
    int count() const; // stand-ins, groups included

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    class Proxy;
    void timerCallback() override;
    bool wanted() const; // a client listens and the owner shows
    void apply(juce::Component& parent,
               std::vector<std::unique_ptr<Proxy>>& proxies,
               const std::vector<AccessibleItem>& items,
               juce::Point<int> origin,
               Proxy*& current);

    juce::Component& owner_;
    Build build_;
    std::vector<std::unique_ptr<Proxy>> proxies_;
    bool dirty_ = true;
    bool active_ = false;
    juce::String currentKey_; // the current item's path, to move the focus only when it changes
};

} // namespace northlight::ui
