#pragma once

#include "engine/Snapshot.h"
#include "engine/Transport.h"

#include <vector>

// Renders one block of a Snapshot at the transport position into stereo outputs.
// Phase 2: every track mixes straight to the master; sends, buses and inserts
// arrive with the mixer in Phase 4. Real-time: render() allocates nothing after
// prepare().

namespace aural::engine
{

class Renderer
{
public:
    // Controller thread: allocates scratch for blocks up to maxBlockSize.
    void prepare(int maxBlockSize);
    int maxBlockSize() const noexcept { return maxBlockSize_; }

    // Real-time. Advances `transport` by numSamples when playing (honouring the
    // loop), and writes (adds nothing; overwrites) the mix into outputs. Outputs
    // beyond the first two are cleared. numSamples must not exceed maxBlockSize.
    void render(const Snapshot& snapshot,
                TransportState& transport,
                float* const* outputs,
                int numOutputs,
                int numSamples) noexcept;

    // Pan laws, exposed for tests. Mono sources use constant power; stereo sources
    // use a balance law that leaves the centre untouched.
    static void monoPanGains(float pan, float& left, float& right) noexcept;
    static void stereoPanGains(float pan, float& left, float& right) noexcept;

private:
    void renderSpan(const Snapshot& snapshot,
                    core::SamplePosition position,
                    float* left,
                    float* right,
                    int numSamples) noexcept;

    int maxBlockSize_ = 0;
    std::vector<float> scratchLeft_, scratchRight_;
    std::vector<float> mixLeft_, mixRight_;
};

} // namespace aural::engine
