#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PluginMenu.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace northlight;
using ui::EffectCategory;

namespace
{
hosting::PluginInfo
plugin(const char* id, const char* name, const char* maker, const char* category, bool instrument = false)
{
    hosting::PluginInfo p;
    p.id = id;
    p.name = name;
    p.manufacturer = maker;
    p.category = category;
    p.isInstrument = instrument;
    p.path = std::string("C:/VST3/") + id + ".vst3";
    return p;
}

hosting::PluginRegistry sampleRegistry()
{
    hosting::PluginRegistry r;
    r.setFileResults("C:/VST3/a.vst3",
                     1,
                     {plugin("plate", "Pure Plate", "Klanghelm", "Fx|Reverb"),
                      plugin("comp", "VCA Comp", "Klanghelm", "Fx|Dynamics"),
                      plugin("chorus", "Dimension", "Leafshade", "Fx|Modulation"),
                      plugin("tube", "Tube Tape", "Universal Audio (UADx)", "Fx|Distortion"),
                      plugin("mystery", "Mystery Box", "Leafshade", "Fx"),
                      plugin("synth", "Big Synth", "Leafshade", "Instrument|Synth", true)});
    return r;
}

// The items directly in a menu, by their text up to any tab.
std::vector<const juce::PopupMenu::Item*> itemsOf(const juce::PopupMenu& menu)
{
    std::vector<const juce::PopupMenu::Item*> out;
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
        out.push_back(&it.getItem());
    return out;
}

juce::String nameOf(const juce::PopupMenu::Item& item)
{
    return item.text.upToFirstOccurrenceOf("\t", false, false);
}

const juce::PopupMenu* subMenuNamed(const juce::PopupMenu& menu, const juce::String& name)
{
    for (const auto* item : itemsOf(menu))
        if (item->subMenu != nullptr && item->text == name)
            return item->subMenu.get();
    return nullptr;
}
} // namespace

TEST_CASE("An effect's kind comes from what a built-in is or what a plugin reports", "[ui][plugins]")
{
    const auto registry = sampleRegistry();
    CHECK(ui::effectCategory(plugins::types::compressor, &registry) == EffectCategory::dynamics);
    CHECK(ui::effectCategory("northlight.tapeecho", &registry) == EffectCategory::space);
    CHECK(ui::effectCategory("northlight.patina", &registry) == EffectCategory::drive);
    CHECK(ui::effectCategory("northlight.widener", &registry) == EffectCategory::stereo);
    CHECK(ui::effectCategory(plugins::types::eq, &registry) == EffectCategory::eq);
    CHECK(ui::effectCategory("vst3:plate", &registry) == EffectCategory::space);
    CHECK(ui::effectCategory("vst3:comp", &registry) == EffectCategory::dynamics);
    CHECK(ui::effectCategory("vst3:chorus", &registry) == EffectCategory::modulation);
    CHECK(ui::effectCategory("vst3:tube", &registry) == EffectCategory::drive);
    CHECK(ui::effectCategory("vst3:mystery", &registry) == EffectCategory::other);
    CHECK(ui::effectCategory("vst3:unknown", nullptr) == EffectCategory::other);
}

TEST_CASE("The effects menu groups effects by kind, with recent first and makers last", "[ui][plugins]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto registry = sampleRegistry();
    const auto lists = [](const std::string& key)
    {
        return key == "plugins.recent" ? std::vector<std::string>{"northlight.patina", "vst3:plate"}
                                       : std::vector<std::string>{};
    };
    const auto built = ui::buildPluginMenu(ui::PluginKind::effect, &registry, lists, 10);

    // Recent, then the kinds under Effects, then By maker.
    const auto top = itemsOf(built.menu);
    REQUIRE(top.size() > 3);
    CHECK(top[0]->isSectionHeader);
    CHECK(top[0]->text == "Recent");
    CHECK(nameOf(*top[1]) == "Patina");
    CHECK(top[1]->image != nullptr); // an effect's row carries its kind's icon
    CHECK(built.typeFor(top[1]->itemID) == "northlight.patina");
    REQUIRE(subMenuNamed(built.menu, "By maker") != nullptr);

    // Each kind holds its built-ins, then the scanned plugins of that kind.
    const auto* space = subMenuNamed(built.menu, "Reverb & Delay");
    REQUIRE(space != nullptr);
    std::vector<juce::String> names;
    for (const auto* item : itemsOf(*space))
        if (!item->isSeparator)
            names.push_back(nameOf(*item));
    CHECK(names == std::vector<juce::String>{"Reverb", "Delay", "Tape Echo", "Bloom", "Pure Plate"});
    for (const auto* item : itemsOf(*space))
        if (nameOf(*item) == "Pure Plate")
        {
            CHECK(item->text.contains("\tKlanghelm")); // the maker as a hint
            CHECK(built.typeFor(item->itemID) == "vst3:plate");
        }
    const auto* dynamics = subMenuNamed(built.menu, "Dynamics");
    REQUIRE(dynamics != nullptr);
    CHECK(nameOf(*itemsOf(*dynamics).front()) == "Compressor");
    CHECK(subMenuNamed(built.menu, "Modulation") != nullptr); // only a scanned plugin's kind
    CHECK(subMenuNamed(built.menu, "Other") != nullptr);
    // No instrument among the effects.
    for (juce::PopupMenu::MenuItemIterator it(built.menu, true); it.next();)
        CHECK(it.getItem().text != "Big Synth");

    // Every built-in effect is in the menu once under its kind.
    for (const auto& type : plugins::BuiltInFactory().typeNames())
    {
        if (plugins::BuiltInFactory::isInstrument(type))
            continue;
        INFO(type);
        const auto* kind =
            subMenuNamed(built.menu, ui::effectCategoryName(ui::effectCategory(type, &registry)));
        REQUIRE(kind != nullptr);
        int found = 0;
        for (const auto* item : itemsOf(*kind))
            found += built.typeFor(item->itemID) == type ? 1 : 0;
        CHECK(found == 1);
    }
}

TEST_CASE("The instruments menu keeps its built-ins and makers", "[ui][plugins]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto registry = sampleRegistry();
    const auto built = ui::buildPluginMenu(ui::PluginKind::instrument, &registry);
    CHECK(subMenuNamed(built.menu, "Dynamics") == nullptr);
    CHECK(subMenuNamed(built.menu, "Leafshade") != nullptr);
    bool aurora = false;
    for (const auto* item : itemsOf(built.menu))
        aurora = aurora || nameOf(*item) == "Aurora";
    CHECK(aurora);
}

// Not run by default ([.capture]): draws the effects menu and each of its
// submenus with the app's menu drawing, to NORTHLIGHT_TEST_CAPTURE, for a look.
TEST_CASE("Effects menu to PNG", "[.capture]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_TEST_CAPTURE", {});
    REQUIRE(folder.isNotEmpty());
    const auto& theme = ui::Theme::dark();
    ui::NorthlightLookAndFeel laf(theme);
    const auto registry = sampleRegistry();
    const auto lists = [](const std::string& key)
    {
        return key == "plugins.recent" ? std::vector<std::string>{"northlight.glue",
                                                                  "northlight.tapeecho",
                                                                  "northlight.amp",
                                                                  "vst3:plate"}
                                       : std::vector<std::string>{};
    };
    const auto built = ui::buildPluginMenu(ui::PluginKind::effect, &registry, lists);
    const float scale = 2.0f;
    const auto render = [&](const juce::PopupMenu& menu, const juce::String& name, int highlight)
    {
        int width = 0, height = 0;
        std::vector<int> heights;
        for (const auto* item : itemsOf(menu))
        {
            int w = 0, h = 0;
            if (item->isSectionHeader)
            {
                h = theme.sizes.control;
                w = 120;
            }
            else
                laf.getIdealPopupMenuItemSize(item->text, item->isSeparator, 0, w, h);
            width = std::max(width, w + (item->subMenu != nullptr ? 24 : 0));
            heights.push_back(h);
            height += h;
        }
        width = std::max(width, 200) + 8;
        height += 8;
        juce::Image image(
            juce::Image::ARGB, juce::roundToInt(width * scale), juce::roundToInt(height * scale), true);
        {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale));
            g.setColour(theme.colours.surface1);
            g.fillRoundedRectangle(
                juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
                theme.radius.md);
            int y = 4, i = 0;
            for (const auto* item : itemsOf(menu))
            {
                const juce::Rectangle<int> area(4, y, width - 8, heights[static_cast<size_t>(i)]);
                if (item->isSectionHeader)
                    laf.drawPopupMenuSectionHeader(g, area, item->text);
                else
                    laf.drawPopupMenuItem(g,
                                          area,
                                          item->isSeparator,
                                          true,
                                          i == highlight,
                                          item->isTicked,
                                          item->subMenu != nullptr,
                                          item->text,
                                          item->shortcutKeyDescription,
                                          item->image.get(),
                                          nullptr);
                y += heights[static_cast<size_t>(i)];
                ++i;
            }
        } // the drawing lands when its Graphics closes (a native image)
        const auto file = juce::File(folder).getChildFile("menu-" + name + ".png");
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
    };
    render(built.menu, "effects", 6);
    for (const auto* item : itemsOf(built.menu))
        if (item->subMenu != nullptr)
            render(*item->subMenu, item->text.toLowerCase().replace(" & ", "-").replace(" ", "-"), -1);
}
