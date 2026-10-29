#include "ui/shell/FocusCycle.h"

#include <algorithm>

namespace northlight::ui
{

FocusCycle::FocusCycle(juce::Component& container, Rules rules)
    : container_(container), rules_(std::move(rules))
{
}

std::vector<juce::Component*> FocusCycle::ordered(juce::Component* parent)
{
    auto all = juce::KeyboardFocusTraverser::getAllComponents(parent);
    for (const auto* region : rules_.afterChildren)
    {
        const auto at = std::find(all.begin(), all.end(), region);
        if (at == all.end())
            continue;
        // Its descendants follow it directly; move it past the last of them.
        auto end = std::next(at);
        while (end != all.end() && region->isParentOf(*end))
            ++end;
        std::rotate(at, std::next(at), end);
    }
    return all;
}

std::vector<juce::Component*> FocusCycle::getAllComponents(juce::Component* parent)
{
    auto all = ordered(parent);
    if (rules_.skip)
        std::erase_if(all, [this](const juce::Component* c) { return rules_.skip(*c); });
    return all;
}

juce::Component* FocusCycle::getDefaultComponent(juce::Component* parent)
{
    const auto all = getAllComponents(parent);
    return all.empty() ? nullptr : all.front();
}

juce::Component* FocusCycle::getNextComponent(juce::Component* current)
{
    return step(current, 1);
}

juce::Component* FocusCycle::getPreviousComponent(juce::Component* current)
{
    return step(current, -1);
}

juce::Component* FocusCycle::step(juce::Component* current, int direction)
{
    // Every focusable component in cycle order, the skipped ones included: from a
    // skipped one (a click focused it) Tab goes to the nearest stop that way.
    const auto all = ordered(&container_);
    const auto isStop = [this](const juce::Component* c) { return !rules_.skip || !rules_.skip(*c); };
    const int count = static_cast<int>(all.size());
    if (count == 0)
        return nullptr;
    const auto at = std::find(all.begin(), all.end(), current);
    // From outside the cycle (the window itself): the first or last stop.
    int index = at != all.end() ? static_cast<int>(at - all.begin()) : (direction > 0 ? -1 : count);
    for (int i = 0; i < count; ++i)
    {
        index = ((index + direction) % count + count) % count;
        if (auto* candidate = all[static_cast<size_t>(index)]; isStop(candidate))
            return candidate;
    }
    return nullptr;
}

} // namespace northlight::ui
