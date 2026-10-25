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

namespace northlight::engine
{

// On a cache line of its own: workers rendering neighbouring tracks write their
// runtimes' ramps at once, and two runtimes sharing a line would make it bounce.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padded to the alignment: that is the point
#endif
struct alignas(64) ChannelRuntime
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
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// A send's level, kept across compiles like a channel's gain: a level change is
// pushed here (the renderer ramps to it over the span) instead of recompiling.
struct SendRuntime
{
    std::atomic<float> targetLevel{1.0f};
    float level = 1.0f;  // real-time: where the ramp is
    bool primed = false; // real-time: the first span jumps to the target
};

// An insert slot's wet/dry mix, kept across compiles like a send's level: a mix
// change is pushed here (the renderer ramps to it over the span) instead of
// recompiling.
struct InsertMixRuntime
{
    std::atomic<float> targetMix{1.0f};
    float mix = 1.0f;    // real-time: where the ramp is
    bool primed = false; // real-time: the first span jumps to the target
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
    static constexpr std::int16_t unknown = INT16_MIN;
    std::uint8_t held[128]{};          // 1 while sounding
    core::SamplePosition lastEnd = -1; // where the previous playing span ended
    bool wasPlaying = false;
    // The last value sent of each chased controller (engine chaseKey order:
    // control changes, pitch bend, channel pressure), `unknown` before any.
    std::int16_t sent[130];
    // Scratch for a chase (real-time; no allocation): the value found per key.
    std::int16_t chased[130]{};
    std::uint8_t found[130]{};

    MidiRuntime() noexcept
    {
        for (auto& s : sent)
            s = unknown;
    }
};

// A channel's work buffers for one span (output, dry copy, delayed copy, a
// fourth pair: a track's clip scratch or a bus's sum, then the post-fader copy
// a post-fader key reads and a delayed key), in one allocation. Kept
// across compiles like the runtimes, so a structural edit allocates only for
// new channels. Snapshots share them: only the one being rendered writes.
struct ChannelBuffers
{
    static constexpr int pairs = 6;
    explicit ChannelBuffers(size_t blockSize) : blockSize(blockSize), storage(2 * pairs * blockSize, 0.0f) {}
    float* buffer(int index) noexcept { return storage.data() + static_cast<size_t>(index) * blockSize; }
    size_t blockSize;
    std::vector<float> storage;
};

class RuntimeCache
{
public:
    // The channel's buffers for blocks up to `blockSize` (new ones when it changed).
    std::shared_ptr<ChannelBuffers> buffers(core::Id channelId, size_t blockSize)
    {
        auto& b = buffers_[channelId];
        if (b == nullptr || b->blockSize != blockSize)
            b = std::make_shared<ChannelBuffers>(blockSize);
        return b;
    }
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
    std::shared_ptr<SendRuntime> send(core::Id channelId, core::Id busId)
    {
        auto& r = sends_[{channelId, busId}];
        if (r == nullptr)
            r = std::make_shared<SendRuntime>();
        return r;
    }
    std::shared_ptr<SendRuntime> findSend(core::Id channelId, core::Id busId) const
    {
        const auto it = sends_.find({channelId, busId});
        return it != sends_.end() ? it->second : nullptr;
    }
    std::shared_ptr<InsertMixRuntime> insertMix(core::Id insertId)
    {
        auto& r = insertMixes_[insertId];
        if (r == nullptr)
            r = std::make_shared<InsertMixRuntime>();
        return r;
    }
    std::shared_ptr<InsertMixRuntime> findInsertMix(core::Id insertId) const
    {
        const auto it = insertMixes_.find(insertId);
        return it != insertMixes_.end() ? it->second : nullptr;
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
        std::erase_if(buffers_, [](const auto& entry) { return entry.second.use_count() == 1; });
        std::erase_if(sends_, [](const auto& entry) { return entry.second.use_count() == 1; });
        std::erase_if(insertMixes_, [](const auto& entry) { return entry.second.use_count() == 1; });
        std::erase_if(midi_, [](const auto& entry) { return entry.second.use_count() == 1; });
        std::erase_if(automation_,
                      [](const auto& entry)
                      { return entry.second.use_count() == 1 && !entry.second->touched; });
    }
    void clear()
    {
        channels_.clear();
        buffers_.clear();
        sends_.clear();
        insertMixes_.clear();
        midi_.clear();
        automation_.clear();
        master_.reset();
    }

private:
    std::map<core::Id, std::shared_ptr<ChannelRuntime>> channels_;
    std::map<core::Id, std::shared_ptr<ChannelBuffers>> buffers_;
    std::map<std::pair<core::Id, core::Id>, std::shared_ptr<SendRuntime>> sends_; // (channel, bus)
    std::map<core::Id, std::shared_ptr<InsertMixRuntime>> insertMixes_;           // by insert id
    std::map<core::Id, std::shared_ptr<MidiRuntime>> midi_;
    std::map<core::Id, std::shared_ptr<AutomationRuntime>> automation_;
    std::shared_ptr<MasterRuntime> master_;
};

} // namespace northlight::engine
