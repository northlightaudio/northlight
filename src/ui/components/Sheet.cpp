#include "ui/components/Sheet.h"

#include "ui/Brand.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"

#include <algorithm>

namespace northlight::ui
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
            button->getProperties().set(NorthlightLookAndFeel::propertyVariant, "danger");
        else if (buttons_[i].primary)
            button->getProperties().set(NorthlightLookAndFeel::propertyVariant, "primary");
        else if (buttons_[i].ghost)
            button->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
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
                   std::function<void(int)> onResult,
                   std::unique_ptr<juce::Component> content,
                   int contentHeight,
                   SheetOptions options)
{
    auto* sheet = new Sheet(theme, title, body, std::move(buttons), std::move(onResult));
    sheet->options_ = std::move(options);
    if (sheet->options_.hero)
        sheet->wordmark_ = brand::drawable("northlight-wordmark.svg");
    if (content != nullptr)
    {
        sheet->content_ = std::move(content);
        sheet->contentHeight_ = contentHeight;
        sheet->addAndMakeVisible(*sheet->content_);
    }
    host.addAndMakeVisible(sheet);
    sheet->setBounds(host.getLocalBounds());
    sheet->toFront(true);
    sheet->grabKeyboardFocus();
    sheet->enter();
    return sheet;
}

void Sheet::enter()
{
    if (options_.appearAfterMs > 0)
    {
        // Invisible but already taking the input; it rises once the delay is up.
        setAlpha(0.0f);
        juce::Component::SafePointer<Sheet> safe(this);
        juce::Timer::callAfterDelay(options_.appearAfterMs,
                                    [safe]
                                    {
                                        if (safe != nullptr && !safe->finished_)
                                            safe->appearNow();
                                    });
        return;
    }
    rise();
}

void Sheet::appearNow()
{
    if (appearance_ == Appearance::waiting && !finished_)
        rise();
}

void Sheet::rise()
{
    appearance_ = Appearance::rising;
    const int ms = theme_.motion.ms(theme_.motion.panelMs);
    if (ms <= 0)
    {
        setAlpha(1.0f);
        appearance_ = Appearance::shown;
        return;
    }
    setAlpha(0.0f);
    rise_ = 8;
    resized();
    juce::Component::SafePointer<Sheet> safe(this);
    motion::Tween::run(
        *this,
        ms,
        [safe](float t)
        {
            if (safe == nullptr)
                return;
            safe->setAlpha(t);
            safe->rise_ = juce::roundToInt(8.0f * (1.0f - t));
            safe->resized();
            safe->repaint();
        },
        [safe]
        {
            if (safe != nullptr)
                safe->appearance_ = Appearance::shown;
        });
}

void Sheet::dismiss()
{
    finish(-1, false);
}

void Sheet::setButtonEnabled(int index, bool enabled)
{
    if (index >= 0 && static_cast<size_t>(index) < buttonComponents_.size())
        buttonComponents_[static_cast<size_t>(index)]->setEnabled(enabled);
}

void Sheet::finish(int result, bool callBack)
{
    if (finished_)
        return;
    finished_ = true;
    auto callback = callBack ? std::move(onResult_) : std::function<void(int)>();
    onResult_ = nullptr;
    setInterceptsMouseClicks(false, false); // the window behind is live again at once
    juce::Component::SafePointer<Sheet> safe(this);
    juce::MessageManager::callAsync(
        [safe, callback, result]
        {
            if (callback)
                callback(result);
            if (safe == nullptr)
                return;
            // Fade out, then go.
            auto* sheet = safe.getComponent();
            motion::Tween::cancel(*sheet);
            const float from = sheet->getAlpha();
            motion::Tween::run(
                *sheet,
                sheet->theme_.motion.ms(sheet->theme_.motion.menuMs),
                [safe, from](float t)
                {
                    if (safe != nullptr)
                        safe->setAlpha(from * (1.0f - t));
                },
                [safe]
                {
                    if (safe != nullptr)
                        delete safe.getComponent();
                });
        });
}

int Sheet::bodyHeight(int width) const
{
    if (body_.isEmpty())
        return 0;
    juce::TextLayout layout;
    juce::AttributedString text(body_);
    text.setFont(theme_.body());
    text.setLineSpacing(theme_.fonts.size.body * (theme_.fonts.lineHeight - 1.0f) + 2.0f);
    layout.createLayout(text, static_cast<float>(width));
    return static_cast<int>(std::ceil(layout.getHeight()));
}

int Sheet::eyebrowHeight() const
{
    return options_.eyebrow.isEmpty()
               ? 0
               : juce::roundToInt(theme_.fonts.size.caption * theme_.fonts.lineHeight) + theme_.spacing.xs;
}

juce::Rectangle<int> Sheet::panelBounds() const
{
    const int width = std::min(getWidth() - 2 * theme_.spacing.xl, options_.width);
    const int inner = width - 2 * theme_.spacing.xl;
    const int height = (options_.hero ? heroHeight : 0) + theme_.spacing.xl + eyebrowHeight() +
                       static_cast<int>(theme_.fonts.size.title * theme_.fonts.lineHeight) +
                       (body_.isNotEmpty() ? theme_.spacing.sm + bodyHeight(inner) : 0) + theme_.spacing.xl +
                       (contentHeight_ > 0 ? contentHeight_ + theme_.spacing.xl : 0) + theme_.sizes.control +
                       theme_.spacing.xl;
    return juce::Rectangle<int>(width, height).withCentre(getLocalBounds().getCentre()).translated(0, rise_);
}

void Sheet::paintHero(juce::Graphics& g, juce::Rectangle<int> panel) const
{
    const auto& c = theme_.colours;
    const auto band = panel.withHeight(heroHeight).toFloat();
    juce::Graphics::ScopedSaveState state(g);
    juce::Path clip;
    clip.addRoundedRectangle(band.getX(),
                             band.getY(),
                             band.getWidth(),
                             band.getHeight(),
                             theme_.radius.lg,
                             theme_.radius.lg,
                             true,
                             true,
                             false,
                             false);
    g.reduceClipRegion(clip);
    g.fillAll(c.canvas);
    // The sky and its moon: the top of the photograph at the band's width, nudged
    // up so the moon sits in the clear upper part of the band.
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    if (const auto& photo = brand::photo(); photo.isValid())
    {
        const float scale = band.getWidth() / static_cast<float>(photo.getWidth());
        g.drawImageTransformed(
            photo, juce::AffineTransform::scale(scale).translated(band.getX(), band.getY() - 16.0f));
    }
    g.setColour(c.canvas.withAlpha(0.12f));
    g.fillRect(band);
    // The band dissolves into the panel: clear over the sky, then dark quickly
    // under "Welcome to" and the wordmark so they read, and the panel by the end.
    juce::ColourGradient dusk = juce::ColourGradient::vertical(
        c.surface0.withAlpha(0.0f), band.getY() + band.getHeight() * 0.40f, c.surface0, band.getBottom());
    dusk.addColour(0.58, c.surface0.withAlpha(0.85f));
    g.setGradientFill(dusk);
    g.fillRect(band);
    // "Welcome to" and the wordmark, at the panel's text margin.
    const float x = band.getX() + static_cast<float>(theme_.spacing.xl);
    const float baseline = band.getBottom() - static_cast<float>(theme_.spacing.md);
    if (wordmark_ != nullptr)
        brand::drawWordmark(g, *wordmark_, x, baseline, 30.0f);
    g.setFont(theme_.captionCaps());
    g.setColour(c.textPrimary); // over the fading sky, secondary is too faint
    g.drawText("WELCOME TO",
               juce::Rectangle<float>(x, baseline - 30.0f - 22.0f, band.getWidth(), 16.0f),
               juce::Justification::centredLeft);
}

void Sheet::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.overlay);
    const auto panel = panelBounds();
    g.setColour(c.surface0);
    g.fillRoundedRectangle(panel.toFloat(), theme_.radius.lg);
    if (options_.hero)
        paintHero(g, panel);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), theme_.radius.lg, 1.0f);
    auto area = panel.withTrimmedTop(options_.hero ? heroHeight : 0).reduced(theme_.spacing.xl);
    if (options_.eyebrow.isNotEmpty())
    {
        g.setFont(theme_.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText(options_.eyebrow.toUpperCase(),
                   area.removeFromTop(eyebrowHeight() - theme_.spacing.xs),
                   juce::Justification::centredLeft);
        area.removeFromTop(theme_.spacing.xs);
    }
    g.setColour(c.textPrimary);
    g.setFont(theme_.title());
    g.drawText(title_,
               area.removeFromTop(static_cast<int>(theme_.fonts.size.title * theme_.fonts.lineHeight)),
               juce::Justification::centredLeft);
    if (body_.isNotEmpty())
    {
        area.removeFromTop(theme_.spacing.sm);
        juce::TextLayout layout;
        juce::AttributedString text(body_);
        text.setFont(theme_.body());
        text.setColour(c.textSecondary);
        text.setLineSpacing(theme_.fonts.size.body * (theme_.fonts.lineHeight - 1.0f) + 2.0f);
        layout.createLayout(text, static_cast<float>(area.getWidth()));
        layout.draw(g, area.removeFromTop(bodyHeight(area.getWidth())).toFloat());
    }
    // The footnote, at the left of the button row.
    if (options_.footnote.isNotEmpty())
    {
        auto row = panel.reduced(theme_.spacing.xl).removeFromBottom(theme_.sizes.control);
        if (!buttonComponents_.empty())
            row.setRight(buttonComponents_.front()->getX() - theme_.spacing.md);
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        g.drawFittedText(options_.footnote, row, juce::Justification::centredLeft, 2);
    }
}

void Sheet::resized()
{
    if (auto* parent = getParentComponent())
        setBounds(parent->getLocalBounds());
    auto panel = panelBounds().reduced(theme_.spacing.xl);
    auto row = panel.removeFromBottom(theme_.sizes.control);
    if (content_ != nullptr)
    {
        panel.removeFromBottom(theme_.spacing.xl);
        content_->setBounds(panel.removeFromBottom(contentHeight_));
    }
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
        const bool canCancel = std::any_of(
            buttonComponents_.begin(), buttonComponents_.end(), [](const auto& b) { return b->isEnabled(); });
        if (!options_.blocking || canCancel)
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
    return options_.blocking; // a blocking sheet keeps every key from the window
}

void Sheet::mouseDown(const juce::MouseEvent& e)
{
    if (!options_.blocking && !panelBounds().contains(e.getPosition()))
        finish(-1);
}

} // namespace northlight::ui
