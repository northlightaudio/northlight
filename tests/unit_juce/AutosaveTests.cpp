#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

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

struct AutosaveFixture
{
    AutosaveFixture()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("aural-autosave-tests")
                     .getChildFile(juce::Uuid().toString());
        REQUIRE(folder.createDirectory().wasOk());
        autosaves = folder.getChildFile("Autosave");
    }
    ~AutosaveFixture() { folder.deleteRecursively(); }

    std::unique_ptr<ProjectSession> makeSession()
    {
        auto session = std::make_unique<ProjectSession>(audio);
        session->setAutosave(autosaves, [] { return 0; }); // written on demand in these tests
        return session;
    }

    void addTrack(ProjectSession& session, const std::string& name)
    {
        core::Track t;
        t.name = name;
        session.execute(std::make_unique<core::AddTrack>(t));
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    juce::File folder, autosaves;
};
} // namespace

TEST_CASE("Unsaved changes are autosaved and a crashed session's copy can be restored", "[session][autosave]")
{
    AutosaveFixture f;
    {
        auto session = f.makeSession();
        CHECK_FALSE(session->autosaveNow()); // nothing to keep yet
        f.addTrack(*session, "Drums");
        REQUIRE(session->autosaveNow());
        CHECK(session->currentAutosaveFile().existsAsFile());
        CHECK(session->leftoverAutosaves().empty()); // its own copy is not a leftover
        // A crash: the session never closes, so its copy stays. (Simulated by
        // copying the file aside before the normal close removes it.)
        REQUIRE(session->currentAutosaveFile().copyFileTo(f.folder.getChildFile("kept.aural")));
        REQUIRE(session->currentAutosaveFile().withFileExtension("json").copyFileTo(
            f.folder.getChildFile("kept.json")));
    }
    // The normal close removed it.
    CHECK(f.autosaves.findChildFiles(juce::File::findFiles, false, "*.aural").isEmpty());
    REQUIRE(f.folder.getChildFile("kept.aural").copyFileTo(f.autosaves.getChildFile("Untitled-abc.aural")));
    REQUIRE(f.folder.getChildFile("kept.json").copyFileTo(f.autosaves.getChildFile("Untitled-abc.json")));

    auto next = f.makeSession();
    const auto leftovers = next->leftoverAutosaves();
    REQUIRE(leftovers.size() == 1);
    CHECK(leftovers[0].project == juce::File());
    REQUIRE(next->restoreAutosave(leftovers[0]).empty());
    REQUIRE(next->project().tracks.size() == 1);
    CHECK(next->project().tracks[0].name == "Drums");
    CHECK(next->isModified()); // restored, not saved
    CHECK_FALSE(next->project().tracks[0].strip.empty());
    CHECK(next->leftoverAutosaves().empty()); // taken
}

TEST_CASE("Saving removes the autosave and a saved project's copy names its file", "[session][autosave]")
{
    AutosaveFixture f;
    auto session = f.makeSession();
    const auto song = f.folder.getChildFile("Song.aural");
    REQUIRE(session->save(song).empty());
    f.addTrack(*session, "Bass");
    REQUIRE(session->autosaveNow());
    const auto copy = session->currentAutosaveFile();
    CHECK(copy.getFileName().startsWith("Song-"));
    REQUIRE(copy.existsAsFile());
    REQUIRE(session->save(song).empty());
    CHECK_FALSE(copy.existsAsFile());
    CHECK_FALSE(copy.withFileExtension("json").existsAsFile());

    // A leftover for a saved project belongs to that file when restored.
    f.addTrack(*session, "Keys");
    REQUIRE(session->autosaveNow());
    REQUIRE(copy.copyFileTo(f.folder.getChildFile("x.aural")));
    REQUIRE(copy.withFileExtension("json").copyFileTo(f.folder.getChildFile("x.json")));
    session.reset();
    REQUIRE(f.folder.getChildFile("x.aural").copyFileTo(copy));
    REQUIRE(f.folder.getChildFile("x.json").copyFileTo(copy.withFileExtension("json")));
    auto next = f.makeSession();
    const auto leftovers = next->leftoverAutosaves();
    REQUIRE(leftovers.size() == 1);
    CHECK(leftovers[0].project == song);
    CHECK(leftovers[0].name == "Song");
    REQUIRE(next->restoreAutosave(leftovers[0]).empty());
    CHECK(next->projectFile() == song);
    CHECK(next->project().tracks.size() == 2);
    ProjectSession::discardAutosave(leftovers[0]); // already gone: harmless
}
