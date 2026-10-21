#include "app/MidiFiles.h"
#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Command.h"
#include "ui/PianoRoll.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace aural;

namespace
{
juce::MidiMessage at(juce::MidiMessage m, double tick)
{
    m.setTimeStamp(tick);
    return m;
}

// Writes a file and reads it back, as a file on disk would be.
juce::MidiFile roundTrip(const juce::MidiFile& file)
{
    juce::MemoryOutputStream out;
    REQUIRE(file.writeTo(out, 1));
    juce::MemoryInputStream in(out.getData(), out.getDataSize(), false);
    juce::MidiFile read;
    REQUIRE(read.readFrom(in));
    return read;
}

class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return settings_; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { settings_ = settings; }

private:
    audio::AudioSettings settings_;
};
} // namespace

TEST_CASE("Ticks become samples through the file's tempo map", "[app][midifile]")
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence conductor;
    conductor.addEvent(at(juce::MidiMessage::tempoMetaEvent(500000), 0));    // 120 bpm
    conductor.addEvent(at(juce::MidiMessage::tempoMetaEvent(1000000), 960)); // 60 bpm from beat 2
    file.addTrack(conductor);
    const auto tempo = midifile::TempoMap::fromFile(file);
    CHECK(tempo.ticksPerQuarter == 480);
    CHECK(tempo.firstBpm() == 120.0);
    CHECK(midifile::ticksToSamples(480, tempo, 48000.0) == 24000); // a quarter at 120
    CHECK(midifile::ticksToSamples(960, tempo, 48000.0) == 48000);
    CHECK(midifile::ticksToSamples(1440, tempo, 48000.0) == 96000); // then a quarter at 60
    CHECK(midifile::samplesToTicks(24000, 48000.0, 120.0, 960) == 960.0);

    // No tempo at all: 120 bpm.
    juce::MidiFile plain;
    plain.setTicksPerQuarterNote(96);
    CHECK(midifile::ticksToSamples(96, midifile::TempoMap::fromFile(plain), 44100.0) == 22050);
}

TEST_CASE("A file track becomes clips per channel with notes, controllers, bend and pressure",
          "[app][midifile]")
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence track; // a type 0 track: tempo, a name and two channels
    track.addEvent(at(juce::MidiMessage::tempoMetaEvent(500000), 0));
    track.addEvent(at(juce::MidiMessage::textMetaEvent(3, "Band"), 0));
    track.addEvent(at(juce::MidiMessage::controllerEvent(1, 1, 64), 0));
    track.addEvent(at(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0));
    track.addEvent(at(juce::MidiMessage::pitchWheel(1, 0), 240));
    track.addEvent(at(juce::MidiMessage::noteOff(1, 60), 480));
    track.addEvent(at(juce::MidiMessage::channelPressureChange(1, 90), 480));
    track.addEvent(at(juce::MidiMessage::aftertouchChange(1, 62, 33), 600));
    track.addEvent(at(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(80)), 960));
    track.addEvent(at(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(90)), 1200)); // retrigger
    track.addEvent(at(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(127)), 0));  // never ends
    track.addEvent(at(juce::MidiMessage::controllerEvent(10, 7, 100), 1920));
    file.addTrack(track);
    const auto imported = midifile::fromMidiFile(roundTrip(file), 48000.0);
    REQUIRE(imported.error.empty());
    REQUIRE(imported.parts.size() == 2);
    const auto& keys = imported.parts[0];
    CHECK(keys.name == "Band (ch 1)");
    CHECK(keys.channel == 1);
    REQUIRE(keys.clip.notes.size() == 3);
    CHECK(keys.clip.notes[0].note == 60);
    CHECK(keys.clip.notes[0].start == 0);
    CHECK(keys.clip.notes[0].length == 24000);
    CHECK(keys.clip.notes[0].velocity == 100);
    CHECK(keys.clip.notes[1].note == 62);
    CHECK(keys.clip.notes[1].start == 48000);
    CHECK(keys.clip.notes[1].length == 12000); // ended by the retrigger
    CHECK(keys.clip.notes[2].start == 60000);
    CHECK(keys.clip.notes[2].velocity == 90);
    REQUIRE(keys.clip.controllers.size() == 4);
    CHECK(keys.clip.controllers[0].type == core::MidiControllerType::controller);
    CHECK(keys.clip.controllers[0].number == 1);
    CHECK(keys.clip.controllers[0].value == 64);
    CHECK(keys.clip.controllers[1].type == core::MidiControllerType::pitchBend);
    CHECK(keys.clip.controllers[1].value == -8192);
    CHECK(keys.clip.controllers[1].time == 12000);
    CHECK(keys.clip.controllers[2].type == core::MidiControllerType::channelPressure);
    CHECK(keys.clip.controllers[2].value == 90);
    CHECK(keys.clip.controllers[3].type == core::MidiControllerType::polyPressure);
    CHECK(keys.clip.controllers[3].number == 62);
    const auto& drums = imported.parts[1];
    CHECK(drums.channel == 10);
    REQUIRE(drums.clip.notes.size() == 1);
    CHECK(drums.clip.notes[0].length == 96000); // held to the channel's last event
    CHECK(drums.clip.length == 96001);
}

TEST_CASE("MIDI clips export to a type 1 file and read back the same", "[app][midifile]")
{
    core::Project p;
    p.sampleRate = 48000.0;
    p.tempoBpm = 120.0; // a tick at 960 PPQ is 25 samples
    p.timeSignature = {3, 4};
    core::Track t;
    t.id = 1;
    t.name = "Lead";
    t.kind = core::TrackKind::midi;
    t.midiChannel = 3;
    core::MidiClip a;
    a.id = 2;
    a.start = 24000;
    a.length = 48000;
    a.notes = {{3, 64, 99, 0, 12000}, {4, 67, 80, 40000, 20000}}; // the second is cut at the clip end
    a.controllers = {{5, core::MidiControllerType::controller, 11, 90, 500},
                     {6, core::MidiControllerType::pitchBend, 0, 4096, 1000},
                     {7, core::MidiControllerType::controller, 1, 30, -2000},       // a trim left it before
                     {8, core::MidiControllerType::channelPressure, 0, 12, 60000}}; // past the end: dropped
    a.sortControllers();
    t.midiClips.push_back(a);
    p.tracks.push_back(t);
    const auto* track = &p.tracks[0];
    const auto file = midifile::toMidiFile(p, {{track, {&track->midiClips[0]}}});
    CHECK(file.getNumTracks() == 2);
    CHECK(file.getTimeFormat() == 960);

    const auto read = roundTrip(file);
    juce::MidiMessageSequence signatures;
    read.findAllTimeSigEvents(signatures);
    REQUIRE(signatures.getNumEvents() == 1);
    int numerator = 0, denominator = 0;
    signatures.getEventPointer(0)->message.getTimeSignatureInfo(numerator, denominator);
    CHECK(numerator == 3);
    CHECK(denominator == 4);
    const auto imported = midifile::fromMidiFile(read, 48000.0);
    CHECK(imported.tempo.firstBpm() == 120.0);
    REQUIRE(imported.parts.size() == 1);
    const auto& part = imported.parts[0];
    CHECK(part.name == "Lead");
    CHECK(part.channel == 3);
    // Song positions (the clip started at 24000).
    REQUIRE(part.clip.notes.size() == 2);
    CHECK(part.clip.notes[0].start == 24000);
    CHECK(part.clip.notes[0].length == 12000);
    CHECK(part.clip.notes[1].start == 64000);
    CHECK(part.clip.notes[1].length == 8000);
    REQUIRE(part.clip.controllers.size() == 3);
    CHECK(part.clip.controllers[0].number == 1); // the trimmed value, at the clip's start
    CHECK(part.clip.controllers[0].time == 24000);
    CHECK(part.clip.controllers[1].number == 11);
    CHECK(part.clip.controllers[1].time == 24500);
    CHECK(part.clip.controllers[2].type == core::MidiControllerType::pitchBend);
    CHECK(part.clip.controllers[2].value == 4096);
    CHECK(part.clip.controllers[2].time == 25000);
}

TEST_CASE("Importing a MIDI file adds instrument tracks, takes its tempo when empty, and undoes in one step",
          "[app][midifile]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    ProjectSession session(audio);
    const auto rate = session.project().sampleRate;

    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence conductor;
    conductor.addEvent(at(juce::MidiMessage::tempoMetaEvent(600000), 0)); // 100 bpm
    conductor.addEvent(at(juce::MidiMessage::timeSignatureMetaEvent(6, 8), 0));
    file.addTrack(conductor);
    juce::MidiMessageSequence keys;
    keys.addEvent(at(juce::MidiMessage::textMetaEvent(3, "Keys"), 0));
    keys.addEvent(at(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 480));
    keys.addEvent(at(juce::MidiMessage::noteOff(1, 60), 960));
    keys.addEvent(at(juce::MidiMessage::controllerEvent(1, 64, 127), 480));
    file.addTrack(keys);
    juce::MidiMessageSequence drums;
    drums.addEvent(at(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(120)), 0));
    drums.addEvent(at(juce::MidiMessage::noteOff(10, 36), 240));
    file.addTrack(drums);
    const auto path = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("aural-midifile-" + juce::Uuid().toString() + ".mid");
    {
        juce::FileOutputStream out(path);
        REQUIRE(out.openedOk());
        REQUIRE(file.writeTo(out, 1));
    }

    REQUIRE(session.importMidiFile(path, 0).empty());
    const auto& p = session.project();
    CHECK(p.tempoBpm == 100.0);
    CHECK(p.timeSignature == core::TimeSignature{6, 8});
    REQUIRE(p.tracks.size() == 4); // two MIDI tracks, each over a new instrument channel
    const auto* keysTrack = &p.tracks[0];
    CHECK(keysTrack->name == "Keys");
    CHECK(keysTrack->isMidi());
    const auto* keysChannel = p.findTrack(keysTrack->midiTarget);
    REQUIRE(keysChannel != nullptr);
    CHECK(keysChannel->instrument.type == "aural.aurora");
    REQUIRE(keysTrack->midiClips.size() == 1);
    const auto& clip = keysTrack->midiClips[0];
    REQUIRE(clip.notes.size() == 1);
    CHECK(clip.notes[0].start == static_cast<core::SamplePosition>(rate * 0.6)); // a quarter at 100 bpm
    REQUIRE(clip.controllers.size() == 1);
    CHECK(clip.controllers[0].number == 64);
    const auto* drumsTrack = &p.tracks[2];
    CHECK(drumsTrack->midiChannel == 10);
    const auto* drumsChannel = p.findTrack(drumsTrack->midiTarget);
    REQUIRE(drumsChannel != nullptr);
    CHECK(drumsChannel->instrument.type == "aural.pulsar");

    // Into a chosen instrument: one more MIDI track on the keys' channel, no new channel.
    REQUIRE(session.importMidiFile(path, 0, keysChannel->id).empty());
    CHECK(session.project().tracks.size() == 6);

    session.undo();
    CHECK(session.project().tracks.size() == 4);
    session.undo();
    CHECK(session.project().tracks.empty());
    CHECK(session.project().tempoBpm == 120.0);

    // Export round trip through the session.
    REQUIRE(session.importMidiFile(path, 0).empty());
    const auto out = path.getSiblingFile(path.getFileNameWithoutExtension() + "-out.mid");
    std::vector<std::pair<core::Id, core::Id>> clips;
    for (const auto& t : session.project().tracks)
        for (const auto& c : t.midiClips)
            clips.emplace_back(t.id, c.id);
    REQUIRE(session.exportMidiFile(out, clips).empty());
    const auto again = midifile::readMidiFile(out, rate);
    REQUIRE(again.error.empty());
    REQUIRE(again.parts.size() == 2);
    const auto& before = session.project().tracks[0].midiClips[0].notes;
    REQUIRE(again.parts[0].clip.notes.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK(again.parts[0].clip.notes[i].note == before[i].note);
        CHECK(again.parts[0].clip.notes[i].start == before[i].start);
        CHECK(again.parts[0].clip.notes[i].length == before[i].length);
        CHECK(again.parts[0].clip.notes[i].velocity == before[i].velocity);
    }
    CHECK(session.importMidiFile(out.getSiblingFile("missing.mid"), 0).find("could not open") == 0);
    path.deleteFile();
    out.deleteFile();
}

TEST_CASE("Piano roll lanes have names and keys", "[ui][pianoroll]")
{
    using Lane = ui::PianoRoll::Lane;
    CHECK(ui::PianoRoll::laneForKey("bend") == Lane::controller(core::MidiControllerType::pitchBend));
    CHECK(ui::PianoRoll::laneForKey("cc74") == Lane::controller(core::MidiControllerType::controller, 74));
    CHECK(ui::PianoRoll::laneForKey("nonsense") == Lane::velocities());
    CHECK(ui::PianoRoll::laneName(ui::PianoRoll::laneForKey("mod")) == "CC 1 Mod wheel");
    CHECK(ui::PianoRoll::laneName(ui::PianoRoll::laneForKey("sustain"), true) == "SUS");
    CHECK(ui::PianoRoll::laneName(Lane::controller(core::MidiControllerType::controller, 20), true) ==
          "CC 20");
}
