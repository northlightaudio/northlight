#include "ui/strip/ModuleCard.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "ui/components/Accessibility.h"
#include "ui/strip/StripStyle.h"

namespace northlight::ui::strip
{

namespace
{
juce::String formatValue(const engine::ParameterInfo& info, double v)
{
    // The display hints first: what the minimum means, a note, a whole number.
    if (!info.minimumText.empty() && v <= info.minimum + 1e-6)
        return juce::String::fromUTF8(info.minimumText.c_str());
    if (info.showsNote)
        return juce::String::fromUTF8(core::noteName(static_cast<int>(std::lround(v))).c_str());
    if (info.integer && info.choices.empty())
    {
        const auto n = static_cast<int>(std::lround(v));
        return juce::String(n) + (info.unit.empty() ? juce::String() : " " + juce::String(info.unit));
    }
    if (!info.choices.empty())
    {
        const auto i = static_cast<size_t>(
            juce::jlimit(0, static_cast<int>(info.choices.size()) - 1, static_cast<int>(std::lround(v))));
        return juce::String(info.choices[i]);
    }
    if (info.unit == "Hz")
        return hzText(v);
    if (info.unit == "ms")
        return v >= 100.0 ? juce::String(static_cast<int>(std::lround(v))) + " ms"
                          : juce::String(v, v >= 10.0 ? 1 : 2) + " ms";
    if (info.unit == "dB")
        return dbText(v);
    if (info.unit == ":1")
        return juce::String(v, v >= 10.0 ? 1 : 2) + ":1";
    if (info.unit == "%")
        return percentText(v);
    return juce::String(v, 2);
}
} // namespace

// ----- pill toggle ------------------------------------------------------------------------------

PillToggle::PillToggle(const Theme& theme, const juce::String& text, juce::Colour onColour)
    : juce::Button(text), theme_(theme), onColour_(onColour)
{
    setClickingTogglesState(true);
}

void PillToggle::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? onColour_.withAlpha(isButtonDown ? 0.35f : 0.25f)
                   : (isMouseOver ? c.surface3 : c.surface2));
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    if (on)
    {
        g.setColour(onColour_.withAlpha(0.6f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
    }
    g.setColour(on ? onColour_ : c.textSecondary);
    g.setFont(theme_.caption());
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

// ----- card -------------------------------------------------------------------------------------

ModuleCard::ModuleCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
    : theme_(theme), host_(host), channelId_(channelId), moduleId_(module.id), type_(module.type),
      colour_(moduleColour(theme, module.type)), binder_(module.type, module.params),
      frame_(*this, theme, module.type, colour_)
{
    setOpaque(false);
    frame_.onPower = [this](bool on)
    {
        if (!binder_.updating() && host_.onCommand)
            host_.onCommand(std::make_unique<core::SetInsertBypass>(channelId_, moduleId_, !on));
    };
    frame_.onCollapse = [this]
    {
        if (host_.onCollapse)
            host_.onCollapse(moduleId_, true);
    };
    frame_.onSwapModel = [this](const std::string& model)
    {
        if (!binder_.updating() && host_.onSwapModel)
            host_.onSwapModel(moduleId_, model);
    };
    frame_.setBypass(module.bypass);
}

ModuleCard::~ModuleCard() = default;

double ModuleCard::sampleRate() const noexcept
{
    const auto* fb = liveFeedback();
    return fb != nullptr ? fb->sampleRate() : 48000.0;
}

double ModuleCard::tempoBpm() const noexcept
{
    const auto* fb = liveFeedback();
    return fb != nullptr ? fb->tempoBpm() : 120.0;
}

void ModuleCard::setParam(int index, float value)
{
    if (!binder_.set(index, value))
        return;
    paramsChanged();
    if (host_.onCommand)
        host_.onCommand(
            std::make_unique<core::SetInsertParameter>(channelId_, moduleId_, index, binder_.value(index)));
}

void ModuleCard::applyValues(const std::vector<float>& values, const juce::String& name)
{
    if (!binder_.setAll(values))
        return;
    paramsChanged();
    if (host_.onCommand)
        host_.onCommand(std::make_unique<core::SetInsertParameters>(
            channelId_, moduleId_, binder_.values(), name.toStdString()));
}

void ModuleCard::beginGesture(const juce::String& name)
{
    if (host_.onGestureBegin)
        host_.onGestureBegin(name.toStdString());
}

void ModuleCard::endGesture()
{
    if (host_.onGestureEnd)
        host_.onGestureEnd();
}

void ModuleCard::refresh(const core::Insert& module)
{
    const auto quiet = binder_.quiet();
    const bool bypassChanged = module.bypass != frame_.bypassed();
    frame_.setBypass(module.bypass);
    // Only the model's changes come in: a refresh that repeats a value would
    // pull a control off what automation is playing (tick follows the live
    // processor), flipping between the two every frame.
    const bool changed = binder_.takeModel(module.params) || bypassChanged;
    insertChanged(module);
    if (!changed)
        return;
    binder_.updateAll();
    paramsChanged();
    // Undo, a preset or automation: a rendered face repaints round what moved
    // (docs/ui-performance.md); the whole card only when bypass dims it.
    if (bypassChanged || !repaintsChangedParameters())
        repaint();
}

void ModuleCard::stopWatching()
{
    live_.stop();
}

void ModuleCard::setCollapsed(bool collapsed)
{
    if (collapsed == frame_.isCollapsed())
        return;
    frame_.setCollapsed(collapsed);
    if (collapsed)
        if (auto* fb = liveFeedback())
            fb->setWatched(false);
    resized();
    repaint();
}

void ModuleCard::tick(double elapsedMs)
{
    live_.attach(host_.liveInsert ? host_.liveInsert(moduleId_) : nullptr);
    if (frame_.isCollapsed())
    {
        // A collapsed card shows no levels; let a switched-off module stop measuring.
        live_.pause();
        return;
    }
    // Automation sets the processor's parameters: the controls follow.
    if (const auto* live = live_.processor();
        live != nullptr && !isMouseButtonDown(true) && binder_.follow(*live))
        paramsChanged();
    if (live_.read(elapsedMs, theme_.motion))
        framesArrived(elapsedMs);
    ticked(elapsedMs);
    if (frame_.showStatus(headerStatus()))
        repaint(getLocalBounds().removeFromTop(headerHeight));
}

Knob& ModuleCard::addKnob(int index, const juce::String& label)
{
    const auto& i = info(index);
    auto knob = std::make_unique<Knob>(theme_);
    knob->setRange(i.minimum, i.maximum, i.defaultValue);
    knob->setLogarithmic(i.logarithmic);
    knob->setArcColour(colour_);
    knob->setShowValue(true);
    const auto copy = i;
    knob->format = [copy](double v) { return formatValue(copy, v); };
    knob->setValue(param(index), juce::dontSendNotification);
    knob->setTitle(i.name);
    knob->onDragStart = [this, index] { beginGesture(info(index).name); };
    knob->onDragEnd = [this] { endGesture(); };
    knob->onValueChanged = [this, index](double v)
    {
        if (!binder_.updating())
            setParam(index, static_cast<float>(v));
    };
    addAndMakeVisible(*knob);
    binder_.bind({.index = index,
                  .knob = knob.get(),
                  .label = &addLabel(label.isNotEmpty() ? label : juce::String(i.name))});
    auto& result = *knob;
    owned_.push_back(std::move(knob));
    return result;
}

juce::Label& ModuleCard::addLabel(const juce::String& text)
{
    auto label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);
    label->setFont(theme_.caption());
    label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    label->setJustificationType(juce::Justification::centred);
    label->setMinimumHorizontalScale(0.75f);
    label->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*label);
    auto& result = *label;
    owned_.push_back(std::move(label));
    return result;
}

SegmentedControl& ModuleCard::addChoice(int index)
{
    const auto& i = info(index);
    auto control = std::make_unique<SegmentedControl>(theme_);
    std::vector<juce::String> labels;
    for (const auto& c : i.choices)
        labels.emplace_back(c);
    control->setSegments(labels);
    control->setSelected(static_cast<int>(std::lround(param(index))), juce::dontSendNotification);
    control->setTitle(i.name);
    control->onChange = [this, index](int selected)
    {
        if (!binder_.updating())
            setParam(index, static_cast<float>(selected));
    };
    addAndMakeVisible(*control);
    binder_.bind({.index = index, .choice = control.get()});
    auto& result = *control;
    owned_.push_back(std::move(control));
    return result;
}

PillToggle& ModuleCard::addToggle(int index, const juce::String& text)
{
    auto toggle = std::make_unique<PillToggle>(theme_, text, colour_);
    showToggle(*toggle, param(index) >= 0.5f);
    toggle->setTooltip(info(index).name);
    toggle->onClick = [this, index, t = toggle.get()]
    {
        if (!binder_.updating())
            setParam(index, t->getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(*toggle);
    binder_.bind({.index = index, .toggle = toggle.get()});
    auto& result = *toggle;
    owned_.push_back(std::move(toggle));
    return result;
}

// A knob's stack, top down: the ring, `spacing.xs` and 2, the value's line, then
// the label tucked up into that line's descent so it reads with its value.
int ModuleCard::labelTop(int ring) const noexcept
{
    return ring + theme_.spacing.xs + theme_.lineHeight(theme_.fonts.size.value) - 3;
}

int ModuleCard::knobCellHeight(int ring, float labelSize) const noexcept
{
    // The stack, then a gap to the next row so each label reads as its own knob's.
    return labelTop(ring) + theme_.lineHeight(labelSize) + theme_.spacing.sm;
}

int ModuleCard::knobCellHeight() const noexcept
{
    return knobCellHeight(theme_.sizes.knob, theme_.fonts.size.caption);
}

void ModuleCard::placeKnob(Knob& knob, juce::Rectangle<int> cell, float labelSize)
{
    const int ring = knob.ringSize();
    const int top = labelTop(ring);
    const auto stack = cell.withTrimmedBottom(theme_.spacing.sm)
                           .removeFromBottom(knobCellHeight(ring, labelSize) - theme_.spacing.sm);
    knob.setBounds(stack.withHeight(top + 5)); // the knob's own box holds the whole value line
    if (auto* label = labelFor(knob))
        label->setBounds(stack.withTrimmedTop(top));
}

juce::Rectangle<int> ModuleCard::labelLine(juce::Rectangle<int> cell) const
{
    return cell.withTrimmedBottom(theme_.spacing.sm)
        .removeFromBottom(theme_.lineHeight(theme_.fonts.size.caption));
}

juce::Rectangle<int> ModuleCard::aboveLabelLine(juce::Rectangle<int> cell) const
{
    return cell.withTrimmedBottom(theme_.spacing.sm + theme_.lineHeight(theme_.fonts.size.caption));
}

int ModuleCard::minimumHeight() const
{
    if (frame_.isCollapsed())
        return 0;
    return (frame_.isEmbedded() ? 0 : headerHeight) + minimumBodyHeight();
}

void ModuleCard::layoutKnobs(juce::Rectangle<int> area, int perRow, const std::vector<Knob*>& knobs)
{
    perRow = std::max(1, perRow);
    const int cellW = area.getWidth() / perRow;
    const int cellH = knobCellHeight();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
        placeKnob(*knobs[i],
                  juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH));
    }
}

void ModuleCard::setEmbedded(bool embedded)
{
    frame_.setEmbedded(embedded);
    resized();
    repaint();
}

void ModuleCard::paint(juce::Graphics& g)
{
    frame_.paint(g, headerStatusColour());
}

void ModuleCard::paintOverChildren(juce::Graphics& g)
{
    frame_.paintOverChildren(g);
}

void ModuleCard::resized()
{
    const auto body = frame_.layout();
    if (frame_.isEmbedded() || !frame_.isCollapsed())
        layoutBody(body);
}

void ModuleCard::mouseDown(const juce::MouseEvent& e)
{
    if (frame_.isCollapsed())
    {
        if (host_.onCollapse)
            host_.onCollapse(moduleId_, false);
        return;
    }
    dragging_ = !frame_.isEmbedded() && e.y < headerHeight && e.mods.isLeftButtonDown();
}

void ModuleCard::mouseMove(const juce::MouseEvent& e)
{
    if (!frame_.isCollapsed() && !frame_.isEmbedded())
        setMouseCursor(e.y < headerHeight ? juce::MouseCursor::DraggingHandCursor
                                          : juce::MouseCursor::NormalCursor);
}

void ModuleCard::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ && e.mouseWasDraggedSinceMouseDown() && host_.onReorder)
        host_.onReorder(moduleId_, e.getScreenPosition(), false);
}

void ModuleCard::mouseUp(const juce::MouseEvent& e)
{
    if (dragging_ && e.mouseWasDraggedSinceMouseDown() && host_.onReorder)
        host_.onReorder(moduleId_, e.getScreenPosition(), true);
    dragging_ = false;
}

} // namespace northlight::ui::strip
