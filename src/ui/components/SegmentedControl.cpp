#include "ui/components/SegmentedControl.h"

#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

namespace
{
// UI Automation sees the control as a slider with a readable value (11.3).
template <typename Owner> class ValueInterface final : public juce::AccessibilityValueInterface
{
public:
    explicit ValueInterface(Owner& owner) : owner_(owner) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return owner_.accessibleValue(); }
    juce::String getCurrentValueAsString() const override { return owner_.accessibleValueText(); }
    void setValue(double newValue) override { owner_.setAccessibleValue(newValue); }
    void setValueAsString(const juce::String& newValue) override
    {
        owner_.setAccessibleValue(newValue.getDoubleValue());
    }
    AccessibleValueRange getRange() const override
    {
        const auto range = owner_.accessibleRange();
        return {{range.first, range.second}, owner_.accessibleStep()};
    }

private:
    Owner& owner_;
};
} // namespace

SegmentedControl::SegmentedControl(const Theme& theme) : theme_(theme)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
}

void SegmentedControl::setSegments(std::vector<juce::String> labels)
{
    labels_ = std::move(labels);
    selected_ = juce::jlimit(0, std::max(0, static_cast<int>(labels_.size()) - 1), selected_);
    resized();
    repaint();
}

int SegmentedControl::preferredWidth() const
{
    int width = 2 * theme_.spacing.xxs;
    for (const auto& label : labels_)
        width += juce::GlyphArrangement::getStringWidthInt(theme_.label(), label) + 2 * theme_.spacing.md;
    return width;
}

juce::Rectangle<float> SegmentedControl::segmentBounds(int index) const noexcept
{
    if (labels_.empty())
        return {};
    const auto inner = getLocalBounds().toFloat().reduced(static_cast<float>(theme_.spacing.xxs));
    const float w = inner.getWidth() / static_cast<float>(labels_.size());
    return {inner.getX() + w * static_cast<float>(index), inner.getY(), w, inner.getHeight()};
}

void SegmentedControl::setSelected(int index, juce::NotificationType notify)
{
    if (labels_.empty())
        return;
    index = juce::jlimit(0, static_cast<int>(labels_.size()) - 1, index);
    const bool changed = index != selected_;
    selected_ = index;
    animationFrom_ = thumbX_;
    thumbTargetX_ = segmentBounds(selected_).getX();
    const int duration = theme_.motion.ms(theme_.motion.toggleMs);
    if (duration > 0 && changed)
    {
        animationStart_ = juce::Time::getMillisecondCounterHiRes();
        startTimerHz(60);
    }
    else
        thumbX_ = thumbTargetX_;
    repaint();
    if (changed && notify != juce::dontSendNotification && onChange)
        onChange(selected_);
}

void SegmentedControl::timerCallback()
{
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - animationStart_;
    const double t = juce::jlimit(0.0, 1.0, elapsed / std::max(1, theme_.motion.ms(theme_.motion.toggleMs)));
    const double eased = 1.0 - std::pow(1.0 - t, 3.0); // decelerating
    thumbX_ = animationFrom_ + static_cast<float>(eased) * (thumbTargetX_ - animationFrom_);
    if (t >= 1.0)
        stopTimer();
    repaint();
}

void SegmentedControl::resized()
{
    thumbWidth_ = segmentBounds(0).getWidth();
    thumbX_ = thumbTargetX_ = segmentBounds(selected_).getX();
}

void SegmentedControl::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto bounds = getLocalBounds().toFloat();
    const float radius = bounds.getHeight() * 0.5f;
    g.setColour(c.surface1);
    g.fillRoundedRectangle(bounds, radius);
    if (!labels_.empty())
    {
        const auto thumb = segmentBounds(0).withX(thumbX_);
        g.setColour(c.surface3);
        g.fillRoundedRectangle(thumb, thumb.getHeight() * 0.5f);
    }
    g.setFont(theme_.label());
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
    {
        g.setColour(!isEnabled() ? c.textDisabled : i == selected_ ? c.textPrimary : c.textSecondary);
        g.drawText(
            labels_[static_cast<size_t>(i)], segmentBounds(i).toNearestInt(), juce::Justification::centred);
    }
    if (auto* lnf = dynamic_cast<AuralLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, bounds, radius);
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
        if (segmentBounds(i).contains(e.position))
            setSelected(i, juce::sendNotification);
}

bool SegmentedControl::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
        setSelected(selected_ - 1, juce::sendNotification);
    else if (key == juce::KeyPress::rightKey)
        setSelected(selected_ + 1, juce::sendNotification);
    else
        return false;
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> SegmentedControl::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::slider,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<ValueInterface<SegmentedControl>>(*this)});
}

} // namespace aural::ui
