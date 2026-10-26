#include "ui/PluginWindow.h"

#include "core/Command.h"
#include "ui/Icons.h"
#include "ui/InsertMenu.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"

#include <functional>
#include <vector>

namespace northlight::ui
{

// The bar plus the plugin's editor. Follows the editor's size (a plugin may
// resize itself) and tells the window.
class PluginWindow::Holder final : public juce::Component
{
public:
    Holder(PluginWindow& window,
           hosting::PluginPresets& presets,
           std::unique_ptr<juce::Component> editor,
           const juce::String& pluginName,
           const juce::String& trackName,
           const juce::String& format,
           bool generic,
           bool canSwitchEditor)
        : window_(window), presets_(presets), editor_(std::move(editor)), pluginName_(pluginName),
          trackName_(trackName), format_(format), power_(theme_, Icon::power, "Bypass"),
          pin_(theme_, Icon::pin, "Keep this plugin's windows above the others"),
          generic_(
              theme_, Icon::settings, generic ? "Show the plugin's own editor" : "Show the generic editor")
    {
        generic_.setClickingTogglesState(true);
        generic_.setToggleState(generic, juce::dontSendNotification);
        generic_.onClick = [this]
        {
            if (window_.onShowGeneric)
            {
                const bool on = generic_.getToggleState();
                juce::MessageManager::callAsync(
                    [w = juce::Component::SafePointer<PluginWindow>(&window_), on]
                    {
                        if (w != nullptr && w->onShowGeneric)
                            w->onShowGeneric(on);
                    });
            }
        };
        if (canSwitchEditor)
            addAndMakeVisible(generic_);
        setOpaque(true);
        power_.setClickingTogglesState(true);
        power_.onClick = [this]
        {
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertBypass>(
                    window_.channelId(), window_.insertId(), !power_.getToggleState()));
        };
        addAndMakeVisible(power_);

        refreshPresetName();
        presetsButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        presetsButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
        presetsButton_.onClick = [this] { showPresetsMenu(); };
        addAndMakeVisible(presetsButton_);

        mix_.setSliderStyle(juce::Slider::LinearHorizontal);
        mix_.setRange(0.0, 100.0, 1.0);
        mix_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        mix_.setDoubleClickReturnValue(true, 100.0);
        mix_.setValue(100.0, juce::dontSendNotification);
        mix_.setTooltip("Wet/dry mix");
        mix_.onValueChange = [this]
        {
            repaint();
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertMix>(
                    window_.channelId(), window_.insertId(), static_cast<float>(mix_.getValue() / 100.0)));
        };
        addAndMakeVisible(mix_);

        pin_.setClickingTogglesState(true);
        pin_.onClick = [this]
        {
            window_.setAlwaysOnTop(pin_.getToggleState());
            if (window_.onPinChanged)
                window_.onPinChanged(pin_.getToggleState());
        };
        addAndMakeVisible(pin_);

        addAndMakeVisible(*editor_);
        setSize(std::max(editor_->getWidth(), PluginWindow::minWidth),
                editor_->getHeight() + PluginWindow::barHeight);
    }

    ~Holder() override
    {
        // The editor must go before the plugin instance it edits (the window keeps
        // the instance alive until after this holder is gone).
        removeChildComponent(editor_.get());
        editor_.reset();
    }

    juce::Component& editor() noexcept { return *editor_; }

    // The plugin's programs and preset files (InsertMenu.h): each load is one
    // undo step, and the button then names it.
    void showPresetsMenu()
    {
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        juce::Component::SafePointer<PluginWindow> window(&window_);
        addHostedPresetItems(menu,
                             actions,
                             presets_,
                             window_.keepAlive_,
                             window_.channelId(),
                             window_.insertId(),
                             [window](std::unique_ptr<core::Command> c)
                             {
                                 if (window != nullptr && window->onCommand)
                                     window->onCommand(std::move(c));
                             });
        juce::Component::SafePointer<Holder> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetsButton_),
                           [actions, safe](int chosen)
                           {
                               if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size())
                                   actions[static_cast<size_t>(chosen - 1)]();
                               if (safe != nullptr)
                                   safe->refreshPresetName();
                           });
    }

    // "Presets" until one is known, then its name ("Warm Pad").
    void refreshPresetName()
    {
        const auto name = presets_.currentPresetName();
        presetsButton_.setButtonText(name.empty() ? juce::String("Presets")
                                                  : juce::String::fromUTF8(name.c_str()));
        presetsButton_.setTooltip(name.empty() ? juce::String("Presets")
                                               : "Preset: " + juce::String::fromUTF8(name.c_str()));
    }

    void setPinned(bool pinned) { pin_.setToggleState(pinned, juce::dontSendNotification); }
    void setInsert(const core::Insert& insert)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        power_.setToggleState(!insert.bypass, juce::dontSendNotification);
        power_.setTooltip(insert.bypass ? "Bypassed: click to enable" : "Enabled: click to bypass");
        mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.canvas);
        g.setColour(c.surface0);
        g.fillRect(0, 0, getWidth(), PluginWindow::barHeight);
        g.setColour(c.lineSubtle);
        g.fillRect(0, PluginWindow::barHeight - 1, getWidth(), 1);
        // "Plugin · Track" after the power button, the badge after that, the mix
        // value after the slider.
        g.setFont(theme_.label());
        auto title = titleArea_;
        const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
        const int pluginW = juce::GlyphArrangement::getStringWidthInt(theme_.label(), pluginName_);
        const int dotW = juce::GlyphArrangement::getStringWidthInt(theme_.label(), dot);
        g.setColour(power_.getToggleState() ? c.textPrimary : c.textTertiary);
        g.drawText(pluginName_,
                   title.removeFromLeft(std::min(pluginW, title.getWidth())),
                   juce::Justification::centredLeft,
                   true);
        if (trackName_.isNotEmpty() && title.getWidth() > dotW)
        {
            g.setColour(c.textSecondary);
            g.drawText(dot, title.removeFromLeft(dotW), juce::Justification::centredLeft);
            g.drawText(trackName_, title, juce::Justification::centredLeft, true);
        }
        if (!badgeArea_.isEmpty())
        {
            g.setFont(theme_.captionCaps());
            const auto badge = badgeArea_.toFloat();
            g.setColour(c.accent.withAlpha(0.2f));
            g.fillRoundedRectangle(badge, badge.getHeight() * 0.5f);
            g.setColour(c.accent);
            g.drawText(format_, badgeArea_, juce::Justification::centred);
        }
        g.setFont(theme_.value());
        g.setColour(c.textSecondary);
        g.drawText(juce::String(static_cast<int>(mix_.getValue())) + "%",
                   mixValueArea_,
                   juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int control = theme_.sizes.controlCompact;
        auto bar = getLocalBounds()
                       .removeFromTop(PluginWindow::barHeight)
                       .reduced(theme_.spacing.sm, (PluginWindow::barHeight - control) / 2);
        power_.setBounds(bar.removeFromLeft(control));
        bar.removeFromLeft(theme_.spacing.sm);
        pin_.setBounds(bar.removeFromRight(control));
        bar.removeFromRight(theme_.spacing.sm);
        if (generic_.isVisible())
        {
            generic_.setBounds(bar.removeFromRight(control));
            bar.removeFromRight(theme_.spacing.xs);
        }
        // Wide enough for a preset's name; a long one ends in an ellipsis.
        presetsButton_.setBounds(bar.removeFromRight(std::clamp(bar.getWidth() / 4, 84, 160)));
        bar.removeFromRight(theme_.spacing.sm);
        mixValueArea_ = bar.removeFromRight(36);
        mix_.setBounds(bar.removeFromRight(80));
        bar.removeFromRight(theme_.spacing.sm);
        badgeArea_ = {};
        if (format_.isNotEmpty())
        {
            const int w = juce::GlyphArrangement::getStringWidthInt(theme_.captionCaps(), format_) +
                          2 * theme_.spacing.sm;
            badgeArea_ =
                bar.removeFromRight(w).withSizeKeepingCentre(w, theme_.sizes.icon + theme_.spacing.xxs);
            bar.removeFromRight(theme_.spacing.sm);
        }
        titleArea_ = bar;
        const juce::ScopedValueSetter<bool> guard(layingOut_, true);
        editor_->setTopLeftPosition(0, PluginWindow::barHeight);
    }

    // The plugin resized its editor: grow or shrink the window with it.
    void childBoundsChanged(juce::Component* child) override
    {
        if (child != editor_.get() || layingOut_)
            return;
        const int w = std::max(editor_->getWidth(), PluginWindow::minWidth);
        const int h = editor_->getHeight() + PluginWindow::barHeight;
        if (w != getWidth() || h != getHeight())
            window_.setContentComponentSize(w, h);
    }

private:
    const Theme& theme_ = Theme::dark();
    PluginWindow& window_;
    hosting::PluginPresets& presets_;
    std::unique_ptr<juce::Component> editor_;
    juce::String pluginName_, trackName_, format_;
    IconButton power_, pin_, generic_;
    juce::TextButton presetsButton_;
    juce::Slider mix_;
    juce::Rectangle<int> titleArea_, badgeArea_, mixValueArea_;
    bool updating_ = false;
    bool layingOut_ = false;
};

PluginWindow::PluginWindow(core::Id channelId,
                           core::Id insertId,
                           std::shared_ptr<engine::InsertProcessor> keepAlive,
                           hosting::PluginPresets& presets,
                           std::unique_ptr<juce::Component> editor,
                           bool resizable,
                           const juce::String& pluginName,
                           const juce::String& trackName,
                           const juce::String& format,
                           bool pinned,
                           bool generic,
                           bool canSwitchEditor)
    : juce::DocumentWindow(trackName.isEmpty() ? pluginName : trackName + " - " + pluginName,
                           Theme::dark().colours.canvas,
                           juce::DocumentWindow::closeButton,
                           !offscreenMode()),
      keepAlive_(std::move(keepAlive)), channelId_(channelId), insertId_(insertId)
{
    setUsingNativeTitleBar(true);
    auto holder = std::make_unique<Holder>(
        *this, presets, std::move(editor), pluginName, trackName, format, generic, canSwitchEditor);
    holder_ = holder.get();
    holder_->setPinned(pinned);
    setContentOwned(holder.release(), true);
    setResizable(resizable, false);
    setAlwaysOnTop(pinned);
    showWindow(*this);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent(); // deletes the holder, which deletes the editor first
    holder_ = nullptr;
}

void PluginWindow::setInsert(const core::Insert& insert)
{
    if (holder_ != nullptr)
    {
        holder_->setInsert(insert);
        holder_->refreshPresetName(); // an undone preset, or one loaded elsewhere
    }
}

void PluginWindow::refreshPresetName()
{
    if (holder_ != nullptr)
        holder_->refreshPresetName();
}

void PluginWindow::editorSizeChanged(int width, int height)
{
    setContentComponentSize(std::max(width, minWidth), height + barHeight);
}

void PluginWindow::setEditorSize(int width, int height)
{
    if (holder_ != nullptr && isResizable())
    {
        holder_->editor().setSize(width, height);
        setContentComponentSize(std::max(width, minWidth), height + barHeight);
    }
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        juce::MessageManager::callAsync([close = onClose] { close(); });
}

void PluginWindow::moved()
{
    juce::DocumentWindow::moved();
    if (onBoundsChanged && isVisible())
        onBoundsChanged(getBounds());
}

void PluginWindow::resized()
{
    juce::DocumentWindow::resized();
    if (onBoundsChanged && isVisible() && holder_ != nullptr)
        onBoundsChanged(getBounds());
}

} // namespace northlight::ui
