#include "app/ProjectSession.h"
#include "app/SessionNotices.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;

// The project's life with its files: what survives a close nobody chose, who
// owns an autosave, backups, and media that follows the project (FileActions
// asks the questions; these are the session's rules behind them).

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

struct Lifecycle
{
    Lifecycle()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("northlight-lifecycle-tests")
                     .getChildFile(juce::Uuid().toString());
        REQUIRE(folder.createDirectory().wasOk());
        autosaves = folder.getChildFile("Autosave");
        recordings = folder.getChildFile("Recordings");
    }
    ~Lifecycle() { folder.deleteRecursively(); }

    // Autosave on (every minute), so a close with unsaved changes writes a copy.
    std::unique_ptr<ProjectSession> makeSession(int intervalSeconds = 60)
    {
        auto session = std::make_unique<ProjectSession>(audio);
        session->setAutosave(autosaves, [intervalSeconds] { return intervalSeconds; });
        session->setSharedRecordingsFolder(recordings);
        return session;
    }
    static void addTrack(ProjectSession& session, const std::string& name)
    {
        core::Track t;
        t.name = name;
        session.execute(std::make_unique<core::AddTrack>(t));
    }
    static core::Id addTrackWith(ProjectSession& session, const juce::File& media)
    {
        core::Track t;
        t.name = media.getFileNameWithoutExtension().toStdString();
        core::AudioClip clip;
        clip.mediaPath = media.getFullPathName().toStdString();
        clip.length = 4800;
        t.clips.push_back(clip);
        auto add = std::make_unique<core::AddTrack>(t);
        auto* raw = add.get();
        session.execute(std::move(add));
        return raw->trackId();
    }
    // A short WAV the loader can read.
    static juce::File wav(const juce::File& file)
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
    juce::Array<juce::File> autosaveFiles() const
    {
        return autosaves.findChildFiles(juce::File::findFiles, false, "*.northlight");
    }
    static std::string mediaOf(const ProjectSession& session, size_t track)
    {
        return session.project().tracks[track].clips[0].mediaPath;
    }

    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    juce::File folder, autosaves, recordings;
};
} // namespace

TEST_CASE("Closing with unsaved changes nobody chose to lose leaves a fresh autosave for the next launch",
          "[session][autosave][lifecycle]")
{
    Lifecycle f;
    {
        auto session = f.makeSession();
        Lifecycle::addTrack(*session, "Drums");
        REQUIRE(session->autosaveNow());
        Lifecycle::addTrack(*session, "Bass"); // after the last autosave
    } // closed without Save or Don't save (Windows ending the session, a quit nobody answered)
    REQUIRE(f.autosaveFiles().size() == 1);
    auto next = f.makeSession();
    const auto leftovers = next->leftoverAutosaves();
    REQUIRE(leftovers.size() == 1);
    REQUIRE(next->restoreAutosave(leftovers[0]).empty());
    CHECK(next->project().tracks.size() == 2); // the copy has the edit made after the last autosave
}

TEST_CASE("Don't save lets the close delete the autosave", "[session][autosave][lifecycle]")
{
    Lifecycle f;
    {
        auto session = f.makeSession();
        Lifecycle::addTrack(*session, "Drums");
        REQUIRE(session->autosaveNow());
        session->discardChanges();
    }
    CHECK(f.autosaveFiles().isEmpty());
}

TEST_CASE("A new project over unsaved changes keeps their autosave unless they were discarded",
          "[session][autosave][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    Lifecycle::addTrack(*session, "Drums");
    session->newProject(); // reached without the question (a path that forgot to ask)
    CHECK(f.autosaveFiles().size() == 1);
    CHECK(session->project().tracks.empty());

    Lifecycle::addTrack(*session, "Keys");
    REQUIRE(session->autosaveNow());
    CHECK(f.autosaveFiles().size() == 2);
    session->discardChanges();
    session->newProject();                // Don't save, then New
    CHECK(f.autosaveFiles().size() == 1); // only the first one's copy is left
    session.reset();
    auto next = f.makeSession();
    REQUIRE(next->leftoverAutosaves().size() == 1);
}

TEST_CASE("A restored autosave stays on disk until the project is saved", "[session][autosave][lifecycle]")
{
    Lifecycle f;
    {
        auto session = f.makeSession();
        Lifecycle::addTrack(*session, "Drums");
        REQUIRE(session->autosaveNow());
    }
    auto next = f.makeSession();
    const auto leftover = next->leftoverAutosaves().at(0);
    REQUIRE(next->restoreAutosave(leftover).empty());
    CHECK(leftover.file.existsAsFile());      // a crash now still has it
    CHECK(next->leftoverAutosaves().empty()); // and it is this session's, not a leftover
    REQUIRE(next->save(f.folder.getChildFile("Song.northlight")).empty());
    CHECK_FALSE(leftover.file.existsAsFile());
}

TEST_CASE("Another running instance's autosave is neither offered nor deleted",
          "[session][autosave][lifecycle]")
{
    Lifecycle f;
    auto first = f.makeSession();
    Lifecycle::addTrack(*first, "Drums");
    REQUIRE(first->autosaveNow());
    const auto copy = first->currentAutosaveFile();

    auto second = f.makeSession();
    CHECK(second->instanceId() != first->instanceId());
    CHECK(second->leftoverAutosaves().empty()); // the first still runs: its copy is live
    second.reset();
    CHECK(copy.existsAsFile());

    // Two instances with the same saved project write copies of their own.
    const auto song = f.folder.getChildFile("Song.northlight");
    REQUIRE(first->save(song).empty());
    Lifecycle::addTrack(*first, "Bass");
    REQUIRE(first->autosaveNow());
    auto other = f.makeSession();
    REQUIRE(other->open(song).empty());
    Lifecycle::addTrack(*other, "Keys");
    REQUIRE(other->autosaveNow());
    CHECK(other->currentAutosaveFile() != first->currentAutosaveFile());
    CHECK(other->leftoverAutosaves().empty());

    first.reset(); // closes with unsaved changes: its copy becomes a leftover
    const auto leftovers = other->leftoverAutosaves();
    REQUIRE(leftovers.size() == 1);
    CHECK(leftovers[0].project == song);
}

TEST_CASE("Saving keeps the version it replaces as a backup beside the project",
          "[session][files][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    const auto song = f.folder.getChildFile("Song.northlight");
    const auto backup = ProjectSession::backupFileFor(song);
    CHECK(backup.getFileName() == "Song.northlight-bak");
    Lifecycle::addTrack(*session, "Drums");
    REQUIRE(session->save(song).empty());
    CHECK_FALSE(backup.existsAsFile()); // nothing was replaced
    const auto first = song.loadFileAsString();
    Lifecycle::addTrack(*session, "Bass");
    REQUIRE(session->save(song).empty());
    REQUIRE(backup.existsAsFile());
    CHECK(backup.loadFileAsString() == first);
    CHECK(song.loadFileAsString() != first);
    // The backup opens like any project.
    auto other = f.makeSession();
    REQUIRE(other->open(backup).empty());
    CHECK(other->project().tracks.size() == 1);

    session->setKeepBackup(false);
    const auto second = song.loadFileAsString();
    Lifecycle::addTrack(*session, "Keys");
    REQUIRE(session->save(song).empty());
    CHECK(backup.loadFileAsString() == first); // left as it was
    CHECK(song.loadFileAsString() != second);
    // No temporary file is left beside it.
    CHECK(f.folder.findChildFiles(juce::File::findFiles, false, "*.northlight*").size() == 2);
}

TEST_CASE("Imported media is copied into a saved project's folder when Preferences say so",
          "[session][media][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    const auto outside = Lifecycle::wav(f.folder.getChildFile("Samples").getChildFile("loop.wav"));
    session->setCopyImportedMedia(true);
    // Unsaved: nowhere to copy to yet; it is brought in by the first save.
    CHECK(session->takeInMedia(outside) == outside);
    REQUIRE(session->save(f.folder.getChildFile("Song.northlight")).empty());

    REQUIRE(session->importAudioFile(outside, 0).empty());
    const auto copy = session->mediaFolder().getChildFile("loop.wav");
    CHECK(Lifecycle::mediaOf(*session, 0) == copy.getFullPathName().toStdString());
    CHECK(copy.existsAsFile());
    CHECK(outside.existsAsFile()); // copied, not moved
    // The same file again uses the copy it has; a different one of the same name gets its own.
    CHECK(session->takeInMedia(outside) == copy);
    const auto other = Lifecycle::wav(f.folder.getChildFile("Other").getChildFile("loop.wav"));
    other.appendText("x"); // not the same contents
    CHECK(session->takeInMedia(other) != copy);
    CHECK(session->takeInMedia(copy) == copy); // already inside

    session->setCopyImportedMedia(false);
    CHECK(session->takeInMedia(outside) == outside);
}

TEST_CASE("An unsaved project's media comes into its folder on the first save, undoably",
          "[session][media][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    const auto take = Lifecycle::wav(f.recordings.getChildFile("Vox 1.wav")); // recorded while untitled
    const auto loop = Lifecycle::wav(f.folder.getChildFile("Samples").getChildFile("loop.wav"));
    Lifecycle::addTrackWith(*session, take);
    Lifecycle::addTrackWith(*session, loop);

    const auto song = f.folder.getChildFile("Song.northlight");
    REQUIRE(session->save(song).empty()); // not bringing other media: the recording comes anyway
    const auto media = session->mediaFolder();
    CHECK(Lifecycle::mediaOf(*session, 0) == media.getChildFile("Vox 1.wav").getFullPathName().toStdString());
    CHECK(Lifecycle::mediaOf(*session, 1) == loop.getFullPathName().toStdString());
    CHECK(session->undoName() == "Bring media into the project folder");
    CHECK_FALSE(session->isModified()); // the step is part of what was saved

    // Save As elsewhere, bringing everything along.
    const auto moved = f.folder.getChildFile("Elsewhere").getChildFile("Song 2.northlight");
    REQUIRE(moved.getParentDirectory().createDirectory().wasOk());
    CHECK(session->mediaToBring(moved).first == 2);
    REQUIRE(session->save(moved, true).empty());
    const auto movedMedia = moved.getSiblingFile("Song 2 Media");
    CHECK(Lifecycle::mediaOf(*session, 0) ==
          movedMedia.getChildFile("Vox 1.wav").getFullPathName().toStdString());
    CHECK(Lifecycle::mediaOf(*session, 1) ==
          movedMedia.getChildFile("loop.wav").getFullPathName().toStdString());
    CHECK(session->mediaToBring(moved).first == 0);
    session->undo();
    CHECK(Lifecycle::mediaOf(*session, 1) == loop.getFullPathName().toStdString());
}

TEST_CASE("Relinking points the missing clips at the located file, and the folder's others too",
          "[session][media][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    const auto gone = f.folder.getChildFile("Gone");
    Lifecycle::addTrackWith(*session, gone.getChildFile("kick.wav"));
    Lifecycle::addTrackWith(*session, gone.getChildFile("snare.wav"));
    Lifecycle::addTrackWith(*session, gone.getChildFile("hat.wav"));
    REQUIRE(session->mediaReport().missing.size() == 3);

    const auto found = f.folder.getChildFile("Found");
    const auto kick = Lifecycle::wav(found.getChildFile("kick.wav"));
    Lifecycle::wav(found.getChildFile("snare.wav")); // hat.wav is not there
    CHECK(session->missingFilesIn(found, gone.getChildFile("kick.wav")) == 1);

    CHECK(session->relinkMedia(gone.getChildFile("kick.wav"), found.getChildFile("nothing.wav"), true) == 0);
    CHECK(session->relinkMedia(gone.getChildFile("kick.wav"), kick, false) == 1);
    CHECK(Lifecycle::mediaOf(*session, 0) == kick.getFullPathName().toStdString());
    CHECK(session->mediaReport().missing.size() == 2);
    session->undo();
    CHECK(session->mediaReport().missing.size() == 3);

    CHECK(session->relinkMedia(gone.getChildFile("kick.wav"), kick, true) == 2);
    CHECK(session->undoName() == "Relink media");
    const auto missing = session->mediaReport().missing;
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].getFileName() == "hat.wav");
    session->undo(); // one step for both
    CHECK(session->mediaReport().missing.size() == 3);
    session->redo();
    CHECK(session->mediaReport().missing.size() == 1);
}

TEST_CASE("An unsaved project's unused media are only its own recordings", "[session][media][lifecycle]")
{
    Lifecycle f;
    auto session = f.makeSession();
    Lifecycle::wav(f.recordings.getChildFile("Someone else's take.wav"));
    CHECK(session->mediaReport().unused.empty()); // the shared folder is not this project's
}

TEST_CASE("Take names follow the pattern, and an empty one falls back", "[session][recording]")
{
    CHECK(ProjectSession::takeName("{track} {take}", "Vox", 3) == "Vox 3");
    CHECK(ProjectSession::takeName("{take} - {track}", "Gtr", 12) == "12 - Gtr");
    CHECK(ProjectSession::takeName("Take {take}", "Vox", 1) == "Take 1");
    CHECK(ProjectSession::takeName("   ", "Vox", 2) == "Vox 2");
    Lifecycle f;
    auto session = f.makeSession();
    session->setRecordingBits(24);
    CHECK(session->recordingBits() == 24);
    session->setRecordingBits(16);
    CHECK(session->recordingBits() == 32); // only float and 24-bit
    session->setPreRollBars(-2);
    CHECK(session->preRollBars() == 0);
}

TEST_CASE("Dropouts while a device starts are not counted; once it has settled they are", "[app][dropouts]")
{
    DropoutCounter counter;
    DropoutCounter::Raw raw;
    CHECK(counter.count(raw, 0.0) == 0); // no device yet
    raw.device = "ASIO/Volt/48000/64";
    raw.overruns = 1; // the first callbacks after opening ran late
    CHECK(counter.count(raw, 100.0) == 0);
    raw.overruns = 2;
    CHECK(counter.count(raw, 1500.0) == 0);
    raw.overruns = 3; // after the settle time: a real one
    CHECK(counter.count(raw, 100.0 + DropoutCounter::settleMs + 10.0) == 1);
    raw.diskGaps = 2;
    CHECK(counter.count(raw, 5000.0) == 3);
    CHECK(counter.diskGaps(raw) == 2);
    // A buffer change restarts the device: its glitches are not counted either.
    raw.device = "ASIO/Volt/48000/128";
    raw.overruns = 0;                       // the callback's statistics reset
    CHECK(counter.count(raw, 6000.0) == 2); // the disk gaps stay
    raw.overruns = 4;
    CHECK(counter.count(raw, 6500.0) == 2);
    counter.clear(raw);
    CHECK(counter.count(raw, 9000.0) == 0);
}

TEST_CASE("A Don't save followed by an open that fails keeps the project's autosave",
          "[session][autosave][lifecycle]")
{
    Lifecycle f;
    {
        auto session = f.makeSession();
        Lifecycle::addTrack(*session, "Drums");
        session->discardChanges();
        CHECK_FALSE(session->open(f.folder.getChildFile("missing.northlight")).empty());
        CHECK(session->project().tracks.size() == 1); // still the same project
    } // closed later without a decision
    CHECK(f.autosaveFiles().size() == 1);
}
