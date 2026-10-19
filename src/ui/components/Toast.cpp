#include "ui/components/Toast.h"

#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

class ToastStack::Toast final : public juce::Component
{
public:
    Toast(const Theme& theme,
          Kind kind,
          const juce::String& message,
          const juce::String& action,
          std::function<void()> onAction)
        : theme_(theme), kind_(kind), message_(message), onAction_(std::move(onAction))
    {
        close_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
        close_.getProperties().set(AuralLookAndFeel::propertyIcon, true);
        close_.setTooltip("Dismiss");
        addAndMakeVisible(close_);
        if (action.isNotEmpty())
        {
            action_.setButtonText(action);
            action_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
            action_.onClick = [this]
            {
                if (onAction_)
                    onAction_();
                if (onDismiss)
                    onDismiss();
            };
            addAndMakeVisible(action_);
        }
        close_.onClick = [this]
        {
            if (onDismiss)
                onDismiss();
        };
    }

    bool persistent() const noexcept { return kind_ == Kind::warning || kind_ == Kind::error; }
    double shownAtMs = 0.0;
    std::function<void()> onDismiss;

    int preferredHeight() const
    {
        juce::TextLayout layout;
        juce::AttributedString text(message_);
        text.setFont(theme_.body());
        layout.createLayout(text, static_cast<float>(std::max(80, textWidth())));
        return std::max(theme_.sizes.controlLarge + theme_.spacing.sm,
                        static_cast<int>(layout.getHeight()) + 2 * theme_.spacing.md);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        g.setColour(c.lineStrong);
        g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
        const juce::Colour stripe = kind_ == Kind::success   ? c.success
                                    : kind_ == Kind::warning ? c.warning
                                    : kind_ == Kind::error   ? c.danger
                                                             : c.info;
        g.setColour(stripe);
        g.fillRoundedRectangle(bounds.withWidth(static_cast<float>(theme_.spacing.xs)).reduced(0.0f, 1.0f),
                               theme_.radius.sm);
        juce::TextLayout layout;
        juce::AttributedString text(message_);
        text.setFont(theme_.body());
        text.setColour(c.textPrimary);
        layout.createLayout(text, static_cast<float>(textWidth()));
        layout.draw(g, textArea().toFloat());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.sm, theme_.spacing.xs);
        close_.setBounds(
            area.removeFromRight(theme_.sizes.controlCompact)
                .withSizeKeepingCentre(theme_.sizes.controlCompact, theme_.sizes.controlCompact));
        if (action_.isVisible())
        {
            const int w = juce::GlyphArrangement::getStringWidthInt(theme_.label(), action_.getButtonText()) +
                          2 * theme_.spacing.sm;
            action_.setBounds(area.removeFromRight(w).withSizeKeepingCentre(w, theme_.sizes.controlCompact));
        }
    }

private:
    int textWidth() const
    {
        int w = getWidth() - theme_.spacing.lg - theme_.spacing.sm - theme_.sizes.controlCompact -
                theme_.spacing.sm;
        if (action_.isVisible())
            w -= action_.getWidth() + theme_.spacing.sm;
        return std::max(40, w);
    }
    juce::Rectangle<int> textArea() const
    {
        return {theme_.spacing.lg, theme_.spacing.md, textWidth(), getHeight() - 2 * theme_.spacing.md};
    }

    const Theme& theme_;
    Kind kind_;
    juce::String message_;
    std::function<void()> onAction_;
    juce::TextButton action_, close_{juce::CharPointer_UTF8("\xc3\x97")};
};

ToastStack::ToastStack(const Theme& theme) : theme_(theme)
{
    setInterceptsMouseClicks(false, true);
}

ToastStack::~ToastStack() = default;

void ToastStack::show(Kind kind,
                      const juce::String& message,
                      const juce::String& action,
                      std::function<void()> onAction)
{
    auto toast = std::make_unique<Toast>(theme_, kind, message, action, std::move(onAction));
    toast->shownAtMs = juce::Time::getMillisecondCounterHiRes();
    auto* raw = toast.get();
    toast->onDismiss = [this, raw] { dismiss(raw); };
    addAndMakeVisible(*toast);
    toasts_.push_back(std::move(toast));
    while (toasts_.size() > static_cast<size_t>(maxShown))
        toasts_.erase(toasts_.begin());
    layout();
    startTimer(250);
}

void ToastStack::dismissAll()
{
    toasts_.clear();
    layout();
}

void ToastStack::dismiss(Toast* toast)
{
    // From the toast's own button: drop it on the next message.
    juce::Component::SafePointer<ToastStack> safe(this);
    juce::MessageManager::callAsync(
        [safe, toast]
        {
            if (safe == nullptr)
                return;
            std::erase_if(safe->toasts_, [toast](const auto& t) { return t.get() == toast; });
            safe->layout();
        });
}

void ToastStack::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto before = toasts_.size();
    std::erase_if(toasts_,
                  [now](const auto& t) { return !t->persistent() && now - t->shownAtMs > autoDismissMs; });
    if (toasts_.size() != before)
        layout();
    if (toasts_.empty())
        stopTimer();
}

void ToastStack::resized()
{
    layout();
}

void ToastStack::layout()
{
    int bottom = getHeight();
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it)
    {
        auto& toast = **it;
        toast.setSize(getWidth(), toast.preferredHeight());
        const int h = toast.preferredHeight();
        toast.setBounds(0, bottom - h, getWidth(), h);
        bottom -= h + theme_.spacing.sm;
    }
    setVisible(!toasts_.empty());
}

} // namespace aural::ui
