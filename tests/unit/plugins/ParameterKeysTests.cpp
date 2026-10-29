#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

using namespace northlight;

namespace
{
// Every built-in's parameter keys, in parameter order, as saved projects and
// automation name them. A key comes from the parameter's label unless the
// parameter sets an id (engine::InsertProcessor::parameterKey), so renaming a
// label changes the key and older projects lose that value: give the parameter
// an id with the old key before renaming it. Only an appended parameter adds a
// key here.
const std::map<std::string, std::string> goldenKeys{
    {"northlight.gain", "gain"},
    {"northlight.eq", "high-pass low-freq low-gain mid-freq mid-gain mid-q high-freq high-gain"},
    {"northlight.compressor", "threshold ratio attack release knee makeup auto-makeup detector mix"},
    {"northlight.opto", "peak-reduction gain mode emphasis colour mix"},
    {"northlight.glue", "threshold ratio attack release makeup key-hp mix"},
    {"northlight.widener",
     "low-width mid-width high-width low-split high-split mono-below spread spread-mode haas-time "
     "rotation balance guard guard-at output"},
    {"northlight.patina",
     "age pace heat heat-character heat-drive heat-tone heat-breath chamber chamber-space "
     "chamber-mix chamber-decay chamber-tone chamber-breath drift drift-machine drift-wow "
     "drift-flutter drift-rate drift-stereo drift-breath grain grain-source grain-amount "
     "grain-texture grain-follow grain-breath wear wear-loss wear-dropouts wear-skew wear-breath "
     "crush crush-flavour crush-bits crush-rate crush-breath tone low-cut high-cut width output"},
    {"northlight.exciter",
     "frequency drive character amount focus dynamics transients stereo bass-freq bass mix output "
     "auto-gain"},
    {"northlight.orbit", "input bias speed eq level tape hiss flutter output mix link meter"},
    {"northlight.limiter", "ceiling release lookahead"},
    {"northlight.reverb", "pre-delay decay size damping low-cut width mix"},
    {"northlight.delay", "time feedback tone ping-pong mix sync"},
    {"northlight.tapeecho",
     "heads time sync feedback bass treble wow flutter age drive spread stereo spring mix kill-dry "
     "output"},
    {"northlight.bloom",
     "decay size freeze shimmer pitch swell pre-delay pre-delay-sync diffusion modulation rate "
     "low-cut damping tilt duck duck-release gate width mix"},
    {"northlight.vesper",
     "space decay size pre-delay auto-pre-delay pre-delay-sync clarity breaths follow mud low-cut "
     "damping width centre duck release release-sync ends gap mix output lookahead swell catch "
     "halo"},
    {"northlight.amp",
     "bright volume treble middle bass reverb speed intensity reverb-on tremolo-on master boost "
     "input gate sag quality output cabinet-on cabinet mic-a-on mic-a-type mic-a-position "
     "mic-a-distance mic-a-angle mic-a-level mic-a-pan mic-a-polarity mic-b-on mic-b-type "
     "mic-b-position mic-b-distance mic-b-angle mic-b-level mic-b-pan mic-b-polarity room"},
    {"northlight.gravity", "sustain attack blend level"},
    {"northlight.flare", "drive tone level clip"},
    {"northlight.comet", "time repeats mix mod"},
    {"northlight.tide", "rate depth mix mode"},
    {"northlight.polaris", "reference mute"},
    {"northlight.aurora",
     "osc-1-shape osc-1-octave osc-1-level osc-2-shape osc-2-octave osc-2-semitones osc-2-fine "
     "osc-2-level unison spread width sub noise filter-mode cutoff resonance drive filter-envelope "
     "key-track attack decay sustain release filter-attack filter-decay filter-sustain "
     "filter-release lfo-shape lfo-rate lfo-pitch lfo-cutoff lfo-level glide shimmer velocity "
     "level"},
    {"northlight.pulsar",
     "kick-tune kick-decay kick-click kick-punch kick-level kick-pan kick-output rim-tune "
     "rim-decay rim-ring rim-click rim-level rim-pan rim-output snare-tune snare-decay "
     "snare-snappy snare-crack snare-level snare-pan snare-output clap-tune clap-decay clap-bright "
     "clap-spread clap-level clap-pan clap-output closed-hat-tune closed-hat-decay "
     "closed-hat-bright closed-hat-noise closed-hat-level closed-hat-pan closed-hat-output "
     "open-hat-tune open-hat-decay open-hat-bright open-hat-noise open-hat-level open-hat-pan "
     "open-hat-output low-tom-tune low-tom-decay low-tom-noise low-tom-punch low-tom-level "
     "low-tom-pan low-tom-output high-tom-tune high-tom-decay high-tom-noise high-tom-punch "
     "high-tom-level high-tom-pan high-tom-output drive velocity level"},
    {"northlight.nebula",
     "partials brightness odd-even stretch vowel formant drift spread width halo sub breath attack "
     "decay sustain release motion-rate motion space space-mix tone octave velocity level"},
    {"northlight.starbit",
     "wave duty duty-sweep noise-mode octave coarse attack decay sustain release arpeggio arp-rate "
     "vibrato vibrato-rate vibrato-delay sweep sweep-time glide voices bits rate echo velocity "
     "level wave-step-1 wave-step-2 wave-step-3 wave-step-4 wave-step-5 wave-step-6 wave-step-7 "
     "wave-step-8 wave-step-9 wave-step-10 wave-step-11 wave-step-12 wave-step-13 wave-step-14 "
     "wave-step-15 wave-step-16 wave-step-17 wave-step-18 wave-step-19 wave-step-20 wave-step-21 "
     "wave-step-22 wave-step-23 wave-step-24 wave-step-25 wave-step-26 wave-step-27 wave-step-28 "
     "wave-step-29 wave-step-30 wave-step-31 wave-step-32 arp-sync"},
    {"northlight.lumen",
     "hardness velocity noise bell decay damper age bark symmetry bass treble drive tremolo "
     "tremolo-rate tremolo-sync tremolo-depth tremolo-shape phaser-rate phaser-depth "
     "phaser-feedback phaser-mix chorus-rate chorus-depth chorus-mix voices tune level"},
    {"northlight.cirrus",
     "op-1-ratio op-1-fine op-1-detune op-1-fixed op-1-frequency op-1-wave op-1-level "
     "op-1-attack op-1-decay op-1-sustain op-1-release op-1-velocity op-1-key-scale op-2-ratio "
     "op-2-fine op-2-detune op-2-fixed op-2-frequency op-2-wave op-2-level op-2-attack "
     "op-2-decay op-2-sustain op-2-release op-2-velocity op-2-key-scale op-3-ratio op-3-fine "
     "op-3-detune op-3-fixed op-3-frequency op-3-wave op-3-level op-3-attack op-3-decay "
     "op-3-sustain op-3-release op-3-velocity op-3-key-scale op-4-ratio op-4-fine op-4-detune "
     "op-4-fixed op-4-frequency op-4-wave op-4-level op-4-attack op-4-decay op-4-sustain "
     "op-4-release op-4-velocity op-4-key-scale algorithm feedback voice-mode glide "
     "pitch-envelope pitch-time lfo-shape lfo-rate lfo-sync lfo-pitch lfo-tone tone time "
     "high-cut width quality level"},
    {"northlight.sampler",
     "root key-low key-high tune fine velocity attack decay sustain release start end loop "
     "loop-start loop-end loop-fade one-shot cutoff resonance filter-env voices glide level"},
    {"northlight.strip.gate", "threshold range attack hold release key-hp key-lp"},
    {"northlight.strip.eq",
     "low-cut low-cut-slope low-cut-freq band-1 band-1-shape band-1-freq band-1-gain band-1-q "
     "band-2 band-2-shape band-2-freq band-2-gain band-2-q band-3 band-3-shape band-3-freq "
     "band-3-gain band-3-q band-4 band-4-shape band-4-freq band-4-gain band-4-q high-cut "
     "high-cut-slope high-cut-freq output"},
    {"northlight.strip.compressor", "threshold ratio attack release knee makeup auto-makeup detector mix"},
    {"northlight.strip.deesser", "frequency threshold range mode listen"},
    {"northlight.strip.saturation", "drive type tone output mix"},
    {"northlight.strip.limiter", "input ceiling release"},
    {"northlight.strip.opto", "peak-reduction gain mode emphasis colour mix"},
};

std::string joined(const std::vector<std::string>& keys)
{
    std::string s;
    for (const auto& k : keys)
        s += (s.empty() ? "" : " ") + k;
    return s;
}
} // namespace

TEST_CASE("Every built-in keeps the parameter keys saved projects use", "[plugins][parameters]")
{
    auto types = plugins::BuiltInFactory().typeNames();
    for (const auto& t : plugins::strip::moduleTypes())
        types.push_back(t);
    types.push_back(plugins::strip::types::opto);
    CHECK(types.size() == goldenKeys.size()); // a new built-in adds its keys here
    for (const auto& type : types)
    {
        INFO(type);
        const auto it = goldenKeys.find(type);
        REQUIRE(it != goldenKeys.end());
        CHECK(joined(plugins::builtInParameterLayout(type).keys) == it->second);
    }
}
