#include "ui/components/SegmentedControl.h"

#include "ui/NorthlightLookAndFeel.h"
#include "ui/OpticalCentre.h"
#include "ui/components/Accessibility.h"

#include <algorithm>

namespace northlight::ui
{

SegmentedControl::SegmentedControl(const Theme& theme) : theme_(theme), track_(theme.colours.surface1)
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

void SegmentedControl::setSegmentEnabled(int index, bool enabled)
{
    if (index < 0)
        return;
    if (disabled_.size() <= static_cast<size_t>(index))
        disabled_.resize(static_cast<size_t>(index) + 1, false);
    disabled_[static_cast<size_t>(index)] = !enabled;
    repaint();
}

bool SegmentedControl::isSegmentEnabled(int index) const noexcept
{
    return index >= 0 &&
           (static_cast<size_t>(index) >= disabled_.size() || !disabled_[static_cast<size_t>(index)]);
}

void SegmentedControl::setSegmentTooltip(int index, const juce::String& tooltip)
{
    if (index < 0)
        return;
    if (tooltips_.size() <= static_cast<size_t>(index))
        tooltips_.resize(static_cast<size_t>(index) + 1);
    tooltips_[static_cast<size_t>(index)] = tooltip;
}

juce::String SegmentedControl::getTooltip()
{
    const auto mouse = getMouseXYRelative().toFloat();
    for (int i = 0; i < static_cast<int>(tooltips_.size()); ++i)
        if (tooltips_[static_cast<size_t>(i)].isNotEmpty() && segmentBounds(i).contains(mouse))
            return tooltips_[static_cast<size_t>(i)];
    return juce::SettableTooltipClient::getTooltip();
}

int SegmentedControl::preferredWidth() const
{
    // The segments are equal, so each is as wide as the widest label needs.
    int widest = 0;
    for (const auto& label : labels_)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(theme_.label(), label));
    return 2 * theme_.spacing.xxs + static_cast<int>(labels_.size()) * (widest + 2 * theme_.spacing.md);
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
    g.setColour(track_);
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
        g.setColour(!isEnabled() || !isSegmentEnabled(i) ? c.textDisabled
                    : i == selected_                     ? c.textPrimary
                                                         : c.textSecondary);
        drawOpticallyCentred(g, labels_[static_cast<size_t>(i)], segmentBounds(i));
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, bounds, radius);
}

void SegmentedControl::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
        if (segmentBounds(i).contains(e.position) && isSegmentEnabled(i))
            setSelected(i, juce::sendNotification);
}

bool SegmentedControl::keyPressed(const juce::KeyPress& key)
{
    int step = 0;
    if (key == juce::KeyPress::leftKey)
        step = -1;
    else if (key == juce::KeyPress::rightKey)
        step = 1;
    else
        return false;
    // Skip disabled segments.
    for (int i = selected_ + step; i >= 0 && i < static_cast<int>(labels_.size()); i += step)
        if (isSegmentEnabled(i))
        {
            setSelected(i, juce::sendNotification);
            break;
        }
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> SegmentedControl::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::slider,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<AccessibleValue<SegmentedControl>>(*this)});
}

} // namespace northlight::ui
