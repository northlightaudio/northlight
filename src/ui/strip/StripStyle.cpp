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
    };
    const auto it = hues.find(type);
    return it != hues.end() ? paletteColour(theme, it->second) : theme.colours.textSecondary;
}

} // namespace northlight::ui::strip
