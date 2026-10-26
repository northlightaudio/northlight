#include "engine/AudioEngine.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace northlight::engine
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
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::setProject");
    auto snapshot = compileSnapshot(
        project,
        media_,
        inserts_,
        &delays_,
        &runtimes_,
        [this](const std::string& name) { return midiSourceIndex(name); },
        [this](const std::string& name) { return inputBus(name); },
        preparedBlock_.load(std::memory_order_relaxed));
    outputLatency_.store(snapshot->outputLatency, std::memory_order_relaxed);
    masterMeterSlot_.store(snapshot->masterMeterSlot, std::memory_order_relaxed);
    publisher_.publish(std::move(snapshot));
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
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::startRecording");
    if (session == nullptr || (session->inputs().empty() && session->midiTracks().empty()))
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

void AudioEngine::updateLiveTakes(std::vector<LiveTake>& into) const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::updateLiveTakes");
    if (activeRecord_ == nullptr)
    {
        into.clear();
        return;
    }
    writer_.updateLiveTakes(into);
}

std::optional<AudioEngine::FinishedRecording> AudioEngine::collectRecording()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::collectRecording");
    if (!hasRecordingToCollect())
        return std::nullopt;
    FinishedRecording finished;
    finished.results = writer_.takeResults();
    finished.session = std::move(activeRecord_);
    return finished;
}

void AudioEngine::setInputBuses(std::map<std::string, InputPair> buses)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::setInputBuses");
    inputBuses_ = std::move(buses);
}

InputPair AudioEngine::inputBus(const std::string& name) const
{
    const auto it = inputBuses_.find(name);
    return it != inputBuses_.end() ? it->second : InputPair{};
}

int AudioEngine::midiSourceIndex(const std::string& name)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::midiSourceIndex");
    const std::lock_guard<std::mutex> lock(midiMutex_);
    for (size_t i = 0; i < midiSourceNames_.size(); ++i)
        if (midiSourceNames_[i] == name)
            return static_cast<int>(i);
    midiSourceNames_.push_back(name);
    return static_cast<int>(midiSourceNames_.size() - 1);
}

std::int64_t AudioEngine::steadyNanos() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void AudioEngine::pushMidiInput(const MidiEvent& event, const std::string& source, std::int64_t arrivalNanos)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::pushMidiInput");
    const int index = source.empty() ? -1 : midiSourceIndex(source);
    const std::lock_guard<std::mutex> lock(midiMutex_); // several device threads share the queue
    midiIn_.push(QueuedMidi{event, index, core::invalidId, arrivalNanos});
}

void AudioEngine::pushMidiPreview(const MidiEvent& event, core::Id trackId)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::pushMidiPreview");
    const std::lock_guard<std::mutex> lock(midiMutex_);
    midiIn_.push(QueuedMidi{event, -1, trackId});
}

void AudioEngine::resetCaches()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::resetCaches");
    inserts_.clear();
    delays_.clear();
    runtimes_.clear();
}

AudioEngine::ClockSync AudioEngine::clockSync() const noexcept
{
    ClockSync sync;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto before = syncSequence_.load(std::memory_order_acquire);
        if ((before & 1) != 0)
            continue;
        sync.clock = syncClock_.load(std::memory_order_relaxed);
        sync.steadyNanos = syncNanos_.load(std::memory_order_relaxed);
        // The seqlock's read side: the data loads complete before the second
        // sequence load (a plain acquire load does not order them on ARM).
        std::atomic_thread_fence(std::memory_order_acquire);
        if (syncSequence_.load(std::memory_order_relaxed) == before)
            break;
    }
    return sync;
}

void AudioEngine::setAutomationTouched(core::Id laneId, bool touched)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::setAutomationTouched");
    runtimes_.automation(laneId)->touched.store(touched, std::memory_order_relaxed);
}

AudioEngine::AutomatedValues AudioEngine::automatedValues(core::Id channelId) const
{
    AutomatedValues v;
    if (const auto runtime = runtimes_.find(channelId))
    {
        const int bits = runtime->automated.load(std::memory_order_relaxed);
        v.gain = (bits & 1) != 0;
        v.pan = (bits & 2) != 0;
        v.gainValue = runtime->shownGain.load(std::memory_order_relaxed);
        v.panValue = runtime->shownPan.load(std::memory_order_relaxed);
    }
    return v;
}

void AudioEngine::setLiveGain(core::Id channelId, float gain)
{
    // The master's fader too (an automation write lands here).
    if (const auto runtime = runtimes_.find(channelId))
        runtime->targetGain.store(gain, std::memory_order_relaxed);
}

void AudioEngine::setLivePan(core::Id channelId, float pan)
{
    if (const auto runtime = runtimes_.find(channelId))
        runtime->targetPan.store(pan, std::memory_order_relaxed);
}

void AudioEngine::applyLiveValues(const core::Project& project, core::Id only)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::applyLiveValues");
    const auto channel = [&](const core::Channel& c)
    {
        if (only != core::invalidId && c.id != only)
            return;
        if (const auto runtime = runtimes_.find(c.id))
        {
            runtime->targetGain.store(c.gain, std::memory_order_relaxed);
            runtime->targetPan.store(c.pan, std::memory_order_relaxed);
        }
        for (const auto& s : c.sends)
            if (const auto send = runtimes_.findSend(c.id, s.bus))
                send->targetLevel.store(s.level, std::memory_order_relaxed);
        for (const auto* chained : c.processingChain())
            if (const auto& i = *chained; const auto instance = inserts_.find(i.id))
            {
                // A processor that owns its parameters (a hosted plugin) only gets
                // the ones a command touched (pushInsertParameters).
                if (!instance->ownsParameters())
                {
                    // A parameter a lane is playing belongs to the renderer: the
                    // static value would flash in for a block.
                    const bool reads = c.automationMode == core::AutomationMode::read ||
                                       c.automationMode == core::AutomationMode::touch ||
                                       c.automationMode == core::AutomationMode::latch;
                    const auto count = static_cast<int>(instance->parameters().size());
                    for (int k = 0; k < count && static_cast<size_t>(k) < i.params.size(); ++k)
                    {
                        const auto* lane =
                            reads ? c.findLane(core::AutomationTarget::insertParameter, i.id, k) : nullptr;
                        if (lane != nullptr && !lane->points.empty())
                            continue;
                        instance->setParameter(k, i.params[static_cast<size_t>(k)]);
                    }
                }
                instance->setBypassed(i.bypass);
                if (const auto mix = runtimes_.findInsertMix(i.id))
                    mix->targetMix.store(std::clamp(i.mix, 0.0f, 1.0f), std::memory_order_relaxed);
            }
    };
    for (const auto& t : project.tracks)
        channel(t);
    for (const auto& b : project.buses)
        channel(b);
    channel(project.master);
}

void AudioEngine::pushInsertParameters(const core::Project& project,
                                       const std::vector<core::ParameterTouch>& touches)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::pushInsertParameters");
    for (const auto& touch : touches)
    {
        const auto instance = inserts_.find(touch.insertId);
        if (instance == nullptr)
            continue;
        const core::Insert* insert = nullptr;
        for (const auto& t : project.tracks)
            if (const auto* i = t.findInsert(touch.insertId))
                insert = i;
        for (const auto& b : project.buses)
            if (const auto* i = b.findInsert(touch.insertId))
                insert = i;
        if (const auto* i = project.master.findInsert(touch.insertId))
            insert = i;
        if (insert != nullptr && touch.parameter >= 0 &&
            static_cast<size_t>(touch.parameter) < insert->params.size())
            instance->setParameter(touch.parameter, insert->params[static_cast<size_t>(touch.parameter)]);
    }
}

void AudioEngine::collectGarbage()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::collectGarbage");
    publisher_.collect(isRunning());
    preview_.pump(isRunning());
    media_.prune();
    inserts_.prune();
    delays_.prune();
    runtimes_.prune();
}

void AudioEngine::prepare(double sampleRate, int maxBlockSize)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioEngine::prepare");
    renderer_.prepare(maxBlockSize);
    preparedBlock_.store(std::clamp(maxBlockSize, 1, Snapshot::maxBlockSamples), std::memory_order_relaxed);
    inserts_.prepare(sampleRate, std::min(maxBlockSize, Snapshot::maxBlockSamples));
    loudness_.prepare(sampleRate);
    preview_.prepare(sampleRate, maxBlockSize);
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
            // Locating during a recording ends it (the punch range is handled in process(), not by locating).
            if (recordSession_ != nullptr)
                endRecordingOnRt();
            applyTransportCommand(transport_, command);
            flushDelays_ = true; // the compensation lines hold audio from before the jump
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

    // The sample clock for this block, for the MIDI output's timing.
    const auto blockNanos = clock_();
    const auto previousBlockNanos = std::exchange(lastBlockNanos_, blockNanos);
    {
        const auto nanos = blockNanos;
        const auto sequence = syncSequence_.load(std::memory_order_relaxed);
        syncSequence_.store(sequence + 1, std::memory_order_relaxed);
        // The write side: the odd sequence is visible before any data store.
        std::atomic_thread_fence(std::memory_order_release);
        syncClock_.store(sampleClock_, std::memory_order_relaxed);
        syncNanos_.store(nanos, std::memory_order_relaxed);
        syncSequence_.store(sequence + 2, std::memory_order_release);
    }

    const Snapshot* snapshot = publisher_.acquire();
    if (flushDelays_ && snapshot != nullptr)
    {
        for (auto* line : snapshot->allDelays)
            line->reset();
        flushDelays_ = false;
    }
    // Live MIDI for this block: everything queued since the last one.
    liveMidi_.clear();
    liveMidiCount_ = 0;
    QueuedMidi queued;
    while (midiIn_.pop(queued))
    {
        // A device event lands where it arrived during the last block's span of
        // time, scaled to this block (one block of delay, its timing intact).
        if (queued.arrival > 0)
        {
            int offset = 0;
            if (previousBlockNanos > 0 && blockNanos > previousBlockNanos &&
                queued.arrival > previousBlockNanos)
                offset = static_cast<int>(static_cast<double>(queued.arrival - previousBlockNanos) /
                                          static_cast<double>(blockNanos - previousBlockNanos) * numSamples);
            queued.event.sampleOffset = std::clamp(offset, 0, numSamples - 1);
        }
        if (liveMidi_.push(queued.event))
        {
            liveMidiSources_[liveMidiCount_] = queued.source;
            liveMidiTargets_[liveMidiCount_++] = queued.target;
        }
    }
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
        // A snapshot compiled before the device asked for a longer block holds less.
        const int most = snapshot != nullptr ? std::min(renderer_.maxBlockSize(), snapshot->blockSamples)
                                             : renderer_.maxBlockSize();
        int count = std::min(numSamples - done, std::max(1, most));
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
        {
            recordSession_->capture(
                inChannels > 0 ? inPieces : nullptr, inChannels, count, transport_.position);
            // Live MIDI on the recorded tracks: the events that fall in this segment.
            if (!liveMidi_.empty() && snapshot != nullptr)
                for (const auto& track : snapshot->tracks)
                {
                    if (!track.isMidi || !recordSession_->recordsMidi(track.id))
                        continue;
                    for (size_t i = 0; i < liveMidi_.size(); ++i)
                    {
                        if (liveMidiTargets_[i] != core::invalidId) // an audition, not a performance
                            continue;
                        if (track.midiSource >= 0 && liveMidiSources_[i] != track.midiSource)
                            continue;
                        MidiEvent e = liveMidi_[i];
                        const int at = e.sampleOffset - done;
                        if (at < 0 || (at >= count && done + count < numSamples))
                            continue; // another segment's (the last one takes any past the end)
                        e.sampleOffset = std::clamp(at, 0, count - 1);
                        recordSession_->captureMidi(track.id, e, transport_.position + e.sampleOffset);
                    }
                }
        }

        RenderContext context;
        context.inputs = inChannels > 0 ? inPieces : nullptr;
        context.numInputs = inChannels;
        context.playing = transport_.playing && !countingIn;
        context.recording = recordSession_ != nullptr;
        context.countIn = countingIn;
        context.meters = &meters_;
        context.loudness = &loudness_;
        context.midiOut = &midiOut_;
        context.sampleClock = sampleClock_ + done;
        if (!liveMidi_.empty())
        {
            // Every segment sees the block's live MIDI and plays the events in it.
            context.liveMidi = &liveMidi_;
            context.liveMidiSources = liveMidiSources_.data();
            context.liveMidiTargets = liveMidiTargets_.data();
            context.liveMidiBase = done;
            context.liveMidiLast = done + count >= numSamples;
        }

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

    // An audition over the mix (the browser), after everything else.
    preview_.process(outputs, numOutputs, numSamples);

    sampleClock_ += numSamples;
    position_.store(transport_.position, std::memory_order_relaxed);
    playing_.store(transport_.playing, std::memory_order_relaxed);
    countingIn_.store(transport_.playing && transport_.countInRemaining > 0, std::memory_order_relaxed);
}

} // namespace northlight::engine
