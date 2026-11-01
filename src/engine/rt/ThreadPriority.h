#pragma once

// Scheduling for the engine's own threads. The audio callback thread belongs to the
// device driver and is left alone.
//
// Windows: render workers join the Multimedia Class Scheduler Service (MMCSS) "Pro
// Audio" task. That moves them into the real-time priority band, above anything an
// ordinary process can reach. Windows boosts a busy normal thread (after I/O, or
// when its window is in front) as high as THREAD_PRIORITY_TIME_CRITICAL, so a
// compiler could otherwise tie with a worker and delay the block every other worker
// is waiting on. If MMCSS is unavailable the thread falls back to time-critical.
// The disk thread runs at THREAD_PRIORITY_HIGHEST: above normal work so a busy
// machine cannot starve the streaming windows, below the render workers so reading
// files never takes a core from rendering.
//
// Linux: render workers run SCHED_FIFO at realtimePriority, set directly when the
// user may (RLIMIT_RTPRIO, as the audio group usually grants, or CAP_SYS_NICE) and
// otherwise asked of rtkit (RtKit.h), which grants SCHED_RR (with
// SCHED_RESET_ON_FORK) and caps the priority at its own maximum.
// Without either they stay normal threads. The disk thread gets nice level
// diskThreadNice the same two ways: above normal work, below the real-time workers.
//
// Elsewhere both are no-ops.

namespace northlight::rt
{

// The render workers' SCHED_FIFO priority on Linux (1 to 99): in the range audio
// servers give their own and their clients' threads, so a worker sits near the
// callback thread it renders for and far above normal work. rtkit lowers it to
// its own maximum.
inline constexpr int realtimePriority = 70;
// The disk thread's nice level on Linux.
inline constexpr int diskThreadNice = -10;

class ScopedProAudioThread
{
public:
    ScopedProAudioThread() noexcept;
    ~ScopedProAudioThread() noexcept;

    ScopedProAudioThread(const ScopedProAudioThread&) = delete;
    ScopedProAudioThread& operator=(const ScopedProAudioThread&) = delete;

    // True when the thread got real-time scheduling: it joined MMCSS on Windows, or
    // runs SCHED_FIFO or SCHED_RR on Linux. False when it fell back.
    bool isRealtime() const noexcept { return realtime_; }

private:
    bool realtime_ = false;
    void* task_ = nullptr; // the AVRT task handle (Windows)
};

// What a real-time worker is called in diagnostics: "MMCSS" on Windows, where
// joining it is what the count means, else "real-time".
const char* realtimeSchedulingName() noexcept;

void raiseDiskThreadPriority() noexcept;

} // namespace northlight::rt
