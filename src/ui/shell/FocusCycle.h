#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace northlight::ui
{

// The main window's Tab order (interaction.md 10.4, "Focus model"): JUCE's order
// (explicit focus order, then top to bottom, left to right) with two changes. A
// region listed in `afterChildren` comes after its own controls rather than before
// them, so the arrange view's lanes follow the track headers. And components that
// `skip` names stay out of the cycle, so only the selected track's header and its
// controls are a Tab stop (Up and Down move between tracks). Tab wraps around.
class FocusCycle final : public juce::KeyboardFocusTraverser
{
public:
    struct Rules
    {
        std::vector<const juce::Component*> afterChildren;
        std::function<bool(const juce::Component&)> skip;
    };

    FocusCycle(juce::Component& container, Rules rules);

    juce::Component* getNextComponent(juce::Component* current) override;
    juce::Component* getPreviousComponent(juce::Component* current) override;
    juce::Component* getDefaultComponent(juce::Component* parent) override;
    std::vector<juce::Component*> getAllComponents(juce::Component* parent) override;

private:
    std::vector<juce::Component*> ordered(juce::Component* parent); // skipped ones included
    juce::Component* step(juce::Component* current, int direction);

    juce::Component& container_;
    Rules rules_;
};

} // namespace northlight::ui
