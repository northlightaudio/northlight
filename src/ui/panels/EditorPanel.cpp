#include "ui/panels/EditorPanel.h"

#include "plugins/BuiltIns.h"
#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

// ----- a device card ----------------------------------------------------------------------------

class EditorPanel::Card final : public juce::Component
{
public:
    Card(EditorPanel& owner, const Theme& theme, core::Id channelId, const core::Insert& insert)
        : owner_(owner), theme_(theme), channelId_(channelId), insertId_(insert.id), type_(insert.type),
          power_(theme, Icon::power, "Bypass"), menu_(theme, Icon::menu, "Replace, remove, presets, sidechain")
    {
        power_.setClickingTogglesState(true);
        power_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        menu_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        power_.onClick = [this]
        {
            if (owner_.onCommand)
                owner_.onCommand(std::make_unique<core::SetInsertBypass>(channelId_, insertId_, !power_.getToggleState()));
        };
        menu_.onClick = [this] { showMenu(); };
        addAndMakeVisible(power_);
        addAndMakeVisible(menu_);
        mix_.setSliderStyle(juce::Slider::LinearHorizontal);
        mix_.setRange(0.0, 100.0, 1.0);
        mix_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        mix_.setDoubleClickReturnValue(true, 100.0);
        mix_.setTooltip("Wet/dry mix");
        mix_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Mix");
        };
        mix_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        mix_.onValueChange = [this]
        {
            if (!updating_ && owner_.onCommand)
                owner_.onCommand(std::make_unique<core::SetInsertMix>(channelId_, insertId_, static_cast<float>(mix_.getValue() / 100.0)));
        };
        addAndMakeVisible(mix_);
        open_.setButtonText("Open");
        open_.getProperties().set(AuralLookAndFeel::propertyVariant, "primary");
        open_.onClick = [this]
        {
            if (owner_.onOpenEditor)
                owner_.onOpenEditor(channelId_, insertId_);
        };
        addChildComponent(open_);
        rebuildKnobs();
        refresh(insert);
    }

    core::Id insertId() const noexcept { return insertId_; }
    const std::string& type() const noexcept { return type_; }

    void refresh(const core::Insert& insert)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        bypassed_ = insert.bypass;
        power_.setToggleState(!insert.bypass, juce::dontSendNotification);
        power_.setTooltip(insert.bypass ? "Bypassed: click to enable" : "Enabled: click to bypass");
        mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
        sidechain_ = insert.sidechain;
        const auto live = owner_.liveInsert ? owner_.liveInsert(insertId_) : nullptr;
        for (size_t i = 0; i < knobs_.size() && live != nullptr; ++i)
            knobs_[i]->setValue(live->parameter(static_cast<int>(i)), juce::dontSendNotification);
        latency_ = live != nullptr ? live->latencySamples() : 0;
        repaint();
    }

    int preferredWidth() const noexcept
    {
        const int perRow = 4;
        const int knobW = theme_.sizes.knob + theme_.spacing.xl;
        return std::max(160, std::min(perRow, std::max(1, static_cast<int>(knobs_.size()))) * knobW + 2 * theme_.spacing.md);
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
        auto header = getLocalBounds().removeFromTop(theme_.sizes.control).reduced(theme_.spacing.xs, 0);
        header.removeFromLeft(theme_.sizes.controlCompact + theme_.spacing.xs);
        header.removeFromRight(theme_.sizes.controlCompact + theme_.spacing.xs);
        // Format badge.
        const auto badge = badgeText();
        if (badge.isNotEmpty())
        {
            g.setFont(theme_.captionCaps());
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), badge) + 2 * theme_.spacing.sm;
            auto b = header.removeFromRight(w).withSizeKeepingCentre(w, theme_.sizes.icon + theme_.spacing.xxs).toFloat();
            g.setColour(c.accent.withAlpha(0.2f));
            g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
            g.setColour(c.accent);
            g.drawText(badge, b.toNearestInt(), juce::Justification::centred);
            header.removeFromRight(theme_.spacing.xs);
        }
        g.setFont(theme_.label());
        g.setColour(bypassed_ ? c.textTertiary : c.textPrimary);
        const auto name = owner_.displayName ? juce::String::fromUTF8(owner_.displayName(type_).c_str()) : juce::String(type_);
        g.drawText(name, header, juce::Justification::centredLeft, true);
        if (bypassed_)
        {
            const int w = std::min(header.getWidth(), juce::GlyphArrangement::getStringWidthInt(theme_.label(), name));
            g.fillRect(header.getX(), header.getCentreY(), w, 1);
        }
        // Sidechain chip and latency in the footer line above the mix.
        auto footer = getLocalBounds().removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
        g.setFont(theme_.caption());
        g.setColour(c.textSecondary);
        g.drawText("Mix " + juce::String(static_cast<int>(mix_.getValue())) + "%", footer.removeFromLeft(56), juce::Justification::centredLeft);
        auto info = getLocalBounds().withTrimmedTop(theme_.sizes.control).removeFromTop(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
        if (sidechain_ != core::invalidId && owner_.project_ != nullptr)
        {
            const auto* source = owner_.project_->findChannel(sidechain_);
            const auto text = "key: " + (source != nullptr ? juce::String::fromUTF8(source->name.c_str()) : juce::String("?"));
            g.setFont(theme_.caption());
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 2 * theme_.spacing.sm;
            const auto chip = info.removeFromLeft(w).withSizeKeepingCentre(w, theme_.sizes.icon + theme_.spacing.xxs).toFloat();
            g.setColour(c.accentSubtle);
            g.fillRoundedRectangle(chip, chip.getHeight() * 0.5f);
            g.setColour(c.accent);
            g.drawText(text, chip.toNearestInt(), juce::Justification::centred);
        }
        if (latency_ > 0)
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.caption());
            g.drawText(juce::String(latency_) + " smp", info, juce::Justification::centredRight);
        }
        if (knobs_.empty() && !open_.isVisible())
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.small());
            g.drawText("No parameters", getLocalBounds().reduced(theme_.spacing.md), juce::Justification::centred);
        }
        if (bypassed_)
        {
            g.setColour(c.surface1.withAlpha(0.5f));
            g.fillRoundedRectangle(bounds.withTrimmedTop(static_cast<float>(theme_.sizes.control)).withTrimmedBottom(static_cast<float>(theme_.sizes.controlCompact)), theme_.radius.md);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(theme_.sizes.control).reduced(theme_.spacing.xs, (theme_.sizes.control - theme_.sizes.controlCompact) / 2);
        power_.setBounds(header.removeFromLeft(theme_.sizes.controlCompact));
        menu_.setBounds(header.removeFromRight(theme_.sizes.controlCompact));
        auto footer = area.removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
        footer.removeFromLeft(56);
        mix_.setBounds(footer);
        area.removeFromTop(theme_.sizes.controlCompact); // the info line
        area = area.reduced(theme_.spacing.md, theme_.spacing.xs);
        if (open_.isVisible())
        {
            open_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 120), theme_.sizes.control));
            return;
        }
        const int perRow = 4;
        const int cellW = area.getWidth() / std::max(1, std::min(perRow, static_cast<int>(knobs_.size())));
        const int cellH = theme_.sizes.knob + static_cast<int>(theme_.fonts.size.value) + static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.sm;
        for (size_t i = 0; i < knobs_.size(); ++i)
        {
            const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
            const auto cell = juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
            knobs_[i]->setBounds(cell.withTrimmedBottom(static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.xxs).reduced(theme_.spacing.xs, 0));
            labels_[i]->setBounds(cell.withTop(cell.getBottom() - static_cast<int>(theme_.fonts.size.caption) - theme_.spacing.xxs).withHeight(static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.xxs));
        }
    }

private:
    juce::String badgeText() const
    {
        if (type_.rfind("vst3:", 0) == 0)
            return "VST3";
        if (type_.rfind("clap:", 0) == 0)
            return "CLAP";
        return {};
    }

    void rebuildKnobs()
    {
        knobs_.clear();
        labels_.clear();
        const auto live = owner_.liveInsert ? owner_.liveInsert(insertId_) : nullptr;
        if (live == nullptr)
            return;
        const bool hosted = hosting::PluginRegistry::isHostedType(type_);
        if (hosted && live->parameters().empty())
        {
            // A plugin with its own GUI: open it in its window.
            open_.setVisible(true);
            return;
        }
        for (size_t i = 0; i < live->parameters().size(); ++i)
        {
            const auto& info = live->parameters()[i];
            auto knob = std::make_unique<Knob>(theme_);
            knob->setRange(info.minimum, info.maximum, info.defaultValue);
            const juce::String unit = info.unit.empty() ? juce::String() : " " + juce::String(info.unit);
            knob->format = [unit](double v) { return juce::String(v, std::abs(v) >= 100.0 ? 0 : 2) + unit; };
            knob->setTooltip(info.name);
            const int index = static_cast<int>(i);
            knob->onDragStart = [this, index]
            {
                if (const auto l = owner_.liveInsert ? owner_.liveInsert(insertId_) : nullptr)
                    l->beginParameterGesture(index);
                if (owner_.onGestureBegin)
                    owner_.onGestureBegin(knobs_[static_cast<size_t>(index)]->getTooltip().toStdString());
            };
            knob->onDragEnd = [this, index]
            {
                if (owner_.onGestureEnd)
                    owner_.onGestureEnd();
                if (const auto l = owner_.liveInsert ? owner_.liveInsert(insertId_) : nullptr)
                    l->endParameterGesture(index);
            };
            knob->onValueChanged = [this, index](double value)
            {
                if (!updating_ && owner_.onCommand)
                    owner_.onCommand(std::make_unique<core::SetInsertParameter>(channelId_, insertId_, index, static_cast<float>(value)));
            };
            addAndMakeVisible(*knob);
            auto label = std::make_unique<juce::Label>();
            label->setText(info.name, juce::dontSendNotification);
            label->setFont(theme_.caption());
            label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
            label->setJustificationType(juce::Justification::centred);
            label->setMinimumHorizontalScale(0.7f);
            addAndMakeVisible(*label);
            knobs_.push_back(std::move(knob));
            labels_.push_back(std::move(label));
        }
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, bypassed_ ? "Enable" : "Bypass");
        menu.addItem(2, "Open editor", true);
        menu.addItem(3, "Remove");
        juce::Component::SafePointer<Card> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_),
                           [safe](int choice)
                           {
                               if (safe == nullptr || choice == 0 || !safe->owner_.onCommand)
                                   return;
                               if (choice == 1)
                                   safe->owner_.onCommand(std::make_unique<core::SetInsertBypass>(safe->channelId_, safe->insertId_, !safe->bypassed_));
                               else if (choice == 2 && safe->owner_.onOpenEditor)
                                   safe->owner_.onOpenEditor(safe->channelId_, safe->insertId_);
                               else if (choice == 3)
                                   safe->owner_.onCommand(std::make_unique<core::RemoveInsert>(safe->channelId_, safe->insertId_));
                           });
    }

    EditorPanel& owner_;
    const Theme& theme_;
    core::Id channelId_, insertId_;
    std::string type_;
    IconButton power_, menu_;
    juce::Slider mix_;
    juce::TextButton open_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    bool bypassed_ = false, updating_ = false;
    core::Id sidechain_ = core::invalidId;
    int latency_ = 0;
};

// ----- the panel --------------------------------------------------------------------------------

EditorPanel::EditorPanel(const Theme& theme) : theme_(theme)
{
    setOpaque(true);
    viewport_.setViewedComponent(&strip_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
    add_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    add_.setTooltip("Add an insert");
    add_.onClick = [this] { showAddMenu(); };
    strip_.addAndMakeVisible(add_);
    startTimerHz(10);
}

EditorPanel::~EditorPanel()
{
    stopTimer();
    cards_.clear();
}

const core::Channel* EditorPanel::channel() const
{
    return project_ != nullptr ? project_->findChannel(channelId_) : nullptr;
}

void EditorPanel::setProject(const core::Project* project)
{
    project_ = project;
    rebuild();
}

void EditorPanel::setChannel(core::Id channelId)
{
    channelId_ = channelId;
    rebuild();
}

void EditorPanel::rebuild()
{
    const auto* ch = channel();
    std::vector<core::Id> order;
    if (ch != nullptr)
        for (const auto& insert : ch->inserts)
            order.push_back(insert.id);
    bool same = order == builtOrder_;
    if (same && ch != nullptr)
        for (size_t i = 0; i < cards_.size(); ++i)
            if (cards_[i]->type() != ch->inserts[i].type)
                same = false;
    if (!same)
    {
        cards_.clear();
        if (ch != nullptr)
            for (const auto& insert : ch->inserts)
            {
                auto card = std::make_unique<Card>(*this, theme_, channelId_, insert);
                strip_.addAndMakeVisible(*card);
                cards_.push_back(std::move(card));
            }
        builtOrder_ = order;
    }
    else if (ch != nullptr)
        for (size_t i = 0; i < cards_.size(); ++i)
            cards_[i]->refresh(ch->inserts[i]);
    add_.setVisible(ch != nullptr);
    resized();
    repaint();
}

void EditorPanel::timerCallback()
{
    // Hosted plugins change their own values: keep the knobs current.
    if (const auto* ch = channel())
        for (size_t i = 0; i < cards_.size() && i < ch->inserts.size(); ++i)
            cards_[i]->refresh(ch->inserts[i]);
}

void EditorPanel::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    const auto* ch = channel();
    g.setFont(theme_.label());
    g.setColour(c.textPrimary);
    auto title = getLocalBounds().removeFromTop(theme_.sizes.control).reduced(theme_.spacing.md, 0);
    if (ch == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("Select a track or bus to see its inserts", getLocalBounds(), juce::Justification::centred);
        return;
    }
    g.drawText(juce::String::fromUTF8(ch->name.c_str()) + juce::String::fromUTF8("  \xe2\x96\xb8"), title, juce::Justification::centredLeft);
}

void EditorPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(theme_.sizes.control);
    viewport_.setBounds(area);
    int x = theme_.spacing.md;
    const int h = std::max(theme_.sizes.dockMin - theme_.sizes.control, area.getHeight() - theme_.spacing.md - theme_.sizes.scrollbar);
    for (auto& card : cards_)
    {
        card->setBounds(x, theme_.spacing.xs, card->preferredWidth(), h);
        x += card->preferredWidth() + theme_.spacing.sm;
    }
    add_.setBounds(x, theme_.spacing.xs, theme_.sizes.controlLarge * 2, theme_.sizes.controlLarge);
    x += theme_.sizes.controlLarge * 2 + theme_.spacing.md;
    strip_.setSize(std::max(x, area.getWidth()), h + theme_.spacing.sm);
}

void EditorPanel::showAddMenu()
{
    juce::PopupMenu menu;
    std::vector<std::string> types;
    int item = 1;
    for (const auto& type : plugins::BuiltInFactory().typeNames())
    {
        menu.addItem(item++, plugins::BuiltInFactory::displayName(type));
        types.push_back(type);
    }
    if (registry_ != nullptr && !registry_->plugins().empty())
    {
        menu.addSeparator();
        for (const auto& plugin : registry_->plugins())
        {
            menu.addItem(item++, juce::String::fromUTF8(plugin.name.c_str()) + "  [" + juce::String(plugin.format) + "]");
            types.push_back(hosting::PluginRegistry::typeFor(plugin));
        }
    }
    juce::Component::SafePointer<EditorPanel> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add_),
                       [safe, types](int choice)
                       {
                           if (safe == nullptr || choice <= 0 || static_cast<size_t>(choice) > types.size() || !safe->onCommand)
                               return;
                           core::Insert insert;
                           insert.type = types[static_cast<size_t>(choice - 1)];
                           insert.params = plugins::defaultParameters(insert.type);
                           safe->onCommand(std::make_unique<core::AddInsert>(safe->channelId_, insert));
                       });
}

} // namespace aural::ui
