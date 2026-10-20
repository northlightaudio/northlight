#include "engine/pitch/VocalService.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace aural::engine
{

namespace
{
// Paths in the model are UTF-8.
std::filesystem::path fsPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}
} // namespace

// ----- the edited source ---------------------------------------------------------------------

PitchEditedSource::PitchEditedSource(std::shared_ptr<const AudioSource> base,
                                     std::vector<std::shared_ptr<const pitch::RenderedSpan>> spans)
    : base_(std::move(base)), spans_(std::move(spans))
{
    std::erase_if(spans_, [](const auto& s) { return s == nullptr || s->length() == 0; });
    std::sort(spans_.begin(), spans_.end(), [](const auto& a, const auto& b) { return a->start < b->start; });
}

void PitchEditedSource::read(int channel,
                             std::int64_t sourceStart,
                             float* dest,
                             int numSamples) const noexcept
{
    base_->read(channel, sourceStart, dest, numSamples);
    const std::int64_t end = sourceStart + numSamples;
    // The first span that could reach the block: spans are short and few, so a
    // binary search on the start and a step back is enough.
    auto it = std::upper_bound(spans_.begin(),
                               spans_.end(),
                               sourceStart,
                               [](std::int64_t s, const auto& span) { return s < span->start; });
    if (it != spans_.begin())
        --it;
    while (it != spans_.begin() && (*(it - 1))->start + (*(it - 1))->length() > sourceStart)
        --it;
    for (; it != spans_.end() && (*it)->start < end; ++it)
    {
        const auto& span = **it;
        const std::int64_t from = std::max(sourceStart, span.start);
        const std::int64_t to = std::min(end, span.start + span.length());
        if (to <= from)
            continue;
        const auto& samples = span.channels[static_cast<size_t>(channel) % span.channels.size()];
        std::memcpy(dest + (from - sourceStart),
                    samples.data() + (from - span.start),
                    static_cast<size_t>(to - from) * sizeof(float));
    }
}

// ----- the service ---------------------------------------------------------------------------

VocalService::VocalService(MediaLoader& loader, bool synchronous) : loader_(loader), synchronous_(synchronous)
{
    if (!synchronous_)
        worker_ = std::thread([this] { workerLoop(); });
}

VocalService::~VocalService()
{
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

std::uint64_t VocalService::mediaKey(const std::string& path, double sampleRate) noexcept
{
    auto h = pitch::hashBytes(path.data(), path.size());
    return pitch::hashBytes(&sampleRate, sizeof sampleRate, h);
}

std::string VocalService::cachePathFor(const std::string& path, double sampleRate)
{
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(fsPath(path), ec))
        return {};
    std::ostringstream name;
    name << path << '.' << static_cast<long long>(sampleRate) << ".aural-pitch";
    return name.str();
}

std::shared_ptr<VocalService::Media>
VocalService::mediaFor(const std::string& path, double sampleRate, bool start)
{
    std::shared_ptr<Media> media;
    bool queue = false;
    {
        std::lock_guard lock(mutex_);
        auto& slot = media_[Key{path, sampleRate}];
        if (slot == nullptr)
        {
            slot = std::make_shared<Media>();
            slot->path = path;
            slot->sampleRate = sampleRate;
        }
        media = slot;
        if (start && media->state == State::none)
        {
            media->state = State::analysing;
            queue = true;
            if (!synchronous_)
                analyses_.push_back(media);
        }
    }
    if (queue)
    {
        if (synchronous_)
            analyseNow(*media);
        else
            wake_.notify_all();
    }
    return media;
}

std::shared_ptr<const pitch::PitchAnalysis> VocalService::analysis(const std::string& path, double sampleRate)
{
    const auto media = mediaFor(path, sampleRate, true);
    std::lock_guard lock(mutex_);
    return media->analysis;
}

VocalService::Status VocalService::status(const std::string& path, double sampleRate) const
{
    std::lock_guard lock(mutex_);
    const auto it = media_.find(Key{path, sampleRate});
    if (it == media_.end())
        return {};
    return Status{it->second->state, it->second->progress.load(), it->second->error};
}

std::shared_ptr<const std::vector<std::vector<float>>> VocalService::samples(const std::string& path,
                                                                             double sampleRate) const
{
    std::lock_guard lock(mutex_);
    const auto it = media_.find(Key{path, sampleRate});
    return it == media_.end() ? nullptr : it->second->samples;
}

void VocalService::analyseNow(Media& media)
{
    std::string error;
    std::shared_ptr<AudioSource> source = loader_.load(media.path, media.sampleRate, error);
    auto samples = std::make_shared<std::vector<std::vector<float>>>();
    if (source != nullptr)
    {
        const auto length = source->lengthSamples();
        samples->assign(static_cast<size_t>(std::max(1, source->numChannels())),
                        std::vector<float>(static_cast<size_t>(std::max<std::int64_t>(0, length))));
        for (size_t ch = 0; ch < samples->size(); ++ch)
        {
            // In blocks: a source's read is meant for audio-block sizes.
            constexpr int block = 1 << 16;
            for (std::int64_t at = 0; at < length; at += block)
                source->read(static_cast<int>(ch),
                             at,
                             (*samples)[ch].data() + at,
                             static_cast<int>(std::min<std::int64_t>(block, length - at)));
        }
        source.reset();
    }

    std::shared_ptr<pitch::PitchAnalysis> result;
    if (source == nullptr && samples->empty())
        error = error.empty() ? "could not load the audio" : error;
    else
    {
        const auto cache = cachePathFor(media.path, media.sampleRate);
        std::error_code ec;
        if (!cache.empty() && std::filesystem::exists(fsPath(cache), ec) &&
            std::filesystem::last_write_time(fsPath(cache), ec) >=
                std::filesystem::last_write_time(fsPath(media.path), ec))
        {
            std::ifstream in(fsPath(cache), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            auto cached = std::make_shared<pitch::PitchAnalysis>();
            if (pitch::PitchAnalysis::deserialise(bytes, *cached) &&
                cached->length == static_cast<std::int64_t>((*samples)[0].size()))
                result = cached;
        }
        if (result == nullptr)
        {
            result = std::make_shared<pitch::PitchAnalysis>(pitch::analyse(*samples,
                                                                           media.sampleRate,
                                                                           [&media](double p)
                                                                           {
                                                                               media.progress.store(p);
                                                                               return true;
                                                                           }));
            if (!cache.empty())
            {
                std::ofstream out(fsPath(cache), std::ios::binary | std::ios::trunc);
                const auto bytes = result->serialise();
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
        }
    }
    {
        std::lock_guard lock(mutex_);
        if (result != nullptr)
        {
            media.samples = std::move(samples);
            media.analysis = std::move(result);
            media.state = State::ready;
            media.progress.store(1.0);
        }
        else
        {
            media.state = State::failed;
            media.error = error;
        }
    }
}

void VocalService::renderNow(const RenderJob& job)
{
    std::shared_ptr<const pitch::PitchAnalysis> analysis;
    std::shared_ptr<const std::vector<std::vector<float>>> samples;
    {
        std::lock_guard lock(mutex_);
        analysis = job.media->analysis;
        samples = job.media->samples;
    }
    if (analysis == nullptr || samples == nullptr)
        return;
    const auto shift = pitch::shiftCurve(*analysis, job.edit);
    for (const auto& plan : job.plans)
    {
        auto span = std::make_shared<const pitch::RenderedSpan>(
            pitch::renderSpan(*samples, *analysis, job.edit, shift, plan));
        std::lock_guard lock(mutex_);
        spans_[plan.key] = std::move(span);
        spanOrder_.push_back(plan.key);
        pending_.erase(plan.key);
    }
}

std::shared_ptr<const AudioSource> VocalService::sourceFor(const core::AudioClip& clip,
                                                           double sampleRate,
                                                           std::shared_ptr<const AudioSource> base)
{
    if (base == nullptr || !clip.vocal.changesSound())
    {
        std::lock_guard lock(mutex_);
        clipSpans_.erase(clip.id);
        return base;
    }
    const auto media = mediaFor(clip.mediaPath, sampleRate, true);
    std::shared_ptr<const pitch::PitchAnalysis> analysis;
    {
        std::lock_guard lock(mutex_);
        analysis = media->analysis;
    }
    if (analysis == nullptr)
        return base; // onChanged fires when it is ready
    const auto plans = pitch::planSpans(*analysis, clip.vocal, mediaKey(clip.mediaPath, sampleRate));

    std::vector<std::shared_ptr<const pitch::RenderedSpan>> spans;
    RenderJob job{media, clip.vocal, {}};
    {
        std::lock_guard lock(mutex_);
        const auto& previous = clipSpans_[clip.id];
        for (const auto& plan : plans)
        {
            if (const auto it = spans_.find(plan.key); it != spans_.end())
            {
                spans.push_back(it->second);
                continue;
            }
            if (!pending_.contains(plan.key))
            {
                job.plans.push_back(plan);
                pending_.insert(plan.key);
            }
            // While it renders, the clip's last render of the same stretch plays on.
            for (const auto& old : previous)
                if (old->start == plan.start && old->length() == plan.end - plan.start)
                    spans.push_back(old);
        }
    }
    if (!job.plans.empty())
    {
        if (synchronous_)
        {
            renderNow(job);
            std::lock_guard lock(mutex_);
            spans.clear();
            for (const auto& plan : plans)
                if (const auto it = spans_.find(plan.key); it != spans_.end())
                    spans.push_back(it->second);
        }
        else
        {
            {
                std::lock_guard lock(mutex_);
                renders_.push_back(std::move(job));
            }
            wake_.notify_all();
        }
    }
    {
        std::lock_guard lock(mutex_);
        clipSpans_[clip.id] = spans;
        prune();
    }
    return std::make_shared<PitchEditedSource>(std::move(base), std::move(spans));
}

void VocalService::prune()
{
    // Keeps what clips play and the most recent renders (undo comes back to them).
    constexpr size_t keepRecent = 192;
    if (spans_.size() <= keepRecent)
        return;
    std::set<const pitch::RenderedSpan*> playing;
    for (const auto& [id, list] : clipSpans_)
        for (const auto& s : list)
            playing.insert(s.get());
    while (spans_.size() > keepRecent && !spanOrder_.empty())
    {
        const auto key = spanOrder_.front();
        spanOrder_.pop_front();
        if (const auto it = spans_.find(key); it != spans_.end() && !playing.contains(it->second.get()))
            spans_.erase(it);
    }
}

bool VocalService::isBusy() const
{
    std::lock_guard lock(mutex_);
    return running_ > 0 || !analyses_.empty() || !renders_.empty();
}

void VocalService::waitUntilIdle()
{
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return quit_ || (running_ == 0 && analyses_.empty() && renders_.empty()); });
}

void VocalService::workerLoop()
{
    std::unique_lock lock(mutex_);
    while (!quit_)
    {
        if (renders_.empty() && analyses_.empty())
        {
            idle_.notify_all();
            wake_.wait(lock, [this] { return quit_ || !renders_.empty() || !analyses_.empty(); });
            continue;
        }
        ++running_;
        // Renders first: they are short and the user is waiting to hear them.
        if (!renders_.empty())
        {
            auto job = std::move(renders_.front());
            renders_.pop_front();
            lock.unlock();
            renderNow(job);
        }
        else
        {
            auto media = std::move(analyses_.front());
            analyses_.pop_front();
            lock.unlock();
            analyseNow(*media);
        }
        if (onChanged)
            onChanged();
        lock.lock();
        --running_;
    }
    idle_.notify_all();
}

} // namespace aural::engine
