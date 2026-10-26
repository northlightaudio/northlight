#include "app/AppMenus.h"
#include "ui/Shortcuts.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

using namespace northlight;

namespace
{
// The items of a menu and its submenus, in order (submenus' items after their header).
void collect(const juce::PopupMenu& menu, std::vector<const juce::PopupMenu::Item*>& out)
{
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.subMenu != nullptr)
            collect(*item.subMenu, out);
        else if (!item.isSeparator)
            out.push_back(&item);
    }
}
const juce::PopupMenu::Item* find(const std::vector<const juce::PopupMenu::Item*>& items,
                                  const juce::String& text)
{
    for (const auto* item : items)
        if (item->text == text)
            return item;
    return nullptr;
}
} // namespace

TEST_CASE("Every project menu item runs its own action, however many templates there are", "[app][menus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    menus::ProjectMenu m;
    for (int i = 0; i < 150; ++i)
        m.templates.push_back(
            juce::File::getCurrentWorkingDirectory().getChildFile("T" + juce::String(i) + ".nlt"));
    m.fxTracks = {"Reverb", "Delay"};
    juce::String opened;
    size_t fx = 99;
    bool exported = false;
    m.newFromTemplate = [&](const juce::File& f) { opened = f.getFileNameWithoutExtension(); };
    m.addFxTrack = [&](size_t i) { fx = i; };
    m.exportMix = [&] { exported = true; };
    const auto menu = menus::projectMenu(m);
    std::vector<const juce::PopupMenu::Item*> items;
    collect(menu, items);
    std::set<int> ids;
    for (const auto* item : items)
        CHECK(ids.insert(item->itemID).second); // no two items share an id
    // The 101st template is a template (with ids in ranges it became an FX track).
    REQUIRE(find(items, "T100") != nullptr);
    find(items, "T100")->action();
    CHECK(opened == "T100");
    find(items, "Delay")->action();
    CHECK(fx == 1);
    find(items, "Export...")->action();
    CHECK(exported);
    // Media management needs a saved project.
    CHECK_FALSE(find(items, "Gather media into the project folder")->isEnabled);
}

TEST_CASE("The snap menu ticks the grid in use and reports the one chosen", "[app][menus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    int chosen = -2;
    const auto menu = menus::snapMenu(true, 4, [&](int d) { chosen = d; });
    std::vector<const juce::PopupMenu::Item*> items;
    collect(menu, items);
    CHECK(find(items, "Beat")->isTicked);
    CHECK_FALSE(find(items, "Off")->isTicked);
    find(items, "1/16")->action();
    CHECK(chosen == 16);
    find(items, "Off")->action();
    CHECK(chosen == -1);
}

TEST_CASE("Menu items show their current key from the keyboard map", "[app][menus][shortcuts]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::Shortcuts::resetAll();
    menus::ProjectMenu m;
    m.save = [] {};
    m.exportMix = [] {};
    m.addBus = [] {};
    const auto keyOf = [](const juce::PopupMenu& menu, const juce::String& text)
    {
        std::vector<const juce::PopupMenu::Item*> items;
        collect(menu, items);
        const auto* item = find(items, text);
        return item != nullptr ? item->shortcutKeyDescription : juce::String("(no item)");
    };
    CHECK(keyOf(menus::projectMenu(m), "Save") == "Ctrl+S");
    CHECK(keyOf(menus::projectMenu(m), "Export...") == "Ctrl+E");
    CHECK(keyOf(menus::projectMenu(m), "Add bus") == "Ctrl+B");
    CHECK(keyOf(menus::projectMenu(m), "Save as template...").isEmpty());
    menus::AppMenu a;
    a.preferences = [] {};
    CHECK(keyOf(menus::appMenu(a), "Preferences...") == "Ctrl+,");
    // A rebinding shows the next time the menu is built.
    ui::Shortcuts::bind(ui::Command::save, ui::Key{'W', juce::ModifierKeys::commandModifier});
    CHECK(keyOf(menus::projectMenu(m), "Save") == "Ctrl+W");
    ui::Shortcuts::resetAll();
}
