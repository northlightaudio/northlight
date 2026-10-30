#pragma once

// One channel's strip in the mixer (screens/mixer.md): internal to the mixer,
// included by MixerView.cpp only.

#include "core/Command.h"
#include "engine/Snapshot.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ColourMenu.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/InstrumentOutputs.h"
#include "ui/MixerView.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PluginMenu.h"
#include "ui/SendMenu.h"
#include "ui/SidechainMenu.h"
#include "ui/components/Accessibility.h"
#include "ui/components/AddSlot.h"
#include "ui/mixer/StripParts.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

namespace northlight::ui
{

class MixerView::Strip final : public juce::Component, public juce::DragAndDropTarget
{
public:
    // A group named for its channel, holding its controls (11.3).
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
    }

    Strip(MixerView& owner, const Theme& theme)
        : owner_(owner), theme_(theme), cap_(theme), stripRow_(theme), addInsert_(theme, "Insert"),
          addSend_(theme, "Send"), insertsSummary_(theme), sendsSummary_(theme), pan_(theme), fader_(theme),
          meter_(theme, 2, true), value_(theme)
    {
        setOpaque(true);
        setTitle("Track");
        // A short strip's sections, collapsed: a click lists their slots.
        insertsSummary_.onClick = [this] { showInsertsList(); };
        insertsSummary_.setTooltip("The inserts: click to list them");
        addChildComponent(insertsSummary_);
        sendsSummary_.onClick = [this] { showSendsList(); };
        sendsSummary_.setTooltip("The sends: click to list them");
        addChildComponent(sendsSummary_);
        stripRow_.onToggle = [this](core::Id moduleId, bool on)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetInsertBypass>(id_, moduleId, !on));
        };
        stripRow_.onEdit = [this]
        {
            if (owner_.onShowStrip)
                owner_.onShowStrip(id_);
        };
        addAndMakeVisible(stripRow_);
        setWantsKeyboardFocus(true);
        cap_.onRenamed = [this](const juce::String& name)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetTrackName>(id_, name.toStdString()));
        };
        cap_.onClicked = [this]
        {
            if (owner_.onSelectChannel)
                owner_.onSelectChannel(id_);
        };
        cap_.onMenu = [this] { showGroupMenu(); };
        // The cap's edit button: this channel in the Editor panel.
        cap_.onEdit = [this]
        {
            if (owner_.onShowStrip)
                owner_.onShowStrip(id_);
        };
        cap_.setEditable(true);
        addAndMakeVisible(cap_);

        input_.setTooltip("Input");
        input_.onChange = [this]
        {
            if (updating_)
                return;
            if (midi_)
            {
                const int index = input_.getSelectedId() - 2;
                owner_.onCommand(std::make_unique<core::SetTrackMidiInput>(
                    id_,
                    index >= 0 && static_cast<size_t>(index) < owner_.midiInputNames_.size()
                        ? owner_.midiInputNames_[static_cast<size_t>(index)]
                        : std::string{}));
            }
            else if (const auto choice = inputs::selected(input_, owner_.inputChoices_))
                owner_.onCommand(std::make_unique<core::SetTrackInput>(id_, choice->channel, choice->bus));
        };
        addAndMakeVisible(input_);

        insertsView_.setViewedComponent(&insertsHolder_, false);
        insertsView_.setScrollBarsShown(false, false, true, false);
        insertsView_.setScrollBarThickness(4);
        addAndMakeVisible(insertsView_);
        instrument_.setTooltip("The instrument this track's MIDI plays");
        addChildComponent(instrument_);
        addInstrument_.setTooltip("Choose the instrument this track's MIDI plays");
        addInstrument_.onClick = [this] { showInstrumentMenu(addInstrument_); };
        addChildComponent(addInstrument_);
        addInsert_.setTooltip("Add an insert");
        addInsert_.onClick = [this] { showAddInsertMenu(); };
        insertsHolder_.addAndMakeVisible(addInsert_);
        addSend_.setTooltip("Add a send");
        addSend_.onClick = [this] { showAddSendMenu(); };
        addAndMakeVisible(addSend_);

        pan_.setRange(-1.0, 1.0, 0.0);
        pan_.setShowValue(true);
        pan_.setTooltip("Pan");
        pan_.format = [](double v) { return panText(v); };
        pan_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Pan");
        };
        pan_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        pan_.onValueChanged = [this](double v)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetTrackPan>(id_, static_cast<float>(v)));
        };
        addAndMakeVisible(pan_);

        const auto toggle = [this](juce::TextButton& b,
                                   const char* text,
                                   const char* semantic,
                                   const char* tip,
                                   auto makeCommand)
        {
            b.setButtonText(juce::String::fromUTF8(text));
            b.setClickingTogglesState(true);
            b.getProperties().set(NorthlightLookAndFeel::propertySemantic, semantic);
            b.setTooltip(tip);
            b.onClick = [this, &b, makeCommand]
            {
                if (updating_)
                    return;
                // Ctrl+click on solo: this channel alone (a second one clears it).
                if (&b == &solo_ && juce::ModifierKeys::getCurrentModifiers().isCommandDown())
                {
                    owner_.onCommand(std::make_unique<core::SetExclusiveSolo>(id_));
                    return;
                }
                owner_.onCommand(makeCommand(id_, group_, b.getToggleState()));
            };
            addAndMakeVisible(b);
        };
        toggle(mute_,
               "M",
               "mute",
               "Mute",
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupMute>(group, on);
                   return std::make_unique<core::SetTrackMute>(id, on);
               });
        toggle(solo_,
               "S",
               "solo",
               "Solo (Ctrl+click: solo this alone)",
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupSolo>(group, on);
                   return std::make_unique<core::SetTrackSolo>(id, on);
               });
        toggle(arm_,
               "R",
               "arm",
               "Arm for recording",
               [](core::Id id, core::Id, bool on) -> std::unique_ptr<core::Command>
               { return std::make_unique<core::SetTrackArmed>(id, on); });
        arm_.getProperties().set(NorthlightLookAndFeel::propertyHardOn, true);
        toggle(phase_,
               "\xC3\x98",
               "monitor",
               "Phase invert",
               [](core::Id id, core::Id, bool on) -> std::unique_ptr<core::Command>
               { return std::make_unique<core::SetPhaseInvert>(id, on); });
        phase_.getProperties().set(NorthlightLookAndFeel::propertySemantic, "phase");

        fader_.setMaxDb(static_cast<float>(maxFaderDb));
        fader_.onDragStart = [this]
        {
            breakLink_ = juce::ModifierKeys::getCurrentModifiers().isAltDown();
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Fader");
        };
        fader_.onDragEnd = [this]
        {
            breakLink_ = false;
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        fader_.onGainChanged = [this](float gain) { gainChanged(gain); };
        addAndMakeVisible(fader_);
        addAndMakeVisible(meter_);

        value_.setRange(db::silence, maxFaderDb, 0.1);
        value_.setDecimals(1);
        value_.setUnit("dB");
        value_.format = [](double v) { return db::text(v); };
        value_.parse = [](const juce::String& t) { return db::parse(t); };
        value_.setTooltip("Gain");
        value_.onDragStart = fader_.onDragStart;
        value_.onDragEnd = fader_.onDragEnd;
        value_.onValueChanged = [this](double v) { gainChanged(db::toGain(v)); };
        addAndMakeVisible(value_);

        output_.setTooltip("Output");
        output_.onChange = [this]
        {
            if (updating_)
                return;
            // Ids: 1 the master, 2.. the buses, hardwareBase + pair a device output.
            if (const int id = output_.getSelectedId(); id >= hardwareBase)
            {
                owner_.onCommand(std::make_unique<core::SetHardwareOutput>(id_, id - hardwareBase));
                return;
            }
            const int index = output_.getSelectedId() - 2; // id 1 = master
            const core::Id bus = index >= 0 && static_cast<size_t>(index) < busIds_.size()
                                     ? busIds_[static_cast<size_t>(index)]
                                     : core::invalidId;
            owner_.onCommand(std::make_unique<core::SetOutputBus>(id_, bus));
        };
        addAndMakeVisible(output_);
    }

    // ----- drops: a browser row or an insert slot, at the line ------------------------------
    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        const auto d = details.description.toString();
        return d.startsWith("plugin:") || d.startsWith("insert:");
    }
    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override
    {
        dropIndex_ = dropIndexAt(details.localPosition);
        reorderLineY_ =
            collapsed_ ? -1 : getLocalPoint(&insertsHolder_, juce::Point<int>(0, dropIndex_ * slotHeight)).y;
        dropping_ = true;
        repaint();
    }
    void itemDragExit(const SourceDetails&) override
    {
        dropping_ = false;
        reorderLineY_ = -1;
        repaint();
    }
    void itemDropped(const SourceDetails& details) override
    {
        const int index = dropIndexAt(details.localPosition);
        dropping_ = false;
        reorderLineY_ = -1;
        repaint();
        // After the drop's callback has unwound: the commands rebuild the strips.
        juce::Component::SafePointer<MixerView> owner(&owner_);
        const auto description = details.description.toString();
        const bool copy = juce::ModifierKeys::getCurrentModifiers().isCommandDown();
        juce::MessageManager::callAsync(
            [owner, id = id_, description, index, copy]
            {
                if (owner != nullptr)
                    owner->dropOnStrip(id, description, index, copy);
            });
    }

    core::Id id() const noexcept { return id_; }
    int meterSlot() const noexcept { return meterSlot_; }
    bool isBus() const noexcept { return isBus_; }

    // A values refresh (a fader drag, 20 times a second): only what live
    // commands change (fader, pan, the toggles, bypass states), with no combo
    // refill, no new callbacks and no relayout.
    void setValues(const core::Channel& channel)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        pan_.setValue(channel.pan, juce::dontSendNotification);
        currentGain_ = channel.gain;
        fader_.setGain(channel.gain, juce::dontSendNotification);
        value_.setValue(db::fromGain(channel.gain), juce::dontSendNotification);
        showPlayed();
        showToggle(mute_, channel.mute);
        showToggle(solo_, channel.solo);
        showToggle(phase_, channel.phaseInvert);
        stripRow_.set(channel);
        if (channel.hasInstrument())
            instrument_.setBypassed(channel.instrument.bypass);
        for (size_t i = 0; i < inserts_.size() && i < channel.inserts.size(); ++i)
            inserts_[i]->setBypassed(channel.inserts[i].bypass);
        for (size_t i = 0; i < sends_.size() && i < channel.sends.size() && i < sendNames_.size(); ++i)
        {
            const auto& s = channel.sends[i];
            sends_[i]->set(sendNames_[i], s.level, s.preFader, s.pan, s.mute);
        }
    }

    void setChannel(const core::Channel& channel, bool isBus, const core::Project& project, int slot)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        id_ = channel.id;
        isBus_ = isBus;
        meterSlot_ = slot;
        const auto* track = isBus ? nullptr : project.findTrack(channel.id);
        midi_ = track != nullptr && track->isMidi();
        aux_ = track != nullptr && track->isAux();
        const auto* busModel = isBus ? project.findBus(channel.id) : nullptr;
        fx_ = busModel != nullptr && busModel->fx;
        colour_ = theme_.track.forChannel(project, channel.id);
        fader_.setTrackColour(colour_);
        group_ = channel.group;
        const auto* group = project.findGroup(channel.group);
        int groupNumber = 0;
        for (size_t i = 0; i < project.groups.size(); ++i)
            if (project.groups[i].id == channel.group)
                groupNumber = static_cast<int>(i) + 1;
        const auto stripName =
            juce::String::fromUTF8(channel.name.empty() ? (isBus ? "Bus" : "Track") : channel.name.c_str());
        // The strip is a group named for its channel, and each control names it
        // too ("Mute Bass", not "M"): accessibility-performance.md 11.3.
        setTitle(stripName);
        cap_.setEditTitle("Edit " + stripName + " in the Editor panel");
        titleForTrack(mute_, "Mute", stripName);
        titleForTrack(solo_, "Solo", stripName);
        titleForTrack(arm_, "Arm", stripName);
        titleForTrack(phase_, "Invert phase", stripName);
        titleForTrack(fader_, "Gain", stripName);
        titleForTrack(pan_, "Pan", stripName);
        titleForTrack(meter_, "Level", stripName);
        titleForTrack(value_, "Gain value", stripName);
        titleForTrack(input_, "Input", stripName);
        titleForTrack(output_, "Output", stripName);
        cap_.set(stripName,
                 colour_,
                 id_ == owner_.selected_,
                 groupNumber,
                 group != nullptr && group->colour != 0 ? juce::Colour(group->colour)
                                                        : theme_.colours.accent);
        pan_.setValue(channel.pan, juce::dontSendNotification);
        currentGain_ = channel.gain;
        fader_.setGain(channel.gain, juce::dontSendNotification);
        value_.setValue(db::fromGain(channel.gain), juce::dontSendNotification);
        showPlayed();
        showToggle(mute_, channel.mute);
        showToggle(solo_, channel.solo);
        showToggle(phase_, channel.phaseInvert);
        arm_.setVisible(track != nullptr && !aux_ && !track->isInstrument()); // its MIDI tracks arm
        showToggle(arm_, track != nullptr && track->armed);

        // The input (tracks only): audio inputs or MIDI inputs.
        input_.setVisible(track != nullptr && !aux_ && !track->isInstrument());
        input_.clear(juce::dontSendNotification);
        if (track != nullptr && midi_)
        {
            input_.addItem("All MIDI", 1);
            int selected = 1;
            for (size_t i = 0; i < owner_.midiInputNames_.size(); ++i)
            {
                input_.addItem(juce::String::fromUTF8(owner_.midiInputNames_[i].c_str()),
                               static_cast<int>(i) + 2);
                if (owner_.midiInputNames_[i] == track->midiInput)
                    selected = static_cast<int>(i) + 2;
            }
            input_.setSelectedId(selected, juce::dontSendNotification);
        }
        else if (track != nullptr)
            inputs::fill(input_, owner_.inputChoices_, *track);

        busIds_.clear();
        output_.clear(juce::dontSendNotification);
        output_.addItem("Master", 1);
        int id = 2;
        for (const auto& b : project.buses)
        {
            busIds_.push_back(b.id);
            output_.addItem(juce::String::fromUTF8(b.name.empty() ? "Bus" : b.name.c_str()), id);
            // Itself, or a bus that already feeds this one, would make a loop: greyed.
            output_.setItemEnabled(id, !project.wouldCycle(channel.id, b.id));
            ++id;
        }
        int selected = 1;
        for (size_t i = 0; i < busIds_.size(); ++i)
            if (busIds_[i] == channel.outputBus)
                selected = static_cast<int>(i) + 2;
        // The device's other output pairs: straight out, past the master (its own
        // pair, 1/2, is the master's). One the device lacks stays listed, greyed.
        const auto& pairs = owner_.outputPairs_;
        if (pairs.size() > 1 || channel.hardwareOutput >= 0)
        {
            output_.addSectionHeading("Hardware outputs");
            for (size_t p = 1; p < pairs.size(); ++p)
                output_.addItem(juce::String::fromUTF8(pairs[p].c_str()), hardwareBase + static_cast<int>(p));
            if (channel.hardwareOutput >= static_cast<int>(std::max<size_t>(pairs.size(), 1)))
            {
                const int missing = hardwareBase + channel.hardwareOutput;
                output_.addItem("Out " + juce::String(outputPairName(channel.hardwareOutput)) +
                                    " (not on this device)",
                                missing);
                output_.setItemEnabled(missing, false);
            }
        }
        if (channel.hardwareOutput >= 0)
            selected = hardwareBase + channel.hardwareOutput;
        output_.setSelectedId(selected, juce::dontSendNotification);
        const bool absent = channel.hardwareOutput >= static_cast<int>(std::max<size_t>(pairs.size(), 1));
        output_.setTooltip(absent ? "Out " + juce::String(outputPairName(channel.hardwareOutput)) +
                                        " is not on this device: it plays to its bus or the master meanwhile"
                           : channel.hardwareOutput >= 0
                               ? "Output: straight to the device, past the master (an export mixes it in)"
                               : "Output");
        soloSafe_ = channel.soloSafe;
        solo_.setTooltip(soloSafe_
                             ? "Solo (solo safe: other solos never silence it; Ctrl+click: solo this alone)"
                             : "Solo (Ctrl+click: solo this alone)");
        // Its plugins' delay (shown when there is one).
        latency_ = 0;
        for (const auto* chained : channel.processingChain())
            if (const auto live = owner_.liveInsert ? owner_.liveInsert(chained->id) : nullptr)
                latency_ += std::max(0, live->latencySamples());
        latencyText_ = latency_ > 0 ? "+" + latencyMs(latency_, project.sampleRate) : juce::String();
        sampleRate_ = project.sampleRate;

        stripRow_.set(channel);
        stripRow_.setVisible(stripRow_.hasModules());

        // The instrument slot (a MIDI track's): filled, or an add slot.
        const bool instrumentSlot = track != nullptr && (track->isInstrument() || track->hasInstrument());
        instrumentSlot_ = instrumentSlot;
        instrument_.setVisible(instrumentSlot && track->hasInstrument());
        addInstrument_.setVisible(instrumentSlot && !track->hasInstrument());
        if (instrumentSlot && track->hasInstrument())
        {
            const auto& inst = track->instrument;
            const core::Id instrumentId = inst.id;
            juce::String badge = "INST";
            if (hosting::PluginRegistry::isHostedType(inst.type))
                badge = inst.type.rfind("clap:", 0) == 0 ? "CLAP" : "VST3";
            instrument_.set(
                juce::String::fromUTF8(insertName(inst, owner_.actions()).c_str()), badge, inst.bypass);
            instrument_.setTrouble(insertTrouble(inst, owner_.actions()));
            instrument_.onBadge = badgeAction(inst.type);
            instrument_.dragDescription = nullptr;
            instrument_.onOpen = [this, instrumentId] { owner_.openInsertEditor(id_, instrumentId); };
            instrument_.onBypass = [this, instrumentId]
            {
                if (const auto* ch = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr)
                    if (const auto* ins = ch->findInsert(instrumentId))
                        owner_.onCommand(
                            std::make_unique<core::SetInsertBypass>(id_, instrumentId, !ins->bypass));
            };
            instrument_.onMenu = [this, instrumentId] { showInsertMenu(instrumentId); };
            instrument_.onReorder = nullptr;
        }

        // Insert slots.
        while (inserts_.size() > channel.inserts.size())
            inserts_.pop_back();
        while (inserts_.size() < channel.inserts.size())
        {
            auto made = std::make_unique<InsertSlot>(theme_);
            insertsHolder_.addAndMakeVisible(*made);
            inserts_.push_back(std::move(made));
        }
        bool troubled = false;
        for (size_t i = 0; i < channel.inserts.size(); ++i)
        {
            const auto& insert = channel.inserts[i];
            const core::Id insertId = insert.id;
            juce::String badge;
            if (hosting::PluginRegistry::isHostedType(insert.type))
            {
                const auto* info =
                    owner_.host_ != nullptr ? owner_.host_->registry().findByType(insert.type) : nullptr;
                badge = info != nullptr && info->isInstrument ? "INST"
                        : insert.type.rfind("clap:", 0) == 0  ? "CLAP"
                                                              : "VST3";
            }
            else if (plugins::BuiltInFactory::isInstrument(insert.type))
                badge = "INST";
            inserts_[i]->set(
                juce::String::fromUTF8(insertName(insert, owner_.actions()).c_str()), badge, insert.bypass);
            inserts_[i]->setTrouble(insertTrouble(insert, owner_.actions()));
            troubled = troubled || inserts_[i]->troubled();
            inserts_[i]->onBadge = badgeAction(insert.type);
            inserts_[i]->dragDescription = [this, insertId]
            {
                return "insert:" + juce::String(static_cast<juce::int64>(id_)) + ":" +
                       juce::String(static_cast<juce::int64>(insertId));
            };
            inserts_[i]->onOpen = [this, insertId] { owner_.openInsertEditor(id_, insertId); };
            inserts_[i]->onBypass = [this, insertId]
            {
                if (const auto* ch = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr)
                    if (const auto* ins = ch->findInsert(insertId))
                        owner_.onCommand(
                            std::make_unique<core::SetInsertBypass>(id_, insertId, !ins->bypass));
            };
            inserts_[i]->onMenu = [this, insertId] { showInsertMenu(insertId); };
            inserts_[i]->onReorder = [this, insertId](juce::Point<int> screen, bool released)
            { reorderInsert(insertId, screen, released); };
        }
        insertCount_ = static_cast<int>(channel.inserts.size());
        insertsSummary_.set(juce::String(insertCount_) + (insertCount_ == 1 ? " insert" : " inserts"),
                            troubled);

        // Send rows.
        while (sends_.size() > channel.sends.size())
            sends_.pop_back();
        while (sends_.size() < channel.sends.size())
        {
            auto row = std::make_unique<SendRow>(theme_);
            addAndMakeVisible(*row);
            sends_.push_back(std::move(row));
        }
        sendNames_.clear();
        for (size_t i = 0; i < channel.sends.size(); ++i)
        {
            const auto& send = channel.sends[i];
            const auto* bus = project.findBus(send.bus);
            const core::Id busId = send.bus;
            const bool pre = send.preFader;
            const float level = send.level;
            sendNames_.push_back(
                juce::String::fromUTF8(bus != nullptr && !bus->name.empty() ? bus->name.c_str() : "Bus"));
            sends_[i]->set(sendNames_.back(), level, pre, send.pan, send.mute);
            sends_[i]->setShowPan(owner_.wide_);
            sends_[i]->onLevel = [this, busId, pre](float value)
            { owner_.onCommand(std::make_unique<core::SetSend>(id_, busId, value, pre)); };
            sends_[i]->onPan = [this, busId](float value)
            { owner_.onCommand(std::make_unique<core::SetSendPan>(id_, busId, value)); };
            sends_[i]->onTogglePre = [this, busId, level, pre]
            { owner_.onCommand(std::make_unique<core::SetSend>(id_, busId, level, !pre)); };
            sends_[i]->onMenu = [this, busId, row = sends_[i].get()] { showSendMenu(busId, row); };
            sends_[i]->onGestureBegin = [this]
            {
                if (owner_.onGestureBegin)
                    owner_.onGestureBegin("Send level");
            };
            sends_[i]->onGestureEnd = [this]
            {
                if (owner_.onGestureEnd)
                    owner_.onGestureEnd();
            };
        }
        sendCount_ = static_cast<int>(channel.sends.size());
        sendsSummary_.set(juce::String(sendCount_) + (sendCount_ == 1 ? " send" : " sends"), false);
        // Without a bus to send to, the slot offers a new FX track.
        addSend_.setEnabled(true);
        meter_.setShowScale(owner_.wide_);
        feedsSoloedBus_ = false;
        for (const auto& b : project.buses)
            if (b.solo && (channel.outputBus == b.id ||
                           std::any_of(channel.sends.begin(),
                                       channel.sends.end(),
                                       [&](const core::Send& s) { return s.bus == b.id; })))
                feedsSoloedBus_ = true;
        resized();
        repaint();
    }

    void pushMeter(const rt::MeterBlock::Reading& r, double elapsedMs)
    {
        const float peaks[2] = {r.peakLeft, r.peakRight};
        meter_.push(peaks, 2, elapsedMs);
        stripRow_.updateReductions(owner_.liveInsert);
        showPlayed();
    }

    // An automated fader and pan show what their lanes play. Every refresh ends
    // here, so a model refresh (another strip's fader drag) never shows the static
    // value for a frame before the next meter tick moves it back.
    void showPlayed()
    {
        if (owner_.automatedValues)
            showAutomated(owner_.automatedValues(id_));
    }

    // What this strip needs of the shared row plan, and where its fader band
    // and gain field landed (for the master to match).
    void addTo(RowPlan& rows) const
    {
        rows.input = rows.input || input_.isVisible();
        rows.stripRow = rows.stripRow || stripRow_.isVisible();
        rows.instrument = rows.instrument || instrumentSlot_;
        rows.inserts = std::max(rows.inserts, insertCount_);
        rows.sends = std::max(rows.sends, sendCount_);
        rows.latency = rows.latency || latency_ > 0;
    }
    juce::Rectangle<int> faderBand() const { return fader_.getBounds().getUnion(meter_.getBounds()); }
    int valueY() const { return value_.getY(); }

    // The fader and pan follow what automation plays (unless the user holds them).
    void showAutomated(const MixerView::Automated& a)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        if (a.gain && !fader_.isMouseButtonDown() && !value_.isMouseButtonDown())
        {
            fader_.setGain(a.gainValue, juce::dontSendNotification);
            value_.setValue(db::fromGain(a.gainValue), juce::dontSendNotification);
        }
        if (a.pan && !pan_.isMouseButtonDown())
            pan_.setValue(a.panValue, juce::dontSendNotification);
        if (a.gain != automatedShown_)
        {
            automatedShown_ = a.gain;
            fader_.setTooltip(a.gain ? "Gain (automated: the fader follows its lane)" : "Gain");
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        auto bounds = getLocalBounds().toFloat().reduced(0.0f, 0.5f);
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.sm);
        if (id_ == owner_.selected_)
        {
            g.setColour(c.accent);
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
        }
        else if (feedsSoloedBus_)
        {
            g.setColour(c.solo.withAlpha(0.5f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
        }
        // A drop on a collapsed strip goes at the end of its inserts: the summary lights.
        if (dropping_ && collapsed_)
        {
            g.setColour(c.accent);
            g.drawRoundedRectangle(
                insertsSummary_.getBounds().toFloat().expanded(0.5f), theme_.radius.sm, 2.0f);
        }
        // Its plugins' delay, under the output.
        if (!latencyArea_.isEmpty() && latencyText_.isNotEmpty())
        {
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary);
            g.drawText(latencyText_, latencyArea_, juce::Justification::centred);
        }
        if (hasKeyboardFocus(false))
            if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
                laf->drawFocusRing(g, *this, bounds.reduced(1.0f), theme_.radius.sm);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        // The insertion line of a drag (a slot or a browser row).
        if (reorderLineY_ >= 0)
        {
            g.setColour(c.accent);
            g.fillRect(insertsView_.getX(), reorderLineY_ - 1, insertsView_.getWidth(), 2);
        }
        // Solo safe: a dot in the solo colour in the S button's corner.
        if (soloSafe_)
        {
            const auto b = solo_.getBounds().toFloat();
            g.setColour(c.solo);
            g.fillEllipse(
                juce::Rectangle<float>(5.0f, 5.0f).withCentre({b.getRight() - 5.0f, b.getY() + 5.0f}));
        }
    }

    void resized() override
    {
        const int pad = theme_.spacing.xs;
        const int control = theme_.sizes.controlCompact;
        auto area = getLocalBounds().reduced(pad, 0);
        cap_.setBounds(getLocalBounds().withHeight(control));
        area.removeFromTop(control + pad);
        // Rows another strip has are reserved here too (empty), so every strip's
        // sections line up across the mixer.
        const auto& rows = owner_.rows_;
        if (rows.input || input_.isVisible())
        {
            input_.setBounds(area.removeFromTop(control));
            area.removeFromTop(pad);
        }
        if (rows.stripRow || stripRow_.isVisible())
        {
            stripRow_.setBounds(area.removeFromTop(16));
            area.removeFromTop(pad);
        }
        if (rows.instrument || instrumentSlot_)
        {
            const auto row = area.removeFromTop(slotHeight);
            instrument_.setBounds(row.withTrimmedBottom(1));
            addInstrument_.setBounds(row.withTrimmedBottom(1));
            area.removeFromTop(pad);
        }
        // The anatomy (screens/mixer.md): inserts, sends, pan, the toggles, then
        // the fader, value and output. The fixed rows: pan, toggles, value,
        // output and, when a strip shows one, the latency line; the fader takes
        // the rest between 120 and 200 px, and below 120 the sections collapse.
        // (The last term is the room under the output selector.)
        const int latencyH = rows.latency ? latencyRow : 0;
        const int bottom = theme_.sizes.knob + 16 + pad + control + pad + pad + control + pad + control +
                           latencyH + theme_.spacing.sm;
        const int insertsFull =
            std::min(std::max(rows.inserts, insertCount_) + 1, maxVisibleSlots) * slotHeight + 4;
        const int sendsFull = (std::max(rows.sends, sendCount_) + 1) * slotHeight + 4;
        int faderTravel = area.getHeight() - bottom - insertsFull - sendsFull - 2 * pad;
        collapsed_ = faderTravel < minFaderTravel;
        const int insertsH = collapsed_ ? slotHeight : insertsFull;
        const int sendsH = collapsed_ ? slotHeight : sendsFull;
        faderTravel = std::clamp(area.getHeight() - bottom - insertsH - sendsH - 2 * pad, 24, maxFaderTravel);

        auto insertsArea = area.removeFromTop(insertsH);
        area.removeFromTop(pad);
        auto sendsArea = area.removeFromTop(sendsH);
        area.removeFromTop(pad);
        insertsSummary_.setBounds(insertsArea.withTrimmedBottom(1));
        sendsSummary_.setBounds(sendsArea.withTrimmedBottom(1));
        insertsSummary_.setVisible(collapsed_);
        sendsSummary_.setVisible(collapsed_);
        insertsView_.setVisible(!collapsed_);
        addSend_.setVisible(!collapsed_);
        for (auto& s : sends_)
            s->setVisible(!collapsed_);
        if (!collapsed_)
        {
            insertsView_.setBounds(insertsArea.reduced(0, 2));
            insertsHolder_.setSize(insertsView_.getWidth(), (insertCount_ + 1) * slotHeight);
            int y = 0;
            for (auto& slot : inserts_)
            {
                slot->setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
                y += slotHeight;
            }
            addInsert_.setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
            sendsArea.reduce(0, 2);
            for (auto& row : sends_)
                row->setBounds(sendsArea.removeFromTop(slotHeight).withTrimmedBottom(1));
            addSend_.setBounds(sendsArea.removeFromTop(slotHeight).withTrimmedBottom(1));
        }

        pan_.setBounds(area.removeFromTop(theme_.sizes.knob + 16)
                           .withSizeKeepingCentre(theme_.sizes.knob + 16, theme_.sizes.knob + 16));
        area.removeFromTop(pad);
        auto toggles = area.removeFromTop(control);
        const int count = arm_.isVisible() ? 4 : 3;
        const int tw = (toggles.getWidth() - (count - 1) * theme_.spacing.xxs) / count;
        mute_.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(theme_.spacing.xxs);
        solo_.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(theme_.spacing.xxs);
        if (arm_.isVisible())
        {
            arm_.setBounds(toggles.removeFromLeft(tw));
            toggles.removeFromLeft(theme_.spacing.xxs);
        }
        phase_.setBounds(toggles);
        area.removeFromTop(pad);

        // The fader and the meter beside it (a wide strip puts the dB scale
        // between them).
        auto faderArea = area.removeFromTop(faderTravel + 2 * pad);
        const int meterW = meter_.barsWidth() + (owner_.wide_ ? meter_.scaleWidth() : 0);
        auto meterArea = faderArea.removeFromRight(meterW);
        meter_.setBounds(meterArea.withTrimmedTop(pad));
        faderArea.removeFromRight(pad);
        fader_.setBounds(faderArea);
        area.removeFromTop(pad);
        value_.setBounds(area.removeFromTop(control));
        area.removeFromTop(pad);
        output_.setBounds(area.removeFromTop(control));
        latencyArea_ = latencyH > 0 ? area.removeFromTop(latencyH) : juce::Rectangle<int>();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress('m') || key == juce::KeyPress('M'))
            mute_.triggerClick();
        else if (key == juce::KeyPress('s') || key == juce::KeyPress('S'))
            solo_.triggerClick();
        else if ((key == juce::KeyPress('r') || key == juce::KeyPress('R')) && arm_.isVisible())
            arm_.triggerClick();
        else
            return fader_.keyPressed(key);
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    // Where a drop at `local` goes in the inserts: at the slot boundary under it,
    // or at the end over the rest of the strip (or a collapsed one).
    int dropIndexAt(juce::Point<int> local) const
    {
        if (collapsed_ || !insertsView_.getBounds().contains(local))
            return insertCount_;
        const auto inHolder = insertsHolder_.getLocalPoint(this, local);
        return std::clamp(
            static_cast<int>(std::lround(static_cast<double>(inHolder.y) / slotHeight)), 0, insertCount_);
    }

    // A click on a slot's format badge: the plugin browser, searching for it.
    std::function<void()> badgeAction(const std::string& type)
    {
        const auto* actions = owner_.actions();
        if (!hosting::PluginRegistry::isHostedType(type) || actions == nullptr || !actions->showInBrowser)
            return nullptr;
        return [actions, type] { actions->showInBrowser(type); };
    }

    // The collapsed inserts: each with its menu, the instrument first, then "Add insert".
    void showInsertsList()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto* channel = owner_.project_->findChannel(id_);
        if (channel == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        const auto addSlot = [&](const core::Insert& insert)
        {
            juce::PopupMenu sub;
            addInsertMenuItems(sub, actions, owner_.insertMenuRequest(id_, insert.id, !isBus_));
            const auto trouble = insertTrouble(insert, owner_.actions());
            juce::String name = juce::String::fromUTF8(insertName(insert, owner_.actions()).c_str());
            if (trouble.any())
                name << "  (" << trouble.label() << ")";
            else if (insert.bypass)
                name << "  (bypassed)";
            menu.addSubMenu(name, sub);
        };
        if (channel->hasInstrument())
            addSlot(channel->instrument);
        for (const auto& insert : channel->inserts)
            addSlot(insert);
        if (!channel->inserts.empty() || channel->hasInstrument())
            menu.addSeparator();
        auto built =
            buildPluginMenu(PluginKind::effect, owner_.pluginSources_, static_cast<int>(actions.size()) + 1);
        juce::Component::SafePointer<MixerView> owner(&owner_);
        for (const auto& type : built.types)
            actions.push_back(
                [owner, id = id_, type]
                {
                    if (owner != nullptr)
                        owner->addInsert(id, type);
                });
        menu.addSubMenu("Add insert", built.menu);
        showList(menu, actions, insertsSummary_);
    }

    // The collapsed sends: each with its menu, then "Add send".
    void showSendsList()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto* channel = owner_.project_->findChannel(id_);
        if (channel == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        for (const auto& send : channel->sends)
        {
            juce::PopupMenu sub;
            addSendItems(sub, actions, send);
            const auto* bus = owner_.project_->findBus(send.bus);
            juce::String name =
                juce::String::fromUTF8(bus != nullptr && !bus->name.empty() ? bus->name.c_str() : "Bus");
            name << "  " << db::gainText(send.level) << " dB" << (send.mute ? " (muted)" : "");
            menu.addSubMenu(name, sub);
        }
        if (!channel->sends.empty())
            menu.addSeparator();
        juce::PopupMenu add;
        addSendTargets(add, actions);
        menu.addSubMenu("Add send", add);
        showList(menu, actions, sendsSummary_);
    }

    static void
    showList(juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, juce::Component& target)
    {
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                           [actions](int result)
                           {
                               if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                                   actions[static_cast<size_t>(result) - 1]();
                           });
    }

    // One send's items: where it taps, mute, its pan and removal.
    void
    addSendItems(juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, const core::Send& send)
    {
        juce::Component::SafePointer<MixerView> owner(&owner_);
        const core::Id id = id_, busId = send.bus;
        const auto command = [owner](std::unique_ptr<core::Command> c)
        {
            if (owner != nullptr && owner->onCommand)
                owner->onCommand(std::move(c));
        };
        const auto next = [&] { return static_cast<int>(actions.size()) + 1; };
        menu.addItem(next(), "Pre-fader", true, send.preFader);
        actions.push_back([command, id, busId, level = send.level, pre = send.preFader]
                          { command(std::make_unique<core::SetSend>(id, busId, level, !pre)); });
        menu.addItem(next(), "Mute send", true, send.mute);
        actions.push_back([command, id, busId, mute = send.mute]
                          { command(std::make_unique<core::SetSendMute>(id, busId, !mute)); });
        juce::PopupMenu pan;
        for (const float p : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
        {
            pan.addItem(next(), panText(p), true, std::abs(send.pan - p) < 0.01f);
            actions.push_back([command, id, busId, p]
                              { command(std::make_unique<core::SetSendPan>(id, busId, p)); });
        }
        menu.addSubMenu("Pan: " + panText(send.pan), pan);
        menu.addSeparator();
        menu.addItem(next(), "Remove send");
        actions.push_back([command, id, busId] { command(std::make_unique<core::RemoveSend>(id, busId)); });
    }

    // The buses this channel can send to, and a new FX track for it.
    void addSendTargets(juce::PopupMenu& menu, std::vector<std::function<void()>>& actions)
    {
        auto built = buildSendMenu(*owner_.project_, id_, static_cast<int>(actions.size()) + 1);
        juce::Component::SafePointer<MixerView> owner(&owner_);
        for (const auto bus : built.targets)
            actions.push_back(
                [owner, id = id_, bus]
                {
                    if (owner != nullptr && owner->onCommand)
                        owner->onCommand(std::make_unique<core::SetSend>(id, bus, 1.0f, false));
                });
        menu = built.menu;
        menu.addSeparator();
        owner_.addFxSendItems(menu, actions, id_);
    }

    void showSendMenu(core::Id busId, juce::Component* row)
    {
        const auto* channel = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr;
        const auto* send = channel != nullptr ? channel->findSend(busId) : nullptr;
        if (send == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        addSendItems(menu, actions, *send);
        showList(menu, actions, *row);
    }

    void gainChanged(float gain)
    {
        if (updating_)
            return;
        // In a group the move is relative: every member scales by the same ratio.
        // A member sitting at -inf has no ratio, so it moves alone; Alt breaks the link.
        if (group_ != core::invalidId && currentGain_ > 1.0e-6f && !breakLink_)
            owner_.onCommand(std::make_unique<core::SetGroupGain>(group_, gain / currentGain_));
        else
            owner_.onCommand(std::make_unique<core::SetTrackGain>(id_, gain));
    }

    void reorderInsert(core::Id insertId, juce::Point<int> screen, bool released)
    {
        const auto local = insertsHolder_.getLocalPoint(nullptr, screen);
        const int index = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(local.y) / slotHeight)), 0, insertCount_);
        if (!released)
        {
            reorderLineY_ = getLocalPoint(&insertsHolder_, juce::Point<int>(0, index * slotHeight)).y;
            repaint();
            return;
        }
        reorderLineY_ = -1;
        repaint();
        const auto* channel = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr;
        if (channel == nullptr)
            return;
        const int from = channel->insertIndex(insertId);
        const int to = index > from ? index - 1 : index;
        if (from >= 0 && to != from)
            owner_.onCommand(std::make_unique<core::MoveInsert>(id_, insertId, to));
    }

    void showGroupMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto& project = *owner_.project_;
        juce::PopupMenu menu;
        // The channel this menu is for, fixed now: a rebuild while it is open may
        // give this strip another channel.
        const core::Id id = id_;
        std::vector<std::function<void()>> actions;
        int item = 1;
        juce::PopupMenu groups;
        groups.addItem(item++, "None", true, group_ == core::invalidId);
        actions.push_back(
            [this, id] { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id, core::invalidId)); });
        for (const auto& g : project.groups)
        {
            groups.addItem(item++, g.name, true, g.id == group_);
            const core::Id groupId = g.id;
            actions.push_back([this, id, groupId]
                              { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id, groupId)); });
        }
        groups.addSeparator();
        groups.addItem(item++, "New group");
        actions.push_back(
            [this, id]
            {
                if (owner_.project_ == nullptr)
                    return;
                const auto n = owner_.project_->groups.size();
                core::Group g;
                g.name = "Group " + std::to_string(n + 1);
                g.colour = groupColours[n % std::size(groupColours)];
                owner_.onCommand(std::make_unique<core::AddGroup>(g, std::vector<core::Id>{id}));
            });
        if (group_ != core::invalidId)
        {
            groups.addItem(item++, "Remove group");
            const core::Id groupId = group_;
            actions.push_back([this, groupId]
                              { owner_.onCommand(std::make_unique<core::RemoveGroup>(groupId)); });
        }
        menu.addSubMenu("Fader group", groups);
        juce::PopupMenu colours;
        juce::Component::SafePointer<MixerView> view(&owner_);
        const int firstColour = item;
        item = addColourItems(colours,
                              theme_,
                              colour_,
                              item,
                              [view, id](std::uint32_t argb)
                              {
                                  if (view != nullptr && view->onCommand)
                                      view->onCommand(std::make_unique<core::SetColour>(id, argb));
                              });
        actions.resize(actions.size() + static_cast<size_t>(item - firstColour),
                       [] {}); // the items act themselves
        menu.addSubMenu("Colour", colours);
        menu.addSeparator();
        // Solo safe: other solos never silence it (an FX return, a click track).
        const bool safeNow = soloSafe_;
        menu.addItem(item++, "Solo safe", true, safeNow);
        actions.push_back([this, id, safeNow]
                          { owner_.onCommand(std::make_unique<core::SetSoloSafe>(id, !safeNow)); });
        menu.addItem(item++, "Hide strip");
        actions.push_back(
            [this, id]
            {
                juce::Component::SafePointer<MixerView> owner(&owner_);
                juce::MessageManager::callAsync(
                    [owner, id]
                    {
                        if (owner != nullptr && owner->onCommand)
                            owner->onCommand(std::make_unique<core::SetMixerHidden>(id, true));
                    });
            });
        menu.addItem(item++, "Wide strips", true, owner_.wide_);
        actions.push_back(
            [this]
            {
                juce::Component::SafePointer<MixerView> owner(&owner_);
                juce::MessageManager::callAsync(
                    [owner]
                    {
                        if (owner != nullptr)
                            owner->setWideStrips(!owner->wideStrips());
                    });
            });
        if (isBus_)
        {
            menu.addSeparator();
            menu.addItem(item++, "Remove bus");
            actions.push_back(
                [this, id]
                {
                    // The removal rebuilds the strips, which may delete this one:
                    // run it once the menu's callback has unwound.
                    juce::Component::SafePointer<MixerView> owner(&owner_);
                    juce::MessageManager::callAsync(
                        [owner, id]
                        {
                            if (owner != nullptr && owner->onCommand)
                                owner->onCommand(std::make_unique<core::RemoveBus>(id));
                        });
                });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(cap_),
                           [actions](int chosen)
                           {
                               if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size())
                                   actions[static_cast<size_t>(chosen - 1)]();
                           });
    }

    // An insert's or the instrument slot's menu: the one every slot shares
    // (ui/InsertMenu.h).
    void showInsertMenu(core::Id insertId)
    {
        if (owner_.project_ != nullptr)
            owner_.showInsertMenu(id_, insertId, !isBus_);
    }

    void setInstrument(const std::string& type)
    {
        core::Insert instrument;
        instrument.type = type;
        instrument.params = plugins::defaultParameters(type);
        owner_.onCommand(std::make_unique<core::SetInstrument>(id_, instrument));
    }

    void showInstrumentMenu(juce::Component& target)
    {
        auto built = buildPluginMenu(PluginKind::instrument, owner_.pluginSources_);
        juce::Component::SafePointer<Strip> safe(this);
        built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                                 [safe, built](int choice)
                                 {
                                     if (safe != nullptr)
                                         if (const auto type = built.typeFor(choice); !type.empty())
                                             safe->setInstrument(type);
                                 });
    }

    void showAddInsertMenu() { owner_.showAddInsertMenu(id_, addInsert_); }

    void showAddSendMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        addSendTargets(menu, actions);
        showList(menu, actions, addSend_);
    }

    MixerView& owner_;
    const Theme& theme_;
    core::Id id_ = core::invalidId;
    core::Id group_ = core::invalidId;
    float currentGain_ = 1.0f;
    bool isBus_ = false, midi_ = false, aux_ = false;
    int meterSlot_ = -1;
    int sendCount_ = 0;
    int insertCount_ = 0;
    bool updating_ = false, breakLink_ = false, collapsed_ = false, feedsSoloedBus_ = false;
    int reorderLineY_ = -1;
    juce::Colour colour_;
    std::vector<core::Id> busIds_;
    NameCap cap_;
    juce::ComboBox input_, output_;
    juce::Viewport insertsView_;
    juce::Component insertsHolder_;
    StripRow stripRow_;
    bool automatedShown_ = false;
    bool fx_ = false; // an FX track (a bus fed by sends)
    std::vector<std::unique_ptr<InsertSlot>> inserts_;
    std::vector<std::unique_ptr<SendRow>> sends_;
    AddSlot addInsert_, addSend_;
    InsertSlot instrument_{theme_};
    AddSlot addInstrument_{theme_, "Instrument"};
    bool instrumentSlot_ = false; // an instrument channel: the instrument row shows
    SectionSummary insertsSummary_, sendsSummary_;
    std::vector<juce::String> sendNames_;
    bool soloSafe_ = false, dropping_ = false;
    int dropIndex_ = -1;
    int latency_ = 0; // samples its chain delays it by
    double sampleRate_ = 48000.0;
    juce::String latencyText_;
    juce::Rectangle<int> latencyArea_;
    Knob pan_;
    juce::TextButton mute_, solo_, arm_, phase_;
    Fader fader_;
    Meter meter_;
    NumericField value_;
};

} // namespace northlight::ui
