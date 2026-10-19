#include "engine/AudioEngine.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

namespace aural::engine
{

AudioEngine::AudioEngine(MediaLoader& loader) : media_(loader) {}

// ----- controller -----------------------------------------------------------------------

void AudioEngine::setProject(const core::Project& project)
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::setProject");
    publisher_.publish(compileSnapshot(project, media_));
    collectGarbage();
}

void AudioEngine::send(const TransportCommand& command)
{
    if (!commands_.push(command))
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngine::play()
{
    send(TransportCommand::play());
}

void AudioEngine::stop()
{
    send(TransportCommand::stop());
}

void AudioEngine::locate(core::SamplePosition position)
{
    send(TransportCommand::locate(position));
}

void AudioEngine::setLoop(core::SamplePosition start, core::SamplePosition end, bool enabled)
{
    send(TransportCommand::setLoop(start, end, enabled));
}

void AudioEngine::collectGarbage()
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::collectGarbage");
    publisher_.collect(isRunning());
    media_.prune();
}

void AudioEngine::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::prepare");
    renderer_.prepare(maxBlockSize);
    deviceSampleRate_.store(sampleRate, std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);
}

void AudioEngine::release()
{
    running_.store(false, std::memory_order_release);
    deviceSampleRate_.store(0.0, std::memory_order_relaxed);
}

// ----- real-time ------------------------------------------------------------------------

void AudioEngine::process(float* const* outputs, int numOutputs, int numSamples) noexcept
{
    TransportCommand command;
    for (int i = 0; i < maxCommandsPerBlock && commands_.pop(command); ++i)
        applyTransportCommand(transport_, command);

    if (const Snapshot* snapshot = publisher_.acquire())
    {
        renderer_.render(*snapshot, transport_, outputs, numOutputs, numSamples);
    }
    else
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);
        if (transport_.playing)
            transport_.position += numSamples;
    }

    position_.store(transport_.position, std::memory_order_relaxed);
    playing_.store(transport_.playing, std::memory_order_relaxed);
}

} // namespace aural::engine
