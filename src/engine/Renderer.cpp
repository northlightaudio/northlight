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
    return monitorsMidi(track, playing, recording);
}

bool Renderer::monitorsMidi(const TrackNode& track, bool playing, bool recording) noexcept
{
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
        RenderContext spanContext = context;
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
            renderSpan(snapshot, spanContext, transport.position, left + done, right + done, span, pool);
            renderClicks(snapshot, transport, transport.position, left + done, right + done, span);
            transport.position += span;
            done += span;
            spanContext.liveMidi = nullptr; // delivered once, in the first span
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

void Renderer::renderTrack(const Snapshot& snapshot,
                           const TrackNode& track,
                           core::SamplePosition position,
                           int numSamples,
                           const RenderContext& context) noexcept
{
    numSamples = std::min(numSamples, static_cast<int>(track.outLeft.size()));
    float* left = track.outLeft.data();
    float* right = track.outRight.data();
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
    // An inaudible (muted, un-soloed) track still renders: its inserts keep their
    // envelopes, tails and latency buffers moving, and its key output stays live
    // for sidechains. The routing stage is what keeps it out of the mix.

    // The pan this block ramps from and to (the runtime outlives snapshots).
    const float pan1 =
        track.runtime != nullptr ? track.runtime->targetPan.load(std::memory_order_relaxed) : track.pan;
    const float pan0 = track.runtime != nullptr && track.runtime->panPrimed ? track.runtime->pan : pan1;

    if (track.isMidi)
    {
        // A MIDI track: its events for the span go to the first insert that takes
        // MIDI (the instrument); the rest of the chain follows as on any channel,
        // and the pan (a stereo balance, the instrument's output being stereo)
        // and the polarity apply after it.
        scheduleMidi(track, position, numSamples, context);
        processChain(snapshot, track, left, right, numSamples, &track.events);
        float panLeft0 = 1.0f, panRight0 = 1.0f, panLeft1 = 1.0f, panRight1 = 1.0f;
        stereoPanGains(pan0, panLeft0, panRight0);
        stereoPanGains(pan1, panLeft1, panRight1);
        const float stepL = (panLeft1 - panLeft0) / static_cast<float>(numSamples);
        const float stepR = (panRight1 - panRight0) / static_cast<float>(numSamples);
        const float sign = track.phaseInvert ? -1.0f : 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float>(i + 1);
            left[i] *= sign * (panLeft0 + stepL * t);
            right[i] *= sign * (panRight0 + stepR * t);
        }
        if (track.runtime != nullptr)
        {
            track.runtime->pan = pan1;
            track.runtime->panPrimed = true;
        }
        return;
    }

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

            // Pan ramps from where the last block left it to the target over this
            // block (per sample, so a pan move glides).
            float panLeft0 = 1.0f, panRight0 = 1.0f, panLeft1 = 1.0f, panRight1 = 1.0f;
            const bool mono = clip.source->numChannels() == 1;
            if (mono)
            {
                monoPanGains(pan0, panLeft0, panRight0);
                monoPanGains(pan1, panLeft1, panRight1);
            }
            else
            {
                stereoPanGains(pan0, panLeft0, panRight0);
                stereoPanGains(pan1, panLeft1, panRight1);
            }
            const float stepL = (panLeft1 - panLeft0) / static_cast<float>(numSamples);
            const float stepR = (panRight1 - panRight0) / static_cast<float>(numSamples);

            float* sl = track.scratchLeft.data();
            float* sr = track.scratchRight.data();
            clip.source->read(0, sourcePos, sl, count);
            if (!mono)
                clip.source->read(1, sourcePos, sr, count);
            else
                sr = sl;

            // Fades: a linear ramp over the first fadeIn and last fadeOut samples
            // of the clip (overlapping clips with matching fades crossfade at
            // constant sum). Only the spans inside a fade pay for the ramp.
            const auto clipPos = from - clip.start; // position of sl[0] within the clip
            const auto fadeOutStart = clip.length - clip.fadeOut;
            for (int i = 0; i < count; ++i)
            {
                float f = 1.0f;
                const auto pos = clipPos + i;
                if (pos < clip.fadeIn)
                    f = static_cast<float>(pos) / static_cast<float>(clip.fadeIn);
                if (pos >= fadeOutStart && clip.fadeOut > 0)
                    f *= static_cast<float>(clip.length - pos) / static_cast<float>(clip.fadeOut);
                const float t = static_cast<float>(offset + i + 1);
                left[offset + i] += sl[i] * clip.gain * (panLeft0 + stepL * t) * f;
                right[offset + i] += sr[i] * clip.gain * (panRight0 + stepR * t) * f;
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
            float panLeft0 = 1.0f, panRight0 = 1.0f, panLeft1 = 1.0f, panRight1 = 1.0f;
            monoPanGains(pan0, panLeft0, panRight0);
            monoPanGains(pan1, panLeft1, panRight1);
            const float stepL = (panLeft1 - panLeft0) / static_cast<float>(numSamples);
            const float stepR = (panRight1 - panRight0) / static_cast<float>(numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float t = static_cast<float>(i + 1);
                left[i] += in[i] * (panLeft0 + stepL * t);
                right[i] += in[i] * (panRight0 + stepR * t);
            }
        }
    }
    if (track.runtime != nullptr)
    {
        track.runtime->pan = pan1;
        track.runtime->panPrimed = true;
    }

    if (track.phaseInvert)
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = -left[i];
            right[i] = -right[i];
        }

    processChain(snapshot, track, left, right, numSamples);
}

void Renderer::scheduleMidi(const TrackNode& track,
                            core::SamplePosition position,
                            int numSamples,
                            const RenderContext& context) noexcept
{
    auto& events = track.events;
    events.clear();
    auto* held = track.midi;
    if (held == nullptr)
        return;

    // A discontinuity (stop, jump, loop wrap, or play from elsewhere): everything
    // still sounding ends at the top of the span.
    const bool discontinuous = context.playing ? held->lastEnd != position : held->wasPlaying;
    if (discontinuous)
        for (int n = 0; n < 128; ++n)
            if (held->held[n] != 0)
            {
                events.noteOff(0, n);
                held->held[n] = 0;
            }
    held->wasPlaying = context.playing;
    held->lastEnd = context.playing ? position + numSamples : -1;

    if (context.playing)
    {
        const core::SamplePosition spanEnd = position + numSamples;
        for (const auto& clip : track.midiClips)
        {
            if (clip.start >= spanEnd)
                break;
            if (clip.end() <= position)
                continue;
            for (const auto& note : clip.notes)
            {
                if (note.start >= spanEnd)
                    break;
                const bool startsHere = note.start >= position;
                if (startsHere)
                {
                    events.noteOn(static_cast<int>(note.start - position), note.note, note.velocity);
                    held->held[note.note] = 1;
                }
                // Only a note that is sounding ends (one the transport started inside
                // never began).
                if (note.end < spanEnd && (startsHere || held->held[note.note] != 0) && note.end >= position)
                {
                    events.noteOff(static_cast<int>(note.end - position), note.note);
                    held->held[note.note] = 0;
                }
            }
        }
    }

    // Live input while the track monitors, from its chosen device (or any).
    if (context.liveMidi != nullptr && monitorsMidi(track, context.playing, context.recording))
        for (size_t i = 0; i < context.liveMidi->size(); ++i)
        {
            if (track.midiSource >= 0 && context.liveMidiSources != nullptr &&
                context.liveMidiSources[i] != track.midiSource)
                continue;
            MidiEvent e = (*context.liveMidi)[i];
            e.sampleOffset = std::clamp(e.sampleOffset, 0, numSamples - 1);
            events.push(e);
            if (e.type == MidiEvent::Type::noteOn && e.velocity > 0)
                held->held[e.note & 127] = 1;
            else if (e.type == MidiEvent::Type::noteOff || e.type == MidiEvent::Type::noteOn)
                held->held[e.note & 127] = 0;
            else if (e.type == MidiEvent::Type::allNotesOff)
                std::fill(std::begin(held->held), std::end(held->held), std::uint8_t{0});
        }
    events.sortByTime();
}

void Renderer::processChain(const Snapshot& snapshot,
                            const ChannelNode& channel,
                            float* left,
                            float* right,
                            int numSamples,
                            const MidiEventList* midi) noexcept
{
    bool midiDelivered = false;
    for (size_t k = 0; k < channel.inserts.size(); ++k)
    {
        auto* insert = channel.inserts[k];
        const float mix = k < channel.insertMix.size() ? channel.insertMix[k] : 1.0f;
        // The key signal: the source channel's pre-fader output, already rendered
        // (tracks by level, buses by order).
        const float* keyL = nullptr;
        const float* keyR = nullptr;
        // A source that is muted (or un-soloed) still renders, but its key is
        // silence: mute wins.
        if (const int t = k < channel.insertKeyTrack.size() ? channel.insertKeyTrack[k] : -1; t >= 0)
        {
            const auto& source = snapshot.tracks[static_cast<size_t>(t)];
            keyL = source.audible ? source.outLeft.data() : snapshot.silence.data();
            keyR = source.audible ? source.outRight.data() : snapshot.silence.data();
        }
        else if (const int b = k < channel.insertKeyBus.size() ? channel.insertKeyBus[k] : -1; b >= 0)
        {
            const auto& source = snapshot.buses[static_cast<size_t>(b)];
            keyL = source.audible ? source.outLeft.data() : snapshot.silence.data();
            keyR = source.audible ? source.outRight.data() : snapshot.silence.data();
        }
        const auto run = [&](float* l, float* r)
        {
            if (midi != nullptr && !midiDelivered && insert->acceptsMidi())
            {
                midiDelivered = true;
                insert->processMidi(*midi, l, r, numSamples);
            }
            else if (keyL != nullptr)
                insert->processWithSidechain(l, r, keyL, keyR, numSamples);
            else
                insert->process(l, r, numSamples);
        };
        if (mix >= 1.0f)
        {
            run(left, right);
            continue;
        }
        float* dryL = channel.dryLeft.data();
        float* dryR = channel.dryRight.data();
        if (auto* dry = k < channel.insertDry.size() ? channel.insertDry[k] : nullptr)
            dry->process(left, right, dryL, dryR, numSamples);
        else
        {
            std::copy(left, left + numSamples, dryL);
            std::copy(right, right + numSamples, dryR);
        }
        if (mix > 0.0f)
            run(left, right);
        else
        {
            // Fully dry: the insert still runs (its meters and state keep moving),
            // but on a copy, so the chain only sees the dry path.
            std::copy(dryL, dryL + numSamples, left);
            std::copy(dryR, dryR + numSamples, right);
            run(dryL, dryR); // scratch, discarded
            continue;
        }
        const float dryGain = 1.0f - mix;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = dryL[i] * dryGain + left[i] * mix;
            right[i] = dryR[i] * dryGain + right[i] * mix;
        }
    }
}

void Renderer::trackJob(void* context, int index) noexcept
{
    const auto& job = *static_cast<const SpanJob*>(context);
    const int track = (*job.tracks)[static_cast<size_t>(index)];
    renderTrack(*job.snapshot,
                job.snapshot->tracks[static_cast<size_t>(track)],
                job.position,
                job.numSamples,
                *job.context);
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

    // An edge with compensation reads a delayed copy of the channel's output. The
    // lines run even for an inaudible channel so that unmuting does not replay
    // stale audio; only the sum into the destination is skipped.
    const bool audible = channel.audible;
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
    // The fader ramps from where it was to the target across the block.
    const float gain1 = channel.runtime != nullptr
                            ? channel.runtime->targetGain.load(std::memory_order_relaxed)
                            : channel.gain;
    const float gain0 =
        channel.runtime != nullptr && channel.runtime->gainPrimed ? channel.runtime->gain : gain1;
    const float gainStep = (gain1 - gain0) / static_cast<float>(numSamples);
    const auto gainAt = [&](int i) { return gain0 + gainStep * static_cast<float>(i + 1); };
    {
        const float* l = nullptr;
        const float* r = nullptr;
        delayed(channel.outputDelay, l, r);
        if (audible)
            for (int i = 0; i < numSamples; ++i)
            {
                const float g = gainAt(i);
                outL[i] += l[i] * g;
                outR[i] += r[i] * g;
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
        if (channel.sendsAudible)
            for (int i = 0; i < numSamples; ++i)
            {
                const float level = send.preFader ? send.level : send.level * gainAt(i);
                sl[i] += l[i] * level;
                sr[i] += r[i] * level;
            }
    }
    if (channel.runtime != nullptr)
    {
        channel.runtime->gain = gain1;
        channel.runtime->gainPrimed = true;
    }

    if (!audible && !channel.sendsAudible)
    {
        if (context.meters != nullptr)
            context.meters->silence(channel.meterSlot, numSamples);
        return;
    }
    // A channel heard only through a send still shows what it sends.

    if (context.meters != nullptr)
    {
        // Post-fader level: the pre-fader buffer scaled by the gain, into the delay
        // scratch (free again now that every edge is routed). The pre-fader buffer
        // itself stays intact: it is what sidechained inserts read.
        float* l = channel.delayScratchLeft.data();
        float* r = channel.delayScratchRight.data();
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = gainAt(i);
            l[i] = pre[i] * g;
            r[i] = preR[i] * g;
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
    // Tracks render in parallel within a level; levels in order so a keyed track
    // sees its source finished.
    for (const auto& level : snapshot.trackLevels)
    {
        SpanJob job{&snapshot, &context, position, numSamples, &level};
        const int count = static_cast<int>(level.size());
        if (pool != nullptr && pool->numWorkers() > 0)
            pool->run(count, &trackJob, &job);
        else
            for (int i = 0; i < count; ++i)
                trackJob(&job, i);
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
        // The bus's pre-fader output: its summed inputs through balance (ramped)
        // and phase.
        const float pan1 =
            bus.runtime != nullptr ? bus.runtime->targetPan.load(std::memory_order_relaxed) : bus.pan;
        const float pan0 = bus.runtime != nullptr && bus.runtime->panPrimed ? bus.runtime->pan : pan1;
        float panLeft0 = 1.0f, panRight0 = 1.0f, panLeft1 = 1.0f, panRight1 = 1.0f;
        stereoPanGains(pan0, panLeft0, panRight0);
        stereoPanGains(pan1, panLeft1, panRight1);
        const float stepL = (panLeft1 - panLeft0) / static_cast<float>(numSamples);
        const float stepR = (panRight1 - panRight0) / static_cast<float>(numSamples);
        const float sign = bus.phaseInvert ? -1.0f : 1.0f;
        float* outL = bus.outLeft.data();
        float* outR = bus.outRight.data();
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float>(i + 1);
            outL[i] = bus.sumLeft[static_cast<size_t>(i)] * (panLeft0 + stepL * t) * sign;
            outR[i] = bus.sumRight[static_cast<size_t>(i)] * (panRight0 + stepR * t) * sign;
        }
        if (bus.runtime != nullptr)
        {
            bus.runtime->pan = pan1;
            bus.runtime->panPrimed = true;
        }
        processChain(snapshot, bus, outL, outR, numSamples);
        routeChannel(snapshot, bus, context, left, right, numSamples);
    }

    const float master1 = snapshot.master != nullptr
                              ? snapshot.master->targetGain.load(std::memory_order_relaxed)
                              : snapshot.masterGain;
    const float master0 =
        snapshot.master != nullptr && snapshot.master->primed ? snapshot.master->gain : master1;
    const float masterStep = (master1 - master0) / static_cast<float>(numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = master0 + masterStep * static_cast<float>(i + 1);
        left[i] *= g;
        right[i] *= g;
    }
    if (snapshot.master != nullptr)
    {
        snapshot.master->gain = master1;
        snapshot.master->primed = true;
    }
    if (context.meters != nullptr)
        context.meters->measure(snapshot.masterMeterSlot, left, right, numSamples);
    if (context.loudness != nullptr)
        context.loudness->processBlock(left, right, numSamples);
}

} // namespace aural::engine
