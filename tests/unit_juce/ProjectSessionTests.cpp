#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <thread>
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

// A session on a project saved in a fresh temp folder, so its media folder is
// empty and ours. The device layer is never initialised: no hardware, no callback.
struct SessionFixture
{
    SessionFixture()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("aural-session-tests")
                     .getChildFile(juce::Uuid().toString());
        REQUIRE(folder.createDirectory().wasOk());
        session = std::make_unique<ProjectSession>(audio);
        REQUIRE(session->save(folder.getChildFile("song.aural")).empty());
        media = session->mediaFolder();
        REQUIRE(media.createDirectory().wasOk());
        session->setCountInBars(0); // capture from the first block
    }
    ~SessionFixture()
    {
        session.reset();
        folder.deleteRecursively();
    }

    // An armed track on device input 0, id 1.
    void addArmedTrack(const std::string& name = "Guitar")
    {
        core::Track t;
        t.id = 1;
        t.name = name;
        t.inputChannel = 0;
        t.armed = true;
        session->execute(std::make_unique<core::AddTrack>(t));
    }

    // Stands in for the device: prepares the engine and pushes blocks through it.
    void prepareDevice(double rate) { session->engine().prepare(rate, 64); }
    void runBlocks(int blocks, float value = 0.5f)
    {
        std::vector<float> in(64, value);
        const float* inputs[] = {in.data()};
        std::vector<float> l(64), r(64);
        float* outputs[] = {l.data(), r.data()};
        for (int i = 0; i < blocks; ++i)
            session->engine().process(inputs, 1, outputs, 2, 64);
    }
    // Stops, lets the writer close the files and the session's timer place the clips.
    void stopAndCollect()
    {
        session->stop();
        runBlocks(1);
        for (int i = 0; i < 2000 && !session->engine().hasRecordingToCollect(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        REQUIRE(session->engine().hasRecordingToCollect());
        session->poll();
        REQUIRE_FALSE(session->engine().hasRecordingToCollect());
    }

    juce::Array<juce::File> files(const juce::String& pattern) const
    {
        return media.findChildFiles(juce::File::findFiles, false, pattern);
    }

    // Writes a finished-looking partial file of `samples` samples plus its sidecar.
    void writePartial(const juce::String& base, int takeIndex, int samples, const juce::String& sidecar)
    {
        const auto file = media.getChildFile(
            base + (takeIndex > 0 ? "-take" + juce::String(takeIndex + 1) : juce::String()) + ".partial.wav");
        engine::WavWriter writer;
        REQUIRE(writer.open(file.getFullPathName().toStdString(), 1, 48000.0));
        std::vector<float> v(static_cast<size_t>(samples), 0.25f);
        REQUIRE(writer.writeInterleaved(v.data(), samples));
        REQUIRE(writer.close());
        if (sidecar.isNotEmpty())
            REQUIRE(media.getChildFile(base + ".recording.json").replaceWithText(sidecar));
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    juce::File folder, media;
    std::unique_ptr<ProjectSession> session;
};
} // namespace

TEST_CASE_METHOD(SessionFixture,
                 "Recording is refused while the device rate differs from the project rate",
                 "[app][session][record]")
{
    addArmedTrack();
    CHECK(session->startRecording() == "no audio device is running");
    prepareDevice(44100.0); // the project is at 48 kHz
    const auto error = session->startRecording();
    CHECK(error.find("44100 Hz") != std::string::npos);
    CHECK(error.find("48000 Hz") != std::string::npos);
    CHECK_FALSE(session->isRecording());
    CHECK(files("*.partial.wav").isEmpty());
    CHECK(files("*.recording.json").isEmpty()); // nothing left behind by the refusal
}

TEST_CASE_METHOD(SessionFixture,
                 "A recording ends up as a clip on its track with no partial file or sidecar left",
                 "[app][session][record]")
{
    addArmedTrack();
    prepareDevice(48000.0);
    REQUIRE(session->startRecording().empty());
    CHECK(files("*.recording.json").size() == 1); // written before capture begins
    runBlocks(3);
    stopAndCollect();

    const auto& track = session->project().tracks[0];
    REQUIRE(track.clips.size() == 1);
    CHECK(track.clips[0].start == 0);
    CHECK(track.clips[0].length == 192);
    CHECK(files("*.partial.wav").isEmpty());
    CHECK(files("*.recording.json").isEmpty());
    REQUIRE(files("Guitar-*.wav").size() == 1);
    CHECK(track.clips[0].mediaPath == files("Guitar-*.wav")[0].getFullPathName().toStdString());
}

TEST_CASE_METHOD(SessionFixture,
                 "Two recordings in the same second get different files",
                 "[app][session][record]")
{
    addArmedTrack();
    prepareDevice(48000.0);
    for (int pass = 0; pass < 2; ++pass)
    {
        REQUIRE(session->startRecording().empty());
        runBlocks(2);
        stopAndCollect();
    }
    const auto& clips = session->project().tracks[0].clips;
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].mediaPath != clips[1].mediaPath);
    CHECK(files("Guitar-*.wav").size() == 2);
}

TEST_CASE_METHOD(SessionFixture, "An empty take is deleted rather than placed", "[app][session][record]")
{
    addArmedTrack();
    prepareDevice(48000.0);
    REQUIRE(session->startRecording().empty());
    stopAndCollect(); // stopped before a single block was captured
    CHECK(session->project().tracks[0].clips.empty());
    CHECK(files("*.wav").isEmpty());
    CHECK(files("*.recording.json").isEmpty());
}

TEST_CASE_METHOD(SessionFixture,
                 "Recovery puts a take back on its track, shifted by the recorded latency",
                 "[app][session][recover]")
{
    addArmedTrack();
    writePartial("Guitar-20261019-101010-1",
                 0,
                 500,
                 R"({"format":"aural.recording/1","trackId":1,"trackName":"Guitar","startPosition":1000,)"
                 R"("loopStart":-1,"sampleRate":48000.0,"latencySamples":100})");
    CHECK(session->countPartialRecordings() == 1);
    CHECK(session->recoverPartialRecordings() == 1);
    REQUIRE(session->project().tracks.size() == 1); // no "Recovered" track: the original is still here
    const auto& clips = session->project().tracks[0].clips;
    REQUIRE(clips.size() == 1);
    CHECK(clips[0].start == 900);
    CHECK(clips[0].length == 500);
    CHECK(files("*.partial.wav").isEmpty());
    CHECK(files("*.recovered.wav").size() == 1);
    CHECK(files("*.recording.json").isEmpty());
    CHECK(session->countPartialRecordings() == 0);
}

TEST_CASE_METHOD(SessionFixture,
                 "A later take recovers at the loop start, and no sidecar means a new track",
                 "[app][session][recover]")
{
    addArmedTrack();
    writePartial("Guitar-20261019-101010-1",
                 1,
                 300,
                 R"({"format":"aural.recording/1","trackId":1,"trackName":"Guitar","startPosition":500,)"
                 R"("loopStart":2000,"sampleRate":48000.0,"latencySamples":50})");
    writePartial("Orphan-20261019-101010-1", 0, 200, {});
    CHECK(session->countPartialRecordings() == 2);
    CHECK(session->recoverPartialRecordings() == 2);
    const auto& tracks = session->project().tracks;
    REQUIRE(tracks.size() == 2);
    REQUIRE(tracks[0].clips.size() == 1);
    CHECK(tracks[0].clips[0].start == 1950);
    CHECK(tracks[1].name.find("Recovered") == 0);
    REQUIRE(tracks[1].clips.size() == 1);
    CHECK(tracks[1].clips[0].start == 0);
    CHECK(tracks[1].clips[0].length == 200);
}

TEST_CASE_METHOD(SessionFixture,
                 "Discarding sets partial files aside and drops their sidecars",
                 "[app][session][recover]")
{
    writePartial("Guitar-20261019-101010-1", 0, 100, R"({"format":"aural.recording/1","trackId":1})");
    CHECK(session->discardPartialRecordings() == 1);
    CHECK(session->countPartialRecordings() == 0);
    CHECK(files("*.discarded.wav").size() == 1);
    CHECK(files("*.recording.json").isEmpty());
}
