#pragma once

#include "core/Model.h"

#include <atomic>
#include <cstdint>
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
    // Automation (real-time): a lane playing overrides the target for the span.
    bool gainAutomated = false, panAutomated = false;
    float automatedGain = 1.0f, automatedPan = 0.0f;
    // What automation last played, for the UI (relaxed; `automated` bit 0 gain, bit 1 pan).
    std::atomic<float> shownGain{1.0f}, shownPan{0.0f};
    std::atomic<int> automated{0};
};

// One automation lane's run-time state, kept across compiles: while the user
// touches its control (writing), the lane is not played.
struct AutomationRuntime
{
    std::atomic<bool> touched{false};
};

struct MasterRuntime
{
    std::atomic<float> targetGain{1.0f};
    float gain = 1.0f;
    bool primed = false;
};

// One runtime per channel id, kept across compiles. Controller thread only.
// What a MIDI track's instrument currently holds, and where the last span ended,
// so a stop, a jump or a loop wrap can end the notes still sounding. Real-time
// thread only (the track's own render job).
struct MidiRuntime
{
    std::uint8_t held[128]{};          // 1 while sounding
    core::SamplePosition lastEnd = -1; // where the previous playing span ended
    bool wasPlaying = false;
};

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
    std::shared_ptr<MidiRuntime> midi(core::Id trackId)
    {
        auto& r = midi_[trackId];
        if (r == nullptr)
            r = std::make_shared<MidiRuntime>();
        return r;
    }
    std::shared_ptr<AutomationRuntime> automation(core::Id laneId)
    {
        auto& r = automation_[laneId];
        if (r == nullptr)
            r = std::make_shared<AutomationRuntime>();
        return r;
    }
    std::shared_ptr<AutomationRuntime> findAutomation(core::Id laneId) const
    {
        const auto it = automation_.find(laneId);
        return it != automation_.end() ? it->second : nullptr;
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
        std::erase_if(midi_, [](const auto& entry) { return entry.second.use_count() == 1; });
        std::erase_if(automation_,
                      [](const auto& entry)
                      { return entry.second.use_count() == 1 && !entry.second->touched; });
    }
    void clear()
    {
        channels_.clear();
        midi_.clear();
        automation_.clear();
        master_.reset();
    }

private:
    std::map<core::Id, std::shared_ptr<ChannelRuntime>> channels_;
    std::map<core::Id, std::shared_ptr<MidiRuntime>> midi_;
    std::map<core::Id, std::shared_ptr<AutomationRuntime>> automation_;
    std::shared_ptr<MasterRuntime> master_;
};

} // namespace aural::engine
