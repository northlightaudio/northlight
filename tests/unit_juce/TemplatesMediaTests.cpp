#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace aural;

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

struct Fixture
{
    Fixture()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("aural-template-tests")
                     .getChildFile(juce::Uuid().toString());
        REQUIRE(folder.createDirectory().wasOk());
        session = std::make_unique<ProjectSession>(audio);
        session->setTemplatesFolder(folder.getChildFile("Templates"));
    }
    ~Fixture()
    {
        session.reset();
        folder.deleteRecursively();
    }
    // A short WAV the loader can read.
    juce::File wav(const juce::File& file)
    {
        REQUIRE(file.getParentDirectory().createDirectory().wasOk());
        std::vector<float> samples(4800, 0.25f);
        const float* channels[] = {samples.data()};
        engine::WavWriter writer;
        REQUIRE(writer.open(file.getFullPathName().toStdString(), 1, 48000.0));
        REQUIRE(writer.write(channels, 4800));
        REQUIRE(writer.close());
        return file;
    }
    core::Id addTrackWith(const juce::File& media)
    {
        core::Track t;
        t.name = media.getFileNameWithoutExtension().toStdString();
        core::AudioClip clip;
        clip.mediaPath = media.getFullPathName().toStdString();
        clip.length = 4800;
        t.clips.push_back(clip);
        auto add = std::make_unique<core::AddTrack>(t);
        auto* raw = add.get();
        session->execute(std::move(add));
        return raw->trackId();
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    std::unique_ptr<ProjectSession> session;
    juce::File folder;
};
} // namespace

TEST_CASE("A template keeps the layout without clips and starts unsaved projects", "[session][templates]")
{
    Fixture f;
    const auto id = f.addTrackWith(f.wav(f.folder.getChildFile("kick.wav")));
    core::Bus bus;
    bus.name = "Reverb";
    f.session->execute(std::make_unique<core::AddBus>(bus));
    f.session->execute(std::make_unique<core::SetTrackGain>(id, 0.5f));
    CHECK_FALSE(f.session->saveAsTemplate("  ").empty()); // a name is needed
    REQUIRE(f.session->saveAsTemplate("Band").empty());
    const auto templates = f.session->templates();
    REQUIRE(templates.size() == 1);
    CHECK(templates[0].getFileNameWithoutExtension() == "Band");

    REQUIRE(f.session->newFromTemplate(templates[0]).empty());
    const auto& p = f.session->project();
    REQUIRE(p.tracks.size() == 1);
    CHECK(p.tracks[0].clips.empty());
    CHECK(p.tracks[0].gain == 0.5f);
    CHECK(p.tracks[0].strip.size() == f.session->project().tracks[0].strip.size());
    REQUIRE(p.buses.size() == 1);
    CHECK(p.buses[0].name == "Reverb");
    CHECK(f.session->projectFile() == juce::File());
    CHECK_FALSE(f.session->isModified());
    CHECK_FALSE(f.session->canUndo());
}

TEST_CASE("Media outside the project folder is gathered in, undoably, and unused media is found",
          "[session][media]")
{
    Fixture f;
    const auto project = f.folder.getChildFile("Song.aural");
    REQUIRE(f.session->save(project).empty());
    const auto media = f.session->mediaFolder();
    const auto inside = f.wav(media.getChildFile("vox.wav"));
    const auto outside = f.wav(f.folder.getChildFile("elsewhere").getChildFile("gtr.wav"));
    f.wav(media.getChildFile("old take.wav")); // used by nothing
    f.addTrackWith(inside);
    const auto gtr = f.addTrackWith(outside);

    auto report = f.session->mediaReport();
    REQUIRE(report.outside.size() == 1);
    CHECK(report.outside[0] == outside);
    REQUIRE(report.unused.size() == 1);
    CHECK(report.unused[0].getFileName() == "old take.wav");
    CHECK(report.missing.empty());

    int copied = 0;
    REQUIRE(f.session->consolidateMedia(copied).empty());
    CHECK(copied == 1);
    const auto* track = f.session->project().findTrack(gtr);
    REQUIRE(track != nullptr);
    const juce::File moved(juce::String(track->clips[0].mediaPath));
    CHECK(moved.isAChildOf(media));
    CHECK(moved.existsAsFile());
    CHECK(outside.existsAsFile()); // copied, not moved
    CHECK(f.session->mediaReport().outside.empty());

    f.session->undo();
    CHECK(f.session->project().findTrack(gtr)->clips[0].mediaPath == outside.getFullPathName().toStdString());
}
