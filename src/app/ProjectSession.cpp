#include "ProjectSession.h"

#include "app/MidiFiles.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Comping.h"
#include "core/ProjectJson.h"
#include "core/TempoMap.h"
#include "engine/WavFile.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/Pulsar.h"
#include "ui/Db.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace northlight
{

namespace
{
constexpr int garbageTimerMilliseconds = 250;
}

ProjectSession::ProjectSession(audio::AudioDeviceLayer& audio, juce::File registryFile)
    : audio_(audio), loader_(disk_), registryFile_(std::move(registryFile)), scanner_(registry_),
      inserts_(registry_), engine_(loader_, inserts_)
{
    inserts_.setSampleLoader(sampleFiles_.loader());
    if (registryFile_.existsAsFile())
        if (auto loaded = hosting::PluginRegistry::fromJson(registryFile_.loadFileAsString().toStdString()))
            registry_ = std::move(*loaded);
    scanner_.onProgress = [this] { notify(Change::progress); }; // the status and the browser, not every view
    scanner_.onFinished = [this]
    {
        saveRegistry();
        inserts_.clearFailures(); // a plugin that failed before may load now
        publish();
    };
    vocal_.onChanged = [this] { vocalUpdates_.triggerAsyncUpdate(); };
    vocalUpdates_.onUpdate = [this]
    {
        if (!vocalRecompile_.isTimerRunning())
            vocalRecompile_.startTimer(vocalRecompileMilliseconds);
    };
    vocalRecompile_.onDue = [this]
    {
        for (const auto& t : project_.tracks)
            for (const auto& c : t.clips)
                if (c.vocal.changesSound())
                {
                    engine_.forgetClipSources(); // the renders are new, the clips are not
                    engine_.setProject(project_);
                    return;
                }
    };
    engine_.setClipSourceHook(
        [this](const core::AudioClip& clip, double rate, std::shared_ptr<const engine::AudioSource> base)
        { return vocal_.sourceFor(clip, rate, std::move(base)); });
    engine_.setStretchHook(
        [this](const core::AudioClip& clip, double rate, std::shared_ptr<const engine::AudioSource> base)
        { return stretch_.sourceFor(clip, rate, base); });
    stretch_.onChanged = [this] { stretchUpdates_.triggerAsyncUpdate(); };
    stretchUpdates_.onUpdate = [this]
    {
        // A render that replaced an earlier one (new pitch edits on a stretched
        // clip) is ready: recompile so the earlier one is let go.
        if (stretch_.takeRecompileRequest())
        {
            engine_.forgetClipSources();
            engine_.setProject(project_);
        }
        notify(Change::values);
    };
    audio_.callback().setEngine(&engine_);
    audio_.midi().setEngine(&engine_);
    audio_.addChangeListener(this);
    matchDeviceSampleRate();
    refreshInputBuses();
    publish();
    startTimer(garbageTimerMilliseconds);
    hostedAutomation_.startTimerHz(50);
}

// Parameter keys for saving and loading: a built-in's from its definition, a
// hosted plugin's from its live instance (none until the engine has made it).
class ProjectSession::SessionKeys final : public core::ParameterKeys
{
public:
    explicit SessionKeys(const ProjectSession& session) : session_(session) {}
    core::ParameterLayout layout(const core::Insert& insert) const override
    {
        if (auto builtIn = plugins::builtInParameterLayout(insert.type); !builtIn.keys.empty())
            return builtIn;
        core::ParameterLayout layout;
        const auto live = session_.liveInsert(insert.id);
        if (live == nullptr || live->typeName() != insert.type)
            return layout;
        for (int i = 0; i < static_cast<int>(live->parameters().size()); ++i)
        {
            layout.keys.push_back(live->parameterKey(i));
            layout.defaults.push_back(live->parameters()[static_cast<size_t>(i)].defaultValue);
        }
        return layout;
    }

private:
    const ProjectSession& session_;
};

void ProjectSession::adoptLoadedParameters()
{
    // Built-ins resolve at once; hosted plugins once the engine has made them.
    const SessionKeys keys(*this);
    core::reconcileParameters(project_, keys);
    publish();
    if (core::reconcileParameters(project_, keys))
        publish();
}

ProjectSession::~ProjectSession()
{
    stopTimer();
    hostedAutomation_.stopTimer();
    closeProject(); // the autosave goes only if the user saved or chose not to (waits for a write)
    autosaveAlive_->store(false);
    if (autosaveLock_ != nullptr)
    {
        const auto lock = autosaveLock_->getFile();
        autosaveLock_.reset();
        lock.deleteFile();
    }
    scanner_.cancel();
    audio_.removeChangeListener(this);
    // Stop new callbacks reaching the engine. This does not wait for one already
    // inside engine->process: that is safe only because the application shuts the
    // device down before it destroys the session (Main.cpp). Any other teardown
    // must stop the device first.
    audio_.callback().setEngine(nullptr);
    audio_.midi().setEngine(nullptr);
}

// ----- editing --------------------------------------------------------------------------

void ProjectSession::notify(Change change)
{
    // structure < values < parameters < progress: keep the widest.
    if (static_cast<int>(change) < static_cast<int>(pending_))
        pending_ = change;
    sendChangeMessage();
}

ProjectSession::Change ProjectSession::takeChange() noexcept
{
    const auto change = pending_;
    pending_ = Change::progress;
    return change;
}

class ProjectSession::Batch
{
public:
    // A name makes the runs one undo step; without one they only share the
    // engine work.
    Batch(ProjectSession& session, const std::string& name) : session_(session), grouped_(!name.empty())
    {
        if (grouped_)
            session_.undo_.beginGroup(name);
        ++session_.batchDepth_;
    }
    ~Batch()
    {
        if (grouped_)
            session_.undo_.endGroup();
        if (--session_.batchDepth_ == 0)
            session_.flushPending();
    }
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

private:
    ProjectSession& session_;
    bool grouped_;
};

bool ProjectSession::run(std::unique_ptr<core::Command> command, bool mayWriteAutomation)
{
    command->setDefaultStrip(plugins::strip::defaultStrip()); // every new track and bus has the strip
    if (mayWriteAutomation)
        if (const auto write = command->automationWrite(); write.has_value() && recordAutomation(*write))
            return true; // written into automation, not into the static value
    if (command->capturesInsertState())
        captureInsertStates();
    std::vector<core::Command::ParameterTouch> touches;
    command->touchedParameters(touches);
    mirrorOwnedParameters(touches);
    const auto* applied = command.get(); // the undo history keeps it
    if (!applyChecked(std::move(command)))
        return false;
    const auto kind = applied->changeKind(); // after apply: some commands only know then
    pendingKind_ = std::max(pendingKind_.value_or(core::Command::ChangeKind::cosmetic), kind);
    pendingTouches_.insert(pendingTouches_.end(), touches.begin(), touches.end());
    pendingViews_ = pendingViews_ || applied->rebuildsViews();
    if (const auto live = applied->liveChannel(); pendingChannel_ == core::invalidId)
        pendingChannel_ = live == core::invalidId ? manyChannels : live;
    else if (live != pendingChannel_)
        pendingChannel_ = manyChannels;
    if (batchDepth_ == 0)
        flushPending();
    return true;
}

void ProjectSession::flushPending()
{
    if (!pendingKind_.has_value())
        return;
    const auto kind = *pendingKind_;
    auto touches = std::move(pendingTouches_);
    const bool views = pendingViews_;
    const auto channel = pendingChannel_ == manyChannels ? core::invalidId : pendingChannel_;
    pendingKind_.reset();
    pendingTouches_.clear();
    pendingViews_ = false;
    pendingChannel_ = core::invalidId;
    finishChange(kind, touches, views, channel);
}

void ProjectSession::finishChange(core::Command::ChangeKind kind,
                                  const std::vector<core::Command::ParameterTouch>& touches,
                                  bool rebuildViews,
                                  core::Id liveChannel)
{
    using Kind = core::Command::ChangeKind;
    edited();
    // A rename or a colour: the views rebuild whatever the engine needs.
    const auto viewChange = [&](Change change) { notify(rebuildViews ? Change::structure : change); };
    switch (kind)
    {
        case Kind::cosmetic:
            viewChange(Change::values); // the views redraw; the engine is untouched
            break;
        case Kind::parameters:
            // A parameter goes to its processor alone, without a recompile.
            engine_.pushInsertParameters(project_, touches);
            viewChange(Change::parameters);
            break;
        case Kind::values:
            // A gain, pan or bypass: the running graph takes the new values
            // (ramped) without a recompile; one channel's when that is all it touched.
            engine_.applyLiveValues(project_, liveChannel);
            engine_.pushInsertParameters(project_, touches);
            viewChange(Change::values);
            break;
        case Kind::structure:
            publish();
            engine_.pushInsertParameters(project_, touches);
            break;
    }
}

void ProjectSession::execute(std::unique_ptr<core::Command> command)
{
    run(std::move(command));
}

void ProjectSession::beginGesture(const std::string& name)
{
    if (gestureOpen_)
        undo_.endGroup();
    undo_.beginGroup(name);
    gestureOpen_ = true;
}

void ProjectSession::endGesture()
{
    if (!gestureOpen_)
        return;
    undo_.endGroup();
    gestureOpen_ = false;
    finishPasses(true); // touch passes end with the gesture
}

void ProjectSession::executeGroup(const std::string& name,
                                  std::vector<std::unique_ptr<core::Command>> commands)
{
    if (commands.empty())
        return;
    const Batch batch(*this, name);
    for (auto& command : commands)
        if (!run(std::move(command)))
            break; // the rest of the group was built on what this one would have done
}

std::string ProjectSession::normaliseClip(core::Id trackId, core::Id clipId, float targetDb)
{
    const auto* track = project_.findTrack(trackId);
    const auto* clip = track != nullptr ? track->findClip(clipId) : nullptr;
    if (clip == nullptr)
        return "no such clip";
    std::string error;
    const auto source = loader_.load(clip->mediaPath, project_.sampleRate, error);
    if (source == nullptr)
        return error.empty() ? "could not read " + clip->mediaPath : error;
    float peak = 0.0f;
    std::vector<float> chunk(8192);
    const auto end =
        std::min<std::int64_t>(clip->sourceOffset + clip->sourceLength(), source->lengthSamples());
    for (int ch = 0; ch < source->numChannels(); ++ch)
        for (std::int64_t at = clip->sourceOffset; at < end; at += static_cast<std::int64_t>(chunk.size()))
        {
            const auto n =
                static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(chunk.size()), end - at));
            source->read(ch, at, chunk.data(), n);
            for (int i = 0; i < n; ++i)
                peak = std::max(peak, std::abs(chunk[static_cast<size_t>(i)]));
        }
    if (peak <= 0.0f)
        return "the clip is silent";
    const float target = ui::db::toGain(targetDb);
    execute(std::make_unique<core::SetClipGain>(trackId, clipId, target / peak));
    return {};
}

engine::TempoGuess ProjectSession::guessClipTempo(core::Id trackId, core::Id clipId)
{
    const auto* track = project_.findTrack(trackId);
    const auto* clip = track != nullptr ? track->findClip(clipId) : nullptr;
    if (clip == nullptr)
        return {};
    std::string error;
    const auto source = stretchLoader_.load(clip->mediaPath, project_.sampleRate, error);
    if (source == nullptr)
        return {};
    // The part the clip plays (at most a minute of it is enough to hear the beat).
    const auto from = std::clamp<std::int64_t>(clip->sourceOffset, 0, source->lengthSamples());
    const auto count = std::clamp<std::int64_t>(clip->sourceLength(), 0, source->lengthSamples() - from);
    const auto analysed =
        std::min<std::int64_t>(count, static_cast<std::int64_t>(project_.sampleRate * 60.0));
    std::vector<std::vector<float>> samples(static_cast<size_t>(source->numChannels()),
                                            std::vector<float>(static_cast<size_t>(analysed)));
    std::vector<const float*> channels;
    for (int ch = 0; ch < source->numChannels(); ++ch)
    {
        source->read(ch, from, samples[static_cast<size_t>(ch)].data(), static_cast<int>(analysed));
        channels.push_back(samples[static_cast<size_t>(ch)].data());
    }
    const auto name = juce::File(juce::String::fromUTF8(clip->mediaPath.c_str())).getFileName().toStdString();
    auto guess = engine::guessTempo(name, channels, analysed, project_.sampleRate);
    // The length check wants the whole clip, not the analysed minute.
    if (guess.source == engine::TempoGuess::Source::beats && analysed == count)
        if (const double byLength = engine::tempoFromLength(count, project_.sampleRate, 4.0, guess.bpm);
            byLength > 0.0 && std::abs(byLength / guess.bpm - 1.0) < 0.03)
            guess = {byLength, std::max(guess.confidence, 0.8), engine::TempoGuess::Source::length};
    return guess;
}

void ProjectSession::conformClipTempo(core::Id trackId, core::Id clipId, double sourceBpm)
{
    const auto* track = project_.findTrack(trackId);
    const auto* clip = track != nullptr ? track->findClip(clipId) : nullptr;
    if (clip == nullptr || sourceBpm <= 0.0)
        return;
    // The clip's beats at the tempo it was recorded at, laid over the tempo map
    // from where it starts.
    const auto map = core::tempoMapOf(project_);
    const double seconds = static_cast<double>(clip->sourceLength()) / project_.sampleRate;
    const double quarters = seconds * sourceBpm / 60.0;
    const auto end = map.positionAt(map.quartersAt(static_cast<double>(clip->start)) + quarters);
    const auto length = std::max<core::SamplePosition>(1, end - clip->start);
    const double stretch = static_cast<double>(length) /
                           static_cast<double>(std::max<core::SamplePosition>(1, clip->sourceLength()));
    execute(std::make_unique<core::SetClipStretch>(trackId, clipId, stretch, clip->transpose, length, true));
}

bool ProjectSession::applyChecked(std::unique_ptr<core::Command> command)
{
    // A command can arrive stale: a menu opened on a track that an undo has
    // since removed. Commands check what they reference and fail without
    // changing anything; the command is dropped.
    const auto name = command->name();
    // Arming, monitoring and inputs stay out of the history (Command::undoable):
    // applied and kept, the project modified, nothing to undo.
    if (!command->undoable())
    {
        if (command->apply(project_))
        {
            changedOutsideHistory_ = true;
            lastOutsideHistory_ = std::move(command); // run() reads it after this
            return true;
        }
    }
    else if (undo_.execute(project_, std::move(command)))
        return true;
    juce::Logger::writeToLog("Dropped a stale edit: " + juce::String::fromUTF8(name.c_str()));
    return false;
}

void ProjectSession::undo()
{
    if (!undo_.canUndo())
        return;
    if (!undo_.undo(project_))
    {
        juce::Logger::writeToLog("Could not undo: " + juce::String::fromUTF8(undo_.undoName().c_str()));
        return;
    }
    // The engine work the undone command's kind needs, with the parameters it
    // restored (they reach plugins that own theirs).
    const auto* undone = undo_.redoTop();
    std::vector<core::Command::ParameterTouch> touches;
    undone->touchedParameters(touches);
    pushInsertState(*undone);
    finishChange(undone->changeKind(), touches, undone->rebuildsViews(), undone->liveChannel());
}

void ProjectSession::pushInsertState(const core::Command& command)
{
    const auto id = command.pushesInsertState();
    if (id == core::invalidId)
        return;
    const auto live = engine_.insertInstance(id);
    if (live == nullptr || live->crashed() || !live->ownsParameters())
        return;
    if (const auto* insert = project_.master.findInsert(id))
        live->setState(insert->state);
    for (const auto& t : project_.tracks)
        if (const auto* insert = t.findInsert(id))
            live->setState(insert->state);
    for (const auto& b : project_.buses)
        if (const auto* insert = b.findInsert(id))
            live->setState(insert->state);
}

void ProjectSession::redo()
{
    if (!undo_.canRedo())
        return;
    if (!undo_.redo(project_))
    {
        juce::Logger::writeToLog("Could not redo: " + juce::String::fromUTF8(undo_.redoName().c_str()));
        return;
    }
    const auto* redone = undo_.undoTop();
    std::vector<core::Command::ParameterTouch> touches;
    redone->touchedParameters(touches);
    pushInsertState(*redone);
    finishChange(redone->changeKind(), touches, redone->rebuildsViews(), redone->liveChannel());
}

void ProjectSession::mirrorOwnedParameters(const std::vector<core::Command::ParameterTouch>& touches)
{
    // A processor that owns its parameters (a hosted plugin) is the truth for
    // their values; before a command sets one, the model's list is brought up to
    // date from the instance so the command records the real previous value and
    // undo restores it.
    for (const auto& touch : touches)
    {
        const auto instance = engine_.insertInstance(touch.insertId);
        if (instance == nullptr || !instance->ownsParameters())
            continue;
        core::Insert* insert = nullptr;
        for (auto& t : project_.tracks)
            if (auto* i = t.findInsert(touch.insertId))
                insert = i;
        for (auto& b : project_.buses)
            if (auto* i = b.findInsert(touch.insertId))
                insert = i;
        if (auto* i = project_.master.findInsert(touch.insertId))
            insert = i;
        if (insert == nullptr)
            continue;
        // Up to the touched parameter (a plugin can have thousands; the list
        // only needs to reach what commands record and restore).
        const auto count =
            std::min(instance->parameters().size(), static_cast<size_t>(std::max(0, touch.parameter)) + 1);
        if (insert->params.size() < count)
            insert->params.resize(count, 0.0f);
        for (size_t k = 0; k < count; ++k)
            insert->params[k] = instance->parameter(static_cast<int>(k));
    }
}

core::Track ProjectSession::makeInstrumentChannel(const std::string& type) const
{
    // Named for the instrument ("Pulsar"); a type with no name yet reads "Instrument".
    core::Track channel;
    channel.kind = core::TrackKind::instrument;
    channel.name = inserts_.displayName(type);
    if (channel.name.empty() || channel.name == type)
        channel.name = "Instrument";
    channel.instrument.type = type;
    channel.instrument.params = plugins::defaultParameters(type);
    return channel;
}

std::string ProjectSession::nextMidiTrackName() const
{
    int count = 0;
    for (const auto& t : project_.tracks)
        count += t.isMidi() ? 1 : 0;
    return "MIDI " + std::to_string(count + 1);
}

core::Id ProjectSession::addMidiTrack(const std::string& instrumentType, core::TrackPlace place)
{
    // A MIDI track with a new instrument: the instrument channel and the MIDI
    // track aimed at it, as one step.
    const auto type = instrumentType.empty() ? plugins::aurora::type : instrumentType;
    core::Track midi;
    midi.name = nextMidiTrackName();
    midi.armed = true;
    midi.folder = place.folder;
    auto channel = makeInstrumentChannel(type);
    channel.folder = place.folder;
    auto command = std::make_unique<core::AddInstrumentTrack>(std::move(channel), midi, place.index);
    const auto* raw = command.get(); // kept alive by the undo history
    if (!run(std::move(command)))
        return core::invalidId;
    return raw->midiTrackId();
}

core::Id ProjectSession::addMidiTrackPlaying(core::Id channel, core::TrackPlace place)
{
    core::Track midi;
    midi.name = nextMidiTrackName();
    midi.kind = core::TrackKind::midi;
    midi.armed = true;
    int index = -1;
    if (const auto* target = project_.findTrack(channel); target != nullptr && target->isInstrument())
    {
        midi.midiTarget = channel;
        index = project_.trackIndex(channel);
    }
    if (place.index >= 0)
    {
        index = place.index;
        midi.folder = place.folder;
    }
    auto command = std::make_unique<core::AddTrack>(midi, index);
    const auto* raw = command.get();
    if (!run(std::move(command)))
        return core::invalidId;
    return raw->trackId();
}

ProjectSession::PluginInsertion ProjectSession::insertPlugin(core::Id channelId, const std::string& type)
{
    PluginInsertion result;
    if (isInstrumentType(type))
    {
        const auto* track = project_.findTrack(channelId);
        const auto* channel = track != nullptr && track->isInstrument() ? track
                              : track != nullptr && track->isMidi() ? project_.findTrack(track->midiTarget)
                                                                    : nullptr;
        if (channel != nullptr && channel->isInstrument())
        {
            core::Insert instrument;
            instrument.type = type;
            instrument.params = plugins::defaultParameters(type);
            const auto id = channel->id;
            if (run(std::make_unique<core::SetInstrument>(id, instrument)))
                result = {PluginPlacement::instrumentReplaced, id};
        }
        else if (track != nullptr && track->isMidi())
        {
            const auto midi = track->id; // adding the channel may move the track list
            if (const auto made = playNewInstrument(midi, type); made != core::invalidId)
                result = {PluginPlacement::newInstrument, made, midi};
        }
        else if (const auto midi = addMidiTrack(type); midi != core::invalidId)
            result = {PluginPlacement::newInstrument, project_.findTrack(midi)->midiTarget, midi};
        return result;
    }
    if (project_.findChannel(channelId) == nullptr)
        return result;
    core::Insert insert;
    insert.type = type;
    insert.params = plugins::defaultParameters(type);
    if (run(std::make_unique<core::AddInsert>(channelId, insert)))
        result = {PluginPlacement::effectAdded, channelId};
    return result;
}

core::Id ProjectSession::playNewInstrument(core::Id midiTrack, const std::string& type)
{
    const auto* midi = project_.findTrack(midiTrack);
    if (midi == nullptr || !midi->isMidi())
        return core::invalidId;
    const int midiChannel = midi->midiChannel;
    const Batch batch(*this, "Play a new instrument");
    auto add =
        std::make_unique<core::AddTrack>(makeInstrumentChannel(type), project_.trackIndex(midiTrack) + 1);
    const auto* raw = add.get();
    if (!run(std::move(add)))
        return core::invalidId;
    const auto channel = raw->trackId(); // allocated on apply
    run(std::make_unique<core::SetMidiTarget>(midiTrack, channel, midiChannel));
    return channel;
}

core::Id ProjectSession::addSamplerTrack(const juce::File& file, double startFraction, double endFraction)
{
    namespace sp = plugins::sampler;
    auto channel = makeInstrumentChannel(sp::type);
    if (const auto name = file.getFileNameWithoutExtension().toStdString(); !name.empty())
        channel.name = name;
    channel.instrument.state = sp::encodeState(file.getFullPathName().toStdString());
    if (channel.instrument.params.size() > static_cast<size_t>(sp::end))
    {
        channel.instrument.params[sp::start] = static_cast<float>(std::clamp(startFraction, 0.0, 1.0));
        channel.instrument.params[sp::end] = static_cast<float>(std::clamp(endFraction, 0.0, 1.0));
    }
    core::Track midi;
    midi.name = nextMidiTrackName();
    midi.armed = true;
    auto command = std::make_unique<core::AddInstrumentTrack>(channel, midi);
    const auto* raw = command.get(); // kept alive by the undo history
    if (!run(std::move(command)))
        return core::invalidId;
    return raw->midiTrackId();
}

std::string ProjectSession::previewFile(const juce::File& file)
{
    const double rate = engine_.deviceSampleRate() > 0.0 ? engine_.deviceSampleRate() : project_.sampleRate;
    std::string error;
    auto source = loader_.load(file.getFullPathName().toStdString(), rate, error);
    if (source == nullptr)
        return error.empty() ? "could not read the file" : error;
    if (auto instance = source->createInstance()) // a long file streams through its own window
        source = std::move(instance);
    engine_.preview().play(std::move(source));
    pumpPreview();
    return {};
}

void ProjectSession::stopPreview()
{
    engine_.preview().stop();
    pumpPreview();
}

juce::File ProjectSession::takeInMedia(const juce::File& file)
{
    if (!copyImportedMedia_ || projectFile_ == juce::File() || !file.existsAsFile())
        return file;
    const auto folder = mediaFolder();
    if (file.isAChildOf(folder) || !folder.createDirectory().wasOk())
        return file;
    auto target = folder.getChildFile(file.getFileName());
    if (target.exists())
    {
        // The same file imported twice uses the copy it already has.
        if (target.getSize() == file.getSize() && target.hasIdenticalContentTo(file))
            return target;
        target = folder.getNonexistentChildFile(
            file.getFileNameWithoutExtension(), file.getFileExtension(), false);
    }
    return file.copyFileTo(target) ? target : file;
}

std::string ProjectSession::importAudioFile(const juce::File& file, core::SamplePosition at, core::Id target)
{
    std::string error;
    const auto info = loader_.probe(file.getFullPathName().toStdString(), error);
    if (!info.has_value())
        return error;
    const auto media = takeInMedia(file); // a copy in the project folder, when media is copied

    core::Track track;
    track.name = file.getFileNameWithoutExtension().toStdString();
    core::AudioClip clip;
    clip.name = track.name;
    clip.mediaPath = media.getFullPathName().toStdString();
    clip.start = std::max<core::SamplePosition>(0, at);
    clip.length = info->lengthAt(project_.sampleRate);
    // On the audio track it was dropped on, or a track of its own.
    if (const auto* onto = project_.findTrack(target);
        onto != nullptr && onto->kind == core::TrackKind::audio)
        execute(std::make_unique<core::AddClip>(target, clip));
    else
    {
        track.clips.push_back(clip);
        execute(std::make_unique<core::AddTrack>(track));
    }

    const auto& errors = engine_.mediaErrors();
    if (const auto it = errors.find(clip.mediaPath); it != errors.end())
        return it->second;
    return {};
}

std::string ProjectSession::importMidiFile(const juce::File& file, core::SamplePosition at, core::Id target)
{
    // The file's times become samples at the project rate through its own tempo
    // map, so they do not depend on the project's tempo; only the bar the import
    // lands on and the clips' rounding do.
    auto imported = midifile::readMidiFile(file, project_.sampleRate);
    if (!imported.error.empty())
        return imported.error;
    const Batch batch(*this, "Import MIDI file");
    if (project_.tracks.empty())
    {
        // An empty project takes the file's tempo changes and time signature.
        auto state = midifile::tempoStateOf(imported.tempo, core::TempoState::of(project_));
        if (imported.hasTimeSignature)
            state.signature = imported.timeSignature;
        if (!(state == core::TempoState::of(project_)))
            run(std::make_unique<core::SetTempoMap>(state, "Take the file's tempo"));
    }
    // The clips start at the bar the import lands in and run whole bars.
    const auto map = core::tempoMapOf(project_);
    const auto barOf = [&](core::SamplePosition s)
    { return map.barAtQuarter(map.quartersAt(static_cast<double>(std::max<core::SamplePosition>(0, s)))); };
    const int firstBar = barOf(at);
    const auto start = map.barStart(firstBar);

    // The channel the parts play: the target, or the one a MIDI target plays.
    core::Id channel = core::invalidId;
    if (const auto* t = project_.findTrack(target))
        channel = t->isInstrument() ? t->id
                  : t->isMidi()
                      ? (project_.findTrack(t->midiTarget) != nullptr ? t->midiTarget : core::invalidId)
                      : core::invalidId;
    const bool targeted = channel != core::invalidId;
    core::Id drums = core::invalidId; // a new Pulsar for channel 10, when there is no target
    for (auto& part : imported.parts)
    {
        // Whole bars, at least one. (The file's times are seconds at its own
        // tempo; laid at `start` they keep that timing.)
        const auto end = start + std::max<core::SamplePosition>(1, part.clip.length);
        part.clip.length = map.barStart(std::max(firstBar + 1, barOf(end - 1) + 1)) - start;
        part.clip.start = start;
        core::Track midi;
        midi.name = part.name;
        midi.midiChannel = part.channel;
        core::Id midiTrack = core::invalidId;
        const bool isDrums = part.channel == 10 && !targeted;
        auto& playing = isDrums ? drums : channel;
        if (playing == core::invalidId)
        {
            auto command = std::make_unique<core::AddInstrumentTrack>(
                makeInstrumentChannel(isDrums ? plugins::pulsar::type : plugins::aurora::type), midi);
            const auto* raw = command.get(); // kept alive by the undo history
            if (!run(std::move(command)))
                return "could not add a track for " + part.name;
            midiTrack = raw->midiTrackId();
            if (const auto* made = project_.findTrack(midiTrack))
                playing = made->midiTarget;
        }
        else
        {
            midi.kind = core::TrackKind::midi;
            midi.midiTarget = playing;
            auto command = std::make_unique<core::AddTrack>(midi, project_.trackIndex(playing));
            const auto* raw = command.get();
            if (!run(std::move(command)))
                return "could not add a track for " + part.name;
            midiTrack = raw->trackId();
        }
        run(std::make_unique<core::AddMidiClip>(midiTrack, std::move(part.clip)));
    }
    return {};
}

std::string ProjectSession::exportMidiFile(const juce::File& file,
                                           const std::vector<std::pair<core::Id, core::Id>>& clips)
{
    std::vector<std::pair<const core::Track*, std::vector<const core::MidiClip*>>> tracks;
    for (const auto& [trackId, clipId] : clips)
    {
        const auto* track = project_.findTrack(trackId);
        const auto* clip = track != nullptr ? track->findMidiClip(clipId) : nullptr;
        if (clip == nullptr)
            continue;
        auto it =
            std::find_if(tracks.begin(), tracks.end(), [track](const auto& t) { return t.first == track; });
        if (it == tracks.end())
        {
            tracks.push_back({track, {}});
            it = std::prev(tracks.end());
        }
        it->second.push_back(clip);
    }
    if (tracks.empty())
        return "no MIDI clips to export";
    // In track order, as the arrange view shows them.
    std::sort(tracks.begin(),
              tracks.end(),
              [this](const auto& a, const auto& b)
              { return project_.trackIndex(a.first->id) < project_.trackIndex(b.first->id); });
    return midifile::writeMidiFile(file, midifile::toMidiFile(project_, tracks));
}

// ----- files ------------------------------------------------------------------------------

void ProjectSession::newProject()
{
    closeProject();
    hostedSent_.clear();
    untitledStem_.clear();
    reportedCrashes_.clear();
    reportedFailures_.clear();
    engine_.resetCaches(); // another project's ids must not reuse these instances
    project_ = core::Project{};
    undo_.clear();
    projectFile_ = juce::File();
    changedOutsideHistory_ = false;
    matchDeviceSampleRate();
    publish();
}

std::string ProjectSession::open(const juce::File& file)
{
    if (!beginOpen(file))
        return "another project is still loading";
    return finishLoadNow();
}

// ----- loading in steps ----------------------------------------------------------------------

namespace
{
double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}
} // namespace

bool ProjectSession::beginLoad(LoadKind kind, const juce::File& file, std::string readError)
{
    if (load_ != nullptr)
        return false;
    loadKind_ = kind;
    loadCommitted_ = false;
    loadEngineReported_ = false;
    loadInserts_.clear();
    loadNextInsert_ = 0;
    loadHostedDone_ = 0;
    loadUnitsDone_ = 0.0;
    loadMade_.clear();
    load_ = std::make_unique<ProjectLoad>(file, loader_, std::move(readError));
    return true;
}

bool ProjectSession::beginOpen(const juce::File& file)
{
    return beginLoad(LoadKind::open, file, "could not read project");
}

bool ProjectSession::beginTemplate(const juce::File& file)
{
    return beginLoad(LoadKind::fromTemplate, file, "could not read the template");
}

bool ProjectSession::beginRestore(const Autosave& autosave)
{
    if (!beginLoad(LoadKind::restore, autosave.file, "could not read the autosave"))
        return false;
    loadAutosave_ = autosave;
    return true;
}

void ProjectSession::cancelLoad()
{
    if (canCancelLoad())
    {
        load_.reset();             // stops between files and waits for the thread
        changesDiscarded_ = false; // the current project stays, and so does its question
    }
}

std::string ProjectSession::finishLoadNow()
{
    if (load_ != nullptr)
        load_->wait();
    for (;;)
        if (auto outcome = stepLoad(1.0e9))
            return *outcome;
}

void ProjectSession::commitLoad()
{
    closeProject();
    hostedSent_.clear();
    untitledStem_.clear();
    reportedCrashes_.clear(); // the new project's ids are its own
    reportedFailures_.clear();
    engine_.resetCaches(); // another project's ids must not reuse these instances
    project_ = std::move(*load_->project());
    undo_.clear();
    switch (loadKind_)
    {
        case LoadKind::open:
            projectFile_ = load_->file();
            changedOutsideHistory_ = false;
            break;
        case LoadKind::fromTemplate:
            project_.name = "Untitled";
            projectFile_ = juce::File();
            changedOutsideHistory_ = false;
            matchDeviceSampleRate();
            break;
        case LoadKind::restore:
            projectFile_ = loadAutosave_.project;
            changedOutsideHistory_ = true; // not saved until the user saves it
            // The leftover becomes this session's own copy: it stays on disk until
            // the user saves or discards the changes (the next autosave replaces it).
            autosaveWritten_ = loadAutosave_.file;
            edited();
            break;
    }
    for (const auto& [path, source] : load_->media())
        engine_.adoptMedia(path, source);
    // The processors to make, in the order the compile would make them.
    const auto collect = [this](const core::Channel& channel)
    {
        for (const auto* insert : channel.processingChain())
            loadInserts_.push_back(*insert);
    };
    for (const auto& track : project_.tracks)
        collect(track);
    for (const auto& bus : project_.buses)
        collect(bus);
    collect(project_.master);
    loadCommitted_ = true;
    loadPluginsStartMs_ = nowMs();
    notify(Change::structure);
}

std::optional<std::string> ProjectSession::stepLoad(double budgetMs)
{
    if (load_ == nullptr)
        return std::string{};
    if (!loadCommitted_)
    {
        if (!load_->ready())
            return std::nullopt;
        if (!load_->project().has_value())
        {
            auto error = load_->error();
            load_.reset();
            changesDiscarded_ = false; // the current project stays, and so does its question
            return error;
        }
    }
    const int hostedTotal = load_->timings().hostedPlugins;
    const auto reportPlugins = [&]
    {
        // The next third-party plugin, even with built-ins before it: the line
        // names what the wait is for.
        std::string next;
        for (size_t i = loadNextInsert_; i < loadInserts_.size() && next.empty(); ++i)
            if (hosting::PluginRegistry::isHostedType(loadInserts_[i].type))
                next = inserts_.displayName(loadInserts_[i].type);
        load_->report(LoadProgress::Stage::plugins, loadHostedDone_, hostedTotal, next, loadUnitsDone_);
    };
    if (!loadCommitted_)
    {
        // The new project replaces the current one, a step of its own: the window
        // shows it, and the progress names the first plugin, before any is made.
        commitLoad();
        reportPlugins();
        return std::nullopt;
    }
    if (loadNextInsert_ < loadInserts_.size())
    {
        // Built-ins until the budget runs out; a hosted plugin only as the first
        // of a step, so the progress naming it has been painted while it loads.
        const double start = nowMs();
        int made = 0;
        while (loadNextInsert_ < loadInserts_.size())
        {
            const auto& insert = loadInserts_[loadNextInsert_];
            const bool hosted = hosting::PluginRegistry::isHostedType(insert.type);
            if (hosted && made > 0)
                break;
            if (auto instance = engine_.prepareInsert(insert))
                loadMade_.push_back(std::move(instance));
            ++loadNextInsert_;
            ++made;
            loadUnitsDone_ += hosted ? ProjectLoad::hostedUnits : ProjectLoad::builtInUnits;
            if (hosted)
            {
                ++loadHostedDone_;
                break;
            }
            if (nowMs() - start >= budgetMs)
                break;
        }
        reportPlugins();
        return std::nullopt;
    }
    if (!loadEngineReported_)
    {
        // One more step, so "Preparing playback" shows while the snapshot compiles.
        loadEngineReported_ = true;
        load_->timings().pluginsMs = nowMs() - loadPluginsStartMs_;
        load_->report(LoadProgress::Stage::engine, 0, 0, {}, 0.0);
        return std::nullopt;
    }
    const double engineStart = nowMs();
    adoptLoadedParameters();
    reportPluginTrouble();
    load_->timings().engineMs = nowMs() - engineStart;
    load_->report(LoadProgress::Stage::done, 0, 0, {}, 0.0);
    lastLoadTimings_ = load_->timings();
    loadMade_.clear(); // the snapshot holds them now
    loadInserts_.clear();
    load_.reset();
    return std::string{};
}

juce::File ProjectSession::backupFileFor(const juce::File& project)
{
    return project.withFileExtension("northlight-bak");
}

std::string ProjectSession::save(const juce::File& file, bool bringMedia)
{
    // Media first: the file written must point at where the media will be.
    const auto media = file.getSiblingFile(file.getFileNameWithoutExtension() + " Media");
    if (auto error = this->bringMedia(media, bringMedia); !error.empty())
        return error;
    captureInsertStates();
    const SessionKeys keys(*this);
    const auto text = core::projectToJson(project_, true, &keys);
    // The version being replaced is kept beside it, so a save over a good file
    // with a bad edit can still be taken back from Explorer.
    if (keepBackup_ && file.existsAsFile())
    {
        const auto backup = backupFileFor(file);
        if (!file.copyFileTo(backup))
            return "could not keep a backup at " + backup.getFullPathName().toStdString();
    }
    // replaceWithText writes a temporary file beside the target and then moves it
    // over the target, so a failed write leaves the previous file as it was.
    if (!file.replaceWithText(juce::String(text)))
        return "could not write " + file.getFullPathName().toStdString();
    projectFile_ = file;
    undo_.markSaved();
    changedOutsideHistory_ = false;
    changesDiscarded_ = false;
    removeOwnAutosave();
    notify(Change::structure);
    return {};
}

std::vector<juce::File> ProjectSession::filesToBring(const juce::File& folder, bool everything) const
{
    // An unsaved project's recordings are in the shared folder: they always come
    // along (nothing else would ever keep them); other media when asked.
    const bool untitled = projectFile_ == juce::File();
    const auto shared = sharedRecordingsFolder();
    std::vector<juce::File> files;
    const auto consider = [&](const std::string& path)
    {
        if (path.empty())
            return;
        const juce::File f(juce::String::fromUTF8(path.c_str()));
        if (f.isAChildOf(folder) || std::find(files.begin(), files.end(), f) != files.end() ||
            !f.existsAsFile())
            return;
        if (everything || (untitled && f.isAChildOf(shared)))
            files.push_back(f);
    };
    for (const auto& t : project_.tracks)
    {
        for (const auto& c : t.clips)
            consider(c.mediaPath);
        for (const auto& g : t.takeGroups)
            for (const auto& take : g.takes)
                consider(take.clip.mediaPath);
        consider(t.frozenMedia);
    }
    return files;
}

std::pair<int, juce::int64> ProjectSession::mediaToBring(const juce::File& file) const
{
    const auto files = filesToBring(file.getSiblingFile(file.getFileNameWithoutExtension() + " Media"), true);
    juce::int64 bytes = 0;
    for (const auto& f : files)
        bytes += f.getSize();
    return {static_cast<int>(files.size()), bytes};
}

std::string ProjectSession::bringMedia(const juce::File& folder, bool everything)
{
    const auto files = filesToBring(folder, everything);
    if (files.empty())
        return {};
    std::vector<std::pair<std::string, std::string>> renames;
    std::vector<juce::File> copies;
    for (const auto& source : files)
    {
        // A frozen render keeps its Freeze subfolder.
        const auto into =
            source.getParentDirectory().getFileName() == "Freeze" ? folder.getChildFile("Freeze") : folder;
        if (!into.createDirectory().wasOk())
            return "could not create " + into.getFullPathName().toStdString();
        auto target = into.getChildFile(source.getFileName());
        if (target.exists())
            target = into.getNonexistentChildFile(
                source.getFileNameWithoutExtension(), source.getFileExtension(), false);
        if (!source.copyFileTo(target))
        {
            for (const auto& copy : copies)
                copy.deleteFile(); // all or nothing: the save fails as a whole
            return "could not copy " + source.getFullPathName().toStdString() + " into the project folder";
        }
        copies.push_back(target);
        renames.emplace_back(source.getFullPathName().toStdString(), target.getFullPathName().toStdString());
    }
    execute(std::make_unique<core::SetMediaPaths>(std::move(renames), "Bring media into the project folder"));
    return {};
}

// ----- templates ---------------------------------------------------------------------------

juce::File ProjectSession::templatesFolder() const
{
    if (templatesFolder_ != juce::File())
        return templatesFolder_;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Northlight")
        .getChildFile("Templates");
}

std::vector<juce::File> ProjectSession::templates() const
{
    std::vector<juce::File> found;
    for (const auto& f : templatesFolder().findChildFiles(juce::File::findFiles, false, "*.northlight"))
        found.push_back(f);
    std::sort(found.begin(),
              found.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    return found;
}

std::string ProjectSession::saveAsTemplate(const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty())
        return "a template needs a name";
    captureInsertStates();
    auto layout = project_;
    layout.name = clean.toStdString();
    for (auto& t : layout.tracks)
    {
        t.clips.clear();
        t.midiClips.clear();
        t.takeGroups.clear();
        t.armed = false;
        for (auto& lane : t.automation)
            lane.points.clear();
        std::erase_if(t.automation, [](const core::AutomationLane& l) { return l.points.empty(); });
    }
    for (auto& b : layout.buses)
        b.automation.clear();
    layout.markers.clear();
    const auto folder = templatesFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    const auto file = folder.getChildFile(clean + ".northlight");
    const SessionKeys keys(*this);
    if (!file.replaceWithText(juce::String(core::projectToJson(layout, true, &keys))))
        return "could not write " + file.getFullPathName().toStdString();
    return {};
}

std::string ProjectSession::newFromTemplate(const juce::File& file)
{
    if (!beginTemplate(file))
        return "another project is still loading";
    return finishLoadNow();
}

// ----- media management ------------------------------------------------------------------

namespace
{
bool isMediaFile(const juce::File& f)
{
    if (f.getFileName().endsWithIgnoreCase(".partial.wav") ||
        f.getFileName().endsWithIgnoreCase(".discarded.wav"))
        return false; // unfinished recordings belong to recovery
    return f.hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
}
} // namespace

ProjectSession::MediaReport ProjectSession::mediaReport() const
{
    MediaReport report;
    const auto folder = mediaFolder();
    std::vector<juce::File> used;
    // Every clip's media and every take's (a take the comp does not play is still kept).
    std::vector<const core::AudioClip*> clips;
    for (const auto& t : project_.tracks)
    {
        for (const auto& c : t.clips)
            clips.push_back(&c);
        for (const auto& g : t.takeGroups)
            for (const auto& take : g.takes)
                clips.push_back(&take.clip);
    }
    std::vector<core::AudioClip> frozen; // a frozen track's render, as a clip for the check below
    for (const auto& t : project_.tracks)
        if (t.isFrozen())
        {
            core::AudioClip c;
            c.mediaPath = t.frozenMedia;
            frozen.push_back(c);
        }
    for (const auto& c : frozen)
        clips.push_back(&c);
    for (const auto* clip : clips)
    {
        const auto& c = *clip;
        const juce::File f(juce::String::fromUTF8(c.mediaPath.c_str()));
        if (std::find(used.begin(), used.end(), f) != used.end())
            continue;
        used.push_back(f);
        if (!f.existsAsFile())
            report.missing.push_back(f);
        else if (projectFile_ == juce::File() || !f.isAChildOf(folder))
            report.outside.push_back(f);
    }
    if (projectFile_ != juce::File() && folder.isDirectory())
    {
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, false))
            if (isMediaFile(f) && std::find(used.begin(), used.end(), f) == used.end())
                report.unused.push_back(f);
    }
    else
    {
        // The shared folder holds other projects' recordings too: only this
        // session's own are this project's to remove.
        for (const auto& path : ownSharedRecordings_)
            if (const juce::File f(path);
                f.existsAsFile() && std::find(used.begin(), used.end(), f) == used.end())
                report.unused.push_back(f);
    }
    return report;
}

juce::File ProjectSession::sharedRecordingsFolder() const
{
    if (sharedRecordings_ != juce::File())
        return sharedRecordings_;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Northlight")
        .getChildFile("Recordings");
}

int ProjectSession::missingFilesIn(const juce::File& folder, const juce::File& except) const
{
    int found = 0;
    for (const auto& f : mediaReport().missing)
        if (f != except && folder.getChildFile(f.getFileName()).existsAsFile())
            ++found;
    return found;
}

int ProjectSession::relinkMedia(const juce::File& missing, const juce::File& found, bool searchFolder)
{
    if (!found.existsAsFile())
        return 0;
    std::vector<std::pair<std::string, std::string>> renames{
        {missing.getFullPathName().toStdString(), found.getFullPathName().toStdString()}};
    if (searchFolder)
        for (const auto& f : mediaReport().missing)
            if (const auto candidate = found.getSiblingFile(f.getFileName());
                f != missing && candidate.existsAsFile())
                renames.emplace_back(f.getFullPathName().toStdString(),
                                     candidate.getFullPathName().toStdString());
    const int count = static_cast<int>(renames.size());
    execute(std::make_unique<core::SetMediaPaths>(std::move(renames), "Relink media"));
    return count;
}

std::string ProjectSession::consolidateMedia(int& copied)
{
    copied = 0;
    if (projectFile_ == juce::File())
        return "save the project first: media is gathered next to the project file";
    const auto folder = mediaFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    std::vector<std::pair<std::string, std::string>> renames;
    for (const auto& source : mediaReport().outside)
    {
        auto target = folder.getChildFile(source.getFileName());
        if (target.existsAsFile())
            target = folder.getNonexistentChildFile(
                source.getFileNameWithoutExtension(), source.getFileExtension(), false);
        if (!source.copyFileTo(target))
            return "could not copy " + source.getFullPathName().toStdString();
        renames.emplace_back(source.getFullPathName().toStdString(), target.getFullPathName().toStdString());
        ++copied;
    }
    if (!renames.empty())
        execute(std::make_unique<core::SetMediaPaths>(std::move(renames)));
    return {};
}

bool ProjectSession::trashMedia(const juce::File& f)
{
    if (!f.moveToTrash())
        return false;
    // Its peak cache goes with it.
    for (const auto& sidecar :
         f.getParentDirectory().findChildFiles(juce::File::findFiles, false, f.getFileName() + ".*"))
        sidecar.moveToTrash();
    return true;
}

int ProjectSession::removeUnusedMedia()
{
    int moved = 0;
    for (const auto& f : mediaReport().unused)
        if (trashMedia(f))
        {
            ++moved;
            ownSharedRecordings_.erase(f.getFullPathName());
        }
    return moved;
}

// ----- autosave ---------------------------------------------------------------------------

namespace
{
juce::File sidecarOf(const juce::File& autosave)
{
    return autosave.withFileExtension("json");
}
} // namespace

void ProjectSession::setAutosave(juce::File folder, std::function<int()> intervalSeconds)
{
    autosaveFolder_ = std::move(folder);
    autosaveInterval_ = std::move(intervalSeconds);
    holdAutosaveLock();
}

void ProjectSession::holdAutosaveLock()
{
    if (autosaveLock_ != nullptr)
    {
        const auto old = autosaveLock_->getFile();
        autosaveLock_.reset();
        old.deleteFile();
    }
    if (autosaveFolder_ == juce::File() || !autosaveFolder_.createDirectory().wasOk())
        return;
    // Open for as long as the session lives: Windows refuses to delete a file
    // another process holds open, so the lock's presence after a delete attempt
    // says its owner still runs, and a crash (which closes every handle) frees it.
    auto lock = std::make_unique<juce::FileOutputStream>(autosaveFolder_.getChildFile(instanceId_ + ".lock"));
    if (lock->openedOk())
        autosaveLock_ = std::move(lock);
}

bool ProjectSession::autosaveOwnerRunning(const juce::File& folder, const juce::String& owner)
{
    if (owner.isEmpty())
        return false; // a copy from before instances owned theirs
    const auto lock = folder.getChildFile(owner + ".lock");
    // A lock file that can be deleted is stale (its owner closed or crashed).
    return lock.existsAsFile() && !lock.deleteFile();
}

juce::File ProjectSession::currentAutosaveFile() const
{
    if (autosaveFolder_ == juce::File())
        return {};
    // Named for the project and for this instance, so two instances with the same
    // project open never write over each other's copy.
    juce::String stem;
    if (projectFile_ != juce::File())
        stem = projectFile_.getFileNameWithoutExtension() + "-" +
               juce::String::toHexString(projectFile_.getFullPathName().toLowerCase().hashCode64()) + "-" +
               instanceId_.substring(0, 6);
    else
        stem = "Untitled-" + untitledStem_;
    return autosaveFolder_.getChildFile(juce::File::createLegalFileName(stem) + ".northlight");
}

void ProjectSession::edited()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    lastEditMs_ = now;
    if (dirtySinceMs_ <= 0.0)
        dirtySinceMs_ = now;
}

// Every insert's parameter keys, resolved on the message thread (a hosted plugin's
// come from its live instance), so the project can be written on another thread.
class ProjectSession::ResolvedKeys final : public core::ParameterKeys
{
public:
    explicit ResolvedKeys(const ProjectSession& session)
    {
        const SessionKeys live(session);
        const auto resolve = [&](const core::Channel& channel)
        {
            for (const auto* insert : channel.processingChain())
                if (plugins::builtInParameterLayout(insert->type).keys.empty())
                    layouts_[insert->id] = {insert->type, live.layout(*insert)};
        };
        for (const auto& t : session.project_.tracks)
            resolve(t);
        for (const auto& b : session.project_.buses)
            resolve(b);
        resolve(session.project_.master);
    }
    core::ParameterLayout layout(const core::Insert& insert) const override
    {
        if (auto builtIn = plugins::builtInParameterLayout(insert.type); !builtIn.keys.empty())
            return builtIn;
        const auto it = layouts_.find(insert.id);
        return it != layouts_.end() && it->second.first == insert.type ? it->second.second
                                                                       : core::ParameterLayout{};
    }

private:
    std::map<core::Id, std::pair<std::string, core::ParameterLayout>> layouts_;
};

// What writing an autosave needs, taken on the message thread.
struct ProjectSession::AutosaveJob
{
    juce::File target;
    core::Project project;
    std::unique_ptr<ResolvedKeys> keys;
    juce::String projectPath;
    juce::String owner; // the instance that wrote it
    std::string name;
    std::uint64_t generation = 0;
};

std::shared_ptr<ProjectSession::AutosaveJob> ProjectSession::prepareAutosave()
{
    if (autosaveFolder_ == juce::File() || !isModified())
        return nullptr;
    if (untitledStem_.isEmpty())
        untitledStem_ = juce::Uuid().toString().substring(0, 8);
    if (!autosaveFolder_.createDirectory().wasOk())
        return nullptr;
    captureInsertStates(); // plugin state: message thread
    auto job = std::make_shared<AutosaveJob>();
    job->target = currentAutosaveFile();
    job->project = project_;
    job->keys = std::make_unique<ResolvedKeys>(*this);
    job->projectPath = projectFile_.getFullPathName();
    job->owner = instanceId_;
    job->name = project_.name;
    job->generation = autosaveGeneration_;
    return job;
}

bool ProjectSession::writeAutosave(const AutosaveJob& job)
{
    // Written beside the target and moved over it, so a crash mid-write never
    // leaves a torn copy where the last good one was.
    const auto temp = job.target.getSiblingFile(job.target.getFileName() + ".tmp");
    if (!temp.replaceWithText(juce::String(core::projectToJson(job.project, true, job.keys.get()))) ||
        !temp.moveFileTo(job.target))
    {
        temp.deleteFile();
        return false;
    }
    auto meta = std::make_unique<juce::DynamicObject>();
    meta->setProperty("project", job.projectPath);
    meta->setProperty("owner", job.owner);
    meta->setProperty("name", juce::String::fromUTF8(job.name.c_str()));
    meta->setProperty("time", juce::Time::currentTimeMillis());
    sidecarOf(job.target).replaceWithText(juce::JSON::toString(juce::var(meta.release())));
    return true;
}

void ProjectSession::finishAutosave(const juce::File& target)
{
    // A project saved under a new name since the last autosave: the old copy goes.
    if (autosaveWritten_ != juce::File() && autosaveWritten_ != target)
    {
        autosaveWritten_.deleteFile();
        sidecarOf(autosaveWritten_).deleteFile();
    }
    autosaveWritten_ = target;
}

bool ProjectSession::autosaveNow()
{
    waitForAutosave();
    const auto job = prepareAutosave();
    if (job == nullptr || !writeAutosave(*job))
        return false;
    finishAutosave(job->target);
    dirtySinceMs_ = 0.0;
    return true;
}

void ProjectSession::autosaveInBackground()
{
    if (autosaveWriting_)
        return; // the last one is still being written: the next tick tries again
    const auto job = prepareAutosave();
    if (job == nullptr)
        return;
    autosaveWriting_ = true;
    const double dirtySince = dirtySinceMs_;
    dirtySinceMs_ = 0.0; // an edit from here on is newer than this copy
    autosaveWriter_.add(
        [this, job, dirtySince, alive = autosaveAlive_]
        {
            const bool ok = writeAutosave(*job);
            juce::MessageManager::callAsync(
                [this, job, ok, dirtySince, alive]
                {
                    if (!alive->load())
                        return;
                    autosaveWriting_ = false;
                    if (!ok)
                    {
                        if (dirtySinceMs_ <= 0.0)
                            dirtySinceMs_ = dirtySince; // try again at the next tick
                        return;
                    }
                    if (job->generation != autosaveGeneration_)
                    {
                        // Saved (or closed) while it was being written: it is stale.
                        job->target.deleteFile();
                        sidecarOf(job->target).deleteFile();
                        return;
                    }
                    finishAutosave(job->target);
                });
        });
}

void ProjectSession::waitForAutosave()
{
    // At most one job, a project's worth of JSON: a short wait.
    autosaveWriter_.dropPending();
    autosaveWriter_.waitUntilIdle(10000);
}

void ProjectSession::closeProject()
{
    const bool keep = isModified() && !changesDiscarded_;
    if (keep)
    {
        // Nobody chose to lose these changes: a fresh copy stays (when autosave is
        // on, or a copy was already written) and is no longer this project's, so
        // the next launch offers it.
        const int interval = autosaveInterval_ ? autosaveInterval_() : 0;
        if (interval > 0 || autosaveWritten_ != juce::File())
            autosaveNow();
        waitForAutosave();
        ++autosaveGeneration_;
        autosaveWritten_ = juce::File();
        dirtySinceMs_ = 0.0;
    }
    else
    {
        removeOwnAutosave();
        // The shared folder's recordings this session made that the closing
        // project does not use (its saved copy uses the copies brought into its
        // folder; discarded, it uses nothing) can never be used again: they go to
        // the Recycle Bin rather than pile up.
        std::set<juce::String> used;
        if (!changesDiscarded_)
            for (const auto& t : project_.tracks)
            {
                for (const auto& c : t.clips)
                    used.insert(juce::String::fromUTF8(c.mediaPath.c_str()));
                for (const auto& g : t.takeGroups)
                    for (const auto& take : g.takes)
                        used.insert(juce::String::fromUTF8(take.clip.mediaPath.c_str()));
            }
        for (const auto& path : ownSharedRecordings_)
            if (used.count(path) == 0)
                if (const juce::File f(path); f.existsAsFile())
                    trashMedia(f);
    }
    ownSharedRecordings_.clear();
    changesDiscarded_ = false;
}

void ProjectSession::removeOwnAutosave()
{
    waitForAutosave(); // a copy being written now would outlive the removal
    ++autosaveGeneration_;
    if (autosaveWritten_ != juce::File())
    {
        autosaveWritten_.deleteFile();
        sidecarOf(autosaveWritten_).deleteFile();
    }
    autosaveWritten_ = juce::File();
    dirtySinceMs_ = 0.0;
}

void ProjectSession::autosaveIfDue()
{
    const int interval = autosaveInterval_ ? autosaveInterval_() : 0;
    if (interval <= 0 || dirtySinceMs_ <= 0.0 || gestureOpen_)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Due once the interval has passed since the first unsaved change, and the
    // edits have paused for a second (not in the middle of a burst of commands).
    if (now - dirtySinceMs_ >= interval * 1000.0 && now - lastEditMs_ >= 1000.0)
        autosaveInBackground(); // the plugin states are taken here; the JSON is written off this thread
}

std::vector<ProjectSession::Autosave> ProjectSession::leftoverAutosaves() const
{
    std::vector<Autosave> found;
    if (!autosaveFolder_.isDirectory())
        return found;
    for (const auto& file : autosaveFolder_.findChildFiles(juce::File::findFiles, false, "*.northlight"))
    {
        if (file == autosaveWritten_)
            continue;
        // Another running instance's live copy is not a leftover.
        const auto meta = juce::JSON::parse(sidecarOf(file).loadFileAsString());
        const auto owner = meta.getDynamicObject() != nullptr
                               ? meta.getDynamicObject()->getProperty("owner").toString()
                               : juce::String();
        if (owner == instanceId_ || autosaveOwnerRunning(autosaveFolder_, owner))
            continue;
        Autosave a;
        a.file = file;
        a.time = file.getLastModificationTime();
        a.name = file.getFileNameWithoutExtension().toStdString();
        if (const auto* object = meta.getDynamicObject())
        {
            const auto path = object->getProperty("project").toString();
            if (path.isNotEmpty())
                a.project = juce::File(path);
            const auto name = object->getProperty("name").toString();
            if (name.isNotEmpty())
                a.name = name.toStdString();
            if (const auto ms = static_cast<juce::int64>(object->getProperty("time")); ms > 0)
                a.time = juce::Time(ms);
        }
        if (a.project != juce::File())
            a.name = a.project.getFileNameWithoutExtension().toStdString();
        found.push_back(std::move(a));
    }
    std::sort(
        found.begin(), found.end(), [](const Autosave& x, const Autosave& y) { return x.time > y.time; });
    return found;
}

std::string ProjectSession::restoreAutosave(const Autosave& autosave)
{
    if (!beginRestore(autosave))
        return "another project is still loading";
    return finishLoadNow();
}

void ProjectSession::discardAutosave(const Autosave& autosave)
{
    autosave.file.deleteFile();
    sidecarOf(autosave.file).deleteFile();
}

// ----- transport ---------------------------------------------------------------------------

void ProjectSession::play()
{
    if (!engine_.isPlaying())
    {
        engine_.resetIntegratedLoudness();
        beginPlayback();
    }
    engine_.play();
}

void ProjectSession::beginPlayback()
{
    // Resuming from a pause keeps the first start, so Stop still returns there.
    if (!paused_)
        playStart_ = engine_.position();
    paused_ = false;
}

void ProjectSession::stop()
{
    finishAutomationPasses();
    engine_.stop();
}

void ProjectSession::stopTransport()
{
    const bool rolling = engine_.isPlaying() || engine_.isRecording();
    if (!rolling && !paused_)
        return;
    stop();
    paused_ = false;
    if (returnOnStop_)
    {
        engine_.locate(playStart_);
        disk_.poke();
    }
}

void ProjectSession::pause()
{
    if (!engine_.isPlaying() && !engine_.isRecording())
        return;
    stop();
    paused_ = true;
}

void ProjectSession::togglePlay()
{
    if (engine_.isPlaying() || engine_.isRecording())
        stopTransport();
    else
    {
        beginPlayback();
        engine_.play();
    }
}

void ProjectSession::togglePause()
{
    if (engine_.isPlaying() || engine_.isRecording())
        pause();
    else
    {
        beginPlayback();
        engine_.play();
    }
}

void ProjectSession::returnToStart()
{
    locate(0);
}

void ProjectSession::locate(core::SamplePosition position)
{
    engine_.locate(position);
    disk_.poke(); // streamed clips rebuild their windows at the new position sooner
    // A jump forgets a pause; while rolling, Stop returns to where it jumped to.
    paused_ = false;
    if (engine_.isPlaying() || engine_.isRecording())
        playStart_ = position;
}

std::string ProjectSession::sampleRateWarning() const
{
    const double deviceRate = engine_.deviceSampleRate();
    if (deviceRate <= 0.0 || deviceRate == project_.sampleRate)
        return {};
    return "The device runs at " + std::to_string(static_cast<int>(deviceRate)) +
           " Hz but the project is at " + std::to_string(static_cast<int>(project_.sampleRate)) +
           " Hz; playback will be pitched.";
}

// ----- metronome, loop, punch ------------------------------------------------------------------

void ProjectSession::setMetronome(bool enabled)
{
    metronome_ = enabled;
    engine_.setMetronome(enabled, metronomeGain_);
    notify(Change::structure);
}

void ProjectSession::setMetronomeLevel(float gain)
{
    metronomeGain_ = std::clamp(gain, 0.0f, 1.0f);
    engine_.setMetronome(metronome_, metronomeGain_);
}

void ProjectSession::setPluginSearchPaths(std::vector<std::string> folders)
{
    scanner_.setExtraSearchPaths(std::move(folders));
}

void ProjectSession::setCountInBars(int bars)
{
    countInBars_ = std::clamp(bars, 0, 4);
    notify(Change::structure);
}

void ProjectSession::setRegion(core::SamplePosition start, core::SamplePosition end)
{
    regionStart_ = std::max<core::SamplePosition>(0, std::min(start, end));
    regionEnd_ = std::max(start, end);
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    notify(Change::structure);
}

void ProjectSession::setLoopEnabled(bool enabled)
{
    loop_ = enabled;
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    notify(Change::structure);
}

void ProjectSession::setPunchEnabled(bool enabled)
{
    punch_ = enabled;
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    notify(Change::structure);
}

// ----- recording ---------------------------------------------------------------------------

juce::File ProjectSession::mediaFolder() const
{
    if (projectFile_.existsAsFile() || projectFile_.getFullPathName().isNotEmpty())
        return projectFile_.getSiblingFile(projectFile_.getFileNameWithoutExtension() + " Media");
    return sharedRecordingsFolder();
}

core::SamplePosition ProjectSession::recordingLatencySamples() const
{
    // What the player hears lags the timeline by the device round trip plus the
    // plugin delay compensation on the master, so a take recorded against it
    // lands that much later than it was played.
    const auto graph = static_cast<core::SamplePosition>(engine_.outputLatencySamples());
    if (const auto calibration = audio_.currentCalibration())
        return calibration->roundTripSamples + graph;
    const auto s = audio_.status();
    return s.inputLatencySamples + s.outputLatencySamples + graph;
}

core::SamplePosition ProjectSession::midiRecordingLatencySamples() const
{
    // A MIDI note arrives as it is played, but the player plays to what they
    // hear, which lags by the output path (device output and the master's delay
    // compensation); the input side of the round trip does not apply.
    const auto graph = static_cast<core::SamplePosition>(engine_.outputLatencySamples());
    const auto s = audio_.status();
    return s.outputLatencySamples + graph;
}

namespace
{
// Beside every recording's first file sits "<base>.recording.json" with what the
// writer cannot know and recovery needs: the track, where capture started, the loop
// start for later takes and the latency the clip should be shifted back by.
constexpr const char* sidecarSuffix = ".recording.json";
constexpr const char* partialSuffix = ".partial.wav";

struct RecordingName
{
    juce::String base; // "<track>-<stamp>-<n>" without any take suffix
    int takeIndex = 0; // 0 for "<base>.partial.wav", n-1 for "<base>-take<n>.partial.wav"
};

RecordingName parseRecordingName(const juce::String& fileName)
{
    RecordingName name;
    name.base = fileName.endsWith(partialSuffix)
                    ? fileName.dropLastCharacters(static_cast<int>(std::strlen(partialSuffix)))
                    : fileName;
    const auto dash = name.base.lastIndexOf("-take");
    if (dash >= 0)
    {
        const auto digits = name.base.substring(dash + 5);
        if (digits.isNotEmpty() && digits.containsOnly("0123456789"))
        {
            name.takeIndex = std::max(0, digits.getIntValue() - 1);
            name.base = name.base.substring(0, dash);
        }
    }
    return name;
}

// A base name no existing file in the folder starts with (as "<base>.*" or
// "<base>-take*"), so a new recording never lands on an old one.
juce::String uniqueRecordingBase(const juce::File& folder, const juce::String& wanted)
{
    for (int attempt = 0;; ++attempt)
    {
        const auto candidate = attempt == 0 ? wanted : wanted + "-" + juce::String(attempt + 1);
        if (folder.findChildFiles(juce::File::findFiles, false, candidate + ".*").isEmpty() &&
            folder.findChildFiles(juce::File::findFiles, false, candidate + "-take*").isEmpty())
            return candidate;
    }
}

struct RecordingMeta
{
    core::Id trackId = core::invalidId;
    std::string trackName;
    core::SamplePosition startPosition = 0;
    core::SamplePosition loopStart = -1; // where takes after the first start; -1 = no loop
    double sampleRate = 0.0;
    core::SamplePosition latencySamples = 0;
};

bool writeSidecar(const juce::File& file, const RecordingMeta& meta)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("format", "northlight.recording/1");
    object->setProperty("trackId", static_cast<juce::int64>(meta.trackId));
    object->setProperty("trackName", juce::String(meta.trackName));
    object->setProperty("startPosition", static_cast<juce::int64>(meta.startPosition));
    object->setProperty("loopStart", static_cast<juce::int64>(meta.loopStart));
    object->setProperty("sampleRate", meta.sampleRate);
    object->setProperty("latencySamples", static_cast<juce::int64>(meta.latencySamples));
    return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

std::optional<RecordingMeta> readSidecar(const juce::File& file)
{
    if (!file.existsAsFile())
        return std::nullopt;
    const auto v = juce::JSON::parse(file);
    if (!v.isObject())
        return std::nullopt;
    RecordingMeta meta;
    meta.trackId = static_cast<core::Id>(static_cast<juce::int64>(v.getProperty("trackId", -1)));
    meta.trackName = v.getProperty("trackName", "").toString().toStdString();
    meta.startPosition = static_cast<juce::int64>(v.getProperty("startPosition", 0));
    meta.loopStart = static_cast<juce::int64>(v.getProperty("loopStart", -1));
    meta.sampleRate = static_cast<double>(v.getProperty("sampleRate", 0.0));
    meta.latencySamples = static_cast<juce::int64>(v.getProperty("latencySamples", 0));
    return meta;
}

// Deletes sidecars whose recording files are all gone.
void removeOrphanSidecars(const juce::File& folder)
{
    for (const auto& sidecar :
         folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + sidecarSuffix))
    {
        const auto base =
            sidecar.getFileName().dropLastCharacters(static_cast<int>(std::strlen(sidecarSuffix)));
        if (folder.findChildFiles(juce::File::findFiles, false, base + partialSuffix).isEmpty() &&
            folder.findChildFiles(juce::File::findFiles, false, base + "-take*" + partialSuffix).isEmpty())
            sidecar.deleteFile();
    }
}
} // namespace

std::string ProjectSession::startRecording()
{
    if (engine_.isRecording())
        return {};
    const auto startedAt = engine_.position(); // where Stop returns to (when not already rolling)
    const bool wasRolling = engine_.isPlaying();
    if (!engine_.isRunning())
        return "no audio device is running";
    // The session captures at the device rate and the files are stamped with the
    // project rate: they must agree, or every take would play at the wrong speed.
    const double deviceRate = engine_.deviceSampleRate();
    if (deviceRate > 0.0 && deviceRate != project_.sampleRate)
        return "the device runs at " + std::to_string(static_cast<int>(deviceRate)) +
               " Hz but the project is at " + std::to_string(static_cast<int>(project_.sampleRate)) +
               " Hz; set the device to the project rate before recording";
    const auto folder = mediaFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();

    // Bars on the tempo track: the count-in is the bars before where capture
    // rolls from, so its clicks are the song's own beats.
    const auto map = core::tempoMapOf(project_);
    const bool punching = punch_ && regionEnd_ > regionStart_;
    const bool looping = loop_ && regionEnd_ > regionStart_;

    // Punching in: roll from one bar before the region so the take has a run-up.
    // Pre-roll: from the bars before the playhead, heard but not captured.
    // (Only once the session is known to start; the locate is harmless otherwise.)
    const auto here = engine_.position();
    const auto preRollFrom = !punching && !looping && preRollBars_ > 0
                                 ? std::max<core::SamplePosition>(0, map.move(here, {1, 0.0}, -preRollBars_))
                                 : here;
    const bool preRolling = preRollFrom < here;
    const auto rollFrom =
        punching ? std::max<core::SamplePosition>(0, map.move(regionStart_, {1, 0.0}, -1)) : preRollFrom;
    const auto countIn =
        std::max<core::SamplePosition>(0, rollFrom - map.move(rollFrom, {1, 0.0}, -countInBars_));
    // Capture starts at the punch-in point, or wraps to the loop start first when
    // playback is at or past the loop end.
    core::SamplePosition captureStart = punching     ? std::max(regionStart_, rollFrom)
                                        : preRolling ? here
                                                     : rollFrom;
    if (looping && captureStart >= regionEnd_)
        captureStart = regionStart_;

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto session = std::make_unique<engine::RecordSession>(project_.sampleRate);
    session->setBitsPerSample(recordingBits_);
    int index = 0;
    for (const auto& t : project_.tracks)
    {
        if (t.armed && t.isMidi())
        {
            session->addMidiTrack(t.id);
            continue;
        }
        const auto input = resolvedInput(t);
        if (!t.armed || input.left < 0)
            continue;
        const auto safeName = juce::File::createLegalFileName(juce::String(t.name)).trim();
        const auto base = uniqueRecordingBase(folder,
                                              (safeName.isEmpty() ? juce::String("Track") : safeName) + "-" +
                                                  stamp + "-" + juce::String(++index));
        const auto file = folder.getChildFile(base + partialSuffix);
        RecordingMeta meta;
        meta.trackId = t.id;
        meta.trackName = t.name;
        meta.startPosition = captureStart;
        meta.loopStart = looping ? regionStart_ : -1;
        meta.sampleRate = project_.sampleRate;
        meta.latencySamples = recordingLatencySamples();
        if (!writeSidecar(folder.getChildFile(base + sidecarSuffix), meta))
            return "could not write to " + folder.getFullPathName().toStdString();
        if (input.right >= 0)
            session->addStereoInput(t.id, input.left, input.right, file.getFullPathName().toStdString());
        else
            session->addInput(t.id, input.left, file.getFullPathName().toStdString());
    }
    if (session->inputs().empty() && session->midiTracks().empty())
        return "arm a track and give it an input first";

    if (punching || preRolling)
        engine_.locate(rollFrom);
    // The pre-roll borrows the engine's punch: capture from the playhead on, with
    // no end. The session's own punch range comes back when the files are in.
    if (preRolling)
        engine_.setPunch(here, std::numeric_limits<core::SamplePosition>::max() / 2, true);

    std::string error;
    if (!engine_.startRecording(std::move(session), error, countIn))
    {
        if (preRolling)
            engine_.setPunch(regionStart_, regionEnd_, punch_);
        removeOrphanSidecars(folder);
        return error;
    }
    recordingPunched_ = punching;
    if (!wasRolling)
    {
        if (!paused_)
            playStart_ = startedAt;
        paused_ = false;
    }
    recordingPreRolled_ = preRolling;
    recordingInFlight_ = true;
    notify(Change::structure);
    return {};
}

std::string ProjectSession::takeName(const std::string& pattern, const std::string& track, int take)
{
    auto name = juce::String::fromUTF8(pattern.c_str())
                    .replace("{track}", juce::String::fromUTF8(track.c_str()))
                    .replace("{take}", juce::String(take))
                    .trim();
    if (name.isEmpty())
        name = juce::String::fromUTF8(track.c_str()) + " " + juce::String(take);
    return name.toStdString();
}

void ProjectSession::finishRecording(engine::AudioEngine::FinishedRecording finished)
{
    // The device stopped under the recording (it was unplugged): the engine ended
    // it, not the user.
    const bool cutShort = !engine_.isRunning();
    recordingInFlight_ = false;
    if (recordingPreRolled_)
    {
        recordingPreRolled_ = false;
        engine_.setPunch(regionStart_, regionEnd_, punch_);
    }
    // Take numbers go on from what each track has already: its clips (not the
    // ones a comp is made of) and the takes in its take groups.
    std::map<core::Id, int> takesBefore;
    for (const auto& t : project_.tracks)
    {
        int count = static_cast<int>(t.midiClips.size());
        for (const auto& c : t.clips)
            if (std::none_of(t.takeGroups.begin(),
                             t.takeGroups.end(),
                             [&](const core::TakeGroup& g) { return g.ownsClip(c.id); }))
                ++count;
        for (const auto& g : t.takeGroups)
            count += static_cast<int>(g.takes.size());
        takesBefore[t.id] = count;
    }
    const bool shared = projectFile_ == juce::File();
    core::SamplePosition savedUpTo = 0;
    const auto latency = recordingLatencySamples();
    std::uint64_t dropped = 0; // per input the count is cumulative over takes: take the largest
    std::int64_t lost = 0;
    int takes = 0;
    int failures = 0;

    // One "Record" step; the engine work happens once, below.
    auto batch = std::make_unique<Batch>(*this, "Record");
    // Each track's passes, in recording order (the results come per input, take by take).
    std::map<core::Id, std::vector<std::pair<int, core::AudioClip>>> passes;
    for (const auto& r : finished.results)
    {
        dropped = std::max(dropped, r.dropped);
        lost += r.samplesLost;
        juce::File partial{juce::String(r.path)};
        if (!r.ok)
        {
            ++failures;
            // What did reach the disk is still good audio: repair the header (it may
            // be stale) and keep it like any other take.
            if (r.samplesWritten <= 0 || !engine::WavWriter::repair(r.path))
            {
                partial.deleteFile();
                continue;
            }
        }
        else if (r.samplesWritten <= 0)
        {
            partial.deleteFile(); // an empty take (stopped at a loop wrap, or punched out at once)
            continue;
        }
        // Named by the take pattern (Preferences, Recording): the clip and its file.
        const auto* track = project_.findTrack(r.trackId);
        const auto name =
            track != nullptr
                ? takeName(takeNamePattern_, track->name, takesBefore[r.trackId] + r.takeIndex + 1)
                : std::string{};
        const auto legal = juce::File::createLegalFileName(juce::String::fromUTF8(name.c_str())).trim();
        juce::File final =
            (legal.isNotEmpty()
                 ? partial.getSiblingFile(legal + ".wav")
                 : partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".wav")))
                .getNonexistentSibling(false);
        if (!partial.moveFileTo(final))
            final = partial;
        if (shared)
            ownSharedRecordings_.insert(final.getFullPathName());

        // Each loop pass is a take, placed at the loop start.
        const auto placement = engine::placeRecording(r.startPosition, r.samplesWritten, latency);
        savedUpTo = std::max(savedUpTo, placement.start + placement.length);
        core::AudioClip clip;
        clip.name = name.empty() ? final.getFileNameWithoutExtension().toStdString() : name;
        clip.mediaPath = final.getFullPathName().toStdString();
        clip.start = placement.start;
        clip.sourceOffset = placement.sourceOffset;
        clip.length = placement.length;
        if (clip.length > 0 && project_.findTrack(r.trackId) != nullptr)
        {
            passes[r.trackId].emplace_back(r.takeIndex, std::move(clip));
            takes = std::max(takes, r.takeIndex + 1);
        }
    }
    // A track's passes become takes in a take group (docs/design/screens/comping.md)
    // when there are several (loop recording), when punching in, or when they
    // land on a take group already there; a single pass elsewhere is a clip.
    for (auto& [trackId, list] : passes)
    {
        std::stable_sort(
            list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<core::AudioClip> clips;
        for (auto& [index, clip] : list)
            clips.push_back(std::move(clip));
        const auto* track = project_.findTrack(trackId);
        if (track != nullptr && core::recordingMakesTakes(*track, clips, recordingPunched_))
            run(std::make_unique<core::RecordTakes>(trackId, std::move(clips)), false);
        else
            for (auto& clip : clips)
                run(std::make_unique<core::AddClip>(trackId, std::move(clip)), false);
    }
    // MIDI takes: the captured events become clips, placed like the audio (the
    // player heard the instrument late by the output path, and played to that).
    // What a track's passes become is its record mode: takes (a take group for
    // loop passes, a punch, or passes over a MIDI group; else a clip each), or
    // merged into (replacing over their range in) the clip under them.
    int midiClips = 0;
    std::map<core::Id, std::vector<core::MidiClip>> midiPasses;
    for (auto& take : engine::buildMidiTakes(
             finished.session->capturedMidi(), finished.session->takeSpans(), midiRecordingLatencySamples()))
        if (const auto* track = project_.findTrack(take.trackId))
        {
            ++midiClips;
            take.clip.name = takeName(takeNamePattern_, track->name, ++takesBefore[take.trackId]);
            savedUpTo = std::max(savedUpTo, take.clip.start + take.clip.length);
            midiPasses[take.trackId].push_back(std::move(take.clip));
        }
    for (auto& [trackId, list] : midiPasses)
    {
        const auto* track = project_.findTrack(trackId);
        if (track == nullptr)
            continue;
        if (track->midiRecordMode != core::MidiRecordMode::takes)
            run(std::make_unique<core::RecordMidiPasses>(
                    trackId, std::move(list), track->midiRecordMode == core::MidiRecordMode::replace),
                false);
        else if (core::recordingMakesTakes(*track, list, recordingPunched_))
            run(std::make_unique<core::RecordTakes>(trackId, std::move(list)), false);
        else
            for (auto& clip : list)
                run(std::make_unique<core::AddMidiClip>(trackId, std::move(clip)), false);
    }
    removeOrphanSidecars(mediaFolder());
    if (cutShort)
    {
        // Error text in three parts (interaction.md 10.6): what happened, what it
        // means for the take, what to do.
        const auto seconds = static_cast<int>(static_cast<double>(savedUpTo) / project_.sampleRate);
        const auto upTo = juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
        notices_.push(
            {.kind = Notice::Kind::error,
             .text = (lastDeviceName_.empty() ? std::string("The audio device") : "The " + lastDeviceName_) +
                     " stopped. Playback stopped; " +
                     (savedUpTo > 0 ? "the recording was saved up to " + upTo.toStdString()
                                    : std::string("nothing had been recorded yet")) +
                     ". Reconnect it or choose another device in Audio Setup.",
             .audioSetup = true});
        deviceLossTold_ = true;
        if (failures > 0)
            notices_.push(Notice::Kind::warning,
                          "Recording failed on " + std::to_string(failures) + " file(s): " +
                              std::to_string(lost) + " samples never reached the disk; what did is placed");
        batch.reset(); // ends the step and publishes
        notify(Change::structure);
        return;
    }

    if (failures > 0)
        notices_.push(Notice::Kind::warning,
                      "Recording failed on " + std::to_string(failures) + " file(s): " +
                          std::to_string(lost) + " samples never reached the disk; what did is placed");
    else if (dropped > 0)
        notices_.push(Notice::Kind::warning,
                      "Recording has " + std::to_string(dropped) + " samples of silence from disk overruns");
    else if (finished.session->inputs().empty())
        notices_.push(Notice::Kind::success,
                      midiClips > 0 ? "Recorded " + std::to_string(midiClips) + " MIDI clip(s)"
                                    : std::string("Recorded no MIDI: nothing was played"));
    else
        notices_.push(Notice::Kind::success,
                      "Recorded " + std::to_string(finished.session->inputs().size()) + " track(s)" +
                          (takes > 1 ? ", " + std::to_string(takes) + " takes" : std::string{}) +
                          ", placed " + std::to_string(latency) + " samples early for latency");
    batch.reset(); // ends the step and publishes
    notify(Change::structure);
}

void ProjectSession::updateLiveRecording(engine::LiveRecording& into)
{
    // The files closed since the last frame: place the clips now rather than at
    // the next 250 ms poll, so the live takes hand over to them without a gap.
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
    auto* session = engine_.activeRecording();
    if (session == nullptr)
    {
        into.takes.clear();
        into.midi.clear();
        return;
    }
    into.latency = recordingLatencySamples();
    engine_.updateLiveTakes(into.takes);
    into.midi.clear();
    if (session->midiTracks().empty() || session->startPosition() < 0)
        return; // nothing captured yet (a count-in)
    const auto spans = session->takeSpans();
    const auto midiLatency = midiRecordingLatencySamples();
    into.midi = engine::buildMidiTakes(session->capturedMidi(), spans, midiLatency);
    // The current take shows as a clip even before a note is played.
    const int current = static_cast<int>(spans.size()) - 1;
    for (const auto trackId : session->midiTracks())
    {
        bool shown = false;
        for (const auto& take : into.midi)
            shown = shown || (take.trackId == trackId && take.takeIndex == current);
        if (shown)
            continue;
        engine::MidiTake take;
        take.trackId = trackId;
        take.takeIndex = current;
        take.clip.start = std::max<core::SamplePosition>(0, spans.back().start - midiLatency);
        take.clip.length =
            std::max<core::SamplePosition>(1, spans.back().end - midiLatency - take.clip.start);
        into.midi.push_back(std::move(take));
    }
}

int ProjectSession::countPartialRecordings() const
{
    const auto folder = mediaFolder();
    return folder.isDirectory() ? folder.getNumberOfChildFiles(juce::File::findFiles, "*.partial.wav") : 0;
}

int ProjectSession::discardPartialRecordings()
{
    const auto folder = mediaFolder();
    int discarded = 0;
    if (!folder.isDirectory())
        return 0;
    for (const auto& partial : folder.findChildFiles(juce::File::findFiles, false, "*.partial.wav"))
        if (partial.moveFileTo(
                partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".discarded.wav"))))
            ++discarded;
    removeOrphanSidecars(folder);
    return discarded;
}

bool ProjectSession::isTrackMonitoring(const core::Track& track) const
{
    engine::TrackNode node;
    node.isMidi = track.isMidi();
    node.inputChannel = resolvedInput(track).left;
    node.armed = track.armed;
    node.monitor = track.monitor;
    return node.isMidi ? engine::Renderer::monitorsMidi(node, engine_.isPlaying(), engine_.isRecording())
                       : engine::Renderer::monitorsInput(node, engine_.isPlaying(), engine_.isRecording());
}

int ProjectSession::recoverPartialRecordings()
{
    const auto folder = mediaFolder();
    if (!folder.isDirectory())
        return 0;
    int recovered = 0;
    std::map<juce::String, core::Id> newTracks; // recording base -> the track made for its takes
    std::optional<Batch> batch;                 // opened with the first recovery: one step, published once
    for (const auto& partial : folder.findChildFiles(juce::File::findFiles, false, "*.partial.wav"))
    {
        const auto path = partial.getFullPathName().toStdString();
        if (!engine::WavWriter::repair(path))
        {
            // Not even a header (killed right after the file was opened): nothing to
            // keep. Anything else unreadable is set aside rather than offered forever.
            if (partial.getSize() < 44)
                partial.deleteFile();
            else
                partial.moveFileTo(
                    partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".discarded.wav")));
            continue;
        }
        const auto info = engine::readWavInfo(path);
        if (!info.has_value() || info->lengthSamples <= 0)
        {
            partial.deleteFile();
            continue;
        }
        const auto recoveredFile =
            partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".recovered.wav"))
                .getNonexistentSibling(false);
        if (!partial.moveFileTo(recoveredFile))
            continue;

        // With its sidecar the take goes back where it was captured, on its own
        // track if that still exists; without one it lands at the start of a new
        // track, which is at least audible.
        const auto name = parseRecordingName(partial.getFileName());
        const auto meta = readSidecar(folder.getChildFile(name.base + sidecarSuffix));
        core::AudioClip clip;
        clip.name = recoveredFile.getFileNameWithoutExtension().toStdString();
        clip.mediaPath = recoveredFile.getFullPathName().toStdString();
        clip.length = info->lengthSamples;
        if (meta.has_value())
        {
            const auto captureStart =
                name.takeIndex > 0 && meta->loopStart >= 0 ? meta->loopStart : meta->startPosition;
            const auto placement =
                engine::placeRecording(captureStart, info->lengthSamples, meta->latencySamples);
            clip.start = placement.start;
            clip.sourceOffset = placement.sourceOffset;
            clip.length = placement.length;
        }
        if (clip.length <= 0)
            continue;
        // The original track if it still exists, else one new track per recording
        // (all its takes together), made on the first take.
        core::Id target = meta.has_value() && project_.findTrack(meta->trackId) != nullptr ? meta->trackId
                                                                                           : core::invalidId;
        if (target == core::invalidId)
            if (const auto made = newTracks.find(name.base); made != newTracks.end())
                target = made->second;
        if (!batch.has_value())
            batch.emplace(*this, "Recover recordings");
        if (target != core::invalidId)
        {
            run(std::make_unique<core::AddClip>(target, clip), false);
        }
        else
        {
            core::Track track;
            track.name = "Recovered: " + (meta.has_value() && !meta->trackName.empty()
                                              ? meta->trackName
                                              : recoveredFile.getFileNameWithoutExtension().toStdString());
            track.clips.push_back(clip);
            auto add = std::make_unique<core::AddTrack>(track);
            const auto* raw = add.get();
            if (run(std::move(add), false)) // a recovered track gets the channel strip like any other
                newTracks[name.base] = raw->trackId();
        }
        ++recovered;
    }
    batch.reset(); // ends the step and publishes
    removeOrphanSidecars(folder);
    if (recovered > 0)
        notices_.push(Notice::Kind::success,
                      "Recovered " + std::to_string(recovered) + " unfinished recording(s)");
    return recovered;
}

// ----- bounce --------------------------------------------------------------------------------

std::string ProjectSession::startBounce(BounceJob::Options options)
{
    if (isBouncing())
        return "a bounce is already running";
    if (project_.tracks.empty())
        return "nothing to bounce";
    if (options.stems && !options.stemTracks.empty() &&
        std::none_of(options.stemTracks.begin(),
                     options.stemTracks.end(),
                     [this](core::Id id) { return project_.findTrack(id) != nullptr; }))
        return "no tracks selected";
    captureInsertStates(); // the offline engines restore the live plugin states
    stop();
    const double outputRate = engine::outputRate(options.format, project_.sampleRate);
    bounce_ = std::make_unique<BounceJob>(project_, inserts_, options);
    bounce_->onFinished = [this, options, outputRate](const BounceJob::Result& result)
    {
        // An error, a cancel or nothing written is told plainly; a mix held at
        // the ceiling is a warning; a finished export offers its file.
        auto kind = Notice::Kind::success;
        if (!result.error.empty())
            kind = Notice::Kind::error;
        else if (result.cancelled || result.files.empty())
            kind = Notice::Kind::info;
        else if (result.limitedByCeiling)
            kind = Notice::Kind::warning;
        notices_.push(kind,
                      BounceJob::describe(result, options, outputRate),
                      kind == Notice::Kind::success || kind == Notice::Kind::warning ? result.files.front()
                                                                                     : juce::File{});
        notify(Change::structure);
    };
    notices_.push(Notice::Kind::status, "Bouncing...");
    bounce_->start();
    notify(Change::structure);
    return {};
}

std::string ProjectSession::freezeTrack(core::Id trackId)
{
    if (isFreezing())
        return "a freeze is already running";
    if (isBouncing())
        return "wait for the export to finish";
    const auto* track = project_.findTrack(trackId);
    if (track == nullptr || !track->hasAudio())
        return "only a track that makes audio can be frozen";
    if (track->isFrozen())
        return {};
    for (const auto& t : project_.tracks)
        if (t.isAux() && t.auxTrack == trackId)
            return "this instrument's extra outputs feed other tracks; it cannot be frozen yet";
    const auto folder = mediaFolder().getChildFile("Freeze");
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    captureInsertStates(); // the offline engine restores the live plugin states
    const auto safe = juce::File::createLegalFileName(juce::String::fromUTF8(track->name.c_str()))
                          .trim()
                          .replaceCharacter(' ', '-');
    const auto file =
        folder.getNonexistentChildFile((safe.isEmpty() ? juce::String("Track") : safe) + "-freeze", ".wav");
    BounceJob::Options options;
    options.output = file;
    options.start = 0;
    // The song and three seconds for tails (a reverb, a release).
    options.end = project_.length() + static_cast<core::SamplePosition>(project_.sampleRate * 3.0);
    freeze_ = std::make_unique<BounceJob>(core::freezeRenderProject(project_, trackId), inserts_, options);
    const auto name = track->name;
    freeze_->onFinished = [this, trackId, file, name](const BounceJob::Result& result)
    {
        if (!result.error.empty() || result.cancelled)
        {
            notices_.push(Notice::Kind::warning,
                          "Freezing " + name + " failed: " + (result.cancelled ? "cancelled" : result.error));
            file.deleteFile();
        }
        else if (project_.findTrack(trackId) == nullptr)
        {
            notices_.push(Notice::Kind::warning, "The track was removed while it was being frozen.");
            file.deleteFile();
        }
        else
        {
            execute(std::make_unique<core::SetTrackFrozen>(trackId, file.getFullPathName().toStdString()));
            notices_.push(Notice::Kind::success,
                          "Froze " + name + ": it plays its render until you unfreeze it.");
        }
        notify(Change::structure);
    };
    freeze_->start();
    notify(Change::structure);
    return {};
}

// ----- plugins -------------------------------------------------------------------------------

void ProjectSession::scanPlugins(bool all)
{
    scanner_.start(all);
    notify(Change::structure);
}

void ProjectSession::clearPluginBlocklist()
{
    registry_.clearBlocklist();
    saveRegistry();
    notify(Change::structure);
}

void ProjectSession::retryBlockedPlugin(const std::string& path)
{
    registry_.unblock(path);
    saveRegistry();
    if (!scanner_.isScanning())
        scanner_.start(false);
    notify(Change::structure);
}

void ProjectSession::reloadInsert(core::Id insertId)
{
    // The crashed instance is kept (never freed) and the next compile makes a
    // fresh one from the model's state, which a crash never overwrote.
    engine_.forgetInsert(insertId);
    reportedCrashes_.erase(insertId);
    publish();
}

void ProjectSession::retryPlugin(const std::string& type)
{
    inserts_.clearFailure(type);
    reportedFailures_.erase(type);
    publish();
}

void ProjectSession::blocklistPlugin(const std::string& type)
{
    const auto* info = registry_.findByType(type);
    if (info == nullptr)
        return;
    const auto reason = inserts_.failure(type);
    registry_.block(info->path, reason.empty() ? std::string("failed to load") : reason);
    saveRegistry();
    notify(Change::structure);
}

void ProjectSession::reportPluginTrouble()
{
    // The project's hosted plugins that did not load: missing on this machine
    // (not in the registry) or failed (in it, but refused).
    std::vector<std::string> missing, failed;
    const auto look = [&](const core::Insert& insert)
    {
        if (!hosting::PluginRegistry::isHostedType(insert.type) ||
            engine_.insertInstance(insert.id) != nullptr)
            return;
        auto& into = registry_.findByType(insert.type) != nullptr ? failed : missing;
        const auto name = registry_.findByType(insert.type) != nullptr ? inserts_.displayName(insert.type)
                          : !insert.pluginName.empty()                 ? insert.pluginName
                                                                       : std::string("an unnamed plugin");
        if (std::find(into.begin(), into.end(), name) == into.end())
            into.push_back(name);
        if (&into == &failed)
            reportedFailures_.insert(insert.type); // told here, not again one by one
    };
    const auto channel = [&](const core::Channel& c)
    {
        if (c.hasInstrument())
            look(c.instrument);
        for (const auto* list : {&c.inserts, &c.midiInserts})
            for (const auto& i : *list)
                look(i);
    };
    for (const auto& t : project_.tracks)
        channel(t);
    for (const auto& b : project_.buses)
        channel(b);
    channel(project_.master);
    if (missing.empty() && failed.empty())
        return;
    const auto list = [](const std::vector<std::string>& names)
    {
        std::string text;
        for (size_t i = 0; i < names.size(); ++i)
            text += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
        return text;
    };
    std::string text;
    if (!missing.empty())
        text += (missing.size() == 1 ? "1 plugin is not installed: "
                                     : std::to_string(missing.size()) + " plugins are not installed: ") +
                list(missing) + ". Their settings are kept.";
    if (!failed.empty())
        text += std::string(text.empty() ? "" : " ") +
                (failed.size() == 1 ? "1 plugin failed to load: "
                                    : std::to_string(failed.size()) + " plugins failed to load: ") +
                list(failed) + ".";
    notices_.push(Notice::Kind::warning, text, "Scan for plugins", [this] { scanPlugins(false); });
}

void ProjectSession::saveRegistry()
{
    if (registryFile_.getFullPathName().isEmpty())
        return;
    registryFile_.getParentDirectory().createDirectory();
    registryFile_.replaceWithText(juce::String(registry_.toJson()));
}

bool ProjectSession::isInstrumentType(const std::string& type) const
{
    if (plugins::BuiltInFactory::isInstrument(type))
        return true;
    const auto* info = registry_.findByType(type);
    return info != nullptr && info->isInstrument;
}

void ProjectSession::captureInsertStates()
{
    const auto capture = [this](core::Channel& channel)
    {
        // A crashed plugin is never called again: the project keeps the state it
        // had before the crash.
        if (channel.hasInstrument())
            if (const auto live = engine_.insertInstance(channel.instrument.id);
                live != nullptr && !live->crashed())
                if (const auto state = live->state(); !state.empty())
                    channel.instrument.state = state;
        for (auto* list : {&channel.inserts, &channel.midiInserts})
            for (auto& insert : *list)
                if (const auto live = engine_.insertInstance(insert.id); live != nullptr && !live->crashed())
                    if (const auto state = live->state(); !state.empty())
                        insert.state = state;
        // A hosted plugin's name, so it is still named where it is missing.
        const auto name = [this](core::Insert& insert)
        {
            if (const auto* info = registry_.findByType(insert.type))
                insert.pluginName = info->name;
        };
        if (channel.hasInstrument())
            name(channel.instrument);
        for (auto* list : {&channel.inserts, &channel.midiInserts})
            for (auto& insert : *list)
                name(insert);
        // Which windows are open, so they reopen with the project.
        if (isEditorOpen)
        {
            if (channel.hasInstrument())
                channel.instrument.windowOpen = isEditorOpen(channel.instrument.id);
            for (auto* list : {&channel.inserts, &channel.midiInserts})
                for (auto& insert : *list)
                    insert.windowOpen = isEditorOpen(insert.id);
        }
    };
    for (auto& t : project_.tracks)
        capture(t);
    for (auto& b : project_.buses)
        capture(b);
    capture(project_.master);
}

// ----- internals ---------------------------------------------------------------------------

void ProjectSession::publish()
{
    engine_.setProject(project_);
    notify(Change::structure);
}

void ProjectSession::matchDeviceSampleRate()
{
    // An empty project simply adopts the device rate; one with media keeps its own
    // rate and the UI shows the mismatch.
    const auto status = audio_.status();
    if (status.open && status.sampleRate > 0.0 && project_.length() == 0 &&
        project_.sampleRate != status.sampleRate)
        project_.sampleRate = status.sampleRate;
}

bool ProjectSession::refreshInputBuses()
{
    const auto status = audio_.status();
    std::map<std::string, engine::InputPair> buses;
    if (status.open)
        for (const auto& [name, pair] : audio::resolveInputBuses(audio_.inputBuses(), status.activeInputs))
            buses[name] = engine::InputPair{pair.first, pair.second};
    // The device's output pairs, for channels played to a hardware output.
    const int pairs = status.open ? std::max(1, static_cast<int>(status.activeOutputs.size()) / 2) : 1;
    const bool pairsChanged = pairs != engine_.outputPairs();
    engine_.setOutputPairs(pairs);
    if (buses == inputBuses_)
        return pairsChanged;
    inputBuses_ = buses;
    engine_.setInputBuses(std::move(buses));
    return true;
}

engine::InputPair ProjectSession::resolvedInput(const core::Track& track) const
{
    if (track.inputBus.empty())
        return {track.inputChannel, -1};
    const auto it = inputBuses_.find(track.inputBus);
    return it != inputBuses_.end() ? it->second : engine::InputPair{};
}

void ProjectSession::watchDevice()
{
    const auto s = audio_.status();
    const std::string device = s.open ? s.deviceType + "\n" + s.outputDevice : std::string{};
    if (device == lastDevice_)
        return;
    const bool wasOpen = !lastDevice_.empty();
    const auto lost = lastDeviceName_.empty() ? std::string("The audio device") : "The " + lastDeviceName_;
    if (wasOpen && !s.open)
    {
        // Gone (unplugged, or its driver reset). Stop the transport now: a fallback
        // device that opens later must not start playing out of other speakers. A
        // recording it cut short says so itself, with how much was saved.
        deviceLost_ = true;
        const bool wasPlaying = isPlaying();
        stop();
        if (!recordingInFlight_ && !engine_.hasRecordingToCollect() && !deviceLossTold_)
        {
            notices_.push({.kind = Notice::Kind::error,
                           .text = lost + " was disconnected. " + (wasPlaying ? "Playback stopped. " : "") +
                                   "Reconnect it or choose another device in Audio Setup.",
                           .audioSetup = true});
            deviceLossTold_ = true;
        }
    }
    else if (s.open && (deviceLost_ || (wasOpen && s.preferredUnavailable)))
    {
        if (!deviceLost_)
        {
            // The device manager moved to another device by itself (the list changed).
            stop();
            notices_.push({.kind = Notice::Kind::warning,
                           .text = lost + " is unavailable. Playback stopped; Northlight now plays through " +
                                   s.outputDevice + ".",
                           .audioSetup = true});
        }
        else if (s.preferredUnavailable)
            notices_.push(Notice::Kind::info,
                          "Playing through " + s.outputDevice + " until " + audio_.settings().outputDevice +
                              " is back. Press Play to go on.");
        else
            notices_.push(Notice::Kind::success, s.outputDevice + " is back.");
        deviceLost_ = false;
        deviceLossTold_ = false;
    }
    lastDevice_ = device;
    if (s.open)
        lastDeviceName_ = s.outputDevice;
}

void ProjectSession::changeListenerCallback(juce::ChangeBroadcaster*)
{
    watchDevice();
    const double before = project_.sampleRate;
    matchDeviceSampleRate();
    const bool busesChanged = refreshInputBuses();
    if (project_.sampleRate != before || busesChanged)
        publish();
    else
        notify(Change::structure);
}

void ProjectSession::timerCallback()
{
    poll();
    autosaveIfDue();
    // Write mode starts when playback does; a stop from anywhere finishes the passes.
    if (engine_.isPlaying())
    {
        if (!writePassesStarted_)
            startWritePasses();
    }
    else
    {
        writePassesStarted_ = false;
        if (!passes_.empty())
            finishAutomationPasses();
    }
}

// ----- automation writing ---------------------------------------------------------------

namespace
{
constexpr double rampInSeconds = 0.01;  // from the curve into a written pass
constexpr double rampOutSeconds = 0.05; // from a released touch back to the curve

bool writes(core::AutomationMode mode)
{
    return mode == core::AutomationMode::touch || mode == core::AutomationMode::latch ||
           mode == core::AutomationMode::write;
}

// Drops points that repeat the value before them (a held control at UI rate).
std::vector<core::AutomationPoint> thinned(const std::vector<core::AutomationPoint>& points)
{
    std::vector<core::AutomationPoint> out;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const bool repeat = !out.empty() && std::abs(points[i].value - out.back().value) < 1.0e-5f;
        const bool lastOfRun =
            i + 1 < points.size() && std::abs(points[i + 1].value - points[i].value) >= 1.0e-5f;
        if (!repeat || lastOfRun || i + 1 == points.size())
            out.push_back(points[i]);
    }
    return out;
}
} // namespace

float ProjectSession::staticValue(const core::Channel& channel, const AutomationPass& pass) const
{
    switch (pass.target)
    {
        case core::AutomationTarget::gain:
            return channel.gain;
        case core::AutomationTarget::pan:
            return channel.pan;
        case core::AutomationTarget::insertParameter:
            if (const auto* insert = channel.findInsert(pass.insert);
                insert != nullptr && pass.parameter >= 0 &&
                static_cast<size_t>(pass.parameter) < insert->params.size())
                return insert->params[static_cast<size_t>(pass.parameter)];
            if (const auto live = engine_.insertInstance(pass.insert))
                return live->parameter(pass.parameter);
            return 0.0f;
    }
    return 0.0f;
}

bool ProjectSession::recordAutomation(const core::Command::AutomationWrite& write, bool pushValue)
{
    if (!engine_.isPlaying())
        return false;
    AutomationPass key;
    key.channel = write.channel;
    key.target = write.target;
    if (write.target == core::AutomationTarget::insertParameter)
    {
        key.insert = write.insert;
        key.parameter = write.parameter;
    }
    const float value = write.value;
    const auto* channel = project_.findChannel(key.channel);
    if (channel == nullptr || !writes(channel->automationMode))
        return false;
    std::shared_ptr<engine::InsertProcessor> instance;
    if (key.target == core::AutomationTarget::insertParameter)
    {
        instance = engine_.insertInstance(key.insert);
        if (instance == nullptr || key.parameter < 0 ||
            key.parameter >= static_cast<int>(instance->parameters().size()))
            return false;
    }
    const auto position = engine_.position();
    auto it = std::find_if(passes_.begin(),
                           passes_.end(),
                           [&](const AutomationPass& p)
                           {
                               return p.channel == key.channel && p.target == key.target &&
                                      p.insert == key.insert && p.parameter == key.parameter;
                           });
    if (it != passes_.end() && !it->points.empty() && position < it->points.back().position)
    {
        // The transport looped back: what was written so far is one pass.
        std::vector<std::unique_ptr<core::Command>> commands;
        commitPass(*it, it->points.back().position, commands);
        it->points.clear();
        if (!commands.empty())
            executeGroupWithoutRecording(commands);
        it->lane = channel->findLane(key.target, key.insert, key.parameter) != nullptr
                       ? channel->findLane(key.target, key.insert, key.parameter)->id
                       : core::invalidId;
        if (it->lane != core::invalidId)
            engine_.setAutomationTouched(it->lane, true);
    }
    if (it == passes_.end())
    {
        key.mode = channel->automationMode;
        if (const auto* lane = channel->findLane(key.target, key.insert, key.parameter))
        {
            key.lane = lane->id;
            engine_.setAutomationTouched(lane->id, true);
        }
        passes_.push_back(key);
        it = passes_.end() - 1;
    }
    if (!it->points.empty() && it->points.back().position == position)
        it->points.back().value = value;
    else
        it->points.push_back({position, value});
    // The value is heard at once (a plugin's own edit is already in the plugin).
    if (!pushValue)
        return true;
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            engine_.setLiveGain(key.channel, value);
            break;
        case core::AutomationTarget::pan:
            engine_.setLivePan(key.channel, value);
            break;
        case core::AutomationTarget::insertParameter:
            instance->setParameter(key.parameter, value);
            break;
    }
    return true;
}

void ProjectSession::startWritePasses()
{
    writePassesStarted_ = true;
    const auto position = engine_.position();
    const auto start = [&](const core::Channel& channel)
    {
        if (channel.automationMode != core::AutomationMode::write)
            return;
        for (const auto& lane : channel.automation)
        {
            AutomationPass pass;
            pass.channel = channel.id;
            pass.target = lane.target;
            pass.insert = lane.insert;
            pass.parameter = lane.parameter;
            pass.mode = core::AutomationMode::write;
            pass.lane = lane.id;
            pass.points.push_back({position, staticValue(channel, pass)});
            engine_.setAutomationTouched(lane.id, true);
            passes_.push_back(std::move(pass));
        }
    };
    for (const auto& t : project_.tracks)
        start(t);
    for (const auto& b : project_.buses)
        start(b);
}

void ProjectSession::commitPass(const AutomationPass& pass,
                                core::SamplePosition end,
                                std::vector<std::unique_ptr<core::Command>>& out) const
{
    const auto* channel = project_.findChannel(pass.channel);
    if (channel == nullptr || pass.points.empty())
        return;
    const auto* lane = channel->findLane(pass.target, pass.insert, pass.parameter);
    const auto recorded = thinned(pass.points);
    const auto rate = project_.sampleRate;
    const auto start = recorded.front().position;
    const bool touch = pass.mode == core::AutomationMode::touch;
    const auto stop = touch ? recorded.back().position : std::max(end, recorded.back().position);
    const auto rampIn = static_cast<core::SamplePosition>(rate * rampInSeconds);
    const auto rampOut = static_cast<core::SamplePosition>(rate * rampOutSeconds);
    const float fallback = staticValue(*channel, pass);
    // A lane with no points has no curve yet: the static value stands in.
    const auto curveAt = [&](core::SamplePosition at)
    { return lane != nullptr && !lane->points.empty() ? lane->valueAt(at) : fallback; };

    std::vector<core::AutomationPoint> points;
    if (lane != nullptr)
        for (const auto& p : lane->points)
            if (p.position < start - rampIn)
                points.push_back(p);
    if (start - rampIn >= 0)
        points.push_back({start - rampIn, curveAt(start - rampIn)});
    points.insert(points.end(), recorded.begin(), recorded.end());
    core::SamplePosition resume = stop;
    if (touch)
    {
        resume = stop + rampOut; // back to the curve after a short glide
        points.push_back({resume, curveAt(resume)});
    }
    else if (stop > recorded.back().position)
        points.push_back({stop, recorded.back().value}); // latch and write hold to the stop
    if (lane != nullptr)
        for (const auto& p : lane->points)
            if (p.position > resume)
                points.push_back(p);

    if (lane != nullptr)
    {
        out.push_back(
            std::make_unique<core::SetAutomationPoints>(pass.channel, lane->id, points, "Write automation"));
        // What was written shows: the channel's lanes open and the lane in view.
        if (lane->view.hidden || lane->view.collapsed)
        {
            auto view = lane->view;
            view.hidden = view.collapsed = false;
            out.push_back(std::make_unique<core::SetAutomationLaneView>(pass.channel, lane->id, view));
        }
        if (!channel->automationOpen)
            out.push_back(std::make_unique<core::SetAutomationOpen>(pass.channel, true));
    }
    else // a new lane opens the channel's lanes itself
    {
        core::AutomationLane added;
        added.target = pass.target;
        added.insert = pass.insert;
        added.parameter = pass.parameter;
        added.points = points;
        out.push_back(std::make_unique<core::AddAutomationLane>(pass.channel, added));
    }
}

void ProjectSession::finishPasses(bool touchOnly)
{
    if (passes_.empty())
        return;
    const auto end = engine_.position();
    std::vector<std::unique_ptr<core::Command>> commands;
    std::vector<core::Id> lanes;
    for (auto it = passes_.begin(); it != passes_.end();)
    {
        if (touchOnly && (it->mode != core::AutomationMode::touch || it->fromPlugin))
        {
            ++it;
            continue;
        }
        commitPass(*it, end, commands);
        if (it->lane != core::invalidId)
            lanes.push_back(it->lane);
        it = passes_.erase(it);
    }
    if (!commands.empty())
        executeGroupWithoutRecording(commands);
    for (const auto lane : lanes)
        engine_.setAutomationTouched(lane, false);
}

void ProjectSession::finishAutomationPasses()
{
    finishPasses(false);
}

void ProjectSession::handlePluginEdit(const core::Channel& channel,
                                      const core::Insert& insert,
                                      const engine::InsertProcessor::ParameterEdit& edit)
{
    using Kind = engine::InsertProcessor::ParameterEdit::Kind;
    if (edit.kind == Kind::value)
    {
        // A plugin reporting back what a lane just sent it is not the user.
        if (const auto sent = hostedSent_.find({insert.id, edit.index});
            sent != hostedSent_.end() && std::abs(sent->second - edit.value) <= 1.0e-4f)
            return;
        if (recordAutomation(
                *core::SetInsertParameter(channel.id, insert.id, edit.index, edit.value).automationWrite(),
                false))
            for (auto& pass : passes_)
                if (pass.insert == insert.id && pass.parameter == edit.index &&
                    pass.target == core::AutomationTarget::insertParameter)
                    pass.fromPlugin = true;
    }
    else if (edit.kind == Kind::end && channel.automationMode == core::AutomationMode::touch)
        finishPass(channel.id, insert.id, edit.index); // the plugin let go of its control
}

void ProjectSession::finishPass(core::Id channel, core::Id insert, int parameter)
{
    const auto it = std::find_if(passes_.begin(),
                                 passes_.end(),
                                 [&](const AutomationPass& p)
                                 {
                                     return p.channel == channel && p.insert == insert &&
                                            p.parameter == parameter &&
                                            p.target == core::AutomationTarget::insertParameter;
                                 });
    if (it == passes_.end())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commitPass(*it, engine_.position(), commands);
    const auto lane = it->lane;
    passes_.erase(it);
    if (!commands.empty())
        executeGroupWithoutRecording(commands);
    if (lane != core::invalidId)
        engine_.setAutomationTouched(lane, false);
}

void ProjectSession::playHostedAutomation()
{
    // Edits the plugins made themselves (their own editors) first: while writing
    // they become automation, otherwise the plugin simply keeps them.
    {
        std::vector<std::pair<core::Id, core::Id>> hosted; // channel, insert
        const auto collect = [&](const core::Channel& channel)
        {
            if (channel.hasInstrument() && hosting::PluginRegistry::isHostedType(channel.instrument.type))
                hosted.emplace_back(channel.id, channel.instrument.id);
            for (const auto& insert : channel.inserts)
                if (hosting::PluginRegistry::isHostedType(insert.type))
                    hosted.emplace_back(channel.id, insert.id);
        };
        for (const auto& t : project_.tracks)
            collect(t);
        for (const auto& b : project_.buses)
            collect(b);
        collect(project_.master);
        for (const auto& [channelId, insertId] : hosted)
        {
            const auto instance = engine_.insertInstance(insertId);
            if (instance == nullptr)
                continue;
            // A change made in the plugin's own editor is a change to the project,
            // kept by the plugin rather than the history.
            if (instance->takeStateChanged())
                changedOutsideHistory_ = true;
            engine::InsertProcessor::ParameterEdit edit;
            for (int n = 0; n < 1024 && instance->takeParameterEdit(edit); ++n)
            {
                changedOutsideHistory_ = true;
                const auto* channel = project_.findChannel(channelId);
                const auto* insert = channel != nullptr ? channel->findInsert(insertId) : nullptr;
                if (insert != nullptr)
                    handlePluginEdit(*channel, *insert, edit);
            }
        }
    }
    const auto position = engine_.position();
    const auto play = [&](const core::Channel& channel)
    {
        if (channel.automationMode != core::AutomationMode::read &&
            channel.automationMode != core::AutomationMode::touch &&
            channel.automationMode != core::AutomationMode::latch)
            return;
        for (const auto& lane : channel.automation)
        {
            if (lane.target != core::AutomationTarget::insertParameter || lane.points.empty())
                continue;
            const auto instance = engine_.insertInstance(lane.insert);
            if (instance == nullptr || !instance->ownsParameters() || lane.parameter < 0 ||
                lane.parameter >= static_cast<int>(instance->parameters().size()))
                continue;
            // A lane being written is the user's.
            if (std::any_of(passes_.begin(),
                            passes_.end(),
                            [&](const AutomationPass& p) { return p.lane == lane.id; }))
                continue;
            // Sent whenever the plugin is somewhere else (the lane moved, the
            // instance is new, or the user turned the plugin's own knob in Read).
            const float value = lane.valueAt(position);
            if (std::abs(instance->parameter(lane.parameter) - value) <= 1.0e-4f)
                continue;
            hostedSent_[{lane.insert, lane.parameter}] = value;
            instance->setParameter(lane.parameter, value);
        }
    };
    for (const auto& t : project_.tracks)
        play(t);
    for (const auto& b : project_.buses)
        play(b);
}

void ProjectSession::executeGroupWithoutRecording(std::vector<std::unique_ptr<core::Command>>& commands)
{
    // Inside an open gesture (a pass committed by a loop wrap mid-drag) the
    // commands join the gesture's step (groups nest); otherwise they are a step
    // of their own.
    const Batch batch(*this, "Write automation");
    for (auto& command : commands)
        run(std::move(command), false);
}

void ProjectSession::checkCrashedPlugins()
{
    // A plugin that crashed while processing is switched off (its slot passes
    // audio through); say so once, with a nudge to save, since a crash can leave
    // the process unstable.
    const auto check = [this](const core::Channel& channel)
    {
        std::vector<const core::Insert*> inserts;
        if (channel.hasInstrument())
            inserts.push_back(&channel.instrument);
        for (const auto& insert : channel.inserts)
            inserts.push_back(&insert);
        for (const auto* insert : inserts)
        {
            if (const auto live = liveInsert(insert->id);
                live != nullptr && live->crashed() && reportedCrashes_.insert(insert->id).second)
            {
                const auto id = insert->id;
                notices_.push(Notice::Kind::error,
                              inserts_.displayName(insert->type) + " on " + channel.name +
                                  " crashed and was switched off. Save your project; Reload plugin starts "
                                  "it again from its last saved state.",
                              "Reload plugin",
                              [this, id] { reloadInsert(id); });
                notify(Change::structure);
            }
            // A plugin that is installed but would not load (after the project
            // opened: a new insert, a retry): say why, once, offering the blocklist.
            if (const auto reason = inserts_.failure(insert->type);
                !reason.empty() && registry_.findByType(insert->type) != nullptr &&
                reportedFailures_.insert(insert->type).second)
            {
                const auto type = insert->type;
                notices_.push(Notice::Kind::error,
                              inserts_.displayName(type) + " failed to load: " + reason + ".",
                              "Blocklist",
                              [this, type] { blocklistPlugin(type); });
                notify(Change::structure);
            }
        }
    };
    for (const auto& track : project_.tracks)
        check(track);
    for (const auto& bus : project_.buses)
        check(bus);
    check(project_.master);
}

void ProjectSession::poll()
{
    engine_.collectGarbage();
    // Hosted plugins' main-thread requests (callbacks, latency, flushes while the
    // device is stopped, GUI closures).
    hosting::HostFactory::serviceHostedPlugins(engine_.isRunning());
    if (engine_.insertLatencyChanged() || engine_.insertRestartRequested())
        publish(); // a plugin changed its latency or asked for a restart: new snapshot
    checkCrashedPlugins();
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
}

} // namespace northlight
