#include "ui/PluginWindow.h"

#include <functional>
#include <vector>

namespace aural::ui
{

// The bar plus the plugin's editor. Follows the editor's size (a plugin may
// resize itself) and tells the window.
class PluginWindow::Holder final : public juce::Component
{
public:
    Holder(PluginWindow& window, hosting::PluginPresets& presets, std::unique_ptr<juce::Component> editor)
        : window_(window), presets_(presets), editor_(std::move(editor))
    {
        setOpaque(true);
        bypass_.setButtonText("Bypass");
        bypass_.setClickingTogglesState(true);
        bypass_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.warning);
        bypass_.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
        bypass_.onClick = [this]
        {
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertBypass>(
                    window_.channelId(), window_.insertId(), bypass_.getToggleState()));
        };
        addAndMakeVisible(bypass_);

        presetsButton_.setButtonText("Presets");
        presetsButton_.onClick = [this] { showPresetsMenu(); };
        addAndMakeVisible(presetsButton_);

        mixLabel_.setText("Mix", juce::dontSendNotification);
        mixLabel_.setFont(juce::FontOptions{theme_.type.caption});
        mixLabel_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        addAndMakeVisible(mixLabel_);
        mix_.setSliderStyle(juce::Slider::LinearHorizontal);
        mix_.setRange(0.0, 100.0, 1.0);
        mix_.setTextValueSuffix("%");
        mix_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
        mix_.setDoubleClickReturnValue(true, 100.0);
        mix_.setValue(100.0, juce::dontSendNotification);
        mix_.onValueChange = [this]
        {
            if (!updating_ && window_.onCommand)
                window_.onCommand(std::make_unique<core::SetInsertMix>(
                    window_.channelId(), window_.insertId(), static_cast<float>(mix_.getValue() / 100.0)));
        };
        addAndMakeVisible(mix_);

        pin_.setButtonText("Pin");
        pin_.setTooltip("Keep this window above the others");
        pin_.setClickingTogglesState(true);
        pin_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.accent);
        pin_.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
        pin_.onClick = [this]
        {
            window_.setAlwaysOnTop(pin_.getToggleState());
            if (window_.onPinChanged)
                window_.onPinChanged(pin_.getToggleState());
        };
        addAndMakeVisible(pin_);

        addAndMakeVisible(*editor_);
        setSize(std::max(editor_->getWidth(), 340), editor_->getHeight() + PluginWindow::barHeight);
    }

    ~Holder() override
    {
        // The editor must go before the plugin instance it edits (the window keeps
        // the instance alive until after this holder is gone).
        removeChildComponent(editor_.get());
        editor_.reset();
    }

    void showPresetsMenu()
    {
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        int item = 1;
        const auto programs = presets_.factoryPrograms();
        if (!programs.empty())
        {
            juce::PopupMenu factory;
            const int current = presets_.currentProgram();
            for (size_t i = 0; i < programs.size(); ++i)
            {
                factory.addItem(item++,
                                juce::String::fromUTF8(programs[i].c_str()),
                                true,
                                static_cast<int>(i) == current);
                const int index = static_cast<int>(i);
                actions.push_back([this, index] { presets_.setProgram(index); });
            }
            menu.addSubMenu("Factory", factory);
        }
        if (presets_.supportsPresetFiles())
        {
            const auto folder = presets_.userPresetFolder();
            const auto files = presets_.userPresetFiles();
            const juce::String extension = "." + juce::String(presets_.presetFileExtension());
            if (!files.empty())
            {
                juce::PopupMenu user;
                for (const auto& file : files)
                {
                    user.addItem(
                        item++,
                        file.getRelativePathFrom(folder).upToLastOccurrenceOf(extension, false, false));
                    actions.push_back([this, file] { presets_.loadPresetFile(file); });
                }
                menu.addSubMenu("User", user);
            }
            if (!programs.empty() || !files.empty())
                menu.addSeparator();
            menu.addItem(item++, "Load preset file...");
            actions.push_back([this] { choosePresetFile(false); });
            menu.addItem(item++, "Save preset as...");
            actions.push_back([this] { choosePresetFile(true); });
        }
        if (actions.empty())
            menu.addItem(item++, "No presets", false);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetsButton_),
                           [actions](int chosen)
                           {
                               if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size())
                                   actions[static_cast<size_t>(chosen - 1)]();
                           });
    }

    void choosePresetFile(bool save)
    {
        const auto folder = presets_.userPresetFolder();
        folder.createDirectory();
        const juce::String extension = juce::String(presets_.presetFileExtension());
        chooser_ = std::make_unique<juce::FileChooser>(save ? "Save preset" : "Load preset",
                                                       folder.getChildFile("Preset." + extension),
                                                       "*." + extension);
        const int mode =
            save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting)
                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        juce::Component::SafePointer<Holder> safe(this);
        chooser_->launchAsync(mode,
                              [safe, save, extension](const juce::FileChooser& fc)
                              {
                                  auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (save)
                                  {
                                      if (!file.hasFileExtension(extension))
                                          file = file.withFileExtension(extension);
                                      safe->presets_.savePresetFile(file);
                                  }
                                  else
                                      safe->presets_.loadPresetFile(file);
                              });
    }

    void setPinned(bool pinned) { pin_.setToggleState(pinned, juce::dontSendNotification); }
    void setInsert(const core::Insert& insert)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        bypass_.setToggleState(insert.bypass, juce::dontSendNotification);
        mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(theme_.colours.background);
        g.setColour(theme_.colours.border);
        g.fillRect(0, PluginWindow::barHeight - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop(PluginWindow::barHeight).reduced(theme_.spacing.sm, 4);
        bypass_.setBounds(bar.removeFromLeft(64));
        bar.removeFromLeft(theme_.spacing.sm);
        presetsButton_.setBounds(bar.removeFromLeft(66));
        bar.removeFromLeft(theme_.spacing.sm);
        pin_.setBounds(bar.removeFromRight(44));
        bar.removeFromRight(theme_.spacing.sm);
        mixLabel_.setBounds(bar.removeFromLeft(28));
        mix_.setBounds(bar);
        const juce::ScopedValueSetter<bool> guard(layingOut_, true);
        editor_->setTopLeftPosition(0, PluginWindow::barHeight);
    }

    // The plugin resized its editor: grow or shrink the window with it.
    void childBoundsChanged(juce::Component* child) override
    {
        if (child != editor_.get() || layingOut_)
            return;
        const int w = std::max(editor_->getWidth(), 340);
        const int h = editor_->getHeight() + PluginWindow::barHeight;
        if (w != getWidth() || h != getHeight())
            window_.setContentComponentSize(w, h);
    }

private:
    const Theme& theme_ = Theme::dark();
    PluginWindow& window_;
    hosting::PluginPresets& presets_;
    std::unique_ptr<juce::Component> editor_;
    juce::TextButton bypass_, pin_, presetsButton_;
    juce::Label mixLabel_;
    juce::Slider mix_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool updating_ = false;
    bool layingOut_ = false;
};

PluginWindow::PluginWindow(core::Id channelId,
                           core::Id insertId,
                           std::shared_ptr<engine::InsertProcessor> keepAlive,
                           hosting::PluginPresets& presets,
                           std::unique_ptr<juce::Component> editor,
                           bool resizable,
                           const juce::String& title,
                           bool pinned)
    : juce::DocumentWindow(title, Theme::dark().colours.background, juce::DocumentWindow::closeButton),
      keepAlive_(std::move(keepAlive)), channelId_(channelId), insertId_(insertId)
{
    setUsingNativeTitleBar(true);
    auto holder = std::make_unique<Holder>(*this, presets, std::move(editor));
    holder_ = holder.get();
    holder_->setPinned(pinned);
    setContentOwned(holder.release(), true);
    setResizable(resizable, false);
    setAlwaysOnTop(pinned);
    setVisible(true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent(); // deletes the holder, which deletes the editor first
    holder_ = nullptr;
}

void PluginWindow::setInsert(const core::Insert& insert)
{
    if (holder_ != nullptr)
        holder_->setInsert(insert);
}

void PluginWindow::editorSizeChanged(int width, int height)
{
    setContentComponentSize(std::max(width, 340), height + barHeight);
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        juce::MessageManager::callAsync([close = onClose] { close(); });
}

void PluginWindow::moved()
{
    juce::DocumentWindow::moved();
    if (onMoved && isVisible())
        onMoved(getPosition());
}

} // namespace aural::ui
