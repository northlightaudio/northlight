#include "engine/AudioEngine.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace aural::engine
{

AudioEngine::AudioEngine(MediaLoader& loader, const InsertFactory& inserts, int workerThreads)
    : media_(loader), inserts_(inserts),
      pool_(workerThreads < 0 ? WorkerPool::recommendedWorkerCount() : workerThreads)
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
    publisher_.publish(compileSnapshot(project, media_, inserts_));
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

void AudioEngine::setMetronome(bool enabled, float gain)
{
    send(TransportCommand::setMetronome(enabled, gain));
}

void AudioEngine::setPunch(core::SamplePosition in, core::SamplePosition out, bool enabled)
{
    send(TransportCommand::setPunch(in, out, enabled));
}

bool AudioEngine::startRecording(std::unique_ptr<RecordSession> session,
                                 std::string& error,
                                 core::SamplePosition countInSamples)
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
    if (!commands_.push(TransportCommand::record(countInSamples)))
    {
        // The command queue is full (the UI is flooding it): nothing will ever start
        // this session, so end it now rather than leave it pending forever.
        pendingRecord_.store(nullptr, std::memory_order_release);
        activeRecord_->finish();
        while (writer_.isWriting())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        writer_.takeResults();
        activeRecord_.reset();
        droppedCommands_.fetch_add(1, std::memory_order_relaxed);
        error = "the engine is busy; try again";
        return false;
    }
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
    inserts_.prune();
}

void AudioEngine::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("AudioEngine::prepare");
    renderer_.prepare(maxBlockSize);
    inserts_.prepare(sampleRate, std::min(maxBlockSize, Snapshot::maxBlockSamples));
    loudness_.prepare(sampleRate);
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
                applyTransportCommand(transport_, command); // playing + count-in
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

    if (renderer_.maxBlockSize() <= 0)
    {
        // prepare() has not run: nothing is allocated, so just keep the outputs quiet.
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);
        return;
    }

    const Snapshot* snapshot = publisher_.acquire();
    constexpr int maxChannels = 64;
    float* outPieces[maxChannels];
    const float* inPieces[maxChannels];
    const int outChannels = std::min(numOutputs, maxChannels);
    const int inChannels = std::min(numInputs, maxChannels);

    // The block is processed in segments so that the count-in end and the punch
    // boundaries fall exactly on sample positions, and so that no segment exceeds
    // the renderer's maximum.
    int done = 0;
    while (done < numSamples)
    {
        int count = std::min(numSamples - done, std::max(1, renderer_.maxBlockSize()));
        const bool countingIn = transport_.playing && transport_.countInRemaining > 0;

        // Playing at or past the loop end (a locate there, or the loop enabled while
        // outside it): wrap before capturing so the take is placed where its audio
        // belongs, marking a new take if one is already in progress.
        if (transport_.playing && !countingIn && transport_.loopIsValid() &&
            transport_.position >= transport_.loopEnd)
        {
            transport_.position = transport_.loopStart;
            if (recordSession_ != nullptr && recordSession_->capturedSamples() > 0)
                recordSession_->markTake(transport_.loopStart);
        }
        if (countingIn)
            count = static_cast<int>(std::min<core::SamplePosition>(count, transport_.countInRemaining));
        else if (transport_.playing && recordSession_ != nullptr)
        {
            // Stop the segment at the next punch edge or loop end ahead of the
            // position, so capture ranges and take boundaries fall on exact samples.
            if (transport_.punchIsValid())
                for (const auto edge : {transport_.punchIn, transport_.punchOut})
                    if (edge > transport_.position && edge - transport_.position < count)
                        count = static_cast<int>(edge - transport_.position);
            if (transport_.loopIsValid() && transport_.loopEnd > transport_.position &&
                transport_.loopEnd - transport_.position < count)
                count = static_cast<int>(transport_.loopEnd - transport_.position);
        }

        for (int ch = 0; ch < outChannels; ++ch)
            outPieces[ch] = outputs[ch] != nullptr ? outputs[ch] + done : nullptr;
        for (int ch = 0; ch < inChannels; ++ch)
            inPieces[ch] = inputs != nullptr && inputs[ch] != nullptr ? inputs[ch] + done : nullptr;

        const auto segmentStart = transport_.position;
        const bool capturing = recordSession_ != nullptr && !countingIn && transport_.playing &&
                               transport_.insidePunch(transport_.position);
        if (capturing)
            recordSession_->capture(
                inChannels > 0 ? inPieces : nullptr, inChannels, count, transport_.position);

        RenderContext context;
        context.inputs = inChannels > 0 ? inPieces : nullptr;
        context.numInputs = inChannels;
        context.playing = transport_.playing && !countingIn;
        context.recording = recordSession_ != nullptr;
        context.countIn = countingIn;
        context.meters = &meters_;
        context.loudness = &loudness_;

        if (snapshot != nullptr)
        {
            renderer_.render(*snapshot, transport_, context, outPieces, outChannels, count, &pool_);
        }
        else
        {
            for (int ch = 0; ch < outChannels; ++ch)
                if (outPieces[ch] != nullptr)
                    std::fill(outPieces[ch], outPieces[ch] + count, 0.0f);
            if (context.playing)
                transport_.position += count;
        }
        if (countingIn)
            transport_.countInRemaining -= count;
        // A loop wrap while capturing starts a new take at the loop start.
        if (capturing && transport_.loopIsValid() && transport_.position == transport_.loopStart &&
            segmentStart + count == transport_.loopEnd)
            recordSession_->markTake(transport_.loopStart);
        done += count;
    }

    position_.store(transport_.position, std::memory_order_relaxed);
    playing_.store(transport_.playing, std::memory_order_relaxed);
    countingIn_.store(transport_.playing && transport_.countInRemaining > 0, std::memory_order_relaxed);
}

} // namespace aural::engine
