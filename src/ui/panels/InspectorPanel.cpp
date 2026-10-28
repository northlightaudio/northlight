#include "ui/panels/InspectorPanel.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "core/TempoMap.h"
#include "ui/Db.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Shortcuts.h"
#include "ui/TrackIcon.h"
#include "ui/components/Accessibility.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

InspectorPanel::InspectorPanel(const Theme& theme)
    : theme_(theme), gain_(theme), start_(theme), length_(theme), clipGain_(theme), fadeIn_(theme),
      fadeOut_(theme)
{
    setOpaque(true);
    name_.setFont(theme_.label());
    name_.setJustification(juce::Justification::centredLeft);
    name_.setIndents(theme_.spacing.sm, 0);
    name_.onReturnKey = [this]
    {
        if (updating_ || !onCommand)
            return;
        if (clipId_ != core::invalidId)
            return; // clip names are edited in the arrange view
        onCommand(std::make_unique<core::SetTrackName>(trackId_, name_.getText().toStdString()));
    };
    name_.onFocusLost = [this] { name_.onReturnKey(); };
    addAndMakeVisible(name_);

    gain_.setRange(-100.0, 6.0, 0.1);
    gain_.setDecimals(1);
    gain_.setUnit("dB");
    gain_.format = [](double v) { return v <= -100.0 ? juce::String("-inf") : juce::String(v, 1); };
    gain_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Gain");
    };
    gain_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    gain_.onValueChanged = [this](double db)
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackGain>(trackId_, db::toGain(db)));
    };
    addAndMakeVisible(gain_);

    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setDoubleClickReturnValue(true, 0.0);
    pan_.setTooltip("Pan: drag, double-click to centre");
    pan_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Pan");
    };
    pan_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    pan_.onValueChange = [this]
    {
        repaint(panValue_);
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackPan>(trackId_, static_cast<float>(pan_.getValue())));
    };
    addAndMakeVisible(pan_);

    const auto toggle = [this](juce::TextButton& b, const char* semantic, const juce::String& tip)
    {
        b.setClickingTogglesState(true);
        b.getProperties().set(NorthlightLookAndFeel::propertySemantic, semantic);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    toggle(mute_, "mute", {});
    toggle(solo_, "solo", {});
    toggle(arm_, "arm", {});
    Shortcuts::tag(mute_, "Mute", Command::muteTrack);
    Shortcuts::tag(solo_, "Solo", Command::soloTrack);
    Shortcuts::tag(arm_, "Arm for recording", Command::armSelected);
    arm_.getProperties().set(NorthlightLookAndFeel::propertyHardOn, true);
    mute_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackMute>(trackId_, mute_.getToggleState()));
    };
    solo_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackSolo>(trackId_, solo_.getToggleState()));
    };
    arm_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackArmed>(trackId_, arm_.getToggleState()));
    };

    for (auto* field : {&start_, &length_})
    {
        field->setRange(0.0, 1.0e7, 0.001);
        field->setDecimals(3);
        field->setUnit("s");
        field->setEnabled(false);
        addChildComponent(*field);
    }
    clipGain_.setRange(-60.0, 24.0, 0.1);
    clipGain_.setDecimals(1);
    clipGain_.setUnit("dB");
    clipGain_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Clip gain");
    };
    clipGain_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    clipGain_.onValueChanged = [this](double db)
    {
        if (!updating_ && onCommand && clipId_ != core::invalidId)
            onCommand(std::make_unique<core::SetClipGain>(trackId_, clipId_, db::toGain(db)));
    };
    addChildComponent(clipGain_);
    for (auto* field : {&fadeIn_, &fadeOut_})
    {
        field->setRange(0.0, 600000.0, 1.0);
        field->setDecimals(0);
        field->setUnit("ms");
        field->onValueChanged = [this](double)
        {
            if (updating_ || !onCommand || clipId_ == core::invalidId)
                return;
            const auto toSamples = [this](double ms)
            { return static_cast<core::SamplePosition>(std::round(ms * sampleRate() / 1000.0)); };
            onCommand(std::make_unique<core::SetClipFades>(
                trackId_, clipId_, toSamples(fadeIn_.value()), toSamples(fadeOut_.value())));
        };
        addChildComponent(*field);
    }

    // A MIDI track's record mode.
    recordMode_.setSegments({"Takes", "Merge", "Replace"});
    recordMode_.setTrackColour(theme_.colours.surface0);
    recordMode_.setSegmentTooltip(0, "Each pass is a take; loop passes make takes to comp");
    recordMode_.setSegmentTooltip(1, "The notes join the clip under them (overdub)");
    recordMode_.setSegmentTooltip(2, "The notes replace what the clip under them had over the pass");
    recordMode_.onChange = [this](int index)
    {
        if (updating_ || !onCommand)
            return;
        onCommand(std::make_unique<core::SetMidiRecordMode>(trackId_,
                                                            index == 1   ? core::MidiRecordMode::merge
                                                            : index == 2 ? core::MidiRecordMode::replace
                                                                         : core::MidiRecordMode::takes));
    };
    addChildComponent(recordMode_);

    // The piano roll's notes.
    notePitch_.setRange(0.0, 127.0, 1.0);
    notePitch_.setDecimals(0);
    notePitch_.format = [](double v)
    { return juce::String(core::noteName(static_cast<int>(std::lround(v)))); };
    notePitch_.parse = [](const juce::String& text)
    {
        // "C3", "F#2", "Bb1" or a number.
        const auto t = text.trim();
        if (t.containsOnly("0123456789"))
            return t.getDoubleValue();
        static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int pc = 11; pc >= 0; --pc)
            for (const bool flat : {false, true})
            {
                juce::String name = names[pc];
                if (flat)
                {
                    if (!name.endsWith("#"))
                        continue;
                    name = juce::String(names[(pc + 1) % 12]) + "b";
                }
                if (t.startsWithIgnoreCase(name) &&
                    t.substring(name.length()).trim().containsOnly("-0123456789") &&
                    t.substring(name.length()).trim().isNotEmpty())
                    return static_cast<double>(std::clamp(
                        (t.substring(name.length()).trim().getIntValue() - core::middleCOctave) * 12 +
                            core::middleC + pc,
                        0,
                        127));
            }
        return t.getDoubleValue();
    };
    notePitch_.setTooltip("The note's pitch: type C3, F#2 or a number");
    noteStart_.setRange(0.0, 1.0e9, 1.0);
    noteStart_.setDecimals(0);
    noteStart_.setTooltip("Where the note starts: bar.beat.tick (960 ticks a beat)");
    noteLength_.setRange(1.0 / 64.0, 1024.0, 1.0 / 16.0);
    noteLength_.setDecimals(3);
    noteLength_.setUnit("beats");
    noteLength_.setTooltip("The note's length in beats (0.25: a sixteenth)");
    noteVelocity_.setRange(1.0, 127.0, 1.0);
    noteVelocity_.setDecimals(0);
    noteVelocity_.setTooltip("Velocity, 1 to 127");
    noteMute_.setClickingTogglesState(true);
    noteMute_.getProperties().set(NorthlightLookAndFeel::propertySemantic, "mute");
    noteMute_.setTooltip("A muted note stays in the clip and plays nothing");
    gesture(notePitch_, "Set note pitch");
    gesture(noteStart_, "Move notes");
    gesture(noteLength_, "Set note length");
    gesture(noteVelocity_, "Set velocity");
    notePitch_.onValueChanged = [this](double v)
    {
        const auto first = selectedNotes();
        if (updating_ || first.empty())
            return;
        const int delta = static_cast<int>(std::lround(v)) - first.front().note;
        editNotes("Set note pitch",
                  [delta](core::MidiNote& n) { n.note = std::clamp(n.note + delta, 0, 127); });
    };
    noteStart_.onValueChanged = [this](double v)
    {
        const auto first = selectedNotes();
        const auto* c = midiClip();
        if (updating_ || first.empty() || c == nullptr)
            return;
        const auto delta = static_cast<core::SamplePosition>(v) - (c->start + first.front().start);
        editNotes("Move notes",
                  [delta](core::MidiNote& n)
                  { n.start = std::max<core::SamplePosition>(0, n.start + delta); });
    };
    noteLength_.onValueChanged = [this](double beats)
    {
        const auto first = selectedNotes();
        const auto* c = midiClip();
        if (updating_ || first.empty() || c == nullptr || project_ == nullptr)
            return;
        const auto map = core::tempoMapOf(*project_);
        const auto at = c->start + first.front().start;
        const auto quarters = beats * core::TempoMap::quartersPerBeat(map.signatureAt(at));
        const auto delta =
            std::max<core::SamplePosition>(1, std::llround(map.stepSamples({0, quarters}, at))) -
            first.front().length;
        editNotes("Set note length",
                  [delta](core::MidiNote& n)
                  { n.length = std::max<core::SamplePosition>(1, n.length + delta); });
    };
    noteVelocity_.onValueChanged = [this](double v)
    {
        const auto first = selectedNotes();
        if (updating_ || first.empty())
            return;
        const int delta = static_cast<int>(std::lround(v)) - first.front().velocity;
        editNotes("Set velocity",
                  [delta](core::MidiNote& n) { n.velocity = std::clamp(n.velocity + delta, 1, 127); });
    };
    noteMute_.onClick = [this]
    {
        if (updating_)
            return;
        const bool muted = noteMute_.getToggleState();
        if (vocal_)
            editVocalNotes(muted ? "Mute notes" : "Unmute notes",
                           [muted](core::VocalNote& n) { n.muted = muted; });
        else
            editNotes(muted ? "Mute notes" : "Unmute notes", [muted](core::MidiNote& n) { n.muted = muted; });
    };
    for (auto* f : {&notePitch_, &noteStart_, &noteLength_, &noteVelocity_})
        addChildComponent(*f);
    addChildComponent(noteMute_);

    // The audio editor's vocal notes.
    vocalPitch_.setRange(0.0, 12700.0, 1.0);
    vocalPitch_.setDecimals(0);
    vocalPitch_.setUnit("ct");
    vocalPitch_.format = [](double cents)
    {
        const int note = static_cast<int>(std::lround(cents / 100.0));
        const int offset = static_cast<int>(std::lround(cents - note * 100.0));
        return juce::String(core::noteName(note)) +
               (offset == 0 ? juce::String() : (offset > 0 ? " +" : " ") + juce::String(offset));
    };
    vocalPitch_.setTooltip("The pitch the note plays: drag (Shift fine) or type cents (6900 = A3)");
    vocalDrift_.setRange(0.0, 100.0, 1.0);
    vocalDrift_.setDecimals(0);
    vocalDrift_.setUnit("%");
    vocalDrift_.setTooltip("How much of the slow drift is kept");
    vocalVibrato_.setRange(0.0, 200.0, 1.0);
    vocalVibrato_.setDecimals(0);
    vocalVibrato_.setUnit("%");
    vocalVibrato_.setTooltip("How much vibrato: 100% as sung");
    for (auto* f : {&slideIn_, &slideOut_})
    {
        f->setRange(0.0, 2000.0, 1.0);
        f->setDecimals(0);
        f->setUnit("ms");
    }
    slideIn_.setTooltip("The slide into the note from the one before");
    slideOut_.setTooltip("The slide out of the note into the next");
    vocalGain_.setRange(-48.0, 12.0, 0.1);
    vocalGain_.setDecimals(1);
    vocalGain_.setUnit("dB");
    gesture(vocalPitch_, "Move pitch");
    gesture(vocalDrift_, "Set drift");
    gesture(vocalVibrato_, "Set vibrato");
    gesture(slideIn_, "Set slide");
    gesture(slideOut_, "Set slide");
    gesture(vocalGain_, "Set note gain");
    const auto transpose = [this]
    {
        const auto* c = vocalClip();
        return c != nullptr ? static_cast<float>(c->transpose * 100.0) : 0.0f;
    };
    vocalPitch_.onValueChanged = [this, transpose](double v)
    {
        const auto first = selectedVocalNotes();
        if (updating_ || first.empty())
            return;
        const float delta = static_cast<float>(v) - (first.front().target + transpose());
        editVocalNotes("Move pitch",
                       [delta](core::VocalNote& n)
                       { n.target = std::clamp(n.target + delta, 2400.0f, 10800.0f); });
    };
    vocalDrift_.onValueChanged = [this](double v)
    {
        if (!updating_)
            editVocalNotes("Set drift", [v](core::VocalNote& n) { n.drift = static_cast<float>(v / 100.0); });
    };
    vocalVibrato_.onValueChanged = [this](double v)
    {
        if (!updating_)
            editVocalNotes("Set vibrato",
                           [v](core::VocalNote& n) { n.vibrato = static_cast<float>(v / 100.0); });
    };
    const auto slide = [this](bool in, double ms)
    {
        if (updating_)
            return;
        const auto samples = static_cast<core::SamplePosition>(std::llround(ms * sampleRate() / 1000.0));
        editVocalNotes("Set slide",
                       [in, samples](core::VocalNote& n)
                       {
                           // At most two fifths of the note, as the editor's handles allow.
                           const auto limit = n.length() * 2 / 5;
                           (in ? n.transitionIn : n.transitionOut) =
                               std::clamp<core::SamplePosition>(samples, 0, limit);
                       });
    };
    slideIn_.onValueChanged = [slide](double ms) { slide(true, ms); };
    slideOut_.onValueChanged = [slide](double ms) { slide(false, ms); };
    vocalGain_.onValueChanged = [this](double db)
    {
        if (!updating_)
            editVocalNotes("Set note gain", [db](core::VocalNote& n) { n.gainDb = static_cast<float>(db); });
    };
    for (auto* f : {&vocalPitch_, &vocalDrift_, &vocalVibrato_, &slideIn_, &slideOut_, &vocalGain_})
        addChildComponent(*f);
}

void InspectorPanel::gesture(NumericField& field, const std::string& name)
{
    field.onDragStart = [this, name]
    {
        if (onGestureBegin)
            onGestureBegin(name);
    };
    field.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
}

void InspectorPanel::setProject(const core::Project* project)
{
    project_ = project;
    refresh();
}

void InspectorPanel::setSelection(core::Id trackId, core::Id clipId)
{
    trackId_ = trackId;
    clipId_ = clipId;
    notes_.clear();
    refresh();
}

void InspectorPanel::setNotes(core::Id trackId, core::Id clipId, std::vector<core::Id> notes)
{
    const bool showing = !notes_.empty() && !vocal_;
    if (notes.empty() && !showing)
        return; // another editor's selection, or the track's, stays
    notesTrack_ = trackId;
    notesClip_ = clipId;
    notes_ = std::move(notes);
    vocal_ = false;
    refresh();
}

void InspectorPanel::setVocalNotes(core::Id trackId, core::Id clipId, std::vector<core::Id> notes)
{
    const bool showing = !notes_.empty() && vocal_;
    if (notes.empty() && !showing)
        return;
    notesTrack_ = trackId;
    notesClip_ = clipId;
    notes_ = std::move(notes);
    vocal_ = true;
    refresh();
}

InspectorPanel::Shows InspectorPanel::shows() const noexcept
{
    if (!notes_.empty())
        return vocal_ ? Shows::vocalNotes : Shows::notes;
    if (track() == nullptr)
        return Shows::nothing;
    return clip() != nullptr ? Shows::clip : Shows::track;
}

const core::AudioClip* InspectorPanel::vocalClip() const
{
    const auto* t = project_ != nullptr ? project_->findTrack(notesTrack_) : nullptr;
    return t != nullptr ? t->findClip(notesClip_) : nullptr;
}

const core::MidiClip* InspectorPanel::midiClip() const
{
    const auto* t = project_ != nullptr ? project_->findTrack(notesTrack_) : nullptr;
    return t != nullptr ? t->findMidiClip(notesClip_) : nullptr;
}

std::vector<core::MidiNote> InspectorPanel::selectedNotes() const
{
    std::vector<core::MidiNote> out;
    if (const auto* c = midiClip(); c != nullptr && !vocal_)
        for (const auto& n : c->notes)
            if (std::find(notes_.begin(), notes_.end(), n.id) != notes_.end())
                out.push_back(n);
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

std::vector<core::VocalNote> InspectorPanel::selectedVocalNotes() const
{
    std::vector<core::VocalNote> out;
    if (const auto* c = vocalClip(); c != nullptr && vocal_)
        for (const auto& n : c->vocal.notes)
            if (std::find(notes_.begin(), notes_.end(), n.id) != notes_.end())
                out.push_back(n);
    return out;
}

void InspectorPanel::editNotes(const std::string& name, const std::function<void(core::MidiNote&)>& change)
{
    std::vector<std::unique_ptr<core::Command>> commands;
    for (auto n : selectedNotes())
    {
        const auto before = n;
        change(n);
        if (!(n == before))
            commands.push_back(std::make_unique<core::SetNote>(notesTrack_, notesClip_, n));
    }
    if (commands.empty())
        return;
    if (onCommands)
        onCommands(std::move(commands), name);
    else if (onCommand)
        for (auto& c : commands)
            onCommand(std::move(c));
}

void InspectorPanel::editVocalNotes(const std::string& name,
                                    const std::function<void(core::VocalNote&)>& change)
{
    const auto* c = vocalClip();
    if (c == nullptr || !onCommand)
        return;
    auto edit = c->vocal;
    for (auto& n : edit.notes)
        if (std::find(notes_.begin(), notes_.end(), n.id) != notes_.end())
            change(n);
    if (edit == c->vocal)
        return;
    onCommand(std::make_unique<core::SetVocalEdit>(notesTrack_, notesClip_, std::move(edit), name));
}

const core::Track* InspectorPanel::track() const
{
    return project_ != nullptr ? project_->findTrack(trackId_) : nullptr;
}

const core::AudioClip* InspectorPanel::clip() const
{
    const auto* t = track();
    return t != nullptr && clipId_ != core::invalidId ? t->findClip(clipId_) : nullptr;
}

double InspectorPanel::sampleRate() const noexcept
{
    return project_ != nullptr && project_->sampleRate > 0.0 ? project_->sampleRate : 48000.0;
}

void InspectorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    // Notes gone from the model (deleted, undone) leave the selection; none left:
    // back to the track or clip.
    if (!notes_.empty())
    {
        const auto still = [this](core::Id id)
        {
            if (vocal_)
            {
                const auto* c = vocalClip();
                return c != nullptr && c->vocal.findNote(id) != nullptr;
            }
            const auto* c = midiClip();
            return c != nullptr && c->findNote(id) != nullptr;
        };
        std::erase_if(notes_, [&](core::Id id) { return !still(id); });
    }
    const auto showing = shows();
    const bool notesMode = showing == Shows::notes, vocalMode = showing == Shows::vocalNotes;
    for (auto* f : {&notePitch_, &noteStart_, &noteLength_, &noteVelocity_})
        f->setVisible(notesMode);
    for (auto* f : {&vocalPitch_, &vocalDrift_, &vocalVibrato_, &slideIn_, &slideOut_, &vocalGain_})
        f->setVisible(vocalMode);
    noteMute_.setVisible(notesMode || vocalMode);
    if (notesMode)
    {
        const auto first = selectedNotes();
        const auto* c = midiClip();
        if (!first.empty() && c != nullptr && project_ != nullptr)
        {
            const auto& n = first.front();
            const auto map = core::tempoMapOf(*project_);
            const auto at = c->start + n.start;
            notePitch_.setValue(n.note, juce::dontSendNotification);
            noteStart_.setValue(static_cast<double>(at), juce::dontSendNotification);
            noteStart_.format = [map](double v)
            {
                const auto bb = map.barsBeatsAt(static_cast<core::SamplePosition>(v));
                return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." +
                       juce::String(bb.tick).paddedLeft('0', 3);
            };
            noteStart_.parse = [map](const juce::String& text)
            {
                juce::StringArray parts;
                parts.addTokens(text.trim(), ".:", "");
                core::BarsBeats bb;
                bb.bar = std::max(1, parts.size() > 0 ? parts[0].getIntValue() : 1);
                bb.beat = std::max(1, parts.size() > 1 ? parts[1].getIntValue() : 1);
                bb.tick = std::clamp(
                    parts.size() > 2 ? parts[2].getIntValue() : 0, 0, core::BarsBeats::ticksPerBeat - 1);
                return static_cast<double>(map.positionAt(bb));
            };
            // One step: a tick at the note's tempo.
            const double quarters = core::TempoMap::quartersPerBeat(map.signatureAt(at));
            noteStart_.setRange(
                0.0,
                1.0e10,
                std::max(1.0, map.stepSamples({0, quarters / core::BarsBeats::ticksPerBeat}, at)));
            noteLength_.setValue(static_cast<double>(n.length) /
                                     std::max(1.0, map.stepSamples({0, quarters}, at)),
                                 juce::dontSendNotification);
            noteVelocity_.setValue(n.velocity, juce::dontSendNotification);
            showToggle(noteMute_, n.muted);
        }
    }
    if (vocalMode)
    {
        const auto first = selectedVocalNotes();
        const auto* c = vocalClip();
        if (!first.empty() && c != nullptr)
        {
            const auto& n = first.front();
            const double sr = sampleRate();
            vocalPitch_.setValue(n.target + c->transpose * 100.0, juce::dontSendNotification);
            vocalDrift_.setValue(n.drift * 100.0, juce::dontSendNotification);
            vocalVibrato_.setValue(n.vibrato * 100.0, juce::dontSendNotification);
            slideIn_.setValue(static_cast<double>(n.transitionIn) * 1000.0 / sr, juce::dontSendNotification);
            slideOut_.setValue(static_cast<double>(n.transitionOut) * 1000.0 / sr,
                               juce::dontSendNotification);
            vocalGain_.setValue(n.gainDb, juce::dontSendNotification);
            showToggle(noteMute_, n.muted);
        }
    }
    const auto* t = notesMode || vocalMode ? nullptr : track();
    const auto* c = notesMode || vocalMode ? nullptr : clip();
    const bool clipMode = c != nullptr;
    name_.setVisible(t != nullptr && !clipMode);
    gain_.setVisible(t != nullptr && !clipMode);
    pan_.setVisible(t != nullptr && !clipMode);
    mute_.setVisible(t != nullptr && !clipMode);
    solo_.setVisible(t != nullptr && !clipMode);
    arm_.setVisible(t != nullptr && !clipMode && !t->isAux() && !t->isInstrument());
    recordMode_.setVisible(t != nullptr && !clipMode && t->isMidi());
    for (auto* f : {&start_, &length_, &clipGain_, &fadeIn_, &fadeOut_})
        f->setVisible(clipMode);
    if (t == nullptr)
    {
        resized();
        repaint();
        return;
    }
    if (t->isMidi())
        recordMode_.setSelected(t->midiRecordMode == core::MidiRecordMode::merge     ? 1
                                : t->midiRecordMode == core::MidiRecordMode::replace ? 2
                                                                                     : 0,
                                juce::dontSendNotification);
    if (!clipMode)
        name_.setText(juce::String::fromUTF8(t->name.c_str()), juce::dontSendNotification);
    if (clipMode)
    {
        const double sr = sampleRate();
        start_.setValue(static_cast<double>(c->start) / sr, juce::dontSendNotification);
        length_.setValue(static_cast<double>(c->length) / sr, juce::dontSendNotification);
        clipGain_.setValue(db::fromGain(c->gain), juce::dontSendNotification);
        fadeIn_.setValue(static_cast<double>(c->fadeIn) * 1000.0 / sr, juce::dontSendNotification);
        fadeOut_.setValue(static_cast<double>(c->fadeOut) * 1000.0 / sr, juce::dontSendNotification);
    }
    else
    {
        gain_.setValue(db::fromGain(t->gain), juce::dontSendNotification);
        pan_.setValue(t->pan, juce::dontSendNotification);
        showToggle(mute_, t->mute);
        showToggle(solo_, t->solo);
        showToggle(arm_, t->armed);
    }
    resized();
    repaint();
}

juce::Rectangle<int> InspectorPanel::swatchBounds(size_t index) const
{
    const int i = static_cast<int>(index);
    const int col = i % swatchPerRow_, row = i / swatchPerRow_;
    const int x = swatchArea_.getX() + static_cast<int>(std::round(col * (swatchSize_ + swatchGap_)));
    const int y = swatchArea_.getY() + row * (swatchSize_ + theme_.spacing.md);
    return {x, y, swatchSize_, swatchSize_};
}

int InspectorPanel::swatchAt(juce::Point<int> p) const
{
    if (swatchArea_.isEmpty() || clip() != nullptr)
        return -1;
    const int slop = theme_.spacing.xxs;
    for (size_t i = 0; i < theme_.track.palette.size(); ++i)
        if (swatchBounds(i).expanded(slop).contains(p))
            return static_cast<int>(i);
    return -1;
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    const bool notesMode = shows() == Shows::notes || shows() == Shows::vocalNotes;
    const auto* t = notesMode && project_ != nullptr ? project_->findTrack(notesTrack_) : track();
    if (t == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("Select a track or clip",
                   getLocalBounds().reduced(theme_.spacing.md).removeFromTop(theme_.sizes.controlLarge * 2),
                   juce::Justification::centred);
        return;
    }
    const auto colour =
        project_ != nullptr ? theme_.track.forChannel(*project_, t->id) : theme_.track.forTrack(t->colour, 0);

    // Cards, the identity card with a stripe in the track's colour.
    for (size_t i = 0; i < cards_.size(); ++i)
    {
        const auto bounds = cards_[i].toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        if (i == 0)
        {
            const juce::Graphics::ScopedSaveState state(g);
            juce::Path shape;
            shape.addRoundedRectangle(bounds, theme_.radius.md);
            g.reduceClipRegion(shape);
            g.setColour(colour);
            g.fillRect(bounds.withWidth(static_cast<float>(theme_.track.headerStripeWidth)));
        }
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
    }

    for (const auto& caption : captions_)
    {
        auto bounds = caption.bounds;
        if (caption.icon)
        {
            const int size = theme_.sizes.iconSmall;
            Icons::draw(g,
                        *caption.icon,
                        bounds.removeFromLeft(size).withSizeKeepingCentre(size, size).toFloat(),
                        c.textSecondary);
            bounds.removeFromLeft(theme_.spacing.xs + theme_.spacing.xxs);
        }
        g.setColour(c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText(caption.text, bounds, juce::Justification::centredLeft);
        if (caption.aside.isNotEmpty())
        {
            g.setFont(theme_.caption());
            const int captionW =
                juce::GlyphArrangement::getStringWidthInt(theme_.captionCaps(), caption.text) +
                theme_.spacing.md;
            g.drawFittedText(
                caption.aside, bounds.withTrimmedLeft(captionW), juce::Justification::centredRight, 1);
        }
    }

    g.setFont(theme_.body());
    g.setColour(c.textSecondary);
    for (const auto& label : labels_)
        g.drawText(label.text, label.bounds, juce::Justification::centredLeft);

    if (pan_.isVisible())
    {
        const double v = pan_.getValue();
        const int percent = static_cast<int>(std::round(std::abs(v) * 100.0));
        g.setFont(theme_.value());
        g.setColour(c.textPrimary);
        g.drawText(percent == 0 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(percent),
                   panValue_,
                   juce::Justification::centredRight);
    }

    if (notesMode)
        return;
    if (const auto* cl = clip())
    {
        g.setFont(theme_.label());
        g.setColour(c.textPrimary);
        g.drawFittedText(
            juce::String::fromUTF8(cl->name.c_str()), clipName_, juce::Justification::centredLeft, 1);
        return;
    }

    // Swatches: round chips; the track's colour ringed, the hovered one outlined.
    const auto& palette = theme_.track.palette;
    for (size_t i = 0; i < palette.size(); ++i)
    {
        const auto bounds = swatchBounds(i).toFloat();
        g.setColour(palette[i].value);
        g.fillEllipse(bounds);
        const bool current = palette[i].value == colour;
        if (current || static_cast<int>(i) == hoverSwatch_)
        {
            g.setColour(current ? c.textPrimary : c.textTertiary);
            g.drawEllipse(bounds.expanded(2.5f), current ? 1.5f : 1.0f);
        }
    }
}

void InspectorPanel::resized()
{
    cards_.clear();
    captions_.clear();
    labels_.clear();
    clipName_ = {};
    panValue_ = {};
    swatchArea_ = {};
    const auto showing = shows();
    const bool notesMode = showing == Shows::notes || showing == Shows::vocalNotes;
    const auto* t = notesMode && project_ != nullptr ? project_->findTrack(notesTrack_) : track();
    if (t == nullptr)
        return;
    const auto* cl = notesMode ? nullptr : clip();
    const auto& sp = theme_.spacing;
    const int rowH = theme_.sizes.control;
    const int captionH = sp.lg;
    const int fieldW = 96;

    const auto area = getLocalBounds().reduced(sp.md);
    const int x = area.getX() + sp.md, w = area.getWidth() - 2 * sp.md;
    int y = area.getY(), cardTop = 0;
    bool firstRow = true;

    const auto open = [&](const juce::String& caption, const juce::String& aside)
    {
        cardTop = y;
        y += sp.md;
        captions_.push_back({{x, y, w, captionH}, caption, aside});
        y += captionH + sp.sm;
        firstRow = true;
    };
    const auto close = [&]
    {
        y += sp.md;
        cards_.push_back({area.getX(), cardTop, area.getWidth(), y - cardTop});
        y += sp.sm;
    };
    // A row: the label on the left (centred on the first control line), the
    // control placed by the caller on the right of the returned area.
    const auto row = [&](const juce::String& label, int height)
    {
        if (!firstRow)
            y += sp.xs;
        firstRow = false;
        const juce::Rectangle<int> r{x, y, w, height};
        if (label.isNotEmpty())
            labels_.push_back({r.withHeight(rowH), label, {}});
        y += height;
        return r;
    };

    if (notesMode)
    {
        const int fieldWide = std::min(w - 72, 120);
        const int count = static_cast<int>(notes_.size());
        const auto caption =
            (showing == Shows::vocalNotes ? juce::String("VOCAL NOTE") : juce::String("NOTE")) +
            (count > 1 ? "S (" + juce::String(count) + ")" : juce::String());
        open(caption, juce::String::fromUTF8(t->name.c_str()));
        captions_.back().icon = showing == Shows::vocalNotes ? Icon::pitchTool : Icon::fileMidi;
        if (showing == Shows::notes)
        {
            notePitch_.setBounds(row("Pitch", rowH).removeFromRight(fieldWide));
            noteStart_.setBounds(row("Start", rowH).removeFromRight(fieldWide));
            noteLength_.setBounds(row("Length", rowH).removeFromRight(fieldWide));
            noteVelocity_.setBounds(row("Velocity", rowH).removeFromRight(fieldWide));
        }
        else
        {
            vocalPitch_.setBounds(row("Pitch", rowH).removeFromRight(fieldWide));
            vocalDrift_.setBounds(row("Drift", rowH).removeFromRight(fieldWide));
            vocalVibrato_.setBounds(row("Vibrato", rowH).removeFromRight(fieldWide));
            slideIn_.setBounds(row("Slide in", rowH).removeFromRight(fieldWide));
            slideOut_.setBounds(row("Slide out", rowH).removeFromRight(fieldWide));
            vocalGain_.setBounds(row("Gain", rowH).removeFromRight(fieldWide));
        }
        y += sp.xs;
        noteMute_.setBounds(row({}, rowH));
        close();
        return;
    }

    if (cl != nullptr)
    {
        open("CLIP", juce::String::fromUTF8(t->name.c_str()));
        captions_.back().icon = Icon::fileAudio;
        clipName_ = row({}, rowH);
        close();

        open("POSITION", {});
        const int sampleW = std::min(w - 72, 120);
        start_.setBounds(row("Start", rowH).removeFromRight(sampleW));
        length_.setBounds(row("Length", rowH).removeFromRight(sampleW));
        close();

        open("LEVEL", {});
        clipGain_.setBounds(row("Gain", rowH).removeFromRight(sampleW));
        fadeIn_.setBounds(row("Fade in", rowH).removeFromRight(sampleW));
        fadeOut_.setBounds(row("Fade out", rowH).removeFromRight(sampleW));
        close();
        return;
    }

    open(juce::String(trackKindName(t->kind)).toUpperCase(), {});
    captions_.back().icon = trackIcon(t->kind);
    name_.setBounds(row({}, rowH));
    // Small chips with clear air between them: one row of twelve when the gaps
    // stay at least `minGap`, else two of six.
    const int count = static_cast<int>(theme_.track.palette.size());
    const int minGap = sp.xs + sp.xxs;
    swatchSize_ = theme_.sizes.iconSmall;
    swatchPerRow_ = std::max(1, count);
    if (count > 1 && w < count * swatchSize_ + (count - 1) * minGap)
        swatchPerRow_ = (count + 1) / 2;
    swatchGap_ = swatchPerRow_ > 1
                     ? std::min(static_cast<float>(sp.md),
                                static_cast<float>(w - swatchPerRow_ * swatchSize_) / (swatchPerRow_ - 1))
                     : 0.0f;
    const int swatchRows = (count + swatchPerRow_ - 1) / swatchPerRow_;
    y += sp.md; // the swatches stand apart from the name field
    swatchArea_ = row({}, swatchRows * swatchSize_ + (swatchRows - 1) * sp.md);
    y += sp.xs;
    close();

    open("MIX", {});
    gain_.setBounds(row("Gain", rowH).removeFromRight(fieldW));
    // Pan as a slider across the row (a knob would sit alone in the space), its
    // value at the right edge in line with the gain field's.
    {
        auto r = row("Pan", rowH);
        r.removeFromLeft(sp.xxxl + sp.md);
        panValue_ = r.removeFromRight(sp.xl + sp.md).withTrimmedRight(sp.sm);
        r.removeFromRight(sp.xs);
        pan_.setBounds(r);
    }
    y += sp.xs;
    auto toggles = row({}, rowH);
    std::vector<juce::TextButton*> visible;
    for (auto* b : {&mute_, &solo_, &arm_})
        if (b->isVisible())
            visible.push_back(b);
    const int n = static_cast<int>(visible.size());
    for (int i = 0; i < n; ++i)
    {
        const int bw = (toggles.getWidth() - (n - 1 - i) * sp.xs) / (n - i);
        visible[static_cast<size_t>(i)]->setBounds(toggles.removeFromLeft(bw));
        toggles.removeFromLeft(sp.xs);
    }
    close();

    // A MIDI track: what its recorded passes become.
    if (t->isMidi())
    {
        open("RECORDING", {});
        recordMode_.setBounds(row({}, rowH));
        close();
    }
}

void InspectorPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!onCommand || track() == nullptr)
        return;
    const int i = swatchAt(e.getPosition());
    if (i >= 0)
        onCommand(std::make_unique<core::SetColour>(
            trackId_, theme_.track.palette[static_cast<size_t>(i)].value.getARGB()));
}

void InspectorPanel::mouseMove(const juce::MouseEvent& e)
{
    const int i = swatchAt(e.getPosition());
    if (i == hoverSwatch_)
        return;
    hoverSwatch_ = i;
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip(i >= 0 ? juce::String(theme_.track.palette[static_cast<size_t>(i)].name) : juce::String());
    repaint(swatchArea_.expanded(theme_.spacing.xs));
}

void InspectorPanel::mouseExit(const juce::MouseEvent&)
{
    if (hoverSwatch_ < 0)
        return;
    hoverSwatch_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(swatchArea_.expanded(theme_.spacing.xs));
}

} // namespace northlight::ui
