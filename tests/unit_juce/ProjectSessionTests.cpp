#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "engine/Midi.h"
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

TEST_CASE_METHOD(SessionFixture,
                 "A performance on an armed MIDI track becomes a clip with its notes at their positions",
                 "[app][session][record][midi]")
{
    const auto id = session->addMidiTrack(); // armed, every input, the built-in synth
    const auto* track = session->project().findTrack(id);
    REQUIRE(track != nullptr);
    CHECK(track->isMidi());
    CHECK(track->armed);
    REQUIRE(track->inserts.size() == 1);
    prepareDevice(48000.0);
    REQUIRE(session->startRecording().empty());
    runBlocks(2); // the record command applies in the first block: capture from 0
    session->engine().pushMidiInput({0, engine::MidiEvent::Type::noteOn, 0, 64, 90}, "");
    runBlocks(1); // on at 128
    session->engine().pushMidiInput({0, engine::MidiEvent::Type::noteOff, 0, 64, 0}, "");
    runBlocks(4); // off at 192, then the recording runs to 448
    stopAndCollect();

    track = session->project().findTrack(id);
    REQUIRE(track != nullptr);
    REQUIRE(track->midiClips.size() == 1);
    const auto& clip = track->midiClips[0];
    CHECK(clip.start == 0); // no device: nothing to compensate
    CHECK(clip.length == 448);
    REQUIRE(clip.notes.size() == 1);
    CHECK(clip.notes[0].note == 64);
    CHECK(clip.notes[0].velocity == 90);
    CHECK(clip.notes[0].start == 128);
    CHECK(clip.notes[0].length == 64);
    CHECK(session->lastRecordingMessage().find("MIDI clip") != std::string::npos);
    CHECK(files("*.wav").isEmpty()); // no audio was recorded
    CHECK(session->canUndo());
    session->undo();
    CHECK(session->project().findTrack(id)->midiClips.empty());
}

TEST_CASE_METHOD(SessionFixture,
                 "While recording, the live view grows with the take and hands over to the clip",
                 "[app][session][record][live]")
{
    addArmedTrack();
    prepareDevice(48000.0);
    engine::LiveRecording live;
    session->updateLiveRecording(live);
    CHECK(live.empty());

    REQUIRE(session->startRecording().empty());
    runBlocks(9, 0.5f); // 576 samples: two full bins of 0.5
    for (int i = 0; i < 2000; ++i)
    {
        session->updateLiveRecording(live);
        if (!live.takes.empty() && live.takes[0].samplesWritten >= 576)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(live.takes.size() == 1);
    CHECK(live.takes[0].trackId == session->project().tracks[0].id);
    CHECK(live.takes[0].startPosition == 0);
    CHECK(live.takes[0].samplesWritten == 576);
    CHECK(live.latency == session->recordingLatencySamples());
    REQUIRE(live.takes[0].levels[0].maximum.size() == 2);
    CHECK(live.takes[0].levels[0].maximum[1] == 0.5f);
    CHECK(live.takes[0].levels[0].minimum[1] == 0.5f);
    CHECK(live.midi.empty());

    // Stopped: the next update collects the files and the clip stands in for the take.
    session->stop();
    runBlocks(1);
    for (int i = 0; i < 2000 && !session->engine().hasRecordingToCollect(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    session->updateLiveRecording(live);
    CHECK(live.empty());
    REQUIRE(session->project().tracks[0].clips.size() == 1);
    CHECK(session->project().tracks[0].clips[0].length == 576);
}

TEST_CASE_METHOD(SessionFixture,
                 "While recording MIDI, the live view shows the take and the notes held so far",
                 "[app][session][record][midi][live]")
{
    const auto id = session->addMidiTrack();
    prepareDevice(48000.0);
    REQUIRE(session->startRecording().empty());
    engine::LiveRecording live;
    session->updateLiveRecording(live);
    CHECK(live.empty()); // the record command has not applied yet
    runBlocks(2);        // capture from 0; the take runs to 128
    session->updateLiveRecording(live);
    REQUIRE(live.midi.size() == 1); // an empty take, so the track visibly records
    CHECK(live.midi[0].trackId == id);
    CHECK(live.midi[0].takeIndex == 0);
    CHECK(live.midi[0].clip.start == 0);
    CHECK(live.midi[0].clip.length == 128);
    CHECK(live.midi[0].clip.notes.empty());

    session->engine().pushMidiInput({0, engine::MidiEvent::Type::noteOn, 0, 64, 90}, "");
    runBlocks(2); // on at 128, still held at 256
    session->updateLiveRecording(live);
    REQUIRE(live.midi.size() == 1);
    CHECK(live.midi[0].clip.length == 256);
    REQUIRE(live.midi[0].clip.notes.size() == 1);
    CHECK(live.midi[0].clip.notes[0].note == 64);
    CHECK(live.midi[0].clip.notes[0].start == 128);
    CHECK(live.midi[0].clip.notes[0].length == 128); // held: it ends where the take does, for now

    session->engine().pushMidiInput({0, engine::MidiEvent::Type::noteOff, 0, 64, 0}, "");
    runBlocks(1);
    stopAndCollect();
    session->updateLiveRecording(live);
    CHECK(live.empty());
    const auto* track = session->project().findTrack(id);
    REQUIRE(track != nullptr);
    REQUIRE(track->midiClips.size() == 1);
    REQUIRE(track->midiClips[0].notes.size() == 1);
    CHECK(track->midiClips[0].notes[0].length == 128);
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

TEST_CASE_METHOD(SessionFixture,
                 "Takes of one lost recording share a new track, and an empty partial is removed",
                 "[app][session][recover]")
{
    // No track 7 in this project: the first take makes a "Recovered" track and the
    // second joins it. The third file never got a header (killed right after open).
    writePartial("Bass-20261019-101010-1",
                 0,
                 400,
                 R"({"format":"aural.recording/1","trackId":7,"trackName":"Bass","startPosition":0,)"
                 R"("loopStart":1000,"sampleRate":48000.0,"latencySamples":0})");
    writePartial("Bass-20261019-101010-1", 1, 300, {});
    REQUIRE(media.getChildFile("Bass-20261019-101010-1-take3.partial.wav").create().wasOk());
    CHECK(session->countPartialRecordings() == 3);
    CHECK(session->recoverPartialRecordings() == 2);
    const auto& tracks = session->project().tracks;
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].name == "Recovered: Bass");
    REQUIRE(tracks[0].clips.size() == 2);
    CHECK(tracks[0].clips[0].start == 0);
    CHECK(tracks[0].clips[1].start == 1000);
    CHECK(session->countPartialRecordings() == 0);
    CHECK(files("*.recording.json").isEmpty());
}
