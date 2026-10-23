#include "ui/InsertEditor.h"

#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/strip/BuiltInCards.h"

#include <cmath>

namespace northlight::ui
{

InsertEditor::InsertEditor(core::Id channelId,
                           core::Id insertId,
                           const engine::InsertProcessor& processor,
                           const std::string& title,
                           bool embedded)
    : channelId_(channelId), insertId_(insertId), infos_(processor.parameters()), embedded_(embedded)
{
    setOpaque(true);
    title_.setText(
        juce::String::fromUTF8(
            (title.empty() ? plugins::BuiltInFactory::displayName(processor.typeName()) : title).c_str()),
        juce::dontSendNotification);
    title_.setFont(theme_.title());
    title_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
    title_.setVisible(!embedded_);
    addAndMakeVisible(title_);

    bypass_.setClickingTogglesState(true);
    bypass_.getProperties().set(NorthlightLookAndFeel::propertySemantic, "mute");
    bypass_.setVisible(!embedded_);
    bypass_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(
                std::make_unique<core::SetInsertBypass>(channelId_, insertId_, bypass_.getToggleState()));
    };
    addAndMakeVisible(bypass_);

    mix_.setSliderStyle(juce::Slider::LinearHorizontal);
    mix_.setRange(0.0, 100.0, 1.0);
    mix_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    mix_.setDoubleClickReturnValue(true, 100.0);
    mix_.setValue(100.0, juce::dontSendNotification);
    mix_.setTooltip("Wet/dry mix");
    mix_.setVisible(!embedded_);
    mix_.onValueChange = [this]
    {
        repaint();
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetInsertMix>(
                channelId_, insertId_, static_cast<float>(mix_.getValue() / 100.0)));
    };
    addAndMakeVisible(mix_);

    viewport_.setViewedComponent(&rows_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(theme_.sizes.scrollbar);
    addAndMakeVisible(viewport_);

    for (size_t i = 0; i < infos_.size(); ++i)
    {
        const auto& info = infos_[i];
        const int index = static_cast<int>(i);
        Row row;
        row.name = std::make_unique<juce::Label>();
        row.name->setText(info.name, juce::dontSendNotification);
        row.name->setFont(theme_.body());
        row.name->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        row.name->setMinimumHorizontalScale(1.0f);
        rows_.addAndMakeVisible(*row.name);

        const juce::String unit = info.unit.empty() ? juce::String() : juce::String(info.unit);
        const double step = info.unit == "Hz" ? 1.0 : (info.maximum - info.minimum) > 10.0 ? 0.1 : 0.01;
        row.knob = std::make_unique<Knob>(theme_);
        row.knob->setRange(info.minimum, info.maximum, info.defaultValue);
        row.knob->setShowValue(false);
        row.knob->setTooltip(info.name);
        row.knob->format = [unit](double v)
        { return juce::String(v, std::abs(v) >= 100.0 ? 0 : 2) + (unit.isEmpty() ? "" : " " + unit); };
        row.knob->onDragStart = [this, index]
        {
            if (onParameterGestureBegin)
                onParameterGestureBegin(index);
            if (onGestureBegin)
                onGestureBegin(infos_[static_cast<size_t>(index)].name);
        };
        row.knob->onDragEnd = [this, index]
        {
            if (onGestureEnd)
                onGestureEnd();
            if (onParameterGestureEnd)
                onParameterGestureEnd(index);
        };
        row.knob->onValueChanged = [this, index](double value)
        { setParameter(index, static_cast<float>(value)); };
        rows_.addAndMakeVisible(*row.knob);

        row.value = std::make_unique<NumericField>(theme_);
        row.value->setRange(info.minimum, info.maximum, step);
        row.value->setDecimals(step >= 1.0 ? 0 : step >= 0.1 ? 1 : 2);
        row.value->setUnit(unit);
        row.value->onDragStart = row.knob->onDragStart;
        row.value->onDragEnd = row.knob->onDragEnd;
        row.value->onValueChanged = [this, index](double value)
        { setParameter(index, static_cast<float>(value)); };
        rows_.addAndMakeVisible(*row.value);
        parameters_.push_back(std::move(row));
    }
    setSize(preferredWidth, preferredHeight(static_cast<int>(infos_.size()), embedded_));
}

void InsertEditor::showBuiltInBody(const core::Insert& insert,
                                   std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> live)
{
    live_ = std::move(live);
    host_.onCommand = [this](std::unique_ptr<core::Command> command)
    {
        if (onCommand)
            onCommand(std::move(command));
    };
    host_.onGestureBegin = [this](const std::string& name)
    {
        if (onGestureBegin)
            onGestureBegin(name);
    };
    host_.onGestureEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    host_.liveInsert = [this](core::Id id)
    { return live_ ? live_(id) : std::shared_ptr<engine::InsertProcessor>(); };
    host_.project = [this] { return project ? project() : nullptr; };
    body_ = strip::createBuiltInCard(theme_, host_, channelId_, insert);
    if (body_ == nullptr)
        return;
    body_->setEmbedded(true);
    addAndMakeVisible(*body_);
    viewport_.setVisible(false);
    instrument_ = plugins::BuiltInFactory::isInstrument(insert.type);
    mix_.setVisible(!embedded_ && !instrument_); // an instrument has no dry signal to mix
    // A built-in effect's presets, beside the title (screens/plugins.md 7.2b).
    if (!embedded_ && !instrument_ && !plugins::factoryPresets(insert.type).empty())
    {
        params_ = insert.params;
        presets_ = std::make_unique<strip::PresetBar>(theme_, insert.type);
        presets_->setValues(insert.params);
        presets_->onApply = [this](const std::vector<float>& values, const juce::String& name)
        {
            if (!onCommand)
                return;
            if (onGestureBegin)
                onGestureBegin(("Preset " + name).toStdString());
            for (size_t i = 0; i < values.size(); ++i)
                if (i >= params_.size() || params_[i] != values[i])
                    onCommand(std::make_unique<core::SetInsertParameter>(
                        channelId_, insertId_, static_cast<int>(i), values[i]));
            if (onGestureEnd)
                onGestureEnd();
        };
        addAndMakeVisible(*presets_);
    }
    const int header = embedded_     ? 0
                       : instrument_ ? theme_.sizes.control + theme_.spacing.md
                                     : theme_.sizes.control + theme_.spacing.sm +
                                           theme_.sizes.controlCompact + theme_.spacing.md;
    setSize(std::max(preferredWidth, body_->preferredWidth() + 2 * theme_.spacing.lg),
            2 * theme_.spacing.md + header + body_->preferredBodyHeight() + theme_.sizes.controlCompact);
    startTimerHz(60);
}

void InsertEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastTickMs_ > 0.0 ? std::min(100.0, now - lastTickMs_) : 16.0;
    lastTickMs_ = now;
    if (body_ == nullptr)
        return;
    if (onScreen(*this))
        body_->tick(elapsed);
    else
        body_->stopWatching();
}

void InsertEditor::setParameter(int index, float value)
{
    if (updating_)
        return;
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        auto& row = parameters_[static_cast<size_t>(index)];
        row.knob->setValue(value, juce::dontSendNotification);
        row.value->setValue(value, juce::dontSendNotification);
    }
    if (onCommand)
        onCommand(std::make_unique<core::SetInsertParameter>(channelId_, insertId_, index, value));
}

void InsertEditor::setInsert(const core::Insert& insert)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    bypass_.setToggleState(insert.bypass, juce::dontSendNotification);
    mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
    sidechain_ = insert.sidechain;
    params_ = insert.params;
    if (presets_ != nullptr)
        presets_->setValues(insert.params);
    if (body_ != nullptr)
        body_->refresh(insert);
    for (size_t i = 0; i < parameters_.size() && i < insert.params.size(); ++i)
    {
        parameters_[i].knob->setValue(insert.params[i], juce::dontSendNotification);
        parameters_[i].value->setValue(insert.params[i], juce::dontSendNotification);
    }
    repaint();
}

void InsertEditor::setLatency(int samples)
{
    if (latency_ == samples)
        return;
    latency_ = samples;
    repaint();
}

void InsertEditor::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.canvas);
    const auto footer =
        getLocalBounds().removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.lg, 0);
    g.setFont(theme_.caption());
    g.setColour(c.textTertiary);
    juce::String line =
        latency_ > 0 ? "Latency " + juce::String(latency_) + " smp" : juce::String("No latency");
    if (sidechain_ != core::invalidId)
        line << juce::String::fromUTF8("  \xc2\xb7  key input set");
    g.drawText(line, footer, juce::Justification::centredLeft);
    if (!embedded_ && !instrument_)
    {
        g.setColour(c.textSecondary);
        g.drawText("Mix " + juce::String(static_cast<int>(mix_.getValue())) + "%",
                   juce::Rectangle<int>(mix_.getX() - 64, mix_.getY(), 60, mix_.getHeight()),
                   juce::Justification::centredRight);
    }
    if (parameters_.empty() && body_ == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("No parameters", viewport_.getBounds(), juce::Justification::centred);
    }
}

void InsertEditor::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.lg, theme_.spacing.md);
    if (!embedded_)
    {
        auto header = area.removeFromTop(theme_.sizes.control);
        bypass_.setBounds(header.removeFromRight(80));
        if (presets_ != nullptr)
        {
            header.removeFromRight(theme_.spacing.md);
            const int w = std::min(presets_->preferredWidth(), header.getWidth() - 120);
            presets_->setVisible(w >= presets_->minimumWidth());
            presets_->setBounds(header.removeFromRight(std::max(0, w))
                                    .withSizeKeepingCentre(std::max(0, w), theme_.sizes.controlCompact));
        }
        title_.setBounds(header);
        if (!instrument_)
        {
            area.removeFromTop(theme_.spacing.sm);
            auto mixRow = area.removeFromTop(theme_.sizes.controlCompact);
            mix_.setBounds(mixRow.removeFromRight(std::max(80, mixRow.getWidth() / 2)));
        }
        area.removeFromTop(theme_.spacing.md);
    }
    area.removeFromBottom(theme_.sizes.controlCompact); // the footer line
    if (body_ != nullptr)
    {
        body_->setBounds(area);
        return;
    }
    viewport_.setBounds(area);
    const int width = area.getWidth() - theme_.sizes.scrollbar;
    rows_.setSize(std::max(1, width), static_cast<int>(parameters_.size()) * rowHeight);
    int y = 0;
    for (auto& row : parameters_)
    {
        auto r = juce::Rectangle<int>(0, y, width, rowHeight);
        row.name->setBounds(r.removeFromLeft(std::min(160, width / 2)));
        row.value->setBounds(r.removeFromRight(80).withSizeKeepingCentre(80, theme_.sizes.controlCompact));
        row.knob->setBounds(r.withSizeKeepingCentre(theme_.sizes.knob, theme_.sizes.knob));
        y += rowHeight;
    }
}

} // namespace northlight::ui
