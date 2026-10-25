#include "ui/strip/CardFrame.h"

#include "plugins/ChannelStrip.h"

namespace northlight::ui::strip
{

namespace
{
// A small left chevron: collapse the card to its tab.
class CollapseButton final : public juce::Button
{
public:
    explicit CollapseButton(const Theme& theme) : juce::Button("Collapse"), theme_(theme)
    {
        setTooltip("Collapse");
    }
    void paintButton(juce::Graphics& g, bool isMouseOver, bool) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre(8.0f, 10.0f);
        juce::Path chevron;
        chevron.startNewSubPath(r.getRight() - 2.0f, r.getY());
        chevron.lineTo(r.getX() + 2.0f, r.getCentreY());
        chevron.lineTo(r.getRight() - 2.0f, r.getBottom());
        g.setColour(isMouseOver ? theme_.colours.textPrimary : theme_.colours.textTertiary);
        g.strokePath(chevron,
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    const Theme& theme_;
};
} // namespace

CardFrame::CardFrame(juce::Component& card, const Theme& theme, std::string type, juce::Colour colour)
    : card_(card), theme_(theme), type_(std::move(type)), colour_(colour),
      power_(theme, Icon::power, "Switch on")
{
    power_.setClickingTogglesState(true);
    power_.setOnColour(colour_);
    power_.onClick = [this]
    {
        if (onPower)
            onPower(power_.getToggleState());
    };
    card_.addAndMakeVisible(power_);
    power_.setTitle(juce::String(plugins::strip::displayName(type_)) + " on");
    collapse_ = std::make_unique<CollapseButton>(theme_);
    collapse_->onClick = [this]
    {
        if (onCollapse)
            onCollapse();
    };
    card_.addAndMakeVisible(*collapse_);
    // A slot that runs several models (the compressor: Clean or Opto) swaps
    // them from the header.
    if (const auto models = plugins::strip::alternatives(type_); models.size() > 1)
    {
        modelSwitch_ = std::make_unique<SegmentedControl>(theme_);
        std::vector<juce::String> labels;
        int selected = 0;
        for (size_t i = 0; i < models.size(); ++i)
        {
            labels.emplace_back(plugins::strip::modelName(models[i]));
            if (models[i] == type_)
                selected = static_cast<int>(i);
        }
        modelSwitch_->setSegments(labels);
        modelSwitch_->setSelected(selected, juce::dontSendNotification);
        modelSwitch_->setTitle("Compressor model");
        modelSwitch_->setTooltip("Clean or Opto");
        modelSwitch_->onChange = [this, models](int index)
        {
            if (onSwapModel && index >= 0 && static_cast<size_t>(index) < models.size() &&
                models[static_cast<size_t>(index)] != type_)
                onSwapModel(models[static_cast<size_t>(index)]);
        };
        card_.addAndMakeVisible(*modelSwitch_);
    }
}

CardFrame::~CardFrame() = default;

void CardFrame::setBypass(bool bypass)
{
    bypass_ = bypass;
    power_.setToggleState(!bypass_, juce::dontSendNotification);
    power_.setTooltip(bypass_ ? "Switch on" : "Switch off");
}

void CardFrame::setCollapsed(bool collapsed)
{
    collapsed_ = collapsed;
    if (collapsed_)
    {
        hiddenByCollapse_.clear();
        for (auto* child : card_.getChildren())
            if (child != &power_ && child->isVisible())
            {
                hiddenByCollapse_.emplace_back(child);
                child->setVisible(false);
            }
    }
    else
    {
        for (auto& child : hiddenByCollapse_)
            if (child != nullptr)
                child->setVisible(true);
        hiddenByCollapse_.clear();
    }
    card_.setMouseCursor(collapsed_ ? juce::MouseCursor::PointingHandCursor
                                    : juce::MouseCursor::NormalCursor);
}

void CardFrame::setEmbedded(bool embedded)
{
    embedded_ = embedded;
    power_.setVisible(!embedded);
    collapse_->setVisible(!embedded);
    if (modelSwitch_ != nullptr)
        modelSwitch_->setVisible(!embedded && !collapsed_);
}

bool CardFrame::showStatus(const juce::String& status)
{
    if (status == shownStatus_ || embedded_)
        return false;
    shownStatus_ = status;
    return true;
}

// The header row under the hue line, inset like the body.
juce::Rectangle<int> CardFrame::headerArea() const
{
    return card_.getLocalBounds()
        .removeFromTop(headerHeight)
        .withTrimmedTop(hueLine)
        .reduced(theme_.spacing.sm, 0);
}

juce::String CardFrame::headerName() const
{
    const auto& type = modelSwitch_ != nullptr ? plugins::strip::slotOf(type_) : type_;
    return juce::String(plugins::strip::displayName(type));
}

juce::Rectangle<int> CardFrame::layout()
{
    if (embedded_)
        return card_.getLocalBounds().reduced(theme_.spacing.sm, theme_.spacing.xs);
    auto header = headerArea();
    if (collapsed_)
    {
        power_.setBounds(
            header.withSizeKeepingCentre(theme_.sizes.controlCompact, theme_.sizes.controlCompact));
        return {};
    }
    power_.setBounds(header.removeFromLeft(theme_.sizes.controlCompact)
                         .withSizeKeepingCentre(theme_.sizes.controlCompact, theme_.sizes.controlCompact));
    collapse_->setBounds(header.removeFromRight(16));
    if (modelSwitch_ != nullptr)
    {
        // Just after the name.
        header.removeFromLeft(theme_.spacing.xs);
        header.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(theme_.label(), headerName()) +
                              theme_.spacing.sm);
        modelSwitch_->setBounds(
            header.removeFromLeft(std::min(header.getWidth(), modelSwitch_->preferredWidth()))
                .withSizeKeepingCentre(std::min(header.getWidth(), modelSwitch_->preferredWidth()),
                                       theme_.sizes.controlCompact - 2));
    }
    return card_.getLocalBounds().withTrimmedTop(headerHeight).reduced(theme_.spacing.sm, theme_.spacing.xs);
}

void CardFrame::paint(juce::Graphics& g, juce::Colour statusColour) const
{
    if (embedded_)
        return; // the frame draws the surface and the header
    const auto& c = theme_.colours;
    const auto bounds = card_.getLocalBounds().toFloat();
    g.setColour(c.surface1);
    g.fillRoundedRectangle(bounds, theme_.radius.md);
    g.setColour(c.lineSubtle);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
    if (collapsed_)
    {
        // The tab: the hue line, then the name running down under the power button.
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setColour(bypass_ ? colour_.withAlpha(0.25f) : colour_);
        g.fillRect(bounds.withHeight(static_cast<float>(hueLine)));
        const auto name = juce::String(plugins::strip::displayName(type_));
        g.setFont(theme_.label());
        g.setColour(bypass_ ? c.textSecondary : c.textPrimary);
        const float x = bounds.getCentreX();
        const float y = static_cast<float>(headerHeight + theme_.spacing.sm);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, x, y));
        g.drawText(name,
                   juce::Rectangle<float>(x, y - 10.0f, bounds.getHeight() - y - 8.0f, 20.0f),
                   juce::Justification::centredLeft);
        return;
    }
    // A live display's frame repaints inside the body: the header (its clip path
    // and the measured status) is drawn only when the repaint reaches it.
    if (!g.getClipBounds().intersects(headerArea().withY(0).withHeight(headerHeight)))
        return;
    // The hue line along the top: full when on, faint when off.
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setColour(bypass_ ? colour_.withAlpha(0.25f) : colour_);
        g.fillRect(bounds.withHeight(static_cast<float>(hueLine)));
    }
    auto header = headerArea();
    header.removeFromLeft(theme_.sizes.controlCompact + theme_.spacing.xs);
    header.removeFromRight(16 + theme_.spacing.xs); // the collapse chevron
    if (shownStatus_.isNotEmpty())
    {
        g.setFont(theme_.value());
        g.setColour(statusColour);
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), shownStatus_) + 4;
        g.drawText(shownStatus_, header.removeFromRight(w), juce::Justification::centredRight);
    }
    g.setFont(theme_.label());
    g.setColour(bypass_ ? c.textSecondary : c.textPrimary);
    g.drawText(headerName(), header, juce::Justification::centredLeft, true);
}

void CardFrame::paintOverChildren(juce::Graphics& g) const
{
    if (!bypass_ || collapsed_ || embedded_)
        return;
    // Off: the body dims (and stays usable).
    auto body = card_.getLocalBounds().withTrimmedTop(headerHeight).toFloat().reduced(1.0f);
    g.setColour(theme_.colours.surface1.withAlpha(0.42f));
    g.fillRoundedRectangle(body, theme_.radius.md);
}

} // namespace northlight::ui::strip
