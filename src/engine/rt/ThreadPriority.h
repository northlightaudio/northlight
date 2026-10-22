#pragma once

// Scheduling for the engine's own threads. The audio callback thread belongs to the
// device driver and is left alone.
//
// Render workers join the Multimedia Class Scheduler Service (MMCSS) "Pro Audio"
// task. That moves them into the real-time priority band, above anything an
// ordinary process can reach. Windows boosts a busy normal thread (after I/O, or
// when its window is in front) as high as THREAD_PRIORITY_TIME_CRITICAL, so a
// compiler could otherwise tie with a worker and delay the block every other worker
// is waiting on. If MMCSS is unavailable the thread falls back to time-critical.
//
// The disk thread runs at THREAD_PRIORITY_HIGHEST: above normal work so a busy
// machine cannot starve the streaming windows, below the render workers so reading
// files never takes a core from rendering.
//
// Both are no-ops off Windows.

namespace northlight::rt
{

class ScopedProAudioThread
{
public:
    ScopedProAudioThread() noexcept;
    ~ScopedProAudioThread() noexcept;

    ScopedProAudioThread(const ScopedProAudioThread&) = delete;
    ScopedProAudioThread& operator=(const ScopedProAudioThread&) = delete;

    // True when the thread joined MMCSS; false when it fell back.
    bool joinedMmcss() const noexcept { return task_ != nullptr; }

private:
    void* task_ = nullptr; // the AVRT task handle
};

void raiseDiskThreadPriority() noexcept;

} // namespace northlight::rt
