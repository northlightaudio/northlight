#include "ui/components/KeyMenu.h"

#include "core/NoteNames.h"

#include <array>

namespace northlight::ui::keymenu
{

namespace
{
constexpr std::array<core::ScaleKind, 7> scales{core::ScaleKind::major,
                                                core::ScaleKind::minor,
                                                core::ScaleKind::harmonicMinor,
                                                core::ScaleKind::dorian,
                                                core::ScaleKind::mixolydian,
                                                core::ScaleKind::majorPentatonic,
                                                core::ScaleKind::minorPentatonic};
constexpr int firstKeyItem = 100;
} // namespace

juce::PopupMenu build(core::MusicalKey current, bool offerDetect, bool offerNone)
{
    juce::PopupMenu menu;
    if (offerDetect)
        menu.addItem(detectItem, "Detect from this vocal");
    if (offerNone)
        menu.addItem(noneItem, "No key", true, !current.isSet());
    if (offerDetect || offerNone)
        menu.addSeparator();
    for (size_t s = 0; s < scales.size(); ++s)
    {
        juce::PopupMenu roots;
        for (int r = 0; r < 12; ++r)
            roots.addItem(firstKeyItem + static_cast<int>(s) * 12 + r,
                          juce::String(core::pitchClassName(r)) + " " + core::scaleName(scales[s]),
                          true,
                          current.isSet() && current.root == r && current.scale == scales[s]);
        const juce::String title = core::scaleName(scales[s]);
        menu.addSubMenu(title.substring(0, 1).toUpperCase() + title.substring(1),
                        roots,
                        true,
                        nullptr,
                        current.isSet() && current.scale == scales[s]);
    }
    return menu;
}

std::optional<core::MusicalKey> keyFor(int choice)
{
    if (choice == noneItem)
        return core::MusicalKey{};
    if (choice < firstKeyItem || choice >= firstKeyItem + static_cast<int>(scales.size()) * 12)
        return std::nullopt;
    const int index = choice - firstKeyItem;
    return core::MusicalKey{index % 12, scales[static_cast<size_t>(index / 12)]};
}

juce::String name(core::MusicalKey key)
{
    if (!key.isSet())
        return "No key";
    return juce::String(core::pitchClassName(key.root)) + " " + core::scaleName(key.scale);
}

} // namespace northlight::ui::keymenu
