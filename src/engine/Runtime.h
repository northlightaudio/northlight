#pragma once

#include "core/Model.h"

#include <atomic>
#include <map>
#include <memory>

// Per-channel run-time state that outlives snapshots: the fader gain and pan the
// renderer is currently at, and the targets the controller wants. The renderer
// ramps from current to target across every block, so a fader or pan move is a
// glide, not a step, and a target can change without a recompile (live
// parameter changes travel this way). Controller thread writes the targets
// (atomics); only the real-time thread touches the current values.

namespace aural::engine
{

struct ChannelRuntime
{
    std::atomic<float> targetGain{1.0f};
    std::atomic<float> targetPan{0.0f};
    float gain = 1.0f;       // real-time: where the ramp is
    float pan = 0.0f;        // real-time
    bool gainPrimed = false; // real-time: the first block jumps to the target instead of ramping
    bool panPrimed = false;  // from the defaults (gain and pan are set in different stages)
};

struct MasterRuntime
{
    std::atomic<float> targetGain{1.0f};
    float gain = 1.0f;
    bool primed = false;
};

// One runtime per channel id, kept across compiles. Controller thread only.
class RuntimeCache
{
public:
    std::shared_ptr<ChannelRuntime> get(core::Id channelId)
    {
        auto& r = channels_[channelId];
        if (r == nullptr)
            r = std::make_shared<ChannelRuntime>();
        return r;
    }
    std::shared_ptr<ChannelRuntime> find(core::Id channelId) const
    {
        const auto it = channels_.find(channelId);
        return it != channels_.end() ? it->second : nullptr;
    }
    std::shared_ptr<MasterRuntime> master()
    {
        if (master_ == nullptr)
            master_ = std::make_shared<MasterRuntime>();
        return master_;
    }
    // Drops runtimes no snapshot references any more.
    void prune()
    {
        std::erase_if(channels_, [](const auto& entry) { return entry.second.use_count() == 1; });
    }
    void clear()
    {
        channels_.clear();
        master_.reset();
    }

private:
    std::map<core::Id, std::shared_ptr<ChannelRuntime>> channels_;
    std::shared_ptr<MasterRuntime> master_;
};

} // namespace aural::engine
