#include "ui/TrackHeader.h"

#include "ui/Db.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/TrackIcon.h"

#include <cmath>

namespace northlight::ui
{

namespace
{
// The volume slider stops at -60 dB, which means silence.
constexpr double volumeBottomDb = -60.0;
float dbOf(float gain) noexcept
{
    return static_cast<float>(db::fromGain(gain, volumeBottomDb, 6.0));
}
} // namespace

TrackHeader::TrackHeader(const Theme& theme) : theme_(theme), meter_(theme, 2, false), pan_(theme)
{
    setOpaque(true);
    setRepaintsOnMouseActivity(true);
    addMouseListener(&selectOnPress_, true);

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
        {
            if (const auto choice = inputs::selected(input_, inputChoices_))
                onInputChanged(trackId_, choice->channel, choice->bus);
        }
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
        b.getProperties().set(NorthlightLookAndFeel::propertySemantic, semantic);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    toggle(mute_, "mute", "Mute");
    toggle(solo_, "solo", "Solo");
    toggle(arm_, "arm", "Arm for recording");
    arm_.getProperties().set(NorthlightLookAndFeel::propertyHardOn, true);
    toggle(automation_,
           "automationRead",
           "Automation lanes: show or hide them under the track (right-click for the mode)");
    automation_.setClickingTogglesState(false); // the model says whether the lanes are open
    automation_.onClick = [this]
    {
        if (onAutomationToggled)
            onAutomationToggled(trackId_, !automation_.getToggleState());
    };
    automation_.onMenu = [this]
    {
        if (onAutomationMenu)
            onAutomationMenu(trackId_, automation_.getScreenBounds());
    };
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

    monitorButton_.getProperties().set(NorthlightLookAndFeel::propertySemantic, "monitor");
    monitorButton_.getProperties().set(NorthlightLookAndFeel::propertyIcon, true);
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
        const float gain = db::toGain(db, volumeBottomDb);
        if (master_)
            onCommand(std::make_unique<core::SetMasterGain>(gain));
        else
            onCommand(std::make_unique<core::SetTrackGain>(trackId_, gain));
    };
    addAndMakeVisible(volume_);
    addAndMakeVisible(meter_);
    target_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    target_.onClick = [this]
    {
        if (onTargetMenu)
            onTargetMenu(trackId_, target_.getScreenBounds());
    };
    addChildComponent(target_);
    fold_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    fold_.onClick = [this]
    {
        if (onCommand)
            onCommand(std::make_unique<core::SetFolderOpen>(trackId_, !folderOpen_));
    };
    addChildComponent(fold_);

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
    removeMouseListener(&selectOnPress_);
    stopTimer();
}

void TrackHeader::setTrack(const core::Track& track,
                           int index,
                           const core::Project& project,
                           const std::vector<InputChoice>& inputs,
                           const std::vector<std::string>& midiInputNames)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    trackId_ = track.id;
    index_ = index;
    kind_ = track.kind;
    midi_ = track.isMidi();
    aux_ = track.isAux();
    instrument_ = track.isInstrument();
    folder_ = track.isFolder();
    folderOpen_ = track.folderOpen;
    depth_ = project.folderDepth(track);
    fold_.setVisible(folder_);
    fold_.setButtonText(juce::String::fromUTF8(folderOpen_ ? "\xe2\x96\xbe" : "\xe2\x96\xb8"));
    fold_.setTooltip(folderOpen_ ? "Close the folder" : "Open the folder");
    muted_ = track.mute;
    colour_ = theme_.track.forTrack(track.colour, index);
    setTitle("Track " + juce::String(index + 1) + ": " + juce::String::fromUTF8(track.name.c_str()));
    setDescription(trackKindName(track.kind));
    const bool takesInput = !aux_ && !instrument_ && !folder_; // an instrument channel's MIDI tracks take it
    input_.setVisible(takesInput);
    arm_.setVisible(takesInput);
    monitorButton_.setVisible(takesInput);
    // A MIDI track makes notes, not sound: no solo or automation of its own; its
    // volume scales velocities; the chip says what it plays.
    solo_.setVisible(!midi_);
    automation_.setVisible(!midi_ && !folder_);
    mute_.setTooltip(folder_ ? "Mute every track in the folder" : "Mute");
    solo_.setTooltip(folder_ ? "Solo every track in the folder" : "Solo");
    volume_.setTooltip(midi_ ? "Velocity: scales the notes this track sends" : "Volume");
    if (midi_)
    {
        const auto* target = project.findTrack(track.midiTarget);
        juce::String text = juce::String::fromUTF8("\xe2\x86\x92 ");
        text << (target != nullptr ? juce::String::fromUTF8(target->name.c_str())
                                   : juce::String("No instrument"));
        if (target != nullptr && track.midiChannel != 1)
            text << "  ch " << track.midiChannel;
        target_.setButtonText(text);
        target_.setTooltip("The instrument channel this track plays (click to change)");
    }
    // A frozen track's name carries a snowflake.
    name_.setText((track.isFrozen() ? juce::String::fromUTF8("\xe2\x9d\x84 ") : juce::String()) +
                      juce::String::fromUTF8(track.name.c_str()),
                  juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId,
                    track.mute ? theme_.colours.textSecondary : theme_.colours.textPrimary);

    input_.clear(juce::dontSendNotification);
    if (midi_)
    {
        midiNames_ = midiInputNames;
        input_.addItem("All MIDI", 1);
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
        inputChoices_ = inputs;
        inputs::fill(input_, inputChoices_, track);
    }
    resized(); // the input's width follows its text

    output_.clear(juce::dontSendNotification);
    output_.addItem("Master", 1);
    busIds_.clear();
    int outputId = 1;
    for (const auto& bus : project.buses)
    {
        busIds_.push_back(bus.id);
        output_.addItem(juce::String::fromUTF8(bus.name.c_str()), static_cast<int>(busIds_.size()) + 1);
        // A bus that already feeds this track would make a loop: greyed, as in the mixer.
        output_.setItemEnabled(static_cast<int>(busIds_.size()) + 1, !project.wouldCycle(track.id, bus.id));
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
    setAutomation(track);
    refreshMonitorButton();
    resized(); // which buttons show depends on the kind of track
    repaint();
}

void TrackHeader::setValues(const core::Channel& channel)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    mute_.setToggleState(channel.mute, juce::dontSendNotification);
    solo_.setToggleState(channel.solo, juce::dontSendNotification);
    volume_.setValue(channel.gain <= 0.0f ? -60.0 : static_cast<double>(dbOf(channel.gain)),
                     juce::dontSendNotification);
    pan_.setValue(channel.pan, juce::dontSendNotification);
    setAutomation(channel);
}

void TrackHeader::setAutomation(const core::Channel& channel)
{
    automation_.setToggleState(channel.automationOpen, juce::dontSendNotification);
    const char* semantic = "automationRead";
    switch (channel.automationMode)
    {
        case core::AutomationMode::off:
            semantic = "automationOff";
            break;
        case core::AutomationMode::read:
            break;
        case core::AutomationMode::touch:
            semantic = "automationTouch";
            break;
        case core::AutomationMode::latch:
            semantic = "automationLatch";
            break;
        case core::AutomationMode::write:
            semantic = "automationWrite";
            break;
    }
    auto& props = automation_.getProperties();
    if (props[NorthlightLookAndFeel::propertySemantic].toString() != semantic)
    {
        props.set(NorthlightLookAndFeel::propertySemantic, semantic);
        automation_.repaint();
    }
    const int lanes = static_cast<int>(channel.automation.size());
    if (lanes != automationLanes_)
    {
        automationLanes_ = lanes;
        repaint(automation_.getBounds().expanded(theme_.spacing.sm));
    }
}

void TrackHeader::setLiveState(bool monitoring, bool recording)
{
    if (monitoring == monitoring_ && recording == recording_)
        return;
    monitoring_ = monitoring;
    recording_ = recording;
    refreshMonitorButton(); // auto turns from waiting to hearing as the state changes
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
    if (!master_)
        name_.showEditor();
}

void TrackHeader::setMaster(bool master)
{
    if (master == master_)
        return;
    master_ = master;
    name_.setEditable(false, !master, false);
    resized();
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

juce::String TrackHeader::getTooltip()
{
    if (!numberBounds().contains(getMouseXYRelative()))
        return {};
    return "Track " + juce::String(index_ + 1) + ": " + trackKindName(kind_) + ". Drag to reorder";
}

juce::Rectangle<int> TrackHeader::numberBounds() const noexcept
{
    return {theme_.track.headerStripeWidth + indent() + theme_.spacing.sm, 0, 22, theme_.sizes.control};
}

void TrackHeader::mouseDown(const juce::MouseEvent& e)
{
    // (Selection: selectOnPress_ sees this press and every child's.)
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

// The dot: dim when off; `monitor` green when the input is heard (on, or auto
// with its conditions met: armed and stopped or recording); `warning` yellow
// when auto is waiting for them.
void TrackHeader::refreshMonitorButton()
{
    using core::MonitorMode;
    const bool waiting = monitor_ == MonitorMode::automatic && !monitoring_;
    monitorButton_.setToggleState(monitor_ != MonitorMode::off, juce::dontSendNotification);
    monitorButton_.getProperties().set(NorthlightLookAndFeel::propertySemantic,
                                       waiting ? "monitorAuto" : "monitor");
    monitorButton_.setTooltip(monitor_ == MonitorMode::off  ? "Monitor: off"
                              : monitor_ == MonitorMode::on ? "Monitor: on"
                              : waiting
                                  ? "Monitor: auto (waiting: heard while armed and stopped or recording)"
                                  : "Monitor: auto (hearing the input)");
    monitorButton_.setAlpha(monitor_ == MonitorMode::off ? 0.7f : 1.0f);
    monitorButton_.repaint();
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

    // The track-kind icon, in the number's place (the number is in its tooltip).
    {
        const auto box = numberBounds();
        const float size = static_cast<float>(theme_.sizes.iconSmall);
        Icons::draw(g,
                    trackIcon(kind_),
                    juce::Rectangle<float>(static_cast<float>(box.getX()),
                                           static_cast<float>(box.getCentreY()) - size * 0.5f,
                                           size,
                                           size),
                    selected_ ? c.textSecondary : c.textTertiary);
    }

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

void TrackHeader::paintOverChildren(juce::Graphics& g)
{
    // The A toggle's badge: how many lanes the track has, hidden ones included.
    if (!automation_.isVisible() || automationLanes_ == 0)
        return;
    const auto& c = theme_.colours;
    const auto text = juce::String(automationLanes_);
    const auto font = theme_.caption();
    const int h = theme_.sizes.iconSmall;
    const int w = std::max(h, juce::GlyphArrangement::getStringWidthInt(font, text) + theme_.spacing.sm);
    const auto a = automation_.getBounds();
    const auto badge = juce::Rectangle<int>(a.getRight() - w + 5, a.getY() - 5, w, h).toFloat();
    g.setColour(c.canvas);
    g.fillRoundedRectangle(badge.expanded(1.0f), (h + 2) * 0.5f);
    g.setColour(c.surface3);
    g.fillRoundedRectangle(badge, h * 0.5f);
    g.setColour(c.textPrimary);
    g.setFont(font);
    g.drawText(text, badge, juce::Justification::centred, false);
}

void TrackHeader::resized()
{
    const int stripe = theme_.track.headerStripeWidth;
    auto area = getLocalBounds().withTrimmedLeft(stripe + indent()).reduced(theme_.spacing.sm, 0);
    const int control = theme_.sizes.controlCompact;

    // Row 1: number, (a folder's disclosure,) name, M S R monitor.
    auto row1 = area.removeFromTop(theme_.sizes.control);
    row1.removeFromLeft(numberBounds().getWidth());
    if (folder_)
        fold_.setBounds(row1.removeFromLeft(control).withSizeKeepingCentre(control, control));
    // Five button places, so the buttons line up from row to row; a folder has
    // only M and S, and its name takes the rest.
    int buttons = 5;
    if (folder_)
        buttons = 2;
    auto trio = row1.removeFromRight(buttons * control + std::max(0, buttons - 1) * theme_.spacing.xxs);
    const auto place = [&](juce::Component& b)
    {
        if (folder_ && &b != &mute_ && &b != &solo_)
            return;
        b.setBounds(trio.removeFromLeft(control).withSizeKeepingCentre(control, control));
        trio.removeFromLeft(theme_.spacing.xxs);
    };
    place(mute_);
    place(solo_);
    mute_.setVisible(!master_);
    solo_.setVisible(!master_ && !midi_);
    place(arm_);
    place(monitorButton_);
    place(automation_);
    name_.setBounds(row1.withTrimmedRight(theme_.spacing.sm)
                        .withSizeKeepingCentre(std::max(0, row1.getWidth() - theme_.spacing.sm),
                                               theme_.sizes.controlCompact - theme_.spacing.xs));

    // Row 2: input, meter, volume (the row hides below 44 px; the slider below 200 px width).
    const bool row2 = getHeight() >= 44 && !folder_;
    input_.setVisible(row2 && !aux_ && !instrument_);
    meter_.setVisible(row2 && !midi_);
    target_.setVisible(row2 && midi_);
    volume_.setVisible(row2 && getWidth() >= 200);
    if (row2)
    {
        auto row = area.removeFromTop(control);
        if (!aux_ && !instrument_)
        {
            // As wide as the chosen input's name needs, within limits, so the rest
            // of the row is left to the meter or the instrument it plays.
            const int textW = juce::GlyphArrangement::getStringWidthInt(theme_.body(), input_.getText());
            const int inputW =
                std::clamp(textW + 2 * theme_.spacing.sm + theme_.sizes.iconSmall + theme_.spacing.xs,
                           76,
                           std::max(76, row.getWidth() * 2 / 5));
            input_.setBounds(row.removeFromLeft(inputW).withSizeKeepingCentre(inputW, control));
            row.removeFromLeft(theme_.spacing.sm);
        }
        if (volume_.isVisible())
        {
            volume_.setBounds(row.removeFromRight(midi_ ? 48 : std::max(48, row.getWidth() / 3)));
            row.removeFromRight(theme_.spacing.sm);
        }
        meter_.setBounds(
            row.withSizeKeepingCentre(row.getWidth(), 2 * theme_.sizes.meterWidth + theme_.sizes.meterGap));
        target_.setBounds(row.withSizeKeepingCentre(row.getWidth(), control));
    }

    // Row 3 at the large height: pan, output, sends summary.
    const bool row3 = getHeight() >= theme_.sizes.trackRowLarge && !folder_ && !master_;
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

} // namespace northlight::ui
