#include "ui/TrackHeader.h"

#include "ui/AuralLookAndFeel.h"
#include "ui/Icons.h"

#include <cmath>

namespace aural::ui
{

namespace
{
float dbOf(float gain) noexcept
{
    return 20.0f * std::log10(std::max(gain, 1.0e-5f));
}
} // namespace

TrackHeader::TrackHeader(const Theme& theme) : theme_(theme), meter_(theme, 2, false), pan_(theme)
{
    setOpaque(true);
    setRepaintsOnMouseActivity(true);

    name_.setFont(theme_.label());
    name_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setMinimumHorizontalScale(1.0f);
    name_.setEditable(false, true, false);
    name_.setTooltip("Double-click to rename");
    name_.onTextChange = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackName>(trackId_, name_.getText().trim().toStdString()));
    };
    addAndMakeVisible(name_);

    input_.setTextWhenNothingSelected("No input");
    input_.setTooltip("Input");
    input_.onChange = [this]
    {
        if (updating_)
            return;
        if (midi_)
        {
            const int index = input_.getSelectedId() - 2; // id 1 = all inputs
            if (onMidiInputChanged)
                onMidiInputChanged(trackId_,
                                   index >= 0 && static_cast<size_t>(index) < midiNames_.size()
                                       ? midiNames_[static_cast<size_t>(index)]
                                       : std::string{});
        }
        else if (onInputChanged)
            onInputChanged(trackId_, input_.getSelectedId() - 2); // id 1 = none
    };
    addAndMakeVisible(input_);

    output_.setTooltip("Output");
    output_.onChange = [this]
    {
        if (updating_ || !onCommand)
            return;
        const int index = output_.getSelectedId() - 2; // id 1 = master
        onCommand(std::make_unique<core::SetOutputBus>(
            trackId_,
            index >= 0 && static_cast<size_t>(index) < busIds_.size() ? busIds_[static_cast<size_t>(index)]
                                                                      : core::invalidId));
    };
    addChildComponent(output_);

    const auto toggle = [this](juce::TextButton& b, const char* semantic, const char* tip)
    {
        b.setClickingTogglesState(true);
        b.getProperties().set(AuralLookAndFeel::propertySemantic, semantic);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    toggle(mute_, "mute", "Mute");
    toggle(solo_, "solo", "Solo");
    toggle(arm_, "arm", "Arm for recording");
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
        if (!updating_ && onArmChanged)
            onArmChanged(trackId_, arm_.getToggleState());
    };

    monitorButton_.getProperties().set(AuralLookAndFeel::propertySemantic, "monitor");
    monitorButton_.getProperties().set(AuralLookAndFeel::propertyIcon, true);
    monitorButton_.setButtonText(juce::String::fromUTF8("\xe2\x97\x8f"));
    monitorButton_.onClick = [this]
    {
        using core::MonitorMode;
        const auto next = monitor_ == MonitorMode::off         ? MonitorMode::automatic
                          : monitor_ == MonitorMode::automatic ? MonitorMode::on
                                                               : MonitorMode::off;
        if (onMonitorChanged)
            onMonitorChanged(trackId_, next);
    };
    addAndMakeVisible(monitorButton_);

    volume_.setSliderStyle(juce::Slider::LinearHorizontal);
    volume_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volume_.setRange(-60.0, 6.0, 0.1);
    volume_.setDoubleClickReturnValue(true, 0.0);
    volume_.setTooltip("Volume");
    volume_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Track volume");
    };
    volume_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    volume_.onValueChange = [this]
    {
        if (updating_ || !onCommand)
            return;
        const double db = volume_.getValue();
        const float gain = db <= -60.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
        onCommand(std::make_unique<core::SetTrackGain>(trackId_, gain));
    };
    addAndMakeVisible(volume_);
    addAndMakeVisible(meter_);

    pan_.setRange(-1.0, 1.0, 0.0);
    pan_.setShowValue(false);
    pan_.setTooltip("Pan");
    pan_.format = [](double v)
    {
        const int percent = static_cast<int>(std::lround(std::abs(v) * 100.0));
        return percent == 0 ? juce::String("C") : juce::String(percent) + (v < 0 ? "L" : "R");
    };
    pan_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Track pan");
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
    addChildComponent(pan_);
}

TrackHeader::~TrackHeader()
{
    stopTimer();
}

void TrackHeader::setTrack(const core::Track& track,
                           int index,
                           const core::Project& project,
                           const std::vector<std::string>& inputNames,
                           const std::vector<std::string>& midiInputNames)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    trackId_ = track.id;
    index_ = index;
    midi_ = track.isMidi();
    aux_ = track.isAux();
    muted_ = track.mute;
    colour_ = theme_.track.forTrack(track.colour, index);
    input_.setVisible(!aux_);
    arm_.setVisible(!aux_);
    monitorButton_.setVisible(!aux_);
    name_.setText(juce::String::fromUTF8(track.name.c_str()), juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId,
                    track.mute ? theme_.colours.textSecondary : theme_.colours.textPrimary);

    input_.clear(juce::dontSendNotification);
    if (midi_)
    {
        midiNames_ = midiInputNames;
        input_.addItem("All MIDI inputs", 1);
        int selected = 1;
        for (size_t i = 0; i < midiNames_.size(); ++i)
        {
            input_.addItem(midiNames_[i], static_cast<int>(i) + 2);
            if (midiNames_[i] == track.midiInput)
                selected = static_cast<int>(i) + 2;
        }
        if (!track.midiInput.empty() && selected == 1) // chosen but not present now
        {
            input_.addItem(track.midiInput + " (absent)", static_cast<int>(midiNames_.size()) + 2);
            selected = static_cast<int>(midiNames_.size()) + 2;
        }
        input_.setSelectedId(selected, juce::dontSendNotification);
    }
    else
    {
        input_.addItem("No input", 1);
        for (size_t i = 0; i < inputNames.size(); ++i)
            input_.addItem("In " + juce::String(static_cast<int>(i) + 1), static_cast<int>(i) + 2);
        input_.setSelectedId(track.inputChannel >= 0 &&
                                     static_cast<size_t>(track.inputChannel) < inputNames.size()
                                 ? track.inputChannel + 2
                                 : 1,
                             juce::dontSendNotification);
    }

    output_.clear(juce::dontSendNotification);
    output_.addItem("Master", 1);
    busIds_.clear();
    int outputId = 1;
    for (const auto& bus : project.buses)
    {
        busIds_.push_back(bus.id);
        output_.addItem(juce::String::fromUTF8(bus.name.c_str()), static_cast<int>(busIds_.size()) + 1);
        if (bus.id == track.outputBus)
            outputId = static_cast<int>(busIds_.size()) + 1;
    }
    output_.setSelectedId(outputId, juce::dontSendNotification);
    sends_ = static_cast<int>(track.sends.size());

    mute_.setToggleState(track.mute, juce::dontSendNotification);
    solo_.setToggleState(track.solo, juce::dontSendNotification);
    if (track.armed && !armed_ && isShowing())
    {
        pulse_ = 1.0f;
        pulseStartMs_ = juce::Time::getMillisecondCounterHiRes();
        if (theme_.motion.ms(theme_.motion.toggleMs) > 0)
            startTimerHz(60);
    }
    arm_.setToggleState(track.armed, juce::dontSendNotification);
    armed_ = track.armed;
    monitor_ = track.monitor;
    volume_.setValue(track.gain <= 0.0f ? -60.0 : static_cast<double>(dbOf(track.gain)),
                     juce::dontSendNotification);
    pan_.setValue(track.pan, juce::dontSendNotification);
    refreshMonitorButton();
    repaint();
}

void TrackHeader::setLiveState(bool monitoring, bool recording)
{
    if (monitoring == monitoring_ && recording == recording_)
        return;
    monitoring_ = monitoring;
    recording_ = recording;
    repaint();
}

void TrackHeader::pushMeter(const rt::MeterBlock::Reading& reading, double elapsedMs)
{
    const float peaks[2] = {reading.peakLeft, reading.peakRight};
    meter_.push(peaks, 2, elapsedMs);
}

void TrackHeader::setSelected(bool selected)
{
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

void TrackHeader::setHovered(bool hovered)
{
    if (hovered_ == hovered)
        return;
    hovered_ = hovered;
    repaint();
}

void TrackHeader::setMediaMissing(bool missing)
{
    if (missing_ == missing)
        return;
    missing_ = missing;
    repaint();
}

void TrackHeader::startRename()
{
    name_.showEditor();
}

void TrackHeader::timerCallback()
{
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - pulseStartMs_;
    const double duration = std::max(1, theme_.motion.ms(theme_.motion.toggleMs));
    pulse_ = static_cast<float>(std::max(0.0, 1.0 - elapsed / duration));
    if (pulse_ <= 0.0f)
        stopTimer();
    repaint(stripeBounds());
}

juce::Rectangle<int> TrackHeader::stripeBounds() const noexcept
{
    return getLocalBounds().removeFromLeft(theme_.track.headerStripeWidth);
}

juce::Rectangle<int> TrackHeader::numberBounds() const noexcept
{
    return {theme_.track.headerStripeWidth + theme_.spacing.sm, 0, 22, theme_.sizes.control};
}

void TrackHeader::mouseDown(const juce::MouseEvent& e)
{
    if (onSelected)
        onSelected(trackId_);
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu(trackId_, e.getScreenPosition());
        return;
    }
    if (stripeBounds().expanded(2, 0).contains(e.getPosition()))
    {
        showColourMenu();
        return;
    }
    reordering_ = numberBounds().contains(e.getPosition());
}

void TrackHeader::mouseDrag(const juce::MouseEvent& e)
{
    if (reordering_ && onReorderDrag && e.mouseWasDraggedSinceMouseDown())
        onReorderDrag(trackId_, e.getScreenPosition(), false);
}

void TrackHeader::mouseUp(const juce::MouseEvent& e)
{
    if (reordering_ && onReorderDrag && e.mouseWasDraggedSinceMouseDown())
        onReorderDrag(trackId_, e.getScreenPosition(), true);
    reordering_ = false;
}

void TrackHeader::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.y < theme_.sizes.control && e.x > numberBounds().getRight())
        name_.showEditor();
}

void TrackHeader::showColourMenu()
{
    juce::PopupMenu menu;
    const auto& palette = theme_.track.palette;
    for (size_t i = 0; i < palette.size(); ++i)
    {
        juce::Image swatch(juce::Image::ARGB, 14, 14, true);
        juce::Graphics g(swatch);
        g.setColour(palette[i].value);
        g.fillRoundedRectangle(1.0f, 1.0f, 12.0f, 12.0f, 3.0f);
        auto icon = std::make_unique<juce::DrawableImage>();
        icon->setImage(swatch);
        menu.addItem(juce::PopupMenu::Item(palette[i].name)
                         .setID(static_cast<int>(i) + 1)
                         .setTicked(palette[i].value == colour_)
                         .setImage(std::move(icon)));
    }
    menu.addSeparator();
    menu.addItem(1000, "Default");
    juce::Component::SafePointer<TrackHeader> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(stripeBounds().getWidth(), getHeight()).withPosition(getScreenPosition())),
        [safe](int choice)
        {
            if (safe == nullptr || choice == 0 || !safe->onCommand)
                return;
            const auto& palette = safe->theme_.track.palette;
            const std::uint32_t argb = choice == 1000 || static_cast<size_t>(choice) > palette.size()
                                           ? 0u
                                           : palette[static_cast<size_t>(choice - 1)].value.getARGB();
            safe->onCommand(std::make_unique<core::SetColour>(safe->trackId_, argb));
        });
}

void TrackHeader::refreshMonitorButton()
{
    using core::MonitorMode;
    monitorButton_.setToggleState(monitor_ != MonitorMode::off, juce::dontSendNotification);
    monitorButton_.setTooltip(monitor_ == MonitorMode::off         ? "Monitor: off"
                              : monitor_ == MonitorMode::automatic ? "Monitor: auto (while armed and stopped)"
                                                                   : "Monitor: on");
    monitorButton_.setAlpha(monitor_ == MonitorMode::off ? 0.7f : 1.0f);
}

void TrackHeader::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    if (selected_)
        g.fillAll(c.laneSelected);
    else if (hovered_)
        g.fillAll(c.laneHover);

    // The stripe: the track colour; record while recording; a pulse when arming.
    auto stripe = stripeBounds();
    const auto stripeColour = recording_ && armed_ ? c.record : colour_;
    g.setColour(stripeColour);
    g.fillRect(stripe);
    if (pulse_ > 0.0f)
    {
        g.setColour(c.arm.withAlpha(pulse_ * 0.9f));
        g.fillRect(stripe.expanded(static_cast<int>(pulse_ * 3.0f), 0));
    }

    // The number.
    g.setFont(theme_.caption());
    g.setColour(c.textTertiary);
    g.drawText(juce::String(index_ + 1).paddedLeft('0', 2), numberBounds(), juce::Justification::centredLeft);

    // A warning dot by the name when media is missing.
    if (missing_)
    {
        const auto dot = juce::Rectangle<float>(6.0f, 6.0f)
                             .withCentre({static_cast<float>(name_.getRight() + theme_.spacing.xs),
                                          static_cast<float>(name_.getBounds().getCentreY())});
        g.setColour(c.warning);
        g.fillEllipse(dot);
    }

    // The sends summary on the third row.
    if (output_.isVisible())
    {
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        const auto row =
            juce::Rectangle<int>(output_.getRight() + theme_.spacing.sm,
                                 output_.getY(),
                                 std::max(0, getWidth() - output_.getRight() - 2 * theme_.spacing.sm),
                                 output_.getHeight());
        g.drawText(sends_ == 0 ? juce::String("no sends")
                               : juce::String(sends_) + (sends_ == 1 ? " send" : " sends"),
                   row,
                   juce::Justification::centredLeft,
                   true);
    }

    g.setColour(c.lineSubtle);
    g.fillRect(getLocalBounds().removeFromBottom(1));
}

void TrackHeader::resized()
{
    const int stripe = theme_.track.headerStripeWidth;
    auto area = getLocalBounds().withTrimmedLeft(stripe).reduced(theme_.spacing.sm, 0);
    const int control = theme_.sizes.controlCompact;

    // Row 1: number, name, M S R monitor.
    auto row1 = area.removeFromTop(theme_.sizes.control);
    row1.removeFromLeft(numberBounds().getWidth());
    auto trio = row1.removeFromRight(4 * control + 3 * theme_.spacing.xxs);
    const auto place = [&](juce::Component& b)
    {
        b.setBounds(trio.removeFromLeft(control).withSizeKeepingCentre(control, control));
        trio.removeFromLeft(theme_.spacing.xxs);
    };
    place(mute_);
    place(solo_);
    place(arm_);
    place(monitorButton_);
    name_.setBounds(row1.withTrimmedRight(theme_.spacing.sm).withSizeKeepingCentre(
        std::max(0, row1.getWidth() - theme_.spacing.sm), theme_.sizes.controlCompact - theme_.spacing.xs));

    // Row 2: input, meter, volume (the row hides below 44 px; the slider below 200 px width).
    const bool row2 = getHeight() >= 44;
    input_.setVisible(row2 && !aux_);
    meter_.setVisible(row2);
    volume_.setVisible(row2 && getWidth() >= 200);
    if (row2)
    {
        auto row = area.removeFromTop(control);
        if (!aux_)
        {
            const int inputW = std::clamp(row.getWidth() / 3, 76, 96);
            input_.setBounds(row.removeFromLeft(inputW).withSizeKeepingCentre(inputW, control));
            row.removeFromLeft(theme_.spacing.sm);
        }
        if (volume_.isVisible())
        {
            volume_.setBounds(row.removeFromRight(std::max(48, row.getWidth() / 3)));
            row.removeFromRight(theme_.spacing.sm);
        }
        meter_.setBounds(
            row.withSizeKeepingCentre(row.getWidth(), 2 * theme_.sizes.meterWidth + theme_.sizes.meterGap));
    }

    // Row 3 at the large height: pan, output, sends summary.
    const bool row3 = getHeight() >= theme_.sizes.trackRowLarge;
    pan_.setVisible(row3);
    output_.setVisible(row3);
    if (row3)
    {
        auto row =
            area.removeFromTop(theme_.sizes.control + theme_.spacing.xs).withTrimmedTop(theme_.spacing.xs);
        pan_.setBounds(row.removeFromLeft(theme_.sizes.knob)
                           .withSizeKeepingCentre(theme_.sizes.knob, theme_.sizes.knob));
        row.removeFromLeft(theme_.spacing.sm);
        output_.setBounds(row.removeFromLeft(std::min(96, row.getWidth() / 2))
                              .withSizeKeepingCentre(std::min(96, row.getWidth() / 2), control));
    }
}

} // namespace aural::ui
