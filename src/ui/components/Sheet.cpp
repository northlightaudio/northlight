#include "ui/components/Sheet.h"

#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

Sheet::Sheet(const Theme& theme,
             const juce::String& title,
             const juce::String& body,
             std::vector<Button> buttons,
             std::function<void(int)> onResult)
    : theme_(theme), title_(title), body_(body), buttons_(std::move(buttons)), onResult_(std::move(onResult))
{
    setWantsKeyboardFocus(true);
    for (size_t i = 0; i < buttons_.size(); ++i)
    {
        auto button = std::make_unique<juce::TextButton>(buttons_[i].label);
        if (buttons_[i].destructive)
            button->getProperties().set(AuralLookAndFeel::propertyVariant, "danger");
        else if (buttons_[i].primary)
            button->getProperties().set(AuralLookAndFeel::propertyVariant, "primary");
        const int index = static_cast<int>(i);
        button->onClick = [this, index] { finish(index); };
        addAndMakeVisible(*button);
        buttonComponents_.push_back(std::move(button));
    }
}

Sheet* Sheet::show(juce::Component& host,
                   const Theme& theme,
                   const juce::String& title,
                   const juce::String& body,
                   std::vector<Button> buttons,
                   std::function<void(int)> onResult)
{
    auto* sheet = new Sheet(theme, title, body, std::move(buttons), std::move(onResult));
    host.addAndMakeVisible(sheet);
    sheet->setBounds(host.getLocalBounds());
    sheet->toFront(true);
    sheet->grabKeyboardFocus();
    return sheet;
}

void Sheet::finish(int result)
{
    if (finished_)
        return;
    finished_ = true;
    auto callback = std::move(onResult_);
    juce::Component::SafePointer<Sheet> safe(this);
    juce::MessageManager::callAsync(
        [safe, callback, result]
        {
            if (callback)
                callback(result);
            if (safe != nullptr)
                delete safe.getComponent();
        });
}

juce::Rectangle<int> Sheet::panelBounds() const
{
    const int width = std::min(getWidth() - 2 * theme_.spacing.xl, 480);
    juce::TextLayout layout;
    juce::AttributedString text(body_);
    text.setFont(theme_.body());
    layout.createLayout(text, static_cast<float>(width - 2 * theme_.spacing.xl));
    const int height = theme_.spacing.xl +
                       static_cast<int>(theme_.fonts.size.title * theme_.fonts.lineHeight) +
                       theme_.spacing.md + static_cast<int>(layout.getHeight()) + theme_.spacing.xl +
                       theme_.sizes.control + theme_.spacing.xl;
    return juce::Rectangle<int>(width, height).withCentre(getLocalBounds().getCentre());
}

void Sheet::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.overlay);
    const auto panel = panelBounds();
    g.setColour(c.surface0);
    g.fillRoundedRectangle(panel.toFloat(), theme_.radius.lg);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), theme_.radius.lg, 1.0f);
    auto area = panel.reduced(theme_.spacing.xl);
    g.setColour(c.textPrimary);
    g.setFont(theme_.title());
    g.drawText(title_,
               area.removeFromTop(static_cast<int>(theme_.fonts.size.title * theme_.fonts.lineHeight)),
               juce::Justification::centredLeft);
    area.removeFromTop(theme_.spacing.md);
    juce::TextLayout layout;
    juce::AttributedString text(body_);
    text.setFont(theme_.body());
    text.setColour(c.textSecondary);
    layout.createLayout(text, static_cast<float>(area.getWidth()));
    layout.draw(g, area.withTrimmedBottom(theme_.sizes.control + theme_.spacing.xl).toFloat());
}

void Sheet::resized()
{
    if (auto* parent = getParentComponent())
        setBounds(parent->getLocalBounds());
    auto row = panelBounds().reduced(theme_.spacing.xl).removeFromBottom(theme_.sizes.control);
    for (auto it = buttonComponents_.rbegin(); it != buttonComponents_.rend(); ++it)
    {
        auto& button = **it;
        const int w = juce::GlyphArrangement::getStringWidthInt(theme_.label(), button.getButtonText()) +
                      2 * theme_.spacing.md;
        button.setBounds(row.removeFromRight(std::max(w, theme_.sizes.control * 3)));
        row.removeFromRight(theme_.spacing.sm);
    }
}

bool Sheet::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        finish(-1);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i].primary && !buttons_[i].destructive)
            {
                finish(static_cast<int>(i));
                return true;
            }
    return false;
}

void Sheet::mouseDown(const juce::MouseEvent& e)
{
    if (!panelBounds().contains(e.getPosition()))
        finish(-1);
}

} // namespace aural::ui
