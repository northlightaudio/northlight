#pragma once

#include <array>
#include <string>
#include <vector>

// The amp's speaker cabinet and microphones as a model that builds an impulse
// response (we ship no captured responses). Each cabinet is a speaker's
// response (its low resonance, the open or closed back, its cone breakup peaks
// and its roll-off) with the layout of its speakers and its height off the
// floor. Each mic has a type (dynamic, ribbon or condenser: its own curve and
// pickup pattern), a place on the cone (the cap is bright, the edge dark), a
// distance (proximity effect up close for the directional types, the other
// speakers and the floor reflection joining as it moves back, the open back's
// rear wave behind) and an angle off axis (duller). The two mics' distances set
// a real time-of-flight offset between them, so blending them combs as two
// real mics do. A room adds early reflections. The direct sound is made minimum
// phase from its magnitude (by the real cepstrum) and the delays are fractional.
// A user's own response file can stand in for the whole cabinet.
//
// Building is not real-time: the amp builds on a background thread and swaps
// the result in. JUCE-free.

namespace northlight::plugins::cabinet
{

enum Model
{
    open2x12,   // the 1960s American combo's own: two 12" in an open back
    open1x12,   // one 12", open back
    closed4x10, // four 10" in a closed back
    bass1x15,   // a bass cabinet: one 15"
    bass4x10,   // a bass cabinet: four 10"
    custom,     // the user's own impulse response
    modelCount
};

enum MicType
{
    dynamic,   // a cardioid moving coil: presence peak, tight lows
    ribbon,    // a figure-8 ribbon: dark, smooth, big proximity effect
    condenser, // a cardioid condenser: flat and open
    micTypeCount
};

struct Mic
{
    bool on = true;
    int type = dynamic;
    float position = 0.3f;   // 0 the dust cap .. 1 the cone's edge
    float distanceCm = 3.0f; // 1 .. 100
    float angleDeg = 0.0f;   // 0 on axis .. 45
    float levelDb = 0.0f;
    float pan = 0.0f;    // -1 left .. 1 right
    bool invert = false; // polarity
    bool operator==(const Mic&) const = default;
};

struct Settings
{
    int model = open2x12;
    std::array<Mic, 2> mics{};
    float room = 0.1f; // 0..1
    bool operator==(const Settings&) const = default;
};

// A stereo response (right equal to left when the mics sit in the middle).
struct Response
{
    std::vector<float> left, right;
};

// A user's response file, decoded: at its own rate, right empty for mono.
struct UserResponse
{
    double sampleRate = 48000.0;
    std::vector<float> left, right;
};

// The built responses' length at a rate (a power of two, about 40 ms).
int responseLength(double sampleRate) noexcept;
// The longest user response kept (longer files are cut, with a fade).
inline constexpr double maxUserSeconds = 0.5;

// Not real-time. With model custom and no user response, an impulse (the amp
// passes uncoloured).
Response build(const Settings& settings, double sampleRate, const UserResponse* user = nullptr);

// One mic's direct sound (the speaker and the mic, no reflections), in dB at
// `hz`, normalised so that the band 150 Hz - 4 kHz averages 0 dB. For the
// tests and displays.
double directDb(int model, const Mic& mic, double hz) noexcept;

// What the card draws: each speaker's centre in the cabinet's front, in
// metres from the mic's speaker (the first is that speaker, at 0, 0), and the
// speakers' diameter.
struct Layout
{
    std::vector<std::array<double, 2>> speakers;
    double diameter = 0.3;
    double height = 0.45; // the speakers' centre above the floor
    bool openBack = true;
};
Layout layout(int model);

const char* modelName(int model) noexcept;
const char* micTypeName(int type) noexcept;

} // namespace northlight::plugins::cabinet
