#pragma once

// RealtimeKit (rtkit), the system service through which a desktop Linux session
// grants real-time scheduling and raised priority to processes that may not set it
// themselves (PipeWire and PulseAudio use it the same way). It is reached over the
// system D-Bus; libdbus is loaded when first needed, so neither is a build or run
// dependency, and every call simply fails where they are missing.
//
// Not real-time: each call makes a D-Bus round trip. Call when a thread starts.
// Linux only; elsewhere every call fails.

namespace northlight::rt::rtkit
{

// Makes the calling thread real-time (rtkit chooses SCHED_RR) at `priority`,
// lowered to the most rtkit allows; the priority granted, or 0 when rtkit refused
// or is not there. Sets the process's RLIMIT_RTTIME to rtkit's maximum first, as
// rtkit requires: a real-time thread that runs that long without blocking is then
// sent SIGXCPU.
int makeCurrentThreadRealtime(int priority) noexcept;

// Gives the calling thread the nice level `nice` (raised to the lowest rtkit
// allows); whether rtkit did.
bool makeCurrentThreadHighPriority(int nice) noexcept;

} // namespace northlight::rt::rtkit
