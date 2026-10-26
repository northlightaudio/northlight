#include "ui/arrange/ArrangeKeymap.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
// What the key reaches: the first binding that matches it (or null).
const char* firstMatch(const juce::KeyPress& key)
{
    for (const auto& binding : ui::ArrangeKeymap::bindings())
        if (binding.matches(key))
            return binding.what;
    return nullptr;
}
} // namespace

TEST_CASE("Every arrange key reaches a binding that says what it does", "[ui][arrange][keys]")
{
    using Key = juce::KeyPress;
    using Mods = juce::ModifierKeys;
    for (const auto& key : {Key(Key::deleteKey),
                            Key('m'),
                            Key('m', Mods::altModifier, 0),
                            Key(','),
                            Key('d', Mods::commandModifier, 0),
                            Key('v', Mods::commandModifier, 0),
                            Key('1'),
                            Key('7'),
                            Key('m', Mods::commandModifier, 0),
                            Key(Key::upKey, Mods::altModifier, 0),
                            Key('t', Mods::altModifier, 0),
                            Key(Key::leftKey, Mods::altModifier, 0),
                            Key('a'),
                            Key('a', Mods::commandModifier, 0),
                            Key(Key::escapeKey),
                            Key('s'),
                            Key('s', Mods::altModifier, 0),
                            Key(Key::rightKey)})
        CHECK(firstMatch(key) != nullptr);
    CHECK(firstMatch(juce::KeyPress('q')) == nullptr);
    // Snap and ripple are the window's N and Shift+N; G and E are not arrange keys.
    CHECK(firstMatch(juce::KeyPress('g')) == nullptr);
    CHECK(firstMatch(juce::KeyPress('e')) == nullptr);
    // A key two bindings could take goes to the first ('M' typed alone is not Alt+M).
    CHECK(juce::String(firstMatch(juce::KeyPress('m'))) == "Add a marker at the playhead");
    CHECK(juce::String(firstMatch(juce::KeyPress('m', juce::ModifierKeys::altModifier, 0))) ==
          "Add a region from the loop");
    // Shift+M is the window's (mute the selected track), so the arrange view leaves it.
    CHECK(firstMatch(juce::KeyPress('m', juce::ModifierKeys::shiftModifier, 0)) == nullptr);
    for (const auto& binding : ui::ArrangeKeymap::bindings())
        CHECK(juce::String(binding.what).isNotEmpty());
}
