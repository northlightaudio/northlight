#include "ui/Theme.h"

namespace aural::ui
{

const Theme& Theme::dark()
{
    static const Theme theme = []
    {
        Theme t;
        auto& c = t.colours;
        c.background = juce::Colour::fromRGB(0x14, 0x15, 0x18);
        c.surface = juce::Colour::fromRGB(0x1b, 0x1d, 0x21);
        c.surfaceRaised = juce::Colour::fromRGB(0x24, 0x27, 0x2c);
        c.surfaceHover = juce::Colour::fromRGB(0x2c, 0x30, 0x36);
        c.surfacePressed = juce::Colour::fromRGB(0x1f, 0x22, 0x26);
        c.border = juce::Colour::fromRGB(0x2a, 0x2d, 0x33);
        c.borderStrong = juce::Colour::fromRGB(0x3a, 0x3e, 0x46);
        c.textPrimary = juce::Colour::fromRGB(0xe6, 0xe8, 0xeb);
        c.textSecondary = juce::Colour::fromRGB(0x9a, 0x9f, 0xa8);
        c.textDisabled = juce::Colour::fromRGB(0x5c, 0x61, 0x6a);
        c.accent = juce::Colour::fromRGB(0x4f, 0x9c, 0xf5);
        c.accentHover = juce::Colour::fromRGB(0x6c, 0xae, 0xff);
        c.accentText = juce::Colour::fromRGB(0x0b, 0x12, 0x1c);
        c.focus = juce::Colour::fromRGB(0x4f, 0x9c, 0xf5).withAlpha(0.6f);
        c.success = juce::Colour::fromRGB(0x4c, 0xc9, 0x8a);
        c.warning = juce::Colour::fromRGB(0xf0, 0xb4, 0x4c);
        c.danger = juce::Colour::fromRGB(0xf0, 0x5c, 0x5c);
        c.meter = juce::Colour::fromRGB(0x4c, 0xc9, 0x8a);
        c.meterHot = juce::Colour::fromRGB(0xf0, 0xb4, 0x4c);
        c.meterClip = juce::Colour::fromRGB(0xf0, 0x5c, 0x5c);
        return t;
    }();
    return theme;
}

} // namespace aural::ui
