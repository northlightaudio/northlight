#pragma once

#include "core/Model.h"
#include "engine/Feedback.h"
#include "engine/Midi.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Inserts: processors in a channel's chain, between the pre-fader render and the
// fader. The engine owns the interface and the per-insert instance cache; the
// built-in processors live in src/plugins and VST3 hosting joins in Phase 5 behind
// the same interface.
//
// Threading: prepare() and parameter setters run on the controller thread;
// process() runs on a real-time thread. Parameters are atomics the processor reads
// at the start of each block, so a parameter change never needs a new instance and
// the processor's state (filter memory, envelopes) survives snapshot recompiles.

namespace northlight::engine
{

struct ParameterInfo
{
    std::string name;
    std::string unit;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    bool logarithmic = false; // frequencies and times feel better on a log scale
    // A stepped parameter's choices, by value (0, 1, ...); empty for a continuous one.
    std::vector<std::string> choices;
    // A stable key for saved projects and automation; empty: derived from the
    // name (see parameterKey). Set it before renaming a parameter.
    std::string id;
};

// Where the song is for a block, for tempo-synced processors (an arpeggio, a
// delay in note values) and hosted plugins' transport: the tempo and signature
// in effect at the block's first sample (from the project's tempo map).
struct PlayHead
{
    double tempoBpm = 120.0;   // quarter notes a minute
    double quarterNotes = 0.0; // the song position at the block's first sample
    std::int64_t samples = 0;  // the same, in samples
    int numerator = 4, denominator = 4;
    double barStartQuarters = 0.0; // where the bar holding the position began
    int bar = 0;                   // that bar, 0-based
    bool playing = false;          // the position moves (stopped: it holds)
    double sampleRate = 48000.0;
    double quartersPerSample() const noexcept { return tempoBpm / 60.0 / sampleRate; }
};

class InsertProcessor
{
public:
    virtual ~InsertProcessor() = default;

    virtual const std::string& typeName() const noexcept = 0;
    virtual const std::vector<ParameterInfo>& parameters() const noexcept = 0;
    // The stable key of a parameter, saved with projects so values and
    // automation survive the processor reordering its parameters. Built-ins
    // derive it from the parameter's id (or name); a hosted plugin uses its own
    // parameter id. Not real-time.
    virtual std::string parameterKey(int index) const;

    // Controller thread. Allocates and resets state.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Any thread.
    virtual void setParameter(int index, float value) noexcept = 0;
    virtual float parameter(int index) const noexcept = 0;
    // A parameter drag in the generic editor: a hosted plugin turns these into its
    // gesture events (one undo step, automation touch). Defaults do nothing.
    virtual void beginParameterGesture(int) noexcept {}
    virtual void endParameterGesture(int) noexcept {}
    // A parameter the plugin moved itself (its own editor), for automation
    // writing: the value, and the plugin's gesture begin and end. The message
    // thread takes them; a processor that reports none returns false.
    struct ParameterEdit
    {
        enum class Kind : std::uint8_t
        {
            value,
            begin,
            end,
        };
        Kind kind = Kind::value;
        int index = 0;
        float value = 0.0f;
    };
    virtual bool takeParameterEdit(ParameterEdit&) noexcept { return false; }
    // True once after the processor's own state changed where the host does not
    // see it as a parameter (its editor loaded a preset or changed a setting): the
    // project then needs saving. Message thread.
    virtual bool takeStateChanged() noexcept { return false; }
    // True when the processor's own state (presets, its editor) is the authority
    // for its parameters: the cache then pushes the model's parameter list only
    // when a command changes it, never on a compile, so a preset loaded from the
    // plugin's menu is not reverted by the next edit elsewhere.
    virtual bool ownsParameters() const noexcept { return false; }
    // A hosted plugin that crashed while processing: it is no longer called and
    // its slot passes audio through. The session tells the user once.
    virtual bool crashed() const noexcept { return false; }
    // True when the processor must be recreated (a plugin asked for a restart or
    // changed its parameter set): the cache replaces it, carrying the state over,
    // and the next snapshot takes the new instance.
    virtual bool needsRestart() const noexcept { return false; }
    void setBypassed(bool bypass) noexcept { bypass_.store(bypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypass_.load(std::memory_order_relaxed); }

    // Real-time. Processes the stereo pair in place.
    virtual void process(float* left, float* right, int numSamples) noexcept = 0;

    // Real-time: the song's position and tempo for the block about to be
    // processed (the renderer calls it before every process call).
    virtual void setPlayHead(const PlayHead&) noexcept {}

    // Sidechain (key) input. A processor that accepts one gets the source's
    // signal alongside its own; the default ignores it.
    virtual bool acceptsSidechain() const noexcept { return false; }
    virtual void
    processWithSidechain(float* left, float* right, const float*, const float*, int numSamples) noexcept
    {
        process(left, right, numSamples);
    }

    // Instruments (and MIDI effects) take the block's events alongside the audio:
    // the buffers hold what came before in the chain (silence at the head of a
    // MIDI track) and the processor replaces or adds to them. The default ignores
    // the events. Offsets are within the block, in time order.
    virtual bool acceptsMidi() const noexcept { return false; }
    virtual void processMidi(const MidiEventList&, float* left, float* right, int numSamples) noexcept
    {
        process(left, right, numSamples);
    }

    // Extra stereo outputs (a multi-output instrument): after a process call the
    // pairs beyond the main one are read here, index 0 = the first extra pair,
    // channel 0/1; valid until the next process call on the real-time thread.
    virtual int auxOutputCount() const noexcept { return 0; }
    virtual const float* auxOutput(int, int) const noexcept { return nullptr; }
    // What an extra pair carries ("Kick", "Hat + Open"), for naming its output
    // track; empty for no name. Index as auxOutput. Not real-time safe.
    virtual std::string auxOutputName(int) const { return {}; }

    // Live values for an editor's displays (meters, gain reduction, spectrum);
    // null for a processor that publishes none.
    virtual ProcessorFeedback* feedback() noexcept { return nullptr; }

    // Samples of delay the processor adds (for delay compensation, Phase 5).
    virtual int latencySamples() const noexcept { return 0; }

    // Opaque state for processors that own their parameters (hosted plugins):
    // restored once when the instance is created, captured when the project is
    // saved. Built-ins keep their parameters in the model's list; one that has
    // state beyond them (the Sampler's file) keeps it in the model's state too,
    // and the cache passes it on whenever a command changes it.
    virtual std::string state() const { return {}; }
    virtual void setState(const std::string&) {}

private:
    std::atomic<bool> bypass_{false};
};

// Creates processors by type name; the app supplies the built-ins (and later the
// hosted plugins).
class InsertFactory
{
public:
    virtual ~InsertFactory() = default;
    virtual std::vector<std::string> typeNames() const = 0;
    virtual std::unique_ptr<InsertProcessor> create(const std::string& typeName) const = 0;
};

// One processor instance per model insert id, kept across snapshot recompiles so
// state persists; parameters are pushed on every compile. Controller thread only.
class InsertCache
{
public:
    explicit InsertCache(const InsertFactory& factory) : factory_(factory) {}

    // Called when the device starts: new instances get this rate, existing ones are
    // re-prepared.
    void prepare(double sampleRate, int maxBlockSize);

    // The instance for a model insert, created (and prepared) on first sight;
    // nullptr when the type is unknown. Parameters and bypass are applied; the
    // model's state blob only on creation.
    std::shared_ptr<InsertProcessor> get(const core::Insert& insert);
    // The live instance for an insert id, if one exists.
    std::shared_ptr<InsertProcessor> find(core::Id insertId) const;
    // Whether any instance reports a different latency than it had when last
    // compiled (a plugin changed its lookahead): the snapshot needs recompiling.
    bool latencyChanged() const noexcept;
    // Whether any instance asked to be recreated: the snapshot needs recompiling
    // (get() then makes the replacement).
    bool restartRequested() const noexcept;

    // Forgets one instance so the next get() makes a fresh one from the model's
    // state (reloading a crashed plugin). A crashed instance is kept alive for
    // good rather than destroyed: its destructor would run the plugin's code.
    void forget(core::Id insertId);

    // Drops instances no snapshot references any more.
    void prune();
    // Forgets every instance (snapshots keep theirs alive until they retire).
    void clear();

private:
    const InsertFactory& factory_;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 8192;
    std::map<core::Id, std::shared_ptr<InsertProcessor>> instances_;
    std::map<core::Id, int> latencyAtCompile_;
    std::map<core::Id, std::string> appliedState_; // the model state an instance last took
};

} // namespace northlight::engine
