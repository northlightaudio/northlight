#include "engine/Stretch.h"

#include "engine/rt/RealtimeGuard.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <signalsmith-stretch/signalsmith-stretch.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aural::engine
{

namespace
{
// Channel pointers with an offset, as the library indexes them: io[c][i].
struct Offset
{
    const std::vector<float*>* channels = nullptr;
    std::int64_t offset = 0;
    float* operator[](int c) const { return (*channels)[static_cast<size_t>(c)] + offset; }
};
struct ConstOffset
{
    const std::vector<const float*>* channels = nullptr;
    std::int64_t offset = 0;
    const float* operator[](int c) const { return (*channels)[static_cast<size_t>(c)] + offset; }
};

// Too short for the stretcher's latency: a plain linear resample (no transposition).
void resampleShort(const std::vector<const float*>& input,
                   std::int64_t inputLength,
                   const std::vector<float*>& output,
                   std::int64_t outputLength)
{
    for (size_t c = 0; c < output.size(); ++c)
        for (std::int64_t i = 0; i < outputLength; ++i)
        {
            const double x = static_cast<double>(i) * static_cast<double>(inputLength) /
                             static_cast<double>(std::max<std::int64_t>(1, outputLength));
            const auto a = static_cast<std::int64_t>(x);
            const double t = x - static_cast<double>(a);
            const float va = a < inputLength ? input[c][a] : 0.0f;
            const float vb = a + 1 < inputLength ? input[c][a + 1] : 0.0f;
            output[c][i] = static_cast<float>(va + (vb - va) * t);
        }
}
} // namespace

void stretchAudio(const std::vector<const float*>& input,
                  std::int64_t inputLength,
                  const std::vector<float*>& output,
                  std::int64_t outputLength,
                  double sampleRate,
                  double semitones,
                  const std::function<void(std::int64_t done)>& progress)
{
    AURAL_ASSERT_NOT_REALTIME("stretchAudio");
    const int channels = static_cast<int>(std::min(input.size(), output.size()));
    if (channels <= 0 || outputLength <= 0)
        return;
    if (inputLength <= 0)
    {
        for (auto* o : output)
            std::fill(o, o + outputLength, 0.0f);
        if (progress)
            progress(outputLength);
        return;
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch(0x5eed); // seeded: renders repeat exactly
    stretch.presetDefault(channels, static_cast<float>(sampleRate));
    stretch.setTransposeSemitones(static_cast<float>(semitones));
    const double playbackRate = static_cast<double>(inputLength) / static_cast<double>(outputLength);
    const auto seekLength =
        static_cast<std::int64_t>(stretch.outputSeekLength(static_cast<float>(playbackRate)));
    const auto tailOut = static_cast<std::int64_t>(std::ceil(static_cast<double>(seekLength) / playbackRate));
    if (inputLength <= seekLength || outputLength <= tailOut)
    {
        resampleShort(input, inputLength, output, outputLength);
        if (progress)
            progress(outputLength);
        return;
    }

    // As the library's exact(): pre-roll from the head of the input so output
    // sample 0 lines up with input sample 0, the body in chunks (publishing as
    // it goes), then the tail flushed out.
    stretch.outputSeek(ConstOffset{&input, 0}, static_cast<int>(seekLength));
    const std::int64_t bodyOut = outputLength - tailOut;
    const std::int64_t bodyIn = inputLength - seekLength;
    constexpr std::int64_t chunk = 16384;
    std::int64_t outDone = 0, inDone = 0;
    while (outDone < bodyOut)
    {
        const auto outCount = std::min(chunk, bodyOut - outDone);
        const auto inEnd = static_cast<std::int64_t>(
            std::llround(static_cast<double>(outDone + outCount) * static_cast<double>(bodyIn) /
                         static_cast<double>(bodyOut)));
        stretch.process(ConstOffset{&input, seekLength + inDone},
                        static_cast<int>(inEnd - inDone),
                        Offset{&output, outDone},
                        static_cast<int>(outCount));
        outDone += outCount;
        inDone = inEnd;
        if (progress)
            progress(outDone);
    }
    stretch.flush(Offset{&output, bodyOut}, static_cast<int>(tailOut), static_cast<float>(playbackRate));
    if (progress)
        progress(outputLength);
}

// ----- the source ----------------------------------------------------------------------------

StretchedSource::StretchedSource(int numChannels,
                                 std::int64_t length,
                                 double sampleRate,
                                 std::shared_ptr<const StretchRender> render)
    : numChannels_(std::max(1, numChannels)), length_(length), sampleRate_(sampleRate),
      render_(std::move(render))
{
}

void StretchedSource::read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept
{
    std::fill(dest, dest + numSamples, 0.0f);
    const auto& r = *render_;
    const auto ready = r.ready.load(std::memory_order_acquire);
    if (ready <= 0 || r.channels.empty())
        return;
    const auto from = std::max(sourceStart, r.start);
    const auto to = std::min(sourceStart + numSamples, r.start + ready);
    if (to <= from)
        return;
    const auto& samples = r.channels[static_cast<size_t>(channel) % r.channels.size()];
    std::memcpy(dest + (from - sourceStart),
                samples.data() + (from - r.start),
                static_cast<size_t>(to - from) * sizeof(float));
}

// ----- the service ---------------------------------------------------------------------------

StretchService::StretchService(MediaLoader& loader, bool synchronous, int workers)
    : loader_(loader), synchronous_(synchronous)
{
    if (!synchronous_)
        for (int i = 0; i < std::max(1, workers); ++i)
            workers_.emplace_back([this] { workerLoop(); });
}

StretchService::~StretchService()
{
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    for (auto& w : workers_)
        if (w.joinable())
            w.join();
}

std::pair<std::int64_t, std::int64_t>
StretchService::sourceRange(const core::AudioClip& clip, double sampleRate, std::int64_t mediaLength) noexcept
{
    const auto grain = std::max<std::int64_t>(1, static_cast<std::int64_t>(sampleRate * 4.0));
    const auto margin = static_cast<std::int64_t>(sampleRate * 0.5);
    const auto first = clip.sourceOffset - margin;
    const auto last = clip.sourceOffset + clip.sourceLength() + margin;
    auto from = first <= 0 ? 0 : (first / grain) * grain;
    auto to = ((last + grain - 1) / grain) * grain;
    from = std::clamp<std::int64_t>(from, 0, std::max<std::int64_t>(0, mediaLength));
    to = std::clamp<std::int64_t>(to, from, std::max<std::int64_t>(0, mediaLength));
    return {from, to};
}

std::shared_ptr<const AudioSource> StretchService::sourceFor(const core::AudioClip& clip,
                                                             double sampleRate,
                                                             const std::shared_ptr<const AudioSource>& base)
{
    AURAL_ASSERT_NOT_REALTIME("StretchService::sourceFor");
    if (base == nullptr || !clip.isStretched())
        return base;
    const auto mediaLength = base->lengthSamples();
    const auto [from, to] = sourceRange(clip, sampleRate, mediaLength);
    if (to <= from)
        return nullptr;
    const Key key{clip.mediaPath, sampleRate, clip.stretch, clip.transpose, from, to};
    std::shared_ptr<StretchRender> render;
    bool fresh = false;
    {
        std::lock_guard lock(mutex_);
        auto& slot = renders_[key];
        if (slot == nullptr)
        {
            slot = std::make_shared<StretchRender>();
            slot->path = clip.mediaPath;
            slot->sampleRate = sampleRate;
            slot->stretch = clip.stretch;
            slot->transpose = clip.transpose;
            slot->sourceFrom = from;
            slot->sourceTo = to;
            slot->start = static_cast<std::int64_t>(std::llround(static_cast<double>(from) * clip.stretch));
            slot->length = std::max<std::int64_t>(
                1,
                static_cast<std::int64_t>(std::llround(static_cast<double>(to) * clip.stretch)) -
                    slot->start);
            slot->numChannels = std::max(1, base->numChannels());
            fresh = true;
            if (!synchronous_)
                queue_.push_back(slot);
        }
        render = slot;
    }
    if (fresh && synchronous_)
        renderNow(*render);
    else if (fresh)
        wake_.notify_one();
    const auto length =
        static_cast<std::int64_t>(std::llround(static_cast<double>(mediaLength) * clip.stretch));
    return std::make_shared<StretchedSource>(render->numChannels, length, sampleRate, render);
}

std::shared_ptr<const AudioSource> StretchService::media(const std::string& path, double sampleRate)
{
    std::lock_guard lock(loadMutex_);
    const auto key = std::make_pair(path, sampleRate);
    {
        std::lock_guard state(mutex_);
        if (const auto it = media_.find(key); it != media_.end())
            return it->second;
    }
    std::string error;
    std::shared_ptr<const AudioSource> source = loader_.load(path, sampleRate, error);
    std::lock_guard state(mutex_);
    if (source != nullptr)
        media_[key] = source;
    return source;
}

void StretchService::renderNow(StretchRender& render)
{
    const auto source = media(render.path, render.sampleRate);
    if (source == nullptr)
    {
        render.failed.store(true);
        render.finished.store(true);
        return;
    }
    const auto count = render.sourceTo - render.sourceFrom;
    std::vector<std::vector<float>> in(
        static_cast<size_t>(render.numChannels),
        std::vector<float>(static_cast<size_t>(std::max<std::int64_t>(0, count))));
    for (int c = 0; c < render.numChannels; ++c)
        for (std::int64_t at = 0; at < count; at += 1 << 20)
        {
            const auto n = static_cast<int>(std::min<std::int64_t>(1 << 20, count - at));
            source->read(c, render.sourceFrom + at, in[static_cast<size_t>(c)].data() + at, n);
        }
    render.channels.assign(static_cast<size_t>(render.numChannels),
                           std::vector<float>(static_cast<size_t>(render.length), 0.0f));
    std::vector<const float*> inputs;
    std::vector<float*> outputs;
    for (int c = 0; c < render.numChannels; ++c)
    {
        inputs.push_back(in[static_cast<size_t>(c)].data());
        outputs.push_back(render.channels[static_cast<size_t>(c)].data());
    }
    stretchAudio(inputs,
                 count,
                 outputs,
                 render.length,
                 render.sampleRate,
                 render.transpose,
                 [&render](std::int64_t done) { render.ready.store(done, std::memory_order_release); });
    render.finished.store(true);
}

void StretchService::workerLoop()
{
    for (;;)
    {
        std::shared_ptr<StretchRender> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return quit_ || !queue_.empty(); });
            if (quit_)
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
            // Nothing plays it any more (a stretch dragged past it): drop it.
            if (job.use_count() <= 2)
            {
                std::erase_if(renders_, [&job](const auto& entry) { return entry.second == job; });
                if (queue_.empty() && running_ == 0)
                    idle_.notify_all();
                continue;
            }
            ++running_;
        }
        renderNow(*job);
        {
            std::lock_guard lock(mutex_);
            --running_;
            prune();
            if (queue_.empty() && running_ == 0)
                idle_.notify_all();
        }
        if (onChanged)
            onChanged();
    }
}

void StretchService::prune()
{
    // Renders nothing references (the map's own pointer only), once finished; the
    // decoded media once no render needs it.
    std::erase_if(renders_,
                  [](const auto& entry)
                  { return entry.second.use_count() == 1 && entry.second->finished.load(); });
    if (queue_.empty() && running_ == 0)
        media_.clear();
}

double StretchService::progress(const core::AudioClip& clip, double sampleRate) const
{
    if (!clip.isStretched())
        return 1.0;
    std::lock_guard lock(mutex_);
    for (const auto& [key, render] : renders_)
        if (render->path == clip.mediaPath && render->sampleRate == sampleRate &&
            render->stretch == clip.stretch && render->transpose == clip.transpose &&
            render->sourceFrom <= clip.sourceOffset &&
            render->sourceTo >= clip.sourceOffset + clip.sourceLength())
        {
            if (render->finished.load())
                return 1.0;
            return static_cast<double>(render->ready.load()) /
                   static_cast<double>(std::max<std::int64_t>(1, render->length));
        }
    return 0.0;
}

bool StretchService::isBusy() const
{
    std::lock_guard lock(mutex_);
    return !queue_.empty() || running_ > 0;
}

void StretchService::waitUntilIdle()
{
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && running_ == 0; });
}

} // namespace aural::engine
