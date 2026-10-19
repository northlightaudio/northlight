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

void Renderer::render(const Snapshot& snapshot,
                      TransportState& transport,
                      float* const* outputs,
                      int numOutputs,
                      int numSamples,
                      WorkerPool* pool) noexcept
{
    numSamples = std::min(numSamples, maxBlockSize_);
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);

    if (!transport.playing || numSamples <= 0)
        return;

    float* left = mixLeft_.data();
    float* right = mixRight_.data();
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);

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
        renderSpan(snapshot, transport.position, left + done, right + done, span, pool);
        transport.position += span;
        done += span;
        if (transport.loopIsValid() && transport.position >= transport.loopEnd)
            transport.position = transport.loopStart;
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

void Renderer::renderTrack(const TrackNode& track, core::SamplePosition position, int numSamples) noexcept
{
    numSamples = std::min(numSamples, static_cast<int>(track.outLeft.size()));
    float* left = track.outLeft.data();
    float* right = track.outRight.data();
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);
    if (!track.audible)
        return;

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
        const float gainLeft = clip.gain * track.gain * panLeft;
        const float gainRight = clip.gain * track.gain * panRight;

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

void Renderer::trackJob(void* context, int index) noexcept
{
    const auto& job = *static_cast<const SpanJob*>(context);
    renderTrack(job.snapshot->tracks[static_cast<size_t>(index)], job.position, job.numSamples);
}

void Renderer::renderSpan(const Snapshot& snapshot,
                          core::SamplePosition position,
                          float* left,
                          float* right,
                          int numSamples,
                          WorkerPool* pool) noexcept
{
    const auto trackCount = static_cast<int>(snapshot.tracks.size());
    SpanJob job{&snapshot, position, numSamples};
    if (pool != nullptr && pool->numWorkers() > 0)
    {
        pool->run(trackCount, &trackJob, &job);
    }
    else
    {
        for (int t = 0; t < trackCount; ++t)
            trackJob(&job, t);
    }

    // Sum in track order: the result does not depend on which thread did what.
    for (const auto& track : snapshot.tracks)
    {
        if (!track.audible)
            continue;
        const float* tl = track.outLeft.data();
        const float* tr = track.outRight.data();
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] += tl[i];
            right[i] += tr[i];
        }
    }
}

} // namespace aural::engine
