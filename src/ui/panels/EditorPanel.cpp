#include "ui/panels/EditorPanel.h"

#include "plugins/BuiltIns.h"
#include "ui/Db.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PluginMenu.h"
#include "ui/SendMenu.h"
#include "ui/SidechainMenu.h"
#include "ui/components/Fader.h"
#include "ui/components/SegmentedControl.h"
#include "ui/strip/BuiltInCards.h"

#include <map>

namespace northlight::ui
{

// ----- a device card ----------------------------------------------------------------------------

class EditorPanel::Card final : public juce::Component
{
public:
    // `instrument`: the track's instrument slot (replaced and removed as one,
    // not reordered among the inserts).
    Card(EditorPanel& owner,
         const Theme& theme,
         core::Id channelId,
         const core::Insert& insert,
         bool instrument = false)
        : owner_(owner), theme_(theme), channelId_(channelId), insertId_(insert.id), type_(insert.type),
          instrument_(instrument), power_(theme, Icon::power, "Bypass"),
          menu_(theme, Icon::menu, "Replace, remove, presets, sidechain")
    {
        power_.setClickingTogglesState(true);
        power_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        menu_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        power_.onClick = [this]
        {
            if (owner_.onCommand)
                owner_.onCommand(
                    std::make_unique<core::SetInsertBypass>(channelId_, insertId_, !power_.getToggleState()));
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
                owner_.onCommand(std::make_unique<core::SetInsertMix>(
                    channelId_, insertId_, static_cast<float>(mix_.getValue() / 100.0)));
        };
        addAndMakeVisible(mix_);
        mix_.setVisible(!instrument_); // an instrument has no dry signal to mix
        open_.setButtonText("Open");
        open_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "primary");
        open_.onClick = [this]
        {
            if (owner_.onOpenEditor)
                owner_.onOpenEditor(channelId_, insertId_);
        };
        addChildComponent(open_);
        // A built-in's body is its own editor: a display to drag and its knobs.
        host_.onCommand = [this](std::unique_ptr<core::Command> command)
        {
            if (owner_.onCommand)
                owner_.onCommand(std::move(command));
        };
        host_.onGestureBegin = [this](const std::string& name)
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin(name);
        };
        host_.onGestureEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        host_.liveInsert = [this](core::Id id)
        { return owner_.liveInsert ? owner_.liveInsert(id) : std::shared_ptr<engine::InsertProcessor>(); };
        host_.project = [this] { return owner_.project_; };
        body_ = strip::createBuiltInCard(theme_, host_, channelId_, insert);
        if (body_ != nullptr)
        {
            body_->setEmbedded(true);
            addAndMakeVisible(*body_);
        }
        else
            rebuildKnobs();
        refresh(insert);
    }

    // About 60 times a second while the panel shows: the body's displays read
    // the processor's feedback.
    void tick(double elapsedMs)
    {
        if (body_ != nullptr)
            body_->tick(elapsedMs);
    }
    void stopWatching()
    {
        if (body_ != nullptr)
            body_->stopWatching();
    }

    core::Id insertId() const noexcept { return insertId_; }
    const std::string& type() const noexcept { return type_; }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = e.y < theme_.sizes.control && e.mods.isLeftButtonDown();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_ && !instrument_ && e.mouseWasDraggedSinceMouseDown())
            owner_.reorderCard(insertId_, e.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragging_ && !instrument_ && e.mouseWasDraggedSinceMouseDown())
            owner_.reorderCard(insertId_, e.getScreenPosition(), true);
        dragging_ = false;
    }

    void refresh(const core::Insert& insert)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        bypassed_ = insert.bypass;
        power_.setToggleState(!insert.bypass, juce::dontSendNotification);
        power_.setTooltip(insert.bypass ? "Bypassed: click to enable" : "Enabled: click to bypass");
        mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
        sidechain_ = insert.sidechain;
        sidechainPostFader_ = insert.sidechainPostFader;
        if (body_ != nullptr)
            body_->refresh(insert);
        const auto live = owner_.liveInsert ? owner_.liveInsert(insertId_) : nullptr;
        for (size_t i = 0; i < knobs_.size() && live != nullptr; ++i)
            knobs_[i]->setValue(live->parameter(static_cast<int>(i)), juce::dontSendNotification);
        latency_ = live != nullptr ? live->latencySamples() : 0;
        repaint();
    }

    int preferredWidth() const noexcept
    {
        if (body_ != nullptr)
            return body_->preferredWidth();
        const int perRow = 4;
        const int knobW = theme_.sizes.knob + theme_.spacing.xl;
        return std::max(160,
                        std::min(perRow, std::max(1, static_cast<int>(knobs_.size()))) * knobW +
                            2 * theme_.spacing.md);
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
            const int w =
                juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), badge) + 2 * theme_.spacing.sm;
            auto b = header.removeFromRight(w)
                         .withSizeKeepingCentre(w, theme_.sizes.icon + theme_.spacing.xxs)
                         .toFloat();
            g.setColour(c.accent.withAlpha(0.2f));
            g.fillRoundedRectangle(b, b.getHeight() * 0.5f);
            g.setColour(c.accent);
            g.drawText(badge, b.toNearestInt(), juce::Justification::centred);
            header.removeFromRight(theme_.spacing.xs);
        }
        // The "open" dot while the editor window is open.
        if (owner_.isEditorOpen && owner_.isEditorOpen(insertId_))
        {
            g.setColour(c.accent);
            g.fillEllipse(header.removeFromLeft(10).toFloat().withSizeKeepingCentre(6.0f, 6.0f));
            header.removeFromLeft(theme_.spacing.xxs);
        }
        g.setFont(theme_.label());
        g.setColour(bypassed_ || missing_ ? c.textTertiary : c.textPrimary);
        const auto name = owner_.displayName ? juce::String::fromUTF8(owner_.displayName(type_).c_str())
                                             : juce::String(type_);
        g.drawText(name, header, juce::Justification::centredLeft, true);
        if (missing_)
        {
            auto body = getLocalBounds()
                            .withTrimmedTop(theme_.sizes.control + theme_.sizes.controlCompact)
                            .withTrimmedBottom(theme_.sizes.controlCompact);
            Icons::draw(g,
                        Icon::warning,
                        juce::Rectangle<float>(20.0f, 20.0f)
                            .withCentre({static_cast<float>(body.getCentreX()),
                                         static_cast<float>(body.getCentreY() - 14)}),
                        c.warning);
            g.setColour(c.textTertiary);
            g.setFont(theme_.small());
            g.drawText(
                "Not installed", body.withTrimmedTop(body.getHeight() / 2), juce::Justification::centredTop);
        }
        if (bypassed_)
        {
            const int w =
                std::min(header.getWidth(), juce::GlyphArrangement::getStringWidthInt(theme_.label(), name));
            g.fillRect(header.getX(), header.getCentreY(), w, 1);
        }
        // Sidechain chip and latency in the footer line above the mix.
        auto footer =
            getLocalBounds().removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
        g.setFont(theme_.caption());
        g.setColour(c.textSecondary);
        if (!instrument_)
            g.drawText("Mix " + juce::String(static_cast<int>(mix_.getValue())) + "%",
                       footer.removeFromLeft(56),
                       juce::Justification::centredLeft);
        auto info = getLocalBounds()
                        .withTrimmedTop(theme_.sizes.control)
                        .removeFromTop(theme_.sizes.controlCompact)
                        .reduced(theme_.spacing.md, 0);
        if (sidechain_ != core::invalidId && owner_.project_ != nullptr)
        {
            const auto* source = owner_.project_->findChannel(sidechain_);
            const auto text =
                "key: " +
                (source != nullptr ? juce::String::fromUTF8(source->name.c_str()) : juce::String("?")) +
                (sidechainPostFader_ ? " (post)" : "");
            g.setFont(theme_.caption());
            const int w =
                juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 2 * theme_.spacing.sm;
            const auto chip = info.removeFromLeft(w)
                                  .withSizeKeepingCentre(w, theme_.sizes.icon + theme_.spacing.xxs)
                                  .toFloat();
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
        if (knobs_.empty() && !open_.isVisible() && !missing_)
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.small());
            g.drawText(
                "No parameters", getLocalBounds().reduced(theme_.spacing.md), juce::Justification::centred);
        }
        if (bypassed_)
        {
            g.setColour(c.surface1.withAlpha(0.5f));
            g.fillRoundedRectangle(bounds.withTrimmedTop(static_cast<float>(theme_.sizes.control))
                                       .withTrimmedBottom(static_cast<float>(theme_.sizes.controlCompact)),
                                   theme_.radius.md);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header =
            area.removeFromTop(theme_.sizes.control)
                .reduced(theme_.spacing.xs, (theme_.sizes.control - theme_.sizes.controlCompact) / 2);
        power_.setBounds(header.removeFromLeft(theme_.sizes.controlCompact));
        menu_.setBounds(header.removeFromRight(theme_.sizes.controlCompact));
        if (!instrument_)
        {
            auto footer = area.removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
            footer.removeFromLeft(56);
            mix_.setBounds(footer);
        }
        area.removeFromTop(theme_.sizes.controlCompact); // the info line
        if (body_ != nullptr)
        {
            body_->setBounds(area);
            return;
        }
        area = area.reduced(theme_.spacing.md, theme_.spacing.xs);
        if (open_.isVisible())
        {
            open_.setBounds(area.withSizeKeepingCentre(std::min(area.getWidth(), 120), theme_.sizes.control));
            return;
        }
        const int perRow = 4;
        const int cellW = area.getWidth() / std::max(1, std::min(perRow, static_cast<int>(knobs_.size())));
        const int cellH = theme_.sizes.knob + static_cast<int>(theme_.fonts.size.value) +
                          static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.sm;
        for (size_t i = 0; i < knobs_.size(); ++i)
        {
            const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
            const auto cell =
                juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
            knobs_[i]->setBounds(
                cell.withTrimmedBottom(static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.xxs)
                    .reduced(theme_.spacing.xs, 0));
            labels_[i]->setBounds(
                cell.withTop(cell.getBottom() - static_cast<int>(theme_.fonts.size.caption) -
                             theme_.spacing.xxs)
                    .withHeight(static_cast<int>(theme_.fonts.size.caption) + theme_.spacing.xxs));
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
        const bool hosted = hosting::PluginRegistry::isHostedType(type_);
        missing_ = live == nullptr && hosted;
        if (live == nullptr)
            return;
        if (hosted && (live->parameters().empty() || type_.rfind("vst3:", 0) == 0))
        {
            // A plugin with its own GUI (every VST3: JUCE gives a GUI-less one a
            // generic editor): open it in its window.
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
                    owner_.onCommand(std::make_unique<core::SetInsertParameter>(
                        channelId_, insertId_, index, static_cast<float>(value)));
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
        menu.addItem(1, bypassed_ ? "Enable" : "Bypass", !missing_);
        menu.addItem(2, "Open editor", !missing_);
        if (instrument_)
        {
            menu.addItem(4, "Replace instrument...");
            menu.addItem(5, "Remove instrument");
            juce::Component::SafePointer<Card> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_),
                               [safe](int choice)
                               {
                                   if (safe == nullptr || choice == 0 || !safe->owner_.onCommand)
                                       return;
                                   auto& owner = safe->owner_;
                                   if (choice == 1)
                                       owner.onCommand(std::make_unique<core::SetInsertBypass>(
                                           safe->channelId_, safe->insertId_, !safe->bypassed_));
                                   else if (choice == 2 && owner.onOpenEditor)
                                       owner.onOpenEditor(safe->channelId_, safe->insertId_);
                                   else if (choice == 4)
                                       owner.showInstrumentMenu(safe->menu_);
                                   else if (choice == 5)
                                       owner.onCommand(std::make_unique<core::SetInstrument>(safe->channelId_,
                                                                                             core::Insert{}));
                               });
            return;
        }
        menu.addItem(3, "Remove");
        juce::Component::SafePointer<Card> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_),
                           [safe](int choice)
                           {
                               if (safe == nullptr || choice == 0 || !safe->owner_.onCommand)
                                   return;
                               if (choice == 1)
                                   safe->owner_.onCommand(std::make_unique<core::SetInsertBypass>(
                                       safe->channelId_, safe->insertId_, !safe->bypassed_));
                               else if (choice == 2 && safe->owner_.onOpenEditor)
                                   safe->owner_.onOpenEditor(safe->channelId_, safe->insertId_);
                               else if (choice == 3)
                                   safe->owner_.onCommand(std::make_unique<core::RemoveInsert>(
                                       safe->channelId_, safe->insertId_));
                           });
    }

    EditorPanel& owner_;
    const Theme& theme_;
    core::Id channelId_, insertId_;
    std::string type_;
    bool instrument_ = false;
    IconButton power_, menu_;
    juce::Slider mix_;
    juce::TextButton open_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    bool bypassed_ = false, updating_ = false, missing_ = false, dragging_ = false;
    core::Id sidechain_ = core::invalidId;
    bool sidechainPostFader_ = false;
    int latency_ = 0;
    strip::CardHost host_;
    std::unique_ptr<strip::ModuleCard> body_; // a built-in's editor
};

// ----- a send ----------------------------------------------------------------------------------

// One send: the bus it feeds (name, in its colour), a fader for the level with
// its value under it, Pre/Post (where it taps the channel) and a remove button.
class EditorPanel::SendCard final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SendCard(EditorPanel& owner, const Theme& theme, core::Id channelId, core::Id busId)
        : owner_(owner), theme_(theme), channelId_(channelId), busId_(busId), fader_(theme), tap_(theme),
          remove_(theme, Icon::close, "Remove this send")
    {
        fader_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Send level");
        };
        fader_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        fader_.onGainChanged = [this](float gain)
        {
            level_ = gain;
            repaint(valueArea());
            if (!updating_ && owner_.onCommand)
                owner_.onCommand(std::make_unique<core::SetSend>(channelId_, busId_, gain, pre_));
        };
        addAndMakeVisible(fader_);
        tap_.setSegments({"Pre", "Post"});
        tap_.onChange = [this](int index)
        {
            if (!updating_ && owner_.onCommand)
                owner_.onCommand(std::make_unique<core::SetSend>(channelId_, busId_, level_, index == 0));
        };
        addAndMakeVisible(tap_);
        remove_.setSize(theme_.sizes.controlCompact, theme_.sizes.controlCompact);
        remove_.onClick = [this]
        {
            if (owner_.onCommand)
                owner_.onCommand(std::make_unique<core::RemoveSend>(channelId_, busId_));
        };
        addAndMakeVisible(remove_);
    }

    core::Id busId() const noexcept { return busId_; }
    static int preferredWidth() noexcept { return 88; }

    void refresh(const core::Send& send, const core::Bus* bus)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        level_ = send.level;
        pre_ = send.preFader;
        name_ = juce::String::fromUTF8(bus != nullptr && !bus->name.empty() ? bus->name.c_str() : "Bus");
        colour_ = bus != nullptr && bus->colour != 0 ? juce::Colour(bus->colour) : theme_.colours.accent;
        fader_.setTrackColour(colour_);
        fader_.setGain(send.level, juce::dontSendNotification);
        tap_.setSelected(pre_ ? 0 : 1, juce::dontSendNotification);
        setTooltip(name_ + (pre_ ? " (pre-fader: ignores the channel's fader)"
                                 : " (post-fader: follows the channel's fader)"));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
        // The header: the bus's colour and name.
        auto header = getLocalBounds().removeFromTop(theme_.sizes.control).reduced(theme_.spacing.sm, 0);
        header.removeFromRight(theme_.sizes.controlCompact);
        const auto dot = header.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f);
        g.setColour(colour_);
        g.fillEllipse(dot);
        header.removeFromLeft(theme_.spacing.xs);
        g.setFont(theme_.label());
        g.setColour(c.textPrimary);
        g.drawText(name_, header, juce::Justification::centredLeft, true);
        // The level under the fader.
        g.setFont(theme_.caption());
        g.setColour(c.textSecondary);
        g.drawText(db::gainText(level_) + " dB", valueArea(), juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header =
            area.removeFromTop(theme_.sizes.control)
                .reduced(theme_.spacing.xs, (theme_.sizes.control - theme_.sizes.controlCompact) / 2);
        remove_.setBounds(header.removeFromRight(theme_.sizes.controlCompact));
        area = area.reduced(theme_.spacing.sm, 0);
        tap_.setBounds(area.removeFromBottom(theme_.sizes.controlCompact + theme_.spacing.sm)
                           .withTrimmedBottom(theme_.spacing.sm));
        area.removeFromBottom(valueHeight); // the value line
        fader_.setBounds(area.withTrimmedBottom(theme_.spacing.xxs));
    }

private:
    static constexpr int valueHeight = 18;
    juce::Rectangle<int> valueArea() const
    {
        return getLocalBounds()
            .reduced(theme_.spacing.sm, 0)
            .withTrimmedBottom(theme_.sizes.controlCompact + theme_.spacing.sm)
            .removeFromBottom(valueHeight);
    }

    EditorPanel& owner_;
    const Theme& theme_;
    core::Id channelId_, busId_;
    Fader fader_;
    SegmentedControl tap_;
    IconButton remove_;
    juce::String name_;
    juce::Colour colour_;
    float level_ = 1.0f;
    bool pre_ = false, updating_ = false;
};

// ----- the panel --------------------------------------------------------------------------------

EditorPanel::EditorPanel(const Theme& theme) : theme_(theme), stripView_(theme)
{
    setOpaque(true);
    stripView_.host.onCommand = [this](std::unique_ptr<core::Command> c)
    {
        if (onCommand)
            onCommand(std::move(c));
    };
    stripView_.host.onGestureBegin = [this](const std::string& name)
    {
        if (onGestureBegin)
            onGestureBegin(name);
    };
    stripView_.host.onGestureEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    stripView_.host.liveInsert = [this](core::Id id)
    { return liveInsert ? liveInsert(id) : std::shared_ptr<engine::InsertProcessor>{}; };
    stripView_.onWidthChanged = [this]
    {
        // A strip card collapsed or expanded: the strip and what follows it glide.
        rememberBounds();
        animateLayout_ = true;
        resized();
        animateLayout_ = false;
    };
    strip_.addChildComponent(stripView_);
    strip_.onPaint = [this](juce::Graphics& g)
    {
        if (channel() == nullptr)
            return;
        g.setFont(theme_.captionCaps());
        g.setColour(theme_.colours.textTertiary);
        if (addInstrument_.isVisible() || instrumentCard_ != nullptr)
            g.drawText("INSTRUMENT",
                       juce::Rectangle<int>(
                           instrumentX_ + theme_.spacing.xxs, 0, 120, strip::ChannelStripView::captionHeight),
                       juce::Justification::centredLeft);
        g.drawText("INSERTS",
                   juce::Rectangle<int>(
                       insertsX_ + theme_.spacing.xxs, 0, 120, strip::ChannelStripView::captionHeight),
                   juce::Justification::centredLeft);
        if (addSend_.isVisible())
            g.drawText("SENDS",
                       juce::Rectangle<int>(
                           sendsX_ + theme_.spacing.xxs, 0, 120, strip::ChannelStripView::captionHeight),
                       juce::Justification::centredLeft);
    };
    viewport_.setViewedComponent(&strip_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
    add_.setTooltip("Add an insert");
    add_.onClick = [this] { showAddMenu(); };
    strip_.addAndMakeVisible(add_);
    addInstrument_.setTooltip("Choose the instrument this track's MIDI plays");
    addInstrument_.onClick = [this] { showInstrumentMenu(addInstrument_); };
    strip_.addChildComponent(addInstrument_);
    addSend_.setTooltip("Send this channel to an FX track or a bus");
    addSend_.onClick = [this] { showAddSendMenu(); };
    strip_.addChildComponent(addSend_);
    startTimerHz(60);
}

EditorPanel::~EditorPanel()
{
    stopTimer();
    instrumentCard_.reset();
    cards_.clear();
    sendCards_.clear();
    stripView_.setChannel(nullptr);
}

const core::Channel* EditorPanel::channel() const
{
    return project_ != nullptr ? project_->findChannel(channelId_) : nullptr;
}

void EditorPanel::setProject(const core::Project* project)
{
    project_ = project;
    resolveChannel();
    rebuild();
}

void EditorPanel::setChannel(core::Id channelId)
{
    selectedId_ = channelId;
    resolveChannel();
    rebuild();
}

// A MIDI track makes no audio: the panel shows the instrument channel it plays
// (or, aimed nowhere, the MIDI track itself, with a slot to give it one).
void EditorPanel::resolveChannel()
{
    channelId_ = selectedId_;
    midiTrackId_ = core::invalidId;
    const auto* track = project_ != nullptr ? project_->findTrack(selectedId_) : nullptr;
    if (track != nullptr && track->isMidi())
    {
        midiTrackId_ = track->id;
        if (const auto* target = project_->findTrack(track->midiTarget);
            target != nullptr && target->isInstrument())
            channelId_ = target->id;
    }
}

void EditorPanel::rebuild()
{
    const auto* ch = channel();
    std::vector<core::Id> order;
    if (ch != nullptr)
        for (const auto& insert : ch->inserts)
            order.push_back(insert.id);
    // Another channel always rebuilds: two channels can look alike (no
    // inserts, the same strip, sends to the same buses) and the cards hold the
    // channel they edit.
    const bool channelChanged = channelId_ != builtChannel_;
    rememberBounds(); // where the cards are now, before any is rebuilt
    builtChannel_ = channelId_;
    if (channelChanged)
        sendCards_.clear();
    bool same = order == builtOrder_ && !channelChanged;
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
    // The instrument slot: an instrument channel's (or a MIDI track's that plays
    // nothing yet, to give it one).
    const auto* track = project_ != nullptr ? project_->findTrack(channelId_) : nullptr;
    const bool slot =
        track != nullptr && (track->isInstrument() || track->isMidi() || track->hasInstrument());
    const bool hasInstrument = slot && track->hasInstrument();
    bool instrumentChanged = false;
    if (!hasInstrument)
    {
        instrumentChanged = instrumentCard_ != nullptr;
        instrumentCard_.reset();
    }
    else if (instrumentCard_ == nullptr || instrumentCard_->insertId() != track->instrument.id ||
             instrumentCard_->type() != track->instrument.type)
    {
        instrumentCard_ = std::make_unique<Card>(*this, theme_, channelId_, track->instrument, true);
        strip_.addAndMakeVisible(*instrumentCard_);
        instrumentChanged = true;
    }
    else
        instrumentCard_->refresh(track->instrument);
    if (addInstrument_.isVisible() != (slot && !hasInstrument))
    {
        addInstrument_.setVisible(slot && !hasInstrument);
        instrumentChanged = true;
    }
    same = same && !instrumentChanged;
    // Only a change of shape lays the panel out again (each card repaints
    // what changed in it): a knob drag refreshes this many times a second.
    const int stripWidth = stripView_.preferredWidth();
    const bool stripShown = ch != nullptr && !ch->strip.empty();
    const bool preInserts = ch != nullptr && ch->stripPreInserts;
    stripView_.setChannel(ch);
    bool shape = !same || add_.isVisible() != (ch != nullptr) || stripView_.isVisible() != stripShown ||
                 stripWidth != stripView_.preferredWidth() || preInserts != laidOutPreInserts_;
    add_.setVisible(ch != nullptr);
    stripView_.setVisible(stripShown);
    laidOutPreInserts_ = preInserts;
    rebuildSends(ch, shape);
    if (shape || channelChanged)
    {
        animateLayout_ = !channelChanged; // the same channel's cards changed: they move
        resized();
        animateLayout_ = false;
        repaint();
    }
    else
        repaint(getLocalBounds().removeFromTop(theme_.sizes.control)); // the title (a rename)
}

void EditorPanel::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastTickMs_ > 0.0 ? std::min(100.0, now - lastTickMs_) : 16.0;
    lastTickMs_ = now;
    // The built-ins' displays, while the panel shows.
    if (!isShowing())
    {
        if (wasShowing_)
        {
            if (instrumentCard_ != nullptr)
                instrumentCard_->stopWatching();
            for (const auto& card : cards_)
                card->stopWatching();
        }
        wasShowing_ = false;
        return;
    }
    wasShowing_ = true;
    if (instrumentCard_ != nullptr)
        instrumentCard_->tick(elapsed);
    for (const auto& card : cards_)
        card->tick(elapsed);
    // Hosted plugins change their own values: keep the knobs current (10 Hz).
    if (++ticks_ % 6 != 0)
        return;
    if (const auto* ch = channel())
    {
        if (instrumentCard_ != nullptr && ch->hasInstrument())
            instrumentCard_->refresh(ch->instrument);
        for (size_t i = 0; i < cards_.size() && i < ch->inserts.size(); ++i)
            cards_[i]->refresh(ch->inserts[i]);
    }
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
        g.drawText(
            "Select a track or bus to see its inserts", getLocalBounds(), juce::Justification::centred);
        return;
    }
    // A MIDI track's title says what it plays: "MIDI 1 → Drums: Pulsar".
    juce::String heading = juce::String::fromUTF8(ch->name.c_str());
    if (const auto* midi = project_->findTrack(midiTrackId_); midi != nullptr && midi->id != ch->id)
        heading =
            juce::String::fromUTF8(midi->name.c_str()) + juce::String::fromUTF8("  \xe2\x86\x92  ") + heading;
    else if (midi != nullptr)
        heading << "  (plays no instrument)";
    g.drawText(heading + juce::String::fromUTF8("  \xe2\x96\xb8"), title, juce::Justification::centredLeft);
    if (dropIndex_ >= 0)
    {
        // The insertion line between cards (a plugin or a card dragged over).
        int x = insertsX_ -
                theme_.spacing.sm / 2; // the head of the inserts (after the strip when it comes first)
        if (dropIndex_ > 0 && static_cast<size_t>(dropIndex_) <= cards_.size())
            x = cards_[static_cast<size_t>(dropIndex_ - 1)]->getRight() + theme_.spacing.sm / 2;
        const int sx = viewport_.getX() + x - viewport_.getViewPositionX();
        g.setColour(c.accent);
        g.fillRect(
            sx - 1, viewport_.getY() + theme_.spacing.xs, 2, viewport_.getHeight() - theme_.spacing.md);
    }
}

int EditorPanel::insertIndexForX(int stripX) const
{
    int index = 0;
    for (const auto& card : cards_)
    {
        if (stripX < card->getX() + card->getWidth() / 2)
            break;
        ++index;
    }
    return index;
}

bool EditorPanel::isInterestedInDragSource(const SourceDetails& details)
{
    return channel() != nullptr && details.description.toString().startsWith("plugin:");
}

void EditorPanel::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void EditorPanel::itemDragMove(const SourceDetails& details)
{
    const auto local = strip_.getLocalPoint(this, details.localPosition);
    dropIndex_ = insertIndexForX(local.x);
    repaint();
}

void EditorPanel::itemDragExit(const SourceDetails&)
{
    dropIndex_ = -1;
    repaint();
}

void EditorPanel::itemDropped(const SourceDetails& details)
{
    const int index = dropIndex_;
    dropIndex_ = -1;
    repaint();
    const auto type =
        details.description.toString().fromFirstOccurrenceOf("plugin:", false, false).toStdString();
    if (type.empty())
        return;
    if (isInstrumentType(type, registry_))
        setInstrument(type);
    else
        addInsert(type, index);
}

void EditorPanel::addInsert(const std::string& type, int index)
{
    if (!onCommand || channel() == nullptr)
        return;
    core::Insert insert;
    insert.type = type;
    insert.params = plugins::defaultParameters(type);
    onCommand(std::make_unique<core::AddInsert>(channelId_, insert, index));
    if (onInserted)
        onInserted(type);
}

void EditorPanel::setInstrument(const std::string& type)
{
    const auto* track = project_ != nullptr ? project_->findTrack(channelId_) : nullptr;
    if (!onCommand || track == nullptr)
        return;
    core::Insert instrument;
    instrument.type = type;
    instrument.params = plugins::defaultParameters(type);
    if (track->isInstrument())
        onCommand(std::make_unique<core::SetInstrument>(channelId_, instrument));
    else if (track->isMidi() && onPlayNewInstrument)
        onPlayNewInstrument(track->id, type); // a channel for it, and the track aimed there
    else
        return;
    if (onInserted)
        onInserted(type);
}

void EditorPanel::showInstrumentMenu(juce::Component& target)
{
    auto built = buildPluginMenu(PluginKind::instrument, pluginSources_);
    juce::Component::SafePointer<EditorPanel> safe(this);
    built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                             [safe, built](int choice)
                             {
                                 if (safe != nullptr)
                                     if (const auto type = built.typeFor(choice); !type.empty())
                                         safe->setInstrument(type);
                             });
}

void EditorPanel::reorderCard(core::Id insertId, juce::Point<int> screen, bool released)
{
    const auto local = strip_.getLocalPoint(nullptr, screen);
    const int index = insertIndexForX(local.x);
    if (!released)
    {
        dropIndex_ = index;
        repaint();
        return;
    }
    dropIndex_ = -1;
    repaint();
    const auto* ch = channel();
    if (ch == nullptr || !onCommand)
        return;
    const int from = ch->insertIndex(insertId);
    const int to = index > from ? index - 1 : index;
    if (from >= 0 && to != from)
        onCommand(std::make_unique<core::MoveInsert>(channelId_, insertId, to));
}

void EditorPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop(theme_.sizes.control);
    viewport_.setBounds(area);
    const int caption = strip::ChannelStripView::captionHeight;
    const int h = std::max(theme_.sizes.dockMin - theme_.sizes.control,
                           area.getHeight() - theme_.spacing.md - theme_.sizes.scrollbar);
    const auto* ch = channel();
    const bool showStrip = stripView_.isVisible();
    const bool stripFirst = showStrip && ch != nullptr && ch->stripPreInserts;
    int x = theme_.spacing.md;
    // Each card to its place: eased from where it was when the cards changed
    // (a new one fades in), else at once.
    const int ms = animateLayout_ ? theme_.motion.ms(theme_.motion.panelMs) : 0;
    const auto place = [&](juce::Component& c, juce::Rectangle<int> target, const std::string& key)
    {
        auto before = c.getBounds();
        if (const auto it = previousBounds_.find(key); it != previousBounds_.end())
            before = it->second;
        c.setBounds(target);
        if (ms <= 0)
            motion::glideIn(c, target, 1.0f, 0); // settles any glide still running
        else if (before.isEmpty())
            motion::glideIn(c, target.translated(0, 6), 0.0f, ms);
        else
            motion::glideIn(c, before, 1.0f, ms);
    };
    // The instrument, then the strip and the inserts in signal order, each under
    // its caption.
    instrumentX_ = x;
    if (instrumentCard_ != nullptr)
    {
        const int w = instrumentCard_->preferredWidth();
        place(*instrumentCard_,
              {x,
               theme_.spacing.xs + strip::ChannelStripView::captionHeight,
               w,
               h - strip::ChannelStripView::captionHeight},
              "instrument");
        x += w + theme_.spacing.xl;
    }
    else if (addInstrument_.isVisible())
    {
        const int w = theme_.sizes.controlLarge * 4;
        place(addInstrument_,
              {x,
               theme_.spacing.xs + strip::ChannelStripView::captionHeight,
               w,
               h - strip::ChannelStripView::captionHeight},
              "addInstrument");
        x += w + theme_.spacing.xl;
    }
    const auto placeStrip = [&]
    {
        if (!showStrip)
            return;
        place(stripView_, {x, theme_.spacing.xs, stripView_.preferredWidth(), h}, "strip");
        x += stripView_.preferredWidth() + theme_.spacing.xl;
    };
    if (stripFirst)
        placeStrip();
    insertsX_ = x;
    for (auto& card : cards_)
    {
        place(*card,
              {x, theme_.spacing.xs + caption, card->preferredWidth(), h - caption},
              "insert:" + std::to_string(card->insertId()));
        x += card->preferredWidth() + theme_.spacing.sm;
    }
    // The add slot: an empty card the cards' height.
    const int addW = theme_.sizes.controlLarge * 3;
    place(add_, {x, theme_.spacing.xs + caption, addW, h - caption}, "add");
    x += addW + theme_.spacing.xl;
    if (!stripFirst)
        placeStrip();
    // The sends last: they tap the channel after its fader (or, pre-fader, just before it).
    sendsX_ = x;
    if (addSend_.isVisible())
    {
        for (auto& card : sendCards_)
        {
            place(*card,
                  {x, theme_.spacing.xs + caption, SendCard::preferredWidth(), h - caption},
                  "send:" + std::to_string(card->busId()));
            x += SendCard::preferredWidth() + theme_.spacing.sm;
        }
        place(addSend_, {x, theme_.spacing.xs + caption, SendCard::preferredWidth(), h - caption}, "addSend");
        x += SendCard::preferredWidth() + theme_.spacing.xl;
    }
    strip_.setSize(std::max(x, area.getWidth()), h + theme_.spacing.sm);
    strip_.repaint();
}

void EditorPanel::rememberBounds()
{
    previousBounds_.clear();
    for (const auto& card : cards_)
        previousBounds_["insert:" + std::to_string(card->insertId())] = card->getBounds();
    for (const auto& card : sendCards_)
        previousBounds_["send:" + std::to_string(card->busId())] = card->getBounds();
    if (instrumentCard_ != nullptr)
        previousBounds_["instrument"] = instrumentCard_->getBounds();
    previousBounds_["addInstrument"] = addInstrument_.getBounds();
    previousBounds_["strip"] = stripView_.getBounds();
    previousBounds_["add"] = add_.getBounds();
    previousBounds_["addSend"] = addSend_.getBounds();
}

void EditorPanel::rebuildSends(const core::Channel* ch, bool& shape)
{
    // A MIDI track makes no audio, so it has nothing to send.
    const auto* track = ch != nullptr && project_ != nullptr ? project_->findTrack(ch->id) : nullptr;
    const bool shown = ch != nullptr && (track == nullptr || track->hasAudio());
    std::vector<core::Id> order;
    if (shown)
        for (const auto& send : ch->sends)
            order.push_back(send.bus);
    bool same = shown == addSend_.isVisible() && order.size() == sendCards_.size();
    for (size_t i = 0; same && i < order.size(); ++i)
        same = sendCards_[i]->busId() == order[i];
    if (!same)
    {
        sendCards_.clear();
        for (const auto bus : order)
        {
            auto card = std::make_unique<SendCard>(*this, theme_, ch->id, bus);
            strip_.addAndMakeVisible(*card);
            sendCards_.push_back(std::move(card));
        }
        addSend_.setVisible(shown);
        shape = true;
    }
    for (size_t i = 0; i < sendCards_.size(); ++i)
        sendCards_[i]->refresh(ch->sends[i], project_->findBus(ch->sends[i].bus));
}

// The buses and FX tracks this channel can send to: those that would not feed
// it back into itself, with the ones it already sends to ticked.
void EditorPanel::showAddSendMenu()
{
    const auto* ch = channel();
    if (ch == nullptr || project_ == nullptr)
        return;
    auto built = buildSendMenu(*project_, ch->id);
    juce::Component::SafePointer<EditorPanel> safe(this);
    const core::Id channelId = ch->id;
    built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addSend_),
                             [safe, built, channelId](int choice)
                             {
                                 if (const auto bus = built.busFor(choice);
                                     safe != nullptr && bus != core::invalidId && safe->onCommand)
                                     safe->onCommand(
                                         std::make_unique<core::SetSend>(channelId, bus, 1.0f, false));
                             });
}

void EditorPanel::showAddMenu()
{
    // Effects only: an instrument has its own slot.
    auto built = buildPluginMenu(PluginKind::effect, pluginSources_);
    juce::Component::SafePointer<EditorPanel> safe(this);
    built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add_),
                             [safe, built](int choice)
                             {
                                 if (safe != nullptr)
                                     if (const auto type = built.typeFor(choice); !type.empty())
                                         safe->addInsert(type, -1);
                             });
}
} // namespace northlight::ui
