#pragma once

#include "core/Model.h"
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

namespace aural::engine
{

struct ParameterInfo
{
    std::string name;
    std::string unit;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    bool logarithmic = false; // frequencies and times feel better on a log scale
};

class InsertProcessor
{
public:
    virtual ~InsertProcessor() = default;

    virtual const std::string& typeName() const noexcept = 0;
    virtual const std::vector<ParameterInfo>& parameters() const noexcept = 0;

    // Controller thread. Allocates and resets state.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Any thread.
    virtual void setParameter(int index, float value) noexcept = 0;
    virtual float parameter(int index) const noexcept = 0;
    void setBypassed(bool bypass) noexcept { bypass_.store(bypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypass_.load(std::memory_order_relaxed); }

    // Real-time. Processes the stereo pair in place.
    virtual void process(float* left, float* right, int numSamples) noexcept = 0;

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

    // Samples of delay the processor adds (for delay compensation, Phase 5).
    virtual int latencySamples() const noexcept { return 0; }

    // Opaque state for processors that own their parameters (hosted plugins):
    // restored once when the instance is created, captured when the project is
    // saved. Built-ins keep everything in the model's parameter list.
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
};

} // namespace aural::engine
