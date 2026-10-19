#include "engine/Renderer.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aural::engine
{

void Renderer::prepare(int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("Renderer::prepare");
    maxBlockSize_ = std::clamp(maxBlockSize, 1, Snapshot::maxBlockSamples);
    const auto n = static_cast<size_t>(maxBlockSize_);
    mixLeft_.assign(n, 0.0f);
    mixRight_.assign(n, 0.0f);
}

void Renderer::monoPanGains(float pan, float& left, float& right) noexcept
{
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const float angle = (p + 1.0f) * (std::numbers::pi_v<float> / 4.0f);
    left = std::cos(angle);
    right = std::sin(angle);
}

void Renderer::stereoPanGains(float pan, float& left, float& right) noexcept
{
    const float p = std::clamp(pan, -1.0f, 1.0f);
    left = p <= 0.0f ? 1.0f : 1.0f - p;
    right = p >= 0.0f ? 1.0f : 1.0f + p;
}

bool Renderer::monitorsInput(const TrackNode& track, bool playing, bool recording) noexcept
{
    if (track.inputChannel < 0)
        return false;
    switch (track.monitor)
    {
        case core::MonitorMode::off:
            return false;
        case core::MonitorMode::on:
            return true;
        case core::MonitorMode::automatic:
            return track.armed && (!playing || recording);
    }
    return false;
}

void Renderer::render(const Snapshot& snapshot,
                      TransportState& transport,
                      const RenderContext& context,
                      float* const* outputs,
                      int numOutputs,
                      int numSamples,
                      WorkerPool* pool) noexcept
{
    numSamples = std::min(numSamples, maxBlockSize_);
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);
    if (numSamples <= 0)
        return;

    float* left = mixLeft_.data();
    float* right = mixRight_.data();
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);

    if (context.countIn)
    {
        // Count-in: monitored inputs and the click, from the bars before the position.
        RenderContext held = context;
        held.playing = false;
        renderSpan(snapshot, held, transport.position, left, right, numSamples, pool);
        renderClicks(
            snapshot, transport, transport.position - transport.countInRemaining, left, right, numSamples);
    }
    else if (!transport.playing)
    {
        // Stopped: no clip playback, but monitored inputs are still heard.
        click_.reset();
        renderSpan(snapshot, context, transport.position, left, right, numSamples, pool);
    }
    else
    {
        // Split the block at the loop end so position never crosses it.
        int done = 0;
        while (done < numSamples)
        {
            int span = numSamples - done;
            if (transport.loopIsValid())
            {
                // Playing from before the loop runs into it normally; only the loop end wraps.
                if (transport.position >= transport.loopEnd)
                    transport.position = transport.loopStart;
                const auto toLoopEnd = transport.loopEnd - transport.position;
                span = static_cast<int>(std::min<core::SamplePosition>(span, toLoopEnd));
            }
            renderSpan(snapshot, context, transport.position, left + done, right + done, span, pool);
            renderClicks(snapshot, transport, transport.position, left + done, right + done, span);
            transport.position += span;
            done += span;
            if (transport.loopIsValid() && transport.position >= transport.loopEnd)
                transport.position = transport.loopStart;
        }
    }

    if (numOutputs >= 2)
    {
        if (outputs[0] != nullptr)
            std::copy_n(left, numSamples, outputs[0]);
        if (outputs[1] != nullptr)
            std::copy_n(right, numSamples, outputs[1]);
    }
    else if (numOutputs == 1 && outputs[0] != nullptr)
    {
        for (int i = 0; i < numSamples; ++i)
            outputs[0][i] = 0.5f * (left[i] + right[i]);
    }
}

void Renderer::renderClicks(const Snapshot& snapshot,
                            const TransportState& transport,
                            core::SamplePosition start,
                            float* left,
                            float* right,
                            int numSamples) noexcept
{
    if (!transport.metronome)
        return;
    const double spb = core::samplesPerBeat(snapshot.sampleRate, snapshot.tempoBpm, snapshot.timeSignature);
    click_.render(start,
                  numSamples,
                  snapshot.sampleRate,
                  spb,
                  snapshot.timeSignature.numerator,
                  transport.metronomeGain,
                  left,
                  right);
}

void Renderer::renderTrack(const TrackNode& track,
                           core::SamplePosition position,
                           int numSamples,
                           const RenderContext& context) noexcept
{
    numSamples = std::min(numSamples, static_cast<int>(track.outLeft.size()));
    float* left = track.outLeft.data();
    float* right = track.outRight.data();
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
    if (!track.audible)
        return;

    if (context.playing)
    {
        const core::SamplePosition spanEnd = position + numSamples;
        for (const auto& clip : track.clips)
        {
            if (clip.source == nullptr || clip.length <= 0)
                continue;
            if (clip.start >= spanEnd)
                break; // clips are sorted by start
            if (clip.end() <= position)
                continue;

            const auto from = std::max(position, clip.start);
            const auto to = std::min(spanEnd, clip.end());
            const auto count = static_cast<int>(to - from);
            const auto offset = static_cast<int>(from - position);
            const auto sourcePos = clip.sourceOffset + (from - clip.start);

            float panLeft = 1.0f, panRight = 1.0f;
            const bool mono = clip.source->numChannels() == 1;
            if (mono)
                monoPanGains(track.pan, panLeft, panRight);
            else
                stereoPanGains(track.pan, panLeft, panRight);
            const float gainLeft = clip.gain * panLeft;
            const float gainRight = clip.gain * panRight;

            float* sl = track.scratchLeft.data();
            float* sr = track.scratchRight.data();
            clip.source->read(0, sourcePos, sl, count);
            if (mono)
            {
                for (int i = 0; i < count; ++i)
                {
                    left[offset + i] += sl[i] * gainLeft;
                    right[offset + i] += sl[i] * gainRight;
                }
            }
            else
            {
                clip.source->read(1, sourcePos, sr, count);
                for (int i = 0; i < count; ++i)
                {
                    left[offset + i] += sl[i] * gainLeft;
                    right[offset + i] += sr[i] * gainRight;
                }
            }
        }
    }

    // Input monitoring: the device input through the track's pan (the fader follows
    // in the summing stage).
    if (monitorsInput(track, context.playing, context.recording) && track.inputChannel < context.numInputs &&
        context.inputs != nullptr)
    {
        const float* in = context.inputs[track.inputChannel];
        if (in != nullptr)
        {
            float panLeft = 1.0f, panRight = 1.0f;
            monoPanGains(track.pan, panLeft, panRight);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] += in[i] * panLeft;
                right[i] += in[i] * panRight;
            }
        }
    }

    if (track.phaseInvert)
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = -left[i];
            right[i] = -right[i];
        }

    for (auto* insert : track.inserts)
        insert->process(left, right, numSamples);
}

void Renderer::trackJob(void* context, int index) noexcept
{
    const auto& job = *static_cast<const SpanJob*>(context);
    renderTrack(job.snapshot->tracks[static_cast<size_t>(index)], job.position, job.numSamples, *job.context);
}

void Renderer::routeChannel(const Snapshot& snapshot,
                            const ChannelNode& channel,
                            const RenderContext& context,
                            float* masterLeft,
                            float* masterRight,
                            int numSamples) noexcept
{
    const float* pre = channel.outLeft.data();
    const float* preR = channel.outRight.data();
    const auto target = [&](int bus, float*& l, float*& r)
    {
        if (bus >= 0 && static_cast<size_t>(bus) < snapshot.buses.size())
        {
            l = snapshot.buses[static_cast<size_t>(bus)].sumLeft.data();
            r = snapshot.buses[static_cast<size_t>(bus)].sumRight.data();
        }
        else
        {
            l = masterLeft;
            r = masterRight;
        }
    };

    if (!channel.audible)
    {
        if (context.meters != nullptr)
            context.meters->silence(channel.meterSlot);
        return;
    }

    // An edge with compensation reads a delayed copy of the channel's output.
    const auto delayed = [&](DelayLine* delay, const float*& l, const float*& r)
    {
        if (delay == nullptr)
        {
            l = pre;
            r = preR;
            return;
        }
        delay->process(
            pre, preR, channel.delayScratchLeft.data(), channel.delayScratchRight.data(), numSamples);
        l = channel.delayScratchLeft.data();
        r = channel.delayScratchRight.data();
    };

    float* outL = nullptr;
    float* outR = nullptr;
    target(channel.outputBus, outL, outR);
    const float gain = channel.gain;
    {
        const float* l = nullptr;
        const float* r = nullptr;
        delayed(channel.outputDelay, l, r);
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] += l[i] * gain;
            outR[i] += r[i] * gain;
        }
    }

    for (const auto& send : channel.sends)
    {
        float* sl = nullptr;
        float* sr = nullptr;
        target(send.bus, sl, sr);
        const float* l = nullptr;
        const float* r = nullptr;
        delayed(send.delay, l, r);
        const float level = send.preFader ? send.level : send.level * gain;
        for (int i = 0; i < numSamples; ++i)
        {
            sl[i] += l[i] * level;
            sr[i] += r[i] * level;
        }
    }

    if (context.meters != nullptr)
    {
        // Post-fader level: measured from the pre-fader buffer scaled by the gain,
        // reusing the buffer in place now that it has been routed.
        float* l = channel.outLeft.data();
        float* r = channel.outRight.data();
        for (int i = 0; i < numSamples; ++i)
        {
            l[i] *= gain;
            r[i] *= gain;
        }
        context.meters->measure(channel.meterSlot, l, r, numSamples);
    }
}

void Renderer::renderSpan(const Snapshot& snapshot,
                          const RenderContext& context,
                          core::SamplePosition position,
                          float* left,
                          float* right,
                          int numSamples,
                          WorkerPool* pool) noexcept
{
    const auto trackCount = static_cast<int>(snapshot.tracks.size());
    SpanJob job{&snapshot, &context, position, numSamples};
    if (pool != nullptr && pool->numWorkers() > 0)
    {
        pool->run(trackCount, &trackJob, &job);
    }
    else
    {
        for (int t = 0; t < trackCount; ++t)
            trackJob(&job, t);
    }

    // Summing in fixed order: the result does not depend on which thread did what.
    for (const auto& bus : snapshot.buses)
    {
        std::fill(bus.sumLeft.data(), bus.sumLeft.data() + numSamples, 0.0f);
        std::fill(bus.sumRight.data(), bus.sumRight.data() + numSamples, 0.0f);
    }
    for (const auto& track : snapshot.tracks)
        routeChannel(snapshot, track, context, left, right, numSamples);

    for (int index : snapshot.busOrder)
    {
        const auto& bus = snapshot.buses[static_cast<size_t>(index)];
        // The bus's pre-fader output: its summed inputs through balance and phase.
        float panLeft = 1.0f, panRight = 1.0f;
        stereoPanGains(bus.pan, panLeft, panRight);
        const float sign = bus.phaseInvert ? -1.0f : 1.0f;
        float* outL = bus.outLeft.data();
        float* outR = bus.outRight.data();
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = bus.sumLeft[static_cast<size_t>(i)] * panLeft * sign;
            outR[i] = bus.sumRight[static_cast<size_t>(i)] * panRight * sign;
        }
        for (auto* insert : bus.inserts)
            insert->process(outL, outR, numSamples);
        routeChannel(snapshot, bus, context, left, right, numSamples);
    }

    const float master = snapshot.masterGain;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] *= master;
        right[i] *= master;
    }
    if (context.meters != nullptr)
        context.meters->measure(snapshot.masterMeterSlot, left, right, numSamples);
    if (context.loudness != nullptr)
        context.loudness->processBlock(left, right, numSamples);
}

} // namespace aural::engine
