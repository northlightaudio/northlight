#include "ui/ColourMenu.h"

namespace northlight::ui
{

int addColourItems(juce::PopupMenu& menu,
                   const Theme& theme,
                   juce::Colour current,
                   int firstId,
                   std::function<void(std::uint32_t argb)> choose)
{
    int id = firstId;
    for (const auto& entry : theme.track.palette)
    {
        juce::Image swatch(juce::Image::ARGB, 14, 14, true);
        {
            juce::Graphics g(swatch);
            g.setColour(entry.value);
            g.fillRoundedRectangle(1.0f, 1.0f, 12.0f, 12.0f, 3.0f);
        }
        auto icon = std::make_unique<juce::DrawableImage>();
        icon->setImage(swatch);
        const auto argb = entry.value.getARGB();
        menu.addItem(juce::PopupMenu::Item(entry.name)
                         .setID(id++)
                         .setTicked(entry.value == current)
                         .setImage(std::move(icon))
                         .setAction([choose, argb] { choose(argb); }));
    }
    menu.addSeparator();
    menu.addItem(juce::PopupMenu::Item("Default").setID(id++).setAction([choose] { choose(0u); }));
    return id;
}

} // namespace northlight::ui
