#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace northlight::ui
{

class ArrangeView;

// The arrange view's editing keys (screens/arrange.md, interaction.md) as one
// table: what each key is, what it matches and what it does. The window forwards
// every key to ArrangeView::handleEditKey, which looks it up here; the first
// binding that matches and acts takes the key.
struct ArrangeKey
{
    const char* what; // "Delete the selection (or the selected marker)"
    std::function<bool(const juce::KeyPress&)> matches;
    // Acts; false when the key is left to others (Left with nothing selected).
    std::function<bool(ArrangeView&, const juce::KeyPress&)> act;
};

class ArrangeKeymap
{
public:
    static const std::vector<ArrangeKey>& bindings();
    static bool handle(ArrangeView& view, const juce::KeyPress& key);
};

} // namespace northlight::ui
