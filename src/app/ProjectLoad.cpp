#include "app/ProjectLoad.h"

#include "core/ProjectJson.h"
#include "hosting/PluginRegistry.h"
#include "plugins/ChannelStrip.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <vector>

namespace northlight
{

namespace
{
double millisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The files the project plays, each once, in track order.
std::vector<std::string> mediaPaths(const core::Project& project)
{
    std::vector<std::string> paths;
    std::set<std::string> seen;
    const auto add = [&](const std::string& path)
    {
        if (!path.empty() && seen.insert(path).second)
            paths.push_back(path);
    };
    for (const auto& track : project.tracks)
    {
        add(track.frozenMedia);
        for (const auto& clip : track.clips)
            add(clip.mediaPath);
    }
    return paths;
}

const char* const separator = "  \xc2\xb7  "; // a middle dot between spaces
} // namespace

std::string describe(const LoadProgress& progress)
{
    const auto count = [&]
    {
        return std::to_string(std::min(progress.done + 1, progress.total)) + " of " +
               std::to_string(progress.total);
    };
    const auto withItem = [&](std::string text)
    {
        if (progress.total > 0)
            text += separator + count();
        if (!progress.item.empty())
            text += separator + progress.item;
        return text;
    };
    switch (progress.stage)
    {
        case LoadProgress::Stage::reading:
            return "Reading the project";
        case LoadProgress::Stage::media:
            return progress.total > 0 ? withItem("Reading audio") : "Reading the project";
        case LoadProgress::Stage::plugins:
            return progress.total > 0 ? withItem("Loading plugins") : "Setting up instruments and effects";
        case LoadProgress::Stage::engine:
            return "Preparing playback";
        case LoadProgress::Stage::done:
            return "Ready";
    }
    return {};
}

ProjectLoad::ProjectLoad(juce::File file, engine::MediaLoader& loader, std::string readError)
    : file_(std::move(file)), loader_(loader), readError_(std::move(readError)), thread_([this] { run(); })
{
}

ProjectLoad::~ProjectLoad()
{
    cancel();
    wait();
}

void ProjectLoad::wait()
{
    if (thread_.joinable())
        thread_.join();
}

LoadProgress ProjectLoad::progress() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void ProjectLoad::setProgress(
    LoadProgress::Stage stage, int done, int total, std::string item, double unitsDone)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    double start = 0.0;
    switch (stage)
    {
        case LoadProgress::Stage::reading:
            start = 0.0;
            break;
        case LoadProgress::Stage::media:
            start = mediaUnits_;
            break;
        case LoadProgress::Stage::plugins:
            start = pluginUnits_;
            break;
        case LoadProgress::Stage::engine:
            start = engineUnits_;
            break;
        case LoadProgress::Stage::done:
            start = totalUnits_;
            break;
    }
    progress_.stage = stage;
    progress_.done = done;
    progress_.total = total;
    progress_.item = std::move(item);
    const float fraction =
        stage == LoadProgress::Stage::done
            ? 1.0f
            : (totalUnits_ > 0.0 ? static_cast<float>(std::min(1.0, (start + unitsDone) / totalUnits_))
                                 : 0.0f);
    progress_.fraction = std::max(progress_.fraction, fraction);
}

void ProjectLoad::report(LoadProgress::Stage stage, int done, int total, std::string item, double units)
{
    setProgress(stage, done, total, std::move(item), units);
}

void ProjectLoad::run()
{
    auto started = std::chrono::steady_clock::now();
    setProgress(LoadProgress::Stage::reading, 0, 0, file_.getFileName().toStdString(), 0.0);
    const auto text = file_.loadFileAsString().toStdString();
    std::string error;
    auto loaded = text.empty() ? std::nullopt : core::projectFromJson(text, &error);
    if (!loaded.has_value())
    {
        error_ = !file_.existsAsFile() ? "file not found" : error.empty() ? readError_ : error;
        ready_.store(true, std::memory_order_release);
        return;
    }
    plugins::strip::ensureChannelStrip(*loaded); // projects saved before the strip existed
    timings_.readMs = millisecondsSince(started);

    // The work ahead, so the fraction moves evenly from here on.
    const auto paths = mediaPaths(*loaded);
    double pluginWork = 0.0;
    const auto count = [&](const core::Channel& channel)
    {
        for (const auto* insert : channel.processingChain())
        {
            ++timings_.inserts;
            const bool hosted = hosting::PluginRegistry::isHostedType(insert->type);
            timings_.hostedPlugins += hosted ? 1 : 0;
            pluginWork += hosted ? hostedUnits : builtInUnits;
        }
    };
    for (const auto& track : loaded->tracks)
        count(track);
    for (const auto& bus : loaded->buses)
        count(bus);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        mediaUnits_ = 1.0;
        pluginUnits_ = mediaUnits_ + static_cast<double>(paths.size());
        engineUnits_ = pluginUnits_ + pluginWork;
        totalUnits_ = engineUnits_ + 1.0;
    }

    started = std::chrono::steady_clock::now();
    const auto total = static_cast<int>(paths.size());
    for (int i = 0; i < total && !cancelled(); ++i)
    {
        const auto& path = paths[static_cast<size_t>(i)];
        setProgress(LoadProgress::Stage::media,
                    i,
                    total,
                    juce::File(juce::String::fromUTF8(path.c_str())).getFileName().toStdString(),
                    static_cast<double>(i));
        std::string ignored; // the compile records the error when it tries again
        if (auto source = loader_.load(path, loaded->sampleRate, ignored))
            media_[path] = std::move(source);
    }
    timings_.mediaFiles = total;
    timings_.mediaMs = millisecondsSince(started);
    setProgress(LoadProgress::Stage::media, total, total, {}, static_cast<double>(total));
    project_ = std::move(loaded);
    ready_.store(true, std::memory_order_release);
}

} // namespace northlight
