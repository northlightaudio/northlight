#include "ui/strip/HardwareLever.h"

#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/HardwareParts.h"

#include <algorithm>

namespace northlight::ui::strip
{

HardwareLever::HardwareLever(const Theme& theme,
                             juce::String name,
                             std::vector<juce::String> positions,
                             juce::String folder,
                             std::array<juce::String, 3> files)
    : theme_(theme), name_(std::move(name)), positions_(std::move(positions)), folder_(std::move(folder)),
      files_(std::move(files))
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle(name_);
    update();
}

void HardwareLever::setPosition(int position)
{
    position_ = juce::jlimit(0, static_cast<int>(positions_.size()) - 1, position);
    update();
    repaint();
}

juce::Rectangle<float> HardwareLever::nutBounds() const
{
    const float w = static_cast<float>(getWidth());
    return {w * 0.5f - nutSize_ * 0.5f, w * 0.5f - nutSize_ * 0.5f, nutSize_, nutSize_};
}

void HardwareLever::paint(juce::Graphics& g)
{
    const auto nut = nutBounds();
    const int frame = positions_.size() == 2 && position_ == 1 ? 2 : position_;
    const auto& image = hardware::image(folder_, files_[static_cast<size_t>(frame)]);
    const bool enabled = isEnabled();
    if (image.isValid())
        hardware::drawScaled(g, image, getLocalBounds().toFloat(), enabled ? 1.0f : 0.5f);
    else
    {
        // Without its image: a washer and a line for the lever.
        const auto& hw = theme_.hardware;
        const float reach = nut.getWidth() * 0.38f;
        g.setColour(hw.aluminium.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
        g.fillEllipse(nut.reduced(nut.getWidth() * 0.04f));
        g.setColour(hw.knobBody.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
        if (frame == 1)
            g.fillEllipse(juce::Rectangle<float>(reach * 0.5f, reach * 0.5f)
                              .withCentre(nut.getCentre())); // pointing out
        else
            g.drawLine(juce::Line<float>(nut.getCentre(),
                                         nut.getCentre().translated(0.0f, frame == 0 ? -reach : reach)),
                       nut.getWidth() * 0.12f);
    }
    if (enabled && isMouseOver(true))
    {
        g.setColour(theme_.hardware.highlight.withAlpha(isMouseButtonDown() ? 0.10f : 0.07f));
        g.fillEllipse(nut);
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, nut, nut.getWidth() * 0.5f);
}

// The switch itself: the nut and the lever's travel above and below it, not the
// image's empty margin at the sides.
juce::Rectangle<float> HardwareLever::switchBounds() const
{
    const auto nut = nutBounds();
    const float reach = nut.getHeight() * 0.9f;
    return nut.withTop(std::max(0.0f, nut.getY() - reach))
        .withBottom(std::min(static_cast<float>(getHeight()), nut.getBottom() + reach))
        .expanded(nut.getWidth() * 0.15f, 0.0f);
}

bool HardwareLever::hitTest(int x, int y)
{
    return switchBounds().contains(static_cast<float>(x), static_cast<float>(y));
}

void HardwareLever::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    grabKeyboardFocus();
    // A two-way switch flips wherever it is clicked; a three-way one moves toward
    // the side clicked.
    if (positions_.size() == 2)
        press();
    else
        move(e.position.y < nutBounds().getCentreY() ? -1 : 1);
}

bool HardwareLever::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::upKey)
        move(-1);
    else if (key == juce::KeyPress::downKey)
        move(1);
    else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        press();
    else
        return false;
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> HardwareLever::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { press(); }));
}

void HardwareLever::press()
{
    const int n = static_cast<int>(positions_.size());
    choose(n == 2 ? 1 - position_ : (position_ + 1) % n);
}

void HardwareLever::move(int by)
{
    choose(juce::jlimit(0, static_cast<int>(positions_.size()) - 1, position_ + by));
}

void HardwareLever::choose(int position)
{
    if (position == position_)
        return;
    setPosition(position);
    if (onChoose)
        onChoose(position);
}

void HardwareLever::update()
{
    setTooltip(name_ + ": " + positions_[static_cast<size_t>(position_)]);
    setDescription(positions_[static_cast<size_t>(position_)]);
}

} // namespace northlight::ui::strip
