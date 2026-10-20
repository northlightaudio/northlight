#include "ui/panels/InspectorPanel.h"

#include "ui/AuralLookAndFeel.h"
#include "ui/Db.h"

#include <cmath>

namespace aural::ui
{

InspectorPanel::InspectorPanel(const Theme& theme)
    : theme_(theme), gain_(theme), start_(theme), length_(theme), clipGain_(theme), fadeIn_(theme),
      fadeOut_(theme), pan_(theme)
{
    setOpaque(true);
    name_.setFont(theme_.body());
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

    pan_.setRange(-1.0, 1.0, 0.0);
    pan_.format = [](double v)
    {
        const int percent = static_cast<int>(std::round(std::abs(v) * 100.0));
        return percent == 0 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(percent);
    };
    pan_.setTooltip("Pan: drag up/down, double-click to centre");
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
    pan_.onValueChanged = [this](double v)
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackPan>(trackId_, static_cast<float>(v)));
    };
    addAndMakeVisible(pan_);

    const auto toggle = [this](juce::TextButton& b, const char* semantic, const char* tip)
    {
        b.setClickingTogglesState(true);
        b.getProperties().set(AuralLookAndFeel::propertySemantic, semantic);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    toggle(mute_, "mute", "Mute  Shift+M");
    toggle(solo_, "solo", "Solo  Shift+S");
    toggle(arm_, "arm", "Arm for recording  Shift+R");
    arm_.getProperties().set(AuralLookAndFeel::propertyHardOn, true);
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
        field->setRange(0.0, 1.0e12, 1.0);
        field->setDecimals(0);
        field->setUnit("smp");
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
        field->setRange(0.0, 1.0e9, 100.0);
        field->setDecimals(0);
        field->setUnit("smp");
        field->onValueChanged = [this](double)
        {
            if (updating_ || !onCommand || clipId_ == core::invalidId)
                return;
            onCommand(
                std::make_unique<core::SetClipFades>(trackId_,
                                                     clipId_,
                                                     static_cast<core::SamplePosition>(fadeIn_.value()),
                                                     static_cast<core::SamplePosition>(fadeOut_.value())));
        };
        addChildComponent(*field);
    }
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
    refresh();
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

void InspectorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const auto* t = track();
    const auto* c = clip();
    const bool clipMode = c != nullptr;
    name_.setVisible(t != nullptr);
    gain_.setVisible(t != nullptr && !clipMode);
    pan_.setVisible(t != nullptr && !clipMode);
    mute_.setVisible(t != nullptr && !clipMode);
    solo_.setVisible(t != nullptr && !clipMode);
    arm_.setVisible(t != nullptr && !clipMode && !t->isAux() && !t->isInstrument());
    for (auto* f : {&start_, &length_, &clipGain_, &fadeIn_, &fadeOut_})
        f->setVisible(clipMode);
    if (t == nullptr)
        return;
    name_.setText(juce::String::fromUTF8(clipMode ? c->name.c_str() : t->name.c_str()),
                  juce::dontSendNotification);
    name_.setReadOnly(clipMode);
    if (clipMode)
    {
        start_.setValue(static_cast<double>(c->start), juce::dontSendNotification);
        length_.setValue(static_cast<double>(c->length), juce::dontSendNotification);
        clipGain_.setValue(db::fromGain(c->gain), juce::dontSendNotification);
        fadeIn_.setValue(static_cast<double>(c->fadeIn), juce::dontSendNotification);
        fadeOut_.setValue(static_cast<double>(c->fadeOut), juce::dontSendNotification);
    }
    else
    {
        gain_.setValue(db::fromGain(t->gain), juce::dontSendNotification);
        pan_.setValue(t->pan, juce::dontSendNotification);
        mute_.setToggleState(t->mute, juce::dontSendNotification);
        solo_.setToggleState(t->solo, juce::dontSendNotification);
        arm_.setToggleState(t->armed, juce::dontSendNotification);
    }
    resized();
    repaint();
}

juce::Rectangle<int> InspectorPanel::swatchBounds(size_t index) const
{
    const int size = theme_.sizes.icon + theme_.spacing.xs;
    const int perRow = 6;
    return {swatchArea_.getX() + static_cast<int>(index % perRow) * (size + theme_.spacing.xs),
            swatchArea_.getY() + static_cast<int>(index / perRow) * (size + theme_.spacing.xs),
            size,
            size};
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    const auto* t = track();
    if (t == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("Select a track or clip", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const int labelW = 120;
    const int rowH = theme_.sizes.control + theme_.spacing.sm;
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.md);
    g.setFont(theme_.captionCaps());
    g.setColour(c.textTertiary);
    g.drawText(clip() != nullptr ? "CLIP"
                                 : (t->isMidi()         ? "MIDI TRACK"
                                    : t->isInstrument() ? "INSTRUMENT CHANNEL"
                                    : t->isAux()        ? "INSTRUMENT OUTPUT"
                                                        : "AUDIO TRACK"),
               area.removeFromTop(theme_.sizes.controlCompact),
               juce::Justification::centredLeft);
    g.setFont(theme_.body());
    g.setColour(c.textSecondary);
    const auto label = [&](const char* text)
    {
        auto row = area.removeFromTop(rowH);
        g.drawText(text, row.removeFromLeft(labelW), juce::Justification::centredLeft);
    };
    label("Name");
    if (clip() != nullptr)
    {
        label("Start");
        label("Length");
        label("Gain");
        label("Fade in");
        label("Fade out");
        return;
    }
    label("Colour");
    area.removeFromTop(swatchArea_.getHeight() - rowH + theme_.spacing.sm);
    label("Gain");
    label("Pan");
    label("State");
    // Swatches.
    const auto& palette = theme_.track.palette;
    const int slot = project_ != nullptr ? project_->trackIndex(trackId_) : 0;
    const auto current = theme_.track.forTrack(t->colour, slot);
    for (size_t i = 0; i < palette.size(); ++i)
    {
        const auto bounds = swatchBounds(i).toFloat();
        g.setColour(palette[i].value);
        g.fillRoundedRectangle(bounds, theme_.radius.sm);
        if (palette[i].value == current)
        {
            g.setColour(c.textPrimary);
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.5f);
        }
    }
}

void InspectorPanel::resized()
{
    const int labelW = 120;
    const int rowH = theme_.sizes.control + theme_.spacing.sm;
    auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.md);
    area.removeFromTop(theme_.sizes.controlCompact);
    const auto control = [&]
    {
        auto row = area.removeFromTop(rowH);
        row.removeFromLeft(labelW);
        return row.withSizeKeepingCentre(row.getWidth(), theme_.sizes.control);
    };
    name_.setBounds(control());
    if (clip() != nullptr)
    {
        start_.setBounds(control());
        length_.setBounds(control());
        clipGain_.setBounds(control());
        fadeIn_.setBounds(control());
        fadeOut_.setBounds(control());
        return;
    }
    auto swatches = area.removeFromTop(rowH);
    swatches.removeFromLeft(labelW);
    const int size = theme_.sizes.icon + theme_.spacing.xs;
    const int rows = (static_cast<int>(theme_.track.palette.size()) + 5) / 6;
    swatchArea_ = swatches.withHeight(rows * (size + theme_.spacing.xs));
    area.removeFromTop(swatchArea_.getHeight() - rowH + theme_.spacing.sm);
    gain_.setBounds(control().withWidth(100));
    pan_.setBounds(control().withWidth(theme_.sizes.knob + 40).withHeight(theme_.sizes.knob));
    auto state = control();
    const int w = theme_.sizes.controlCompact + theme_.spacing.sm;
    mute_.setBounds(
        state.removeFromLeft(w).reduced(0, (theme_.sizes.control - theme_.sizes.controlCompact) / 2));
    solo_.setBounds(
        state.removeFromLeft(w).reduced(0, (theme_.sizes.control - theme_.sizes.controlCompact) / 2));
    arm_.setBounds(
        state.removeFromLeft(w).reduced(0, (theme_.sizes.control - theme_.sizes.controlCompact) / 2));
}

void InspectorPanel::mouseDown(const juce::MouseEvent& e)
{
    if (track() == nullptr || clip() != nullptr || !onCommand)
        return;
    for (size_t i = 0; i < theme_.track.palette.size(); ++i)
        if (swatchBounds(i).contains(e.getPosition()))
        {
            onCommand(std::make_unique<core::SetColour>(trackId_, theme_.track.palette[i].value.getARGB()));
            return;
        }
}

} // namespace aural::ui
