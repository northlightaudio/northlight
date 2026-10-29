#include "ui/strip/StripStyle.h"

#include "plugins/Amp.h"
#include "plugins/Aurora.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"
#include "plugins/Exciter.h"
#include "plugins/Glue.h"
#include "plugins/Nebula.h"
#include "plugins/Orbit.h"
#include "plugins/Patina.h"
#include "plugins/Pedals.h"
#include "plugins/Pulsar.h"
#include "plugins/Sampler.h"
#include "plugins/Starbit.h"
#include "plugins/TapeEcho.h"
#include "plugins/Vesper.h"
#include "plugins/Widener.h"

#include <unordered_map>

namespace northlight::ui::strip
{

void drawFrequencyGrid(juce::Graphics& g,
                       const Theme& theme,
                       juce::Rectangle<float> p,
                       std::span<const double> lines,
                       float labelGap)
{
    const auto& c = theme.colours;
    g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.regular));
    for (const double hz : lines)
    {
        const float x = hzToX(hz, p);
        const bool decade = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
        g.setColour(decade ? c.gridBar : c.gridBeat);
        g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
        if (hz < 40.0 || hz >= 20000.0)
            continue;
        g.setColour(c.textTertiary);
        const auto label = hz >= 1000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                                        : juce::String(static_cast<int>(hz));
        g.drawText(label,
                   juce::Rectangle<float>(x - 16.0f, p.getBottom() + labelGap, 32.0f, 12.0f),
                   juce::Justification::centred);
    }
}

juce::Colour moduleColour(const Theme& theme, const std::string& type)
{
    namespace p = plugins;
    namespace t = plugins::strip::types;
    // Each type's track palette colour, by name.
    static const std::unordered_map<std::string, const char*> hues{
        {t::gate, "Sea grass"},
        {t::eq, "Overcast"},
        {t::compressor, "Loch"},
        {t::deesser, "Heather"},
        {t::saturation, "Kelp"},
        {t::limiter, "Rowan"},
        {t::opto, "Straw"}, // warm, like the cell's lamp
        // The built-ins, in the same family (a built-in EQ, compressor or limiter
        // takes its strip counterpart's hue).
        {p::types::eq, "Overcast"},
        {p::types::compressor, "Loch"},
        {p::types::opto, "Straw"},
        {p::glue::type, "Thrift"}, // apart from the strip compressors' Loch and Straw
        {p::widener::type, "Sea grass"},
        {p::patina::type, "Kelp"}, // the colour family of the strip saturation
        {p::exciter::type, "Machair"},
        {p::types::limiter, "Rowan"},
        {p::types::gain, "Machair"},
        {p::types::reverb, "Lichen"},
        {p::types::delay, "Gorse"},
        {p::amp::type, "Straw"}, // the warm glow of the valves
        {p::tapeecho::type, "Rowan"},
        {p::orbit::type, "Slate"}, // the machine's aluminium
        // The pedals take their family's hue: the compressor's Loch, the delay's Gorse.
        {p::gravity::type, "Loch"},
        {p::flare::type, "Rowan"},
        {p::comet::type, "Gorse"},
        {p::tide::type, "Sea grass"},
        {p::polaris::type, "Overcast"},
        {p::bloom::type, "Heather"}, // dusk over the moor, apart from the plain reverb's Lichen
        {p::vesper::type, "Thrift"}, // the afterglow
        {p::aurora::type, "Heather"},
        {p::pulsar::type, "Rowan"},
        {p::nebula::type, "Thrift"},
        {p::starbit::type, "Machair"},
        {p::sampler::type, "Lichen"},
        // The MIDI effects: one family, the colour of notes (design-system.md "MIDI face").
        {"northlight.arpeggiator", "Heather"},
        {"northlight.chord", "Heather"},
        {"northlight.chordpads", "Heather"},
        {"northlight.noterepeat", "Heather"},
        {"northlight.noteecho", "Heather"},
        {"northlight.scale", "Heather"},
        {"northlight.transpose", "Heather"},
        {"northlight.velocity", "Heather"},
        {"northlight.notelength", "Heather"},
        {"northlight.humanise", "Heather"},
        {"northlight.keyrange", "Heather"},
        {"northlight.midifilter", "Heather"},
    };
    const auto it = hues.find(type);
    return it != hues.end() ? paletteColour(theme, it->second) : theme.colours.textSecondary;
}

} // namespace northlight::ui::strip
