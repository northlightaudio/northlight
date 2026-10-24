#pragma once

// One shared background thread for processors that have slow, non-real-time
// work to redo when their parameters move (the amp's cabinet response, built
// whenever a mic is dragged, even by automation the real-time thread applies).
// A registered job is polled about every 15 ms; it compares its parameters
// with what it last built and rebuilds only when they changed. The thread
// starts with the first job and stops with the last. JUCE-free.

namespace northlight::plugins::background
{

class Job
{
public:
    virtual ~Job() = default;
    // On the background thread. Must not take long locks the real-time thread
    // waits on (the real-time thread waits on none).
    virtual void poll() = 0;
};

// Controller thread. remove() returns only once the job is not running and
// will not run again, so a job may call it from its destructor.
void add(Job* job);
void remove(Job* job);

} // namespace northlight::plugins::background
