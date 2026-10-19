#include "engine/AudioEngine.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace aural::engine
{

AudioEngine::AudioEngine(MediaLoader& loader, int workerThreads)
    : media_(loader), pool_(workerThreads < 0 ? WorkerPool::recommendedWorkerCount() : workerThreads)
{
}

AudioEngine::~AudioEngine()
{
    // The device must already be stopped. End any recording so the writer closes
    // its files before the session goes away.
    if (activeRecord_ != nullptr && !activeRecord_->isFinished())
    {
        recordSession_ = nullptr;
        pendingRecord_.store(nullptr, std::memory_order_relaxed);
        activeRecord_->finish();
    }
    while (writer_.isWriting())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

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

bool AudioEngine::startRecording(std::unique_ptr<RecordSession> session, std::string& error)
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::startRecording");
    if (session == nullptr || session->inputs().empty())
    {
        error = "no armed tracks with an input";
        return false;
    }
    if (activeRecord_ != nullptr || writer_.isWriting())
    {
        error = "a recording is already in progress";
        return false;
    }
    if (!isRunning())
    {
        error = "the audio device is not running";
        return false;
    }
    if (!writer_.begin(*session))
    {
        error = "could not create the recording files";
        return false;
    }
    activeRecord_ = std::move(session);
    pendingRecord_.store(activeRecord_.get(), std::memory_order_release);
    send(TransportCommand::record());
    return true;
}

bool AudioEngine::hasRecordingToCollect() const noexcept
{
    return activeRecord_ != nullptr && activeRecord_->isFinished() && !writer_.isWriting();
}

std::optional<AudioEngine::FinishedRecording> AudioEngine::collectRecording()
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::collectRecording");
    if (!hasRecordingToCollect())
        return std::nullopt;
    FinishedRecording finished;
    finished.results = writer_.takeResults();
    finished.session = std::move(activeRecord_);
    return finished;
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
    // No real-time thread runs now: a recording in flight ends here so its files
    // get closed and the clips placed.
    if (activeRecord_ != nullptr && !activeRecord_->isFinished())
    {
        recordSession_ = nullptr;
        pendingRecord_.store(nullptr, std::memory_order_relaxed);
        activeRecord_->finish();
        recording_.store(false, std::memory_order_relaxed);
    }
}

// ----- real-time ------------------------------------------------------------------------

void AudioEngine::endRecordingOnRt() noexcept
{
    if (recordSession_ != nullptr)
    {
        recordSession_->finish();
        recordSession_ = nullptr;
    }
    recording_.store(false, std::memory_order_relaxed);
}

void AudioEngine::applyCommand(const TransportCommand& command) noexcept
{
    switch (command.type)
    {
        case TransportCommand::Type::record:
            if (auto* session = pendingRecord_.exchange(nullptr, std::memory_order_acq_rel))
            {
                recordSession_ = session;
                transport_.playing = true;
                recording_.store(true, std::memory_order_relaxed);
            }
            break;
        case TransportCommand::Type::stop:
            endRecordingOnRt();
            applyTransportCommand(transport_, command);
            break;
        case TransportCommand::Type::locate:
            // Locating during a recording ends it (a punch region comes in Phase 3b).
            if (recordSession_ != nullptr)
                endRecordingOnRt();
            applyTransportCommand(transport_, command);
            break;
        default:
            applyTransportCommand(transport_, command);
            break;
    }
}

void AudioEngine::process(
    const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    TransportCommand command;
    for (int i = 0; i < maxCommandsPerBlock && commands_.pop(command); ++i)
        applyCommand(command);

    const bool recording = recordSession_ != nullptr;
    if (recording)
        recordSession_->capture(inputs, numInputs, numSamples, transport_.position);

    RenderContext context;
    context.inputs = inputs;
    context.numInputs = numInputs;
    context.playing = transport_.playing;
    context.recording = recording;

    if (const Snapshot* snapshot = publisher_.acquire())
    {
        // Device blocks larger than the renderer's maximum are rendered in pieces.
        constexpr int maxChannels = 64;
        float* outPieces[maxChannels];
        const float* inPieces[maxChannels];
        const int outChannels = std::min(numOutputs, maxChannels);
        const int inChannels = std::min(numInputs, maxChannels);
        int done = 0;
        while (done < numSamples)
        {
            const int count = std::min(numSamples - done, renderer_.maxBlockSize());
            for (int ch = 0; ch < outChannels; ++ch)
                outPieces[ch] = outputs[ch] != nullptr ? outputs[ch] + done : nullptr;
            for (int ch = 0; ch < inChannels; ++ch)
                inPieces[ch] = inputs[ch] != nullptr ? inputs[ch] + done : nullptr;
            context.inputs = inChannels > 0 ? inPieces : nullptr;
            context.numInputs = inChannels;
            renderer_.render(*snapshot, transport_, context, outPieces, outChannels, count, &pool_);
            done += count;
        }
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
