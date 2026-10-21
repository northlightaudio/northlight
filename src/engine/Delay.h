#pragma once

#include "core/Model.h"

#include <compare>
#include <cstddef>
#include <map>
#include <memory>
#include <vector>

// Delay compensation. Every edge of the routing graph (a channel's output, each
// of its sends) carries a fixed delay so that all paths into a bus or the master
// arrive aligned, whatever latency the inserts along them add. The delay lines
// are kept across snapshot recompiles (a fader move must not empty them), so they
// live in a controller-side cache keyed by the edge, like insert instances.

namespace northlight::engine
{

// A stereo fixed delay. setDelay() on the controller thread (allocates when the
// length changes) before the line is shared with a snapshot; process() and
// reset() on the real-time thread. A line a published snapshot can reference is
// never resized: the cache replaces it instead.
class DelayLine
{
public:
    void setDelay(int samples);
    int delay() const noexcept { return delay_; }
    // Real-time: clears the memory (a locate must not replay old audio).
    void reset() noexcept;
    // Controller, before sharing: copies the most recent samples held by `other`
    // so a line that replaces another continues its audio instead of splicing.
    void continueFrom(const DelayLine& other) noexcept;

    // Writes the delayed input to out (in and out may not overlap). A zero delay
    // copies.
    void process(
        const float* inLeft, const float* inRight, float* outLeft, float* outRight, int numSamples) noexcept;

private:
    int delay_ = 0;
    int position_ = 0;
    std::vector<float> left_, right_;
};

struct DelayKey
{
    core::Id source = core::invalidId;
    core::Id destination = core::invalidId; // a bus id, or invalidId for the master
    bool send = false;
    auto operator<=>(const DelayKey&) const = default;
};

// One delay line per graph edge, kept across compiles. Controller thread only.
// A changed delay gets a fresh line (pre-filled from the old one), and the old
// line retires with the snapshots that reference it.
class DelayCache
{
public:
    // The line for an edge: the existing one when its delay matches, else a new
    // one of `delaySamples` continuing the old audio.
    std::shared_ptr<DelayLine> get(const DelayKey& key, int delaySamples);
    // Drops lines no snapshot references any more.
    void prune();
    // Forgets everything (a new project); live snapshots keep their own references.
    void clear() { lines_.clear(); }
    size_t size() const noexcept { return lines_.size(); }

private:
    std::map<DelayKey, std::shared_ptr<DelayLine>> lines_;
};

} // namespace northlight::engine
