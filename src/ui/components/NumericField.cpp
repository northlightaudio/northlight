#include "ui/components/NumericField.h"

#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

NumericField::NumericField(const Theme& theme) : theme_(theme)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void NumericField::setRange(double minimum, double maximum, double step)
{
    minimum_ = minimum;
    maximum_ = std::max(maximum, minimum);
    step_ = std::max(step, 1.0e-9);
    setValue(value_, juce::dontSendNotification);
}

void NumericField::setValue(double value, juce::NotificationType notify)
{
    value_ = juce::jlimit(minimum_, maximum_, value);
    setTooltip(text() + (unit_.isEmpty() ? juce::String() : " " + unit_));
    repaint();
    if (notify != juce::dontSendNotification && onValueChanged)
        onValueChanged(value_);
}

void NumericField::setUnit(const juce::String& unit)
{
    unit_ = unit;
    repaint();
}

juce::String NumericField::text() const
{
    return format ? format(value_) : juce::String(value_, decimals_);
}

void NumericField::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(isEnabled() ? c.surface2 : c.surface1);
    g.fillRoundedRectangle(bounds, theme_.radius.sm);
    g.setColour(hasKeyboardFocus(true) ? c.accent : c.lineStrong);
    g.drawRoundedRectangle(bounds, theme_.radius.sm, 1.0f);
    if (editor_ != nullptr)
        return;
    auto area = getLocalBounds().reduced(theme_.spacing.sm, 0);
    if (unit_.isNotEmpty())
    {
        g.setFont(theme_.small());
        g.setColour(c.textSecondary);
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), unit_);
        g.drawText(unit_, area.removeFromRight(w), juce::Justification::centredRight);
        area.removeFromRight(theme_.spacing.xxs); // a thin space
    }
    g.setFont(theme_.value());
    g.setColour(isEnabled() ? c.textPrimary : c.textDisabled);
    g.drawText(text(), area, juce::Justification::centredRight);
    if (auto* lnf = dynamic_cast<AuralLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, bounds, theme_.radius.sm);
}

void NumericField::resized()
{
    if (editor_ != nullptr)
        editor_->setBounds(getLocalBounds());
}

void NumericField::stepBy(double steps)
{
    setValue(value_ + steps * step_, juce::sendNotification);
}

void NumericField::mouseDown(const juce::MouseEvent& e)
{
    if (editor_ != nullptr)
        return;
    grabKeyboardFocus();
    dragStartValue_ = value_;
    dragStartY_ = e.y;
    dragging_ = true;
    dragged_ = false;
}

void NumericField::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_ || editor_ != nullptr)
        return;
    if (!dragged_ && e.mouseWasDraggedSinceMouseDown())
    {
        dragged_ = true;
        if (onDragStart)
            onDragStart();
    }
    if (!dragged_)
        return;
    const double perPixel = e.mods.isShiftDown() ? 0.1 : e.mods.isCtrlDown() ? 10.0 : 1.0;
    setValue(dragStartValue_ + static_cast<double>(dragStartY_ - e.y) * perPixel * step_,
             juce::sendNotification);
}

void NumericField::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ && dragged_ && onDragEnd)
        onDragEnd();
    dragging_ = dragged_ = false;
}

void NumericField::mouseDoubleClick(const juce::MouseEvent&)
{
    beginEditing();
}

void NumericField::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!hasKeyboardFocus(true) || editor_ != nullptr)
        return;
    stepBy((wheel.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? 0.1 : 1.0));
}

void NumericField::beginEditing()
{
    if (editor_ != nullptr)
        return;
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setFont(theme_.value());
    editor_->setJustification(juce::Justification::centredRight);
    editor_->setText(text(), juce::dontSendNotification);
    editor_->setSelectAllWhenFocused(true);
    editor_->onReturnKey = [this] { endEditing(true); };
    editor_->onEscapeKey = [this] { endEditing(false); };
    editor_->onFocusLost = [this] { endEditing(true); };
    addAndMakeVisible(*editor_);
    resized();
    editor_->grabKeyboardFocus();
}

void NumericField::endEditing(bool commit)
{
    if (editor_ == nullptr)
        return;
    const auto typed = editor_->getText();
    auto editor = std::move(editor_);
    removeChildComponent(editor.get());
    editor.reset();
    if (commit)
    {
        const double parsed = parse ? parse(typed) : typed.retainCharacters("0123456789.-+").getDoubleValue();
        if (onDragStart)
            onDragStart();
        setValue(parsed, juce::sendNotification);
        if (onDragEnd)
            onDragEnd();
    }
    repaint();
}

bool NumericField::keyPressed(const juce::KeyPress& key)
{
    if (editor_ != nullptr)
        return false;
    if (onKey && onKey(key))
        return true;
    const double scale = key.getModifiers().isShiftDown()  ? 0.1
                         : key.getModifiers().isCtrlDown() ? 10.0
                                                           : 1.0;
    if (key == juce::KeyPress::upKey)
        stepBy(scale);
    else if (key == juce::KeyPress::downKey)
        stepBy(-scale);
    else if (key == juce::KeyPress::returnKey || key == juce::KeyPress(juce::KeyPress::F2Key))
        beginEditing();
    else
        return false;
    return true;
}

} // namespace aural::ui
