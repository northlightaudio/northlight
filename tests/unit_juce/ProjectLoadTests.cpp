#include "app/ProjectLoad.h"
#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "core/ProjectJson.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <thread>
#include <vector>

using namespace northlight;

namespace
{
class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return settings_; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { settings_ = settings; }

private:
    audio::AudioSettings settings_;
};

// Counts what it is asked to load and hands back a short silent source, taking
// `delayMs` over each so a test can cancel part way.
class CountingLoader final : public engine::MediaLoader
{
public:
    explicit CountingLoader(int delayMs = 0) : delayMs_(delayMs) {}
    std::shared_ptr<engine::AudioSource> load(const std::string& path, double rate, std::string&) override
    {
        if (delayMs_ > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
        loads[path]++;
        ++count;
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{std::vector<float>(64)}, rate);
    }
    std::map<std::string, int> loads; // read on the test thread only after wait()
    std::atomic<int> count{0};

private:
    int delayMs_ = 0;
};

struct Folder
{
    Folder()
    {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("northlight-load-tests")
                  .getChildFile(juce::Uuid().toString());
        REQUIRE(dir.createDirectory().wasOk());
    }
    ~Folder() { dir.deleteRecursively(); }
    juce::File dir;
};

// A project file with `tracks` audio tracks, each with two clips; every second
// track's clips share one file, so the files number fewer than the clips.
juce::File writeProject(const juce::File& file, int tracks)
{
    core::Project project;
    project.sampleRate = 48000.0;
    core::Id next = 1;
    for (int t = 0; t < tracks; ++t)
    {
        core::Track track;
        track.id = next++;
        track.name = "Track " + std::to_string(t);
        for (int c = 0; c < 2; ++c)
        {
            core::AudioClip clip;
            clip.id = next++;
            clip.mediaPath = "C:/media/take" + std::to_string(t % 2 == 0 ? t : t * 10 + c) + ".wav";
            clip.start = c * 48000;
            clip.length = 48000;
            track.clips.push_back(clip);
        }
        project.tracks.push_back(track);
    }
    project.nextId = next;
    REQUIRE(file.replaceWithText(juce::String(core::projectToJson(project))));
    return file;
}

juce::File writeWav(const juce::File& file)
{
    std::vector<float> samples(4800, 0.25f);
    const float* channels[] = {samples.data()};
    engine::WavWriter writer;
    REQUIRE(writer.open(file.getFullPathName().toStdString(), 1, 48000.0));
    REQUIRE(writer.write(channels, 4800));
    REQUIRE(writer.close());
    return file;
}
} // namespace

TEST_CASE("The load's progress line names the stage, the count and the item", "[app][load]")
{
    LoadProgress p;
    CHECK(describe(p) == "Reading the project");
    p.stage = LoadProgress::Stage::media;
    CHECK(describe(p) == "Reading the project"); // no audio files to read
    p.done = 2;
    p.total = 12;
    p.item = "kick.wav";
    CHECK(describe(p) == "Reading audio  \xc2\xb7  3 of 12  \xc2\xb7  kick.wav");
    p.done = 12; // finished: the count stays at the total
    p.item.clear();
    CHECK(describe(p) == "Reading audio  \xc2\xb7  12 of 12");
    p.stage = LoadProgress::Stage::plugins;
    p.done = 0;
    p.total = 0; // built-ins only
    CHECK(describe(p) == "Setting up instruments and effects");
    p.total = 3;
    p.item = "Reverb Pro";
    CHECK(describe(p) == "Loading plugins  \xc2\xb7  1 of 3  \xc2\xb7  Reverb Pro");
    p.stage = LoadProgress::Stage::engine;
    CHECK(describe(p) == "Preparing playback");
}

TEST_CASE("A project's media is decoded once per file off the message thread", "[app][load]")
{
    Folder folder;
    const auto file = writeProject(folder.dir.getChildFile("song.northlight"), 4);
    CountingLoader loader;
    ProjectLoad load(file, loader, "could not read project");
    load.wait();
    REQUIRE(load.ready());
    REQUIRE(load.project().has_value());
    CHECK(load.project()->tracks.size() == 4);
    // Tracks 0 and 2 use one file for both clips; tracks 1 and 3 two files each.
    CHECK(loader.loads.size() == 6);
    for (const auto& [path, times] : loader.loads)
        CHECK(times == 1);
    CHECK(load.media().size() == 6);
    const auto progress = load.progress();
    CHECK(progress.stage == LoadProgress::Stage::media);
    CHECK(progress.done == 6);
    CHECK(progress.total == 6);
    CHECK(progress.fraction > 0.0f);
    CHECK(progress.fraction < 1.0f); // the plugins and the snapshot are still to come
    CHECK(load.timings().mediaFiles == 6);
}

TEST_CASE("A load that cannot read its file says why", "[app][load]")
{
    Folder folder;
    CountingLoader loader;
    {
        ProjectLoad load(folder.dir.getChildFile("missing.northlight"), loader, "could not read project");
        load.wait();
        CHECK_FALSE(load.project().has_value());
        CHECK(load.error() == "file not found");
    }
    const auto junk = folder.dir.getChildFile("junk.northlight");
    REQUIRE(junk.replaceWithText("not a project"));
    ProjectLoad load(junk, loader, "could not read project");
    load.wait();
    CHECK_FALSE(load.project().has_value());
    CHECK_FALSE(load.error().empty());
    CHECK(loader.count == 0);
}

TEST_CASE("A cancelled load stops between files", "[app][load]")
{
    Folder folder;
    const auto file = writeProject(folder.dir.getChildFile("song.northlight"), 20);
    CountingLoader loader(20);
    ProjectLoad load(file, loader, "could not read project");
    while (loader.count == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    load.cancel();
    load.wait();
    CHECK(load.ready());
    CHECK(loader.count < 30); // 30 files in all
}

namespace
{
struct SessionFixture
{
    SessionFixture() { session = std::make_unique<ProjectSession>(audio); }
    ~SessionFixture() { session.reset(); }

    // Saves a project of `tracks` tracks, each with a clip on one shared WAV.
    juce::File saveProject(const juce::String& name, int tracks)
    {
        const auto media = writeWav(folder.dir.getChildFile("take.wav"));
        session->newProject();
        for (int t = 0; t < tracks; ++t)
        {
            core::Track track;
            track.name = "Track " + std::to_string(t);
            core::AudioClip clip;
            clip.mediaPath = media.getFullPathName().toStdString();
            clip.length = 4800;
            track.clips.push_back(clip);
            session->execute(std::make_unique<core::AddTrack>(track));
        }
        const auto file = folder.dir.getChildFile(name + ".northlight");
        REQUIRE(session->save(file).empty());
        return file;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    Folder folder;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    std::unique_ptr<ProjectSession> session;
};
} // namespace

TEST_CASE_METHOD(SessionFixture,
                 "Opening in steps reports each stage in order and ends with the project open",
                 "[app][load][session]")
{
    const auto file = saveProject("song", 3);
    session->newProject();
    REQUIRE(session->beginOpen(file));
    CHECK(session->isLoading());
    CHECK_FALSE(session->beginOpen(file)); // one load at a time

    std::vector<LoadProgress> seen;
    std::optional<std::string> outcome;
    int steps = 0;
    while (!(outcome = session->stepLoad(0.0)).has_value())
    {
        seen.push_back(session->loadProgress());
        ++steps;
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // the background part
        REQUIRE(steps < 100000);
    }
    CHECK(outcome->empty());
    CHECK_FALSE(session->isLoading());
    CHECK(session->project().tracks.size() == 3);
    CHECK(session->projectFile() == file);
    CHECK_FALSE(session->isModified());
    REQUIRE(session->engine().mediaErrors().empty());

    // Stages never go back, nor does the fraction; every stage after reading shows.
    for (size_t i = 1; i < seen.size(); ++i)
    {
        CHECK(static_cast<int>(seen[i].stage) >= static_cast<int>(seen[i - 1].stage));
        CHECK(seen[i].fraction >= seen[i - 1].fraction);
    }
    const auto showed = [&](LoadProgress::Stage stage)
    {
        return std::any_of(seen.begin(), seen.end(), [&](const LoadProgress& p) { return p.stage == stage; });
    };
    CHECK(showed(LoadProgress::Stage::plugins));
    CHECK(showed(LoadProgress::Stage::engine));

    // With no time budget, each step makes one processor: the window can repaint
    // between them. Every track has its channel strip's processors.
    const auto& timings = session->lastLoadTimings();
    CHECK(timings.mediaFiles == 1);
    CHECK(timings.inserts >= 3);
    const auto pluginSteps =
        std::count_if(seen.begin(),
                      seen.end(),
                      [](const LoadProgress& p) { return p.stage == LoadProgress::Stage::plugins; });
    CHECK(pluginSteps >= timings.inserts);
}

TEST_CASE_METHOD(SessionFixture,
                 "Cancelling a load before it replaces the project leaves the current one",
                 "[app][load][session]")
{
    const auto other = saveProject("other", 4);
    const auto current = saveProject("current", 1);
    REQUIRE(session->projectFile() == current);
    REQUIRE(session->beginOpen(other));
    CHECK(session->canCancelLoad());
    session->cancelLoad();
    CHECK_FALSE(session->isLoading());
    CHECK(session->project().tracks.size() == 1);
    CHECK(session->projectFile() == current);
    // Another load can start, and open() still works at once.
    CHECK(session->open(other).empty());
    CHECK(session->project().tracks.size() == 4);
}

TEST_CASE_METHOD(SessionFixture,
                 "Once the new project has replaced the current one the load cannot be cancelled",
                 "[app][load][session]")
{
    const auto file = saveProject("song", 2);
    session->newProject();
    REQUIRE(session->beginOpen(file));
    while (session->canCancelLoad())
    {
        REQUIRE_FALSE(session->stepLoad(0.0).has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(session->isLoading());
    CHECK(session->project().tracks.size() == 2); // the new project, plugins still to make
    session->cancelLoad();                        // too late: nothing happens
    CHECK(session->isLoading());
    std::optional<std::string> outcome;
    while (!(outcome = session->stepLoad()).has_value())
    {
    }
    CHECK(outcome->empty());
}

TEST_CASE_METHOD(SessionFixture,
                 "A failed load reports its error and keeps the project",
                 "[app][load][session]")
{
    const auto current = saveProject("current", 2);
    const auto junk = folder.dir.getChildFile("junk.northlight");
    REQUIRE(junk.replaceWithText("{ not json"));
    REQUIRE(session->beginOpen(junk));
    std::optional<std::string> outcome;
    while (!(outcome = session->stepLoad()).has_value())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK_FALSE(outcome->empty());
    CHECK_FALSE(session->isLoading());
    CHECK(session->projectFile() == current);
    CHECK(session->project().tracks.size() == 2);
}

TEST_CASE_METHOD(SessionFixture,
                 "Third-party plugins ahead are announced before the step that makes them",
                 "[app][load][session]")
{
    // A hosted plugin that is not installed still counts as one (it fails fast).
    session->newProject();
    core::Track track;
    track.name = "Vocal";
    session->execute(std::make_unique<core::AddTrack>(track));
    const auto trackId = session->project().tracks.front().id;
    core::Insert plugin;
    plugin.type = "vst3:not-installed";
    session->execute(std::make_unique<core::AddInsert>(trackId, plugin));
    const auto file = folder.dir.getChildFile("vocal.northlight");
    REQUIRE(session->save(file).empty());
    session->newProject();

    REQUIRE(session->beginOpen(file));
    while (session->stepLoad(1.0e9) == std::nullopt && session->canCancelLoad())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // The commit was a step of its own: the project is in, no plugin is made yet,
    // and the progress names the one coming.
    REQUIRE(session->isLoading());
    CHECK(session->project().tracks.size() == 1);
    CHECK(session->loadHasSlowStepsAhead());
    auto progress = session->loadProgress();
    CHECK(progress.stage == LoadProgress::Stage::plugins);
    CHECK(progress.done == 0);
    CHECK(progress.total == 1);
    CHECK(progress.item == "Missing plugin");

    // Built-ins go in a batch; the hosted plugin waits for a step of its own.
    std::optional<std::string> outcome;
    while (session->loadHasSlowStepsAhead())
        REQUIRE_FALSE((outcome = session->stepLoad(1.0e9)).has_value());
    progress = session->loadProgress();
    CHECK(progress.done == 1);
    while (!(outcome = session->stepLoad()).has_value())
    {
    }
    CHECK(outcome->empty());
    CHECK_FALSE(session->loadHasSlowStepsAhead());
}

TEST_CASE_METHOD(SessionFixture, "A project of built-ins has no slow steps ahead", "[app][load][session]")
{
    const auto file = saveProject("song", 2);
    session->newProject();
    REQUIRE(session->beginOpen(file));
    std::optional<std::string> outcome;
    while (!(outcome = session->stepLoad()).has_value())
    {
        CHECK_FALSE(session->loadHasSlowStepsAhead());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(outcome->empty());
}
