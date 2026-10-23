#include "plugins/EffectPresets.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"
#include "plugins/Exciter.h"
#include "plugins/Glue.h"
#include "plugins/Patina.h"
#include "plugins/TapeEcho.h"
#include "plugins/Widener.h"

#include <algorithm>
#include <map>

namespace northlight::plugins
{

namespace
{
// One parameter of a preset, by its name ("Threshold") and either a value in
// the parameter's units or, for a stepped one, its choice's text ("Plate").
struct Setting
{
    Setting(const char* p, double v) : param(p), value(v) {}
    Setting(const char* p, const char* c) : param(p), choice(c) {}
    const char* param;
    double value = 0.0;
    const char* choice = nullptr;
};

std::vector<std::string>& errors()
{
    static std::vector<std::string> list;
    return list;
}

// Builds a type's list: each preset starts from the defaults and changes only
// what it names.
class List
{
public:
    explicit List(const std::string& type) : type_(type), infos_(parameterInfo(type))
    {
        for (const auto& info : infos_)
            base_.push_back(info.defaultValue);
    }

    List& add(const char* name, std::initializer_list<Setting> settings)
    {
        EffectPreset preset{name, base_};
        for (const auto& s : settings)
        {
            const auto it = std::find_if(
                infos_.begin(), infos_.end(), [&](const auto& info) { return info.name == s.param; });
            if (it == infos_.end())
            {
                errors().push_back(type_ + " / " + name + ": no parameter \"" + s.param + "\"");
                continue;
            }
            const auto index = static_cast<size_t>(it - infos_.begin());
            float value = static_cast<float>(s.value);
            if (s.choice != nullptr)
            {
                const auto c = std::find(it->choices.begin(), it->choices.end(), std::string(s.choice));
                if (c == it->choices.end())
                {
                    errors().push_back(type_ + " / " + name + " / " + s.param + ": no choice \"" + s.choice +
                                       "\"");
                    continue;
                }
                value = static_cast<float>(c - it->choices.begin());
            }
            if (value < it->minimum || value > it->maximum)
                errors().push_back(type_ + " / " + name + " / " + s.param + ": out of range");
            preset.values[index] = std::clamp(value, it->minimum, it->maximum);
        }
        list_.push_back(std::move(preset));
        return *this;
    }

    std::vector<EffectPreset> done() { return std::move(list_); }

private:
    std::string type_;
    const std::vector<engine::ParameterInfo>& infos_;
    std::vector<float> base_;
    std::vector<EffectPreset> list_;
};

// ----- the lists ----------------------------------------------------------------------------------
// Each list starts with "Default" (the effect as it is added) unless the effect
// names its defaults; the rest are settings a user would reach for first.

std::vector<EffectPreset> gainPresets()
{
    List l(types::gain);
    l.add("Default", {});
    l.add("-18 dB", {{"Gain", -18.0}});
    l.add("-12 dB", {{"Gain", -12.0}});
    l.add("-6 dB", {{"Gain", -6.0}});
    l.add("-3 dB", {{"Gain", -3.0}});
    l.add("+3 dB", {{"Gain", 3.0}});
    l.add("+6 dB", {{"Gain", 6.0}});
    return l.done();
}

std::vector<EffectPreset> eqPresets()
{
    List l(types::eq);
    l.add("Default", {});
    l.add("Low Cut 80 Hz", {{"High-pass", 80.0}});
    l.add("Vocal Presence",
          {{"High-pass", 90.0},
           {"Low freq", 200.0},
           {"Low gain", -2.0},
           {"Mid freq", 3000.0},
           {"Mid gain", 3.0},
           {"Mid Q", 1.0},
           {"High freq", 10000.0},
           {"High gain", 2.5}});
    l.add("De-mud", {{"High-pass", 60.0}, {"Mid freq", 350.0}, {"Mid gain", -4.0}, {"Mid Q", 1.4}});
    l.add("De-harsh", {{"Mid freq", 3500.0}, {"Mid gain", -3.5}, {"Mid Q", 2.0}});
    l.add("Air", {{"High freq", 12000.0}, {"High gain", 4.0}});
    l.add("Kick Tighten",
          {{"High-pass", 30.0},
           {"Low freq", 60.0},
           {"Low gain", 3.0},
           {"Mid freq", 350.0},
           {"Mid gain", -5.0},
           {"Mid Q", 1.5},
           {"High freq", 4000.0},
           {"High gain", 3.0}});
    l.add("Snare Crack",
          {{"High-pass", 80.0},
           {"Low freq", 200.0},
           {"Low gain", 2.0},
           {"Mid freq", 500.0},
           {"Mid gain", -3.0},
           {"Mid Q", 1.2},
           {"High freq", 5000.0},
           {"High gain", 3.0}});
    l.add("Bass Definition",
          {{"High-pass", 30.0},
           {"Low freq", 80.0},
           {"Low gain", 2.0},
           {"Mid freq", 800.0},
           {"Mid gain", 3.0},
           {"Mid Q", 1.2},
           {"High freq", 5000.0},
           {"High gain", -2.0}});
    l.add("Acoustic Guitar",
          {{"High-pass", 80.0},
           {"Low freq", 200.0},
           {"Low gain", -2.0},
           {"Mid freq", 2500.0},
           {"Mid gain", 1.5},
           {"High freq", 10000.0},
           {"High gain", 2.0}});
    l.add("Overheads",
          {{"High-pass", 150.0},
           {"Mid freq", 400.0},
           {"Mid gain", -2.0},
           {"High freq", 10000.0},
           {"High gain", 2.0}});
    l.add("Smile Curve",
          {{"Low freq", 100.0},
           {"Low gain", 3.0},
           {"Mid freq", 800.0},
           {"Mid gain", -2.0},
           {"Mid Q", 0.7},
           {"High freq", 10000.0},
           {"High gain", 3.0}});
    l.add("Telephone",
          {{"High-pass", 400.0},
           {"Mid freq", 1500.0},
           {"Mid gain", 6.0},
           {"Mid Q", 1.0},
           {"High freq", 3000.0},
           {"High gain", -18.0}});
    return l.done();
}

std::vector<EffectPreset> compressorPresets()
{
    List l(types::compressor);
    l.add("Default", {});
    l.add("Vocal Smooth",
          {{"Threshold", -20.0},
           {"Ratio", 3.0},
           {"Attack", 5.0},
           {"Release", 80.0},
           {"Knee", 6.0},
           {"Auto makeup", "On"},
           {"Detector", "RMS"}});
    l.add("Vocal Upfront",
          {{"Threshold", -24.0},
           {"Ratio", 4.0},
           {"Attack", 2.0},
           {"Release", 60.0},
           {"Knee", 3.0},
           {"Auto makeup", "On"}});
    l.add("Gentle Leveling",
          {{"Threshold", -24.0},
           {"Ratio", 1.8},
           {"Attack", 30.0},
           {"Release", 400.0},
           {"Knee", 12.0},
           {"Auto makeup", "On"},
           {"Detector", "RMS"}});
    l.add("Drum Bus",
          {{"Threshold", -16.0},
           {"Ratio", 4.0},
           {"Attack", 10.0},
           {"Release", 100.0},
           {"Knee", 6.0},
           {"Auto makeup", "On"}});
    l.add("Parallel Smash",
          {{"Threshold", -35.0},
           {"Ratio", 10.0},
           {"Attack", 1.0},
           {"Release", 80.0},
           {"Knee", 0.0},
           {"Makeup", 10.0},
           {"Mix", 35.0}});
    l.add("Kick Punch",
          {{"Threshold", -18.0},
           {"Ratio", 4.0},
           {"Attack", 20.0},
           {"Release", 60.0},
           {"Knee", 3.0},
           {"Auto makeup", "On"}});
    l.add("Snare Snap",
          {{"Threshold", -20.0},
           {"Ratio", 5.0},
           {"Attack", 8.0},
           {"Release", 50.0},
           {"Knee", 3.0},
           {"Auto makeup", "On"}});
    l.add("Bass Even",
          {{"Threshold", -22.0},
           {"Ratio", 4.0},
           {"Attack", 15.0},
           {"Release", 150.0},
           {"Knee", 6.0},
           {"Auto makeup", "On"},
           {"Detector", "RMS"}});
    l.add("Acoustic Guitar",
          {{"Threshold", -20.0},
           {"Ratio", 2.5},
           {"Attack", 15.0},
           {"Release", 150.0},
           {"Knee", 8.0},
           {"Auto makeup", "On"}});
    l.add("Mix Bus",
          {{"Threshold", -14.0},
           {"Ratio", 2.0},
           {"Attack", 30.0},
           {"Release", 200.0},
           {"Knee", 10.0},
           {"Makeup", 2.0},
           {"Detector", "RMS"}});
    l.add("Peak Catcher",
          {{"Threshold", -8.0}, {"Ratio", 10.0}, {"Attack", 0.5}, {"Release", 50.0}, {"Knee", 2.0}});
    l.add("Room Squash",
          {{"Threshold", -30.0},
           {"Ratio", 8.0},
           {"Attack", 1.0},
           {"Release", 250.0},
           {"Knee", 0.0},
           {"Makeup", 8.0},
           {"Mix", 50.0}});
    return l.done();
}

std::vector<EffectPreset> optoPresets()
{
    List l(types::opto);
    l.add("Default", {});
    l.add("Vocal Leveler", {{"Peak reduction", 45.0}, {"Gain", 4.0}, {"Emphasis", 20.0}});
    l.add("Gentle Vocal", {{"Peak reduction", 30.0}, {"Gain", 2.0}, {"Emphasis", 10.0}});
    l.add("Vocal Limit", {{"Peak reduction", 55.0}, {"Gain", 5.0}, {"Mode", "Limit"}, {"Emphasis", 25.0}});
    l.add("Bright Source", {{"Peak reduction", 40.0}, {"Gain", 3.0}, {"Emphasis", 70.0}});
    l.add("Bass Smooth", {{"Peak reduction", 50.0}, {"Gain", 4.0}, {"Colour", 40.0}});
    l.add("Acoustic Guitar", {{"Peak reduction", 35.0}, {"Gain", 3.0}, {"Emphasis", 30.0}});
    l.add("Warm Bus", {{"Peak reduction", 25.0}, {"Gain", 1.5}, {"Colour", 50.0}});
    l.add("Clean Leveler", {{"Peak reduction", 40.0}, {"Gain", 3.0}, {"Colour", 0.0}});
    l.add("Valve Colour", {{"Peak reduction", 10.0}, {"Colour", 80.0}});
    l.add("Parallel Opto", {{"Peak reduction", 75.0}, {"Gain", 8.0}, {"Mix", 40.0}});
    l.add("Room Crush",
          {{"Peak reduction", 85.0}, {"Gain", 10.0}, {"Mode", "Limit"}, {"Colour", 60.0}, {"Mix", 50.0}});
    return l.done();
}

std::vector<EffectPreset> gluePresets()
{
    List l(glue::type);
    l.add("Default", {});
    l.add(
        "Mix Bus Glue",
        {{"Threshold", -12.0}, {"Ratio", "2:1"}, {"Attack", "30 ms"}, {"Release", "Auto"}, {"Makeup", 2.0}});
    l.add(
        "Gentle Mix Bus",
        {{"Threshold", -8.0}, {"Ratio", "1.5:1"}, {"Attack", "30 ms"}, {"Release", "Auto"}, {"Makeup", 1.0}});
    l.add("Mastering Touch",
          {{"Threshold", -6.0},
           {"Ratio", "1.5:1"},
           {"Attack", "30 ms"},
           {"Release", "Auto"},
           {"Makeup", 1.0},
           {"Key HP", 100.0}});
    l.add("Drum Bus Punch",
          {{"Threshold", -18.0},
           {"Ratio", "4:1"},
           {"Attack", "10 ms"},
           {"Release", "0.1 s"},
           {"Makeup", 4.0},
           {"Key HP", 80.0}});
    l.add("Drum Smash",
          {{"Threshold", -30.0},
           {"Ratio", "10:1"},
           {"Attack", "0.3 ms"},
           {"Release", "0.1 s"},
           {"Makeup", 12.0},
           {"Mix", 40.0}});
    l.add("Parallel Glue",
          {{"Threshold", -24.0},
           {"Ratio", "4:1"},
           {"Attack", "3 ms"},
           {"Release", "Auto"},
           {"Makeup", 8.0},
           {"Key HP", 100.0},
           {"Mix", 50.0}});
    l.add(
        "Pumping Bus",
        {{"Threshold", -24.0}, {"Ratio", "4:1"}, {"Attack", "1 ms"}, {"Release", "0.3 s"}, {"Makeup", 6.0}});
    l.add(
        "Vocal Bus",
        {{"Threshold", -16.0}, {"Ratio", "2:1"}, {"Attack", "3 ms"}, {"Release", "0.3 s"}, {"Makeup", 3.0}});
    l.add(
        "Bass Bus",
        {{"Threshold", -16.0}, {"Ratio", "4:1"}, {"Attack", "10 ms"}, {"Release", "0.6 s"}, {"Makeup", 3.0}});
    l.add(
        "Guitar Bus",
        {{"Threshold", -14.0}, {"Ratio", "2:1"}, {"Attack", "10 ms"}, {"Release", "0.3 s"}, {"Makeup", 2.0}});
    l.add(
        "Pad Glue",
        {{"Threshold", -15.0}, {"Ratio", "2:1"}, {"Attack", "30 ms"}, {"Release", "1.2 s"}, {"Makeup", 2.0}});
    return l.done();
}

std::vector<EffectPreset> widenerPresets()
{
    List l(widener::type);
    l.add("Default", {});
    l.add("Mix Width", {{"Mid width", 115.0}, {"High width", 130.0}, {"Mono below", 120.0}, {"Guard", "On"}});
    l.add("Safe Wide",
          {{"Low width", 140.0},
           {"Mid width", 140.0},
           {"High width", 140.0},
           {"Mono below", 150.0},
           {"Guard", "On"},
           {"Guard at", 0.2}});
    l.add("Wide Pad",
          {{"Low width", 80.0},
           {"Mid width", 150.0},
           {"High width", 170.0},
           {"Mono below", 150.0},
           {"Guard", "On"}});
    l.add("Air Width", {{"High width", 160.0}, {"High split", 6000.0}});
    l.add("Mono Low End", {{"Low width", 0.0}, {"Low split", 150.0}, {"Mono below", 120.0}});
    l.add("Mono to Stereo", {{"Spread", 60.0}, {"Spread mode", "Diffuse"}, {"Guard", "On"}});
    l.add("Haas Widen", {{"Spread", 50.0}, {"Spread mode", "Haas"}, {"Haas time", 15.0}});
    l.add("Guitar Double", {{"Spread", 70.0}, {"Spread mode", "Haas"}, {"Haas time", 22.0}, {"Guard", "On"}});
    l.add("Centre Focus", {{"Low width", 80.0}, {"Mid width", 70.0}, {"High width", 90.0}});
    l.add("Narrow 50%", {{"Low width", 50.0}, {"Mid width", 50.0}, {"High width", 50.0}});
    l.add("Mono", {{"Low width", 0.0}, {"Mid width", 0.0}, {"High width", 0.0}});
    return l.done();
}

std::vector<EffectPreset> exciterPresets()
{
    List l(exciter::type);
    l.add("Default", {});
    l.add("Vocal Presence",
          {{"Frequency", 3000.0},
           {"Character", "Tube"},
           {"Drive", 8.0},
           {"Amount", 35.0},
           {"Dynamics", -20.0}});
    l.add("Vocal Air", {{"Frequency", 9000.0}, {"Character", "Air"}, {"Drive", 6.0}, {"Amount", 40.0}});
    l.add("Drum Snap",
          {{"Frequency", 2500.0},
           {"Character", "Crisp"},
           {"Drive", 12.0},
           {"Amount", 30.0},
           {"Transients", 60.0}});
    l.add("Bass Growl",
          {{"Frequency", 900.0},
           {"Character", "Tape"},
           {"Drive", 12.0},
           {"Amount", 30.0},
           {"Bass Freq", 90.0},
           {"Bass", 40.0}});
    l.add("Sub Enhance", {{"Amount", 0.0}, {"Bass Freq", 60.0}, {"Bass", 70.0}});
    l.add("Guitar Edge",
          {{"Frequency", 2000.0}, {"Character", "Tape"}, {"Drive", 14.0}, {"Amount", 30.0}, {"Focus", 30.0}});
    l.add(
        "Warm Tape",
        {{"Frequency", 1500.0}, {"Character", "Tape"}, {"Drive", 10.0}, {"Amount", 25.0}, {"Focus", -40.0}});
    l.add("Piano Clarity",
          {{"Frequency", 3500.0},
           {"Character", "Tube"},
           {"Drive", 5.0},
           {"Amount", 25.0},
           {"Transients", 20.0}});
    l.add("Mix Sparkle",
          {{"Frequency", 6000.0}, {"Character", "Air"}, {"Drive", 4.0}, {"Amount", 20.0}, {"Stereo", 30.0}});
    l.add("Wide Shimmer",
          {{"Frequency", 5000.0}, {"Character", "Crisp"}, {"Amount", 30.0}, {"Stereo", 80.0}});
    l.add("Dull Revive", {{"Frequency", 4000.0}, {"Character", "Crisp"}, {"Drive", 10.0}, {"Amount", 50.0}});
    return l.done();
}

std::vector<EffectPreset> limiterPresets()
{
    List l(types::limiter);
    l.add("Default", {});
    l.add("Streaming -1 dB", {{"Ceiling", -1.0}, {"Release", 80.0}, {"Lookahead", 3.0}});
    l.add("Transparent", {{"Ceiling", -1.0}, {"Release", 200.0}, {"Lookahead", 5.0}});
    l.add("Fast Peaks", {{"Ceiling", -0.3}, {"Release", 5.0}, {"Lookahead", 0.5}});
    l.add("Smooth Release", {{"Ceiling", -0.5}, {"Release", 300.0}, {"Lookahead", 3.0}});
    l.add("Bass Safe", {{"Ceiling", -0.5}, {"Release", 150.0}, {"Lookahead", 5.0}});
    l.add("Drum Bus Catch", {{"Ceiling", -3.0}, {"Release", 10.0}, {"Lookahead", 1.0}});
    l.add("Low Latency", {{"Lookahead", 0.0}});
    l.add("Stem -6 dB", {{"Ceiling", -6.0}, {"Release", 100.0}});
    return l.done();
}

std::vector<EffectPreset> reverbPresets()
{
    List l(types::reverb);
    l.add("Default", {});
    l.add("Ambience",
          {{"Pre-delay", 0.0},
           {"Decay", 0.4},
           {"Size", 30.0},
           {"Damping", 9000.0},
           {"Low cut", 200.0},
           {"Mix", 12.0}});
    l.add("Tight Booth",
          {{"Pre-delay", 2.0},
           {"Decay", 0.3},
           {"Size", 10.0},
           {"Damping", 5000.0},
           {"Low cut", 200.0},
           {"Mix", 15.0}});
    l.add("Small Room",
          {{"Pre-delay", 5.0},
           {"Decay", 0.6},
           {"Size", 25.0},
           {"Damping", 7000.0},
           {"Low cut", 150.0},
           {"Mix", 20.0}});
    l.add("Drum Room",
          {{"Pre-delay", 8.0},
           {"Decay", 0.9},
           {"Size", 40.0},
           {"Damping", 8000.0},
           {"Low cut", 100.0},
           {"Mix", 20.0}});
    l.add("Vocal Plate",
          {{"Pre-delay", 25.0},
           {"Decay", 2.2},
           {"Size", 50.0},
           {"Damping", 7500.0},
           {"Low cut", 200.0},
           {"Mix", 22.0}});
    l.add("Bright Plate",
          {{"Pre-delay", 10.0},
           {"Decay", 1.6},
           {"Size", 45.0},
           {"Damping", 12000.0},
           {"Low cut", 250.0},
           {"Mix", 20.0}});
    l.add("Snare Plate",
          {{"Pre-delay", 0.0},
           {"Decay", 1.2},
           {"Size", 35.0},
           {"Damping", 9000.0},
           {"Low cut", 300.0},
           {"Mix", 25.0}});
    l.add("Dark Chamber",
          {{"Pre-delay", 15.0},
           {"Decay", 1.8},
           {"Size", 55.0},
           {"Damping", 2500.0},
           {"Low cut", 120.0},
           {"Mix", 22.0}});
    l.add("Concert Hall",
          {{"Pre-delay", 30.0},
           {"Decay", 3.0},
           {"Size", 85.0},
           {"Damping", 5000.0},
           {"Low cut", 100.0},
           {"Mix", 25.0}});
    l.add("Cathedral",
          {{"Pre-delay", 50.0},
           {"Decay", 7.0},
           {"Size", 100.0},
           {"Damping", 4000.0},
           {"Low cut", 120.0},
           {"Mix", 30.0}});
    l.add("Send: Room",
          {{"Pre-delay", 5.0},
           {"Decay", 0.8},
           {"Size", 35.0},
           {"Damping", 7000.0},
           {"Low cut", 150.0},
           {"Mix", 100.0}});
    l.add("Send: Plate",
          {{"Pre-delay", 15.0},
           {"Decay", 1.8},
           {"Size", 50.0},
           {"Damping", 8000.0},
           {"Low cut", 200.0},
           {"Mix", 100.0}});
    l.add("Send: Hall",
          {{"Pre-delay", 30.0},
           {"Decay", 2.8},
           {"Size", 80.0},
           {"Damping", 6000.0},
           {"Low cut", 150.0},
           {"Mix", 100.0}});
    return l.done();
}

std::vector<EffectPreset> delayPresets()
{
    List l(types::delay);
    l.add("Default", {});
    l.add("Slapback", {{"Time", 110.0}, {"Feedback", 0.0}, {"Tone", 5000.0}, {"Mix", 25.0}});
    l.add("Short Double", {{"Time", 25.0}, {"Feedback", 0.0}, {"Tone", 9000.0}, {"Mix", 30.0}});
    l.add("Eighth Echo", {{"Sync", "1/8"}, {"Feedback", 30.0}, {"Tone", 6000.0}, {"Mix", 22.0}});
    l.add("Dotted Eighth", {{"Sync", "1/8 D"}, {"Feedback", 35.0}, {"Tone", 6000.0}, {"Mix", 22.0}});
    l.add("Quarter Echo", {{"Sync", "1/4"}, {"Feedback", 30.0}, {"Tone", 7000.0}, {"Mix", 20.0}});
    l.add("Ping-pong 1/8", {{"Sync", "1/8"}, {"Ping-pong", "On"}, {"Feedback", 40.0}, {"Mix", 25.0}});
    l.add("Ping-pong Dotted",
          {{"Sync", "1/8 D"}, {"Ping-pong", "On"}, {"Feedback", 40.0}, {"Tone", 6000.0}, {"Mix", 25.0}});
    l.add("Triplet Bounce", {{"Sync", "1/8 T"}, {"Ping-pong", "On"}, {"Feedback", 35.0}, {"Mix", 22.0}});
    l.add("Dub Throw", {{"Sync", "1/4 D"}, {"Feedback", 65.0}, {"Tone", 2500.0}, {"Mix", 35.0}});
    l.add("Dark Tail", {{"Sync", "1/4"}, {"Feedback", 50.0}, {"Tone", 1500.0}, {"Mix", 25.0}});
    l.add("Long Ambient",
          {{"Sync", "1/2"}, {"Ping-pong", "On"}, {"Feedback", 60.0}, {"Tone", 3000.0}, {"Mix", 25.0}});
    l.add("Send: 1/4 Echo", {{"Sync", "1/4"}, {"Feedback", 40.0}, {"Tone", 5000.0}, {"Mix", 100.0}});
    return l.done();
}

std::vector<EffectPreset> tapeEchoPresets()
{
    List l(tapeecho::type);
    l.add("Default", {});
    l.add("Slapback",
          {{"Heads", "Head 1"}, {"Time", 120.0}, {"Feedback", 0.0}, {"Age", 20.0}, {"Mix", 30.0}});
    l.add("Vintage Slap",
          {{"Heads", "Head 1"},
           {"Time", 90.0},
           {"Feedback", 10.0},
           {"Wow", 25.0},
           {"Age", 50.0},
           {"Treble", -3.0},
           {"Drive", 3.0},
           {"Mix", 30.0}});
    l.add("Clean Tape", {{"Feedback", 35.0}, {"Wow", 5.0}, {"Flutter", 3.0}, {"Age", 0.0}});
    l.add("Multi-head Rhythm",
          {{"Heads", "Heads 1 + 3 + 4"}, {"Time", 150.0}, {"Feedback", 40.0}, {"Mix", 30.0}});
    l.add("Dub Echo",
          {{"Heads", "Head 1"},
           {"Sync", "1/4 D"},
           {"Feedback", 65.0},
           {"Bass", 2.0},
           {"Treble", -4.0},
           {"Age", 50.0},
           {"Mix", 35.0}});
    l.add("Ping-pong 1/8",
          {{"Heads", "Head 1"}, {"Sync", "1/8"}, {"Stereo", "Ping-pong"}, {"Feedback", 40.0}, {"Mix", 28.0}});
    l.add("Stereo Tracks",
          {{"Heads", "Heads 2 + 4"},
           {"Time", 250.0},
           {"Feedback", 35.0},
           {"Spread", 100.0},
           {"Stereo", "Stereo"}});
    l.add("Space Wash",
          {{"Heads", "All four heads"},
           {"Time", 200.0},
           {"Feedback", 60.0},
           {"Treble", -2.0},
           {"Spring", 40.0},
           {"Mix", 35.0}});
    l.add("Spring Echo", {{"Heads", "Head 1"}, {"Time", 220.0}, {"Feedback", 30.0}, {"Spring", 60.0}});
    l.add("Worn Tape",
          {{"Feedback", 45.0},
           {"Treble", -4.0},
           {"Wow", 60.0},
           {"Flutter", 40.0},
           {"Age", 85.0},
           {"Drive", 6.0}});
    l.add("Wobbly Lo-fi",
          {{"Feedback", 30.0}, {"Wow", 90.0}, {"Flutter", 70.0}, {"Age", 70.0}, {"Mix", 40.0}});
    l.add("Send: Dub",
          {{"Heads", "Head 1"},
           {"Sync", "1/4 D"},
           {"Feedback", 60.0},
           {"Treble", -4.0},
           {"Age", 50.0},
           {"Mix", 100.0},
           {"Kill dry", "On"}});
    return l.done();
}

std::vector<EffectPreset> bloomPresets()
{
    List l(bloom::type);
    l.add("Default", {});
    l.add("Huge Shimmer",
          {{"Decay", 12.0},
           {"Size", 90.0},
           {"Shimmer", 50.0},
           {"Pitch", "+12"},
           {"Low cut", 200.0},
           {"Damping", 8000.0},
           {"Mix", 35.0}});
    l.add("Fifths Shimmer",
          {{"Decay", 10.0}, {"Size", 80.0}, {"Shimmer", 40.0}, {"Pitch", "+7"}, {"Mix", 30.0}});
    l.add("Crystal Air",
          {{"Decay", 8.0},
           {"Shimmer", 30.0},
           {"Pitch", "+19"},
           {"Low cut", 400.0},
           {"Damping", 14000.0},
           {"Tilt", 3.0},
           {"Mix", 25.0}});
    l.add("Octave Cloud",
          {{"Decay", 15.0},
           {"Size", 90.0},
           {"Shimmer", 35.0},
           {"Pitch", "+12 / -12"},
           {"Modulation", 40.0},
           {"Mix", 35.0}});
    l.add("Endless Pad",
          {{"Decay", 60.0}, {"Size", 100.0}, {"Modulation", 45.0}, {"Rate", 0.2}, {"Mix", 40.0}});
    l.add("Reverse Swell",
          {{"Decay", 4.0}, {"Size", 70.0}, {"Swell", 1500.0}, {"Pre-delay", 0.0}, {"Mix", 40.0}});
    l.add("Slow Bloom",
          {{"Decay", 20.0}, {"Size", 95.0}, {"Shimmer", 20.0}, {"Swell", 3000.0}, {"Mix", 40.0}});
    l.add("Ducked Hall",
          {{"Decay", 5.0},
           {"Size", 75.0},
           {"Pre-delay", 40.0},
           {"Duck", 60.0},
           {"Duck release", 500.0},
           {"Mix", 30.0}});
    l.add("Synced Wash", {{"Decay", 6.0}, {"Pre-delay sync", "1/8"}, {"Duck", 40.0}, {"Mix", 30.0}});
    l.add("Gated Verb",
          {{"Decay", 3.0},
           {"Size", 50.0},
           {"Diffusion", 100.0},
           {"Low cut", 150.0},
           {"Gate", 300.0},
           {"Mix", 30.0}});
    l.add("Warped Space", {{"Decay", 8.0}, {"Modulation", 90.0}, {"Rate", 2.0}, {"Mix", 35.0}});
    l.add("Dark Abyss",
          {{"Decay", 30.0},
           {"Size", 100.0},
           {"Shimmer", 20.0},
           {"Pitch", "-12"},
           {"Low cut", 40.0},
           {"Damping", 1500.0},
           {"Tilt", -4.0},
           {"Mix", 35.0}});
    l.add("Send: Shimmer",
          {{"Decay", 10.0},
           {"Size", 85.0},
           {"Shimmer", 40.0},
           {"Pitch", "+12"},
           {"Low cut", 200.0},
           {"Mix", 100.0}});
    return l.done();
}

// Patina's list lives with it (its card's own display steps through it too).
std::vector<EffectPreset> patinaPresets()
{
    std::vector<EffectPreset> list;
    for (const auto& p : patina::presets())
        list.push_back({p.name, p.values});
    return list;
}

const std::map<std::string, std::vector<EffectPreset>>& all()
{
    static const std::map<std::string, std::vector<EffectPreset>> lists{
        {types::gain, gainPresets()},
        {types::eq, eqPresets()},
        {types::compressor, compressorPresets()},
        {types::opto, optoPresets()},
        {glue::type, gluePresets()},
        {widener::type, widenerPresets()},
        {patina::type, patinaPresets()},
        {exciter::type, exciterPresets()},
        {types::limiter, limiterPresets()},
        {types::reverb, reverbPresets()},
        {types::delay, delayPresets()},
        {tapeecho::type, tapeEchoPresets()},
        {bloom::type, bloomPresets()},
    };
    return lists;
}
} // namespace

const std::vector<EffectPreset>& factoryPresets(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("plugins::factoryPresets");
    static const std::vector<EffectPreset> none;
    const auto& lists = all();
    const auto it = lists.find(type);
    return it != lists.end() ? it->second : none;
}

std::vector<std::string> typesWithPresets()
{
    std::vector<std::string> types;
    for (const auto& [type, list] : all())
        types.push_back(type);
    return types;
}

const std::vector<std::string>& factoryPresetErrors()
{
    all();
    return errors();
}

} // namespace northlight::plugins
