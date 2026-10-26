#pragma once

// The master's strip, pinned at the mixer's right (screens/mixer.md): internal to
// the mixer, included by MixerView.cpp only.

#include "core/Command.h"
#include "engine/Snapshot.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostedPlugin.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ClapEditor.h"
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

// ----- master -------------------------------------------------------------------------------

class MixerView::MasterStrip final : public juce::Component
{
public:
    MasterStrip(MixerView& owner, const Theme& theme)
        : owner_(owner), theme_(theme), cap_(theme), addInsert_(theme, "Insert"), fader_(theme),
          meter_(theme, 2, true), value_(theme)
    {
        setOpaque(true);
        cap_.set("Master", theme_.track.named(theme_.track.master), false, 0, {});
        cap_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(cap_);
        // The inserts: on the whole mix, before the master fader.
        insertsView_.setViewedComponent(&insertsHolder_, false);
        insertsView_.setScrollBarsShown(false, false, true, false);
        insertsView_.setScrollBarThickness(4);
        addAndMakeVisible(insertsView_);
        addInsert_.setTooltip("Add an insert on the whole mix, before the master fader");
        addInsert_.onClick = [this] { owner_.showAddInsertMenu(core::masterId, addInsert_); };
        insertsHolder_.addAndMakeVisible(addInsert_);
        fader_.setMaxDb(static_cast<float>(maxFaderDb));
        fader_.setTrackColour(theme_.track.named(theme_.track.master));
        fader_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Master fader");
        };
        fader_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        fader_.onGainChanged = [this](float gain)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetMasterGain>(gain));
        };
        addAndMakeVisible(fader_);
        meter_.setShowScale(true);
        addAndMakeVisible(meter_);
        value_.setRange(db::silence, maxFaderDb, 0.1);
        value_.setDecimals(1);
        value_.setUnit("dB");
        value_.format = [](double v) { return db::text(v); };
        value_.parse = [](const juce::String& t) { return db::parse(t); };
        value_.onDragStart = fader_.onDragStart;
        value_.onDragEnd = fader_.onDragEnd;
        value_.onValueChanged = [this](double db)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetMasterGain>(db::toGain(db)));
        };
        addAndMakeVisible(value_);
    }

    void setMaster(const core::Master& master)
    {
        {
            const juce::ScopedValueSetter<bool> guard(updating_, true);
            fader_.setGain(master.gain, juce::dontSendNotification);
            value_.setValue(db::fromGain(master.gain), juce::dontSendNotification);
        }
        showPlayed(); // an automated fader never shows its static value for a frame
        const juce::ScopedValueSetter<bool> guard(updating_, true);

        const bool countChanged = inserts_.size() != master.inserts.size();
        while (inserts_.size() > master.inserts.size())
            inserts_.pop_back();
        while (inserts_.size() < master.inserts.size())
        {
            auto made = std::make_unique<InsertSlot>(theme_);
            insertsHolder_.addAndMakeVisible(*made);
            inserts_.push_back(std::move(made));
        }
        for (size_t i = 0; i < master.inserts.size(); ++i)
        {
            const auto& insert = master.inserts[i];
            const core::Id insertId = insert.id;
            juce::String badge;
            if (hosting::PluginRegistry::isHostedType(insert.type))
                badge = insert.type.rfind("clap:", 0) == 0 ? "CLAP" : "VST3";
            inserts_[i]->set(
                juce::String::fromUTF8(owner_.insertDisplayName(insert.type).c_str()), badge, insert.bypass);
            inserts_[i]->onOpen = [this, insertId] { owner_.openInsertEditor(core::masterId, insertId); };
            inserts_[i]->onBypass = [this, insertId]
            {
                if (const auto* ins =
                        owner_.project_ != nullptr ? owner_.project_->master.findInsert(insertId) : nullptr)
                    owner_.onCommand(
                        std::make_unique<core::SetInsertBypass>(core::masterId, insertId, !ins->bypass));
            };
            inserts_[i]->onMenu = [this, insertId]
            { owner_.showInsertMenu(core::masterId, insertId, false); };
            inserts_[i]->onReorder = [this, insertId](juce::Point<int> screen, bool released)
            { reorderInsert(insertId, screen, released); };
        }
        if (countChanged)
            resized();
    }

    void pushMeter(const rt::MeterBlock::Reading& r, double elapsedMs)
    {
        const float peaks[2] = {r.peakLeft, r.peakRight};
        meter_.push(peaks, 2, elapsedMs);
        showPlayed();
    }

    // An automated master fader shows what its volume lane plays, as a channel's.
    void showPlayed()
    {
        if (!owner_.automatedValues)
            return;
        const auto a = owner_.automatedValues(core::masterId);
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        if (a.gain && !fader_.isMouseButtonDown() && !value_.isMouseButtonDown())
        {
            fader_.setGain(a.gainValue, juce::dontSendNotification);
            value_.setValue(db::fromGain(a.gainValue), juce::dontSendNotification);
        }
        if (a.gain != automatedShown_)
        {
            automatedShown_ = a.gain;
            fader_.setTooltip(a.gain ? "Master (automated: the fader follows its lane)" : "Master");
        }
    }

    // The channel strips' fader band and gain field row, to line up with; an
    // empty band lays the master out on its own.
    void alignTo(juce::Rectangle<int> band, int valueY)
    {
        if (band == band_ && valueY == valueY_)
            return;
        band_ = band;
        valueY_ = valueY;
        resized();
    }

    void pushLoudness(const engine::LoudnessMeter::Reading& r)
    {
        loudness_ = r;
        repaint(loudnessArea_);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (reorderLineY_ >= 0)
        {
            g.setColour(theme_.colours.accent);
            g.fillRect(insertsView_.getX(), reorderLineY_ - 1, insertsView_.getWidth(), 2);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        g.setColour(c.surface1);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 0.5f), theme_.radius.sm);
        // The loudness readout: M / S / I in value mono with the target marker by I.
        const auto text = [](float lufs)
        { return lufs <= -99.0f ? juce::String("-inf") : juce::String(lufs, 1); };
        auto area = loudnessArea_;
        g.setFont(theme_.value());
        const int rowH = area.getHeight() / 3;
        const auto row = [&](const char* label, float lufs, bool target)
        {
            auto r = area.removeFromTop(rowH);
            g.setColour(c.textTertiary);
            g.drawText(label, r.removeFromLeft(14), juce::Justification::centredLeft);
            g.setColour(target && lufs > -99.0f && std::abs(lufs - theme_.meter.loudnessTargetLufs) <= 1.0f
                            ? c.loudnessTarget
                            : c.textSecondary);
            g.drawText(text(lufs), r, juce::Justification::centredRight);
        };
        row("M", loudness_.momentaryLufs, false);
        row("S", loudness_.shortTermLufs, false);
        row("I", loudness_.integratedLufs, true);
    }

    void resized() override
    {
        const int pad = theme_.spacing.xs;
        const int control = theme_.sizes.controlCompact;
        auto area = getLocalBounds().reduced(pad, 0);
        cap_.setBounds(getLocalBounds().withHeight(control));
        area.removeFromTop(control + pad);
        const int loudnessH = 3 * (theme_.lineHeight(theme_.fonts.size.value));
        const bool aligned =
            !band_.isEmpty() && valueY_ + control <= getHeight() && band_.getBottom() + loudnessH <= valueY_;
        // The inserts fill the room above the fader band (lined up with the
        // channels), or take up to six slots of their own.
        {
            const int wanted = (static_cast<int>(inserts_.size()) + 1) * slotHeight + 4;
            const int room = aligned ? band_.getY() - area.getY() - pad
                                     : std::min(wanted, maxVisibleSlots * slotHeight + 4);
            auto insertsArea = area.removeFromTop(std::max(slotHeight + 4, std::min(room, wanted)));
            area.removeFromTop(pad);
            insertsView_.setBounds(insertsArea.reduced(0, 2));
            insertsHolder_.setSize(insertsView_.getWidth(),
                                   (static_cast<int>(inserts_.size()) + 1) * slotHeight);
            int y = 0;
            for (auto& slot : inserts_)
            {
                slot->setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
                y += slotHeight;
            }
            addInsert_.setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
        }
        if (aligned)
        {
            // Lined up with the channels: the same fader band and gain row, the
            // loudness readout between them.
            value_.setBounds(area.getX(), valueY_, area.getWidth(), control);
            loudnessArea_ = juce::Rectangle<int>(
                                area.getX(), band_.getBottom(), area.getWidth(), valueY_ - band_.getBottom())
                                .withSizeKeepingCentre(area.getWidth(), loudnessH);
            auto faderArea =
                juce::Rectangle<int>(area.getX(), band_.getY(), area.getWidth(), band_.getHeight());
            meter_.setBounds(faderArea.removeFromRight(meter_.barsWidth() + meter_.scaleWidth()));
            faderArea.removeFromRight(pad);
            fader_.setBounds(faderArea);
            return;
        }
        value_.setBounds(area.removeFromBottom(control));
        area.removeFromBottom(pad);
        loudnessArea_ = area.removeFromBottom(loudnessH);
        area.removeFromBottom(pad);
        auto faderArea = area.withTrimmedTop(pad).withHeight(
            std::clamp(area.getHeight() - pad, 60, maxFaderTravel + 2 * pad));
        auto meterArea = faderArea.removeFromRight(meter_.barsWidth() + meter_.scaleWidth());
        meter_.setBounds(meterArea);
        faderArea.removeFromRight(pad);
        fader_.setBounds(faderArea);
    }

private:
    void reorderInsert(core::Id insertId, juce::Point<int> screen, bool released)
    {
        const auto local = insertsHolder_.getLocalPoint(nullptr, screen);
        const int count = static_cast<int>(inserts_.size());
        const int index =
            std::clamp(static_cast<int>(std::lround(static_cast<double>(local.y) / slotHeight)), 0, count);
        if (!released)
        {
            reorderLineY_ = getLocalPoint(&insertsHolder_, juce::Point<int>(0, index * slotHeight)).y;
            repaint();
            return;
        }
        reorderLineY_ = -1;
        repaint();
        if (owner_.project_ == nullptr)
            return;
        const int from = owner_.project_->master.insertIndex(insertId);
        const int to = index > from ? index - 1 : index;
        if (from >= 0 && to != from)
            owner_.onCommand(std::make_unique<core::MoveInsert>(core::masterId, insertId, to));
    }

    MixerView& owner_;
    const Theme& theme_;
    bool updating_ = false;
    bool automatedShown_ = false;
    NameCap cap_;
    juce::Viewport insertsView_;
    juce::Component insertsHolder_;
    std::vector<std::unique_ptr<InsertSlot>> inserts_;
    AddSlot addInsert_;
    int reorderLineY_ = -1;
    Fader fader_;
    Meter meter_;
    NumericField value_;
    juce::Rectangle<int> loudnessArea_;
    juce::Rectangle<int> band_;
    int valueY_ = 0;
    engine::LoudnessMeter::Reading loudness_;
};

} // namespace northlight::ui
