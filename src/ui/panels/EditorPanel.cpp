#include "ui/panels/EditorPanel.h"

#include "plugins/BuiltIns.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/strip/BuiltInCards.h"

#include <map>

namespace aural::ui
{

// ----- a device card ----------------------------------------------------------------------------

class EditorPanel::Card final : public juce::Component
{
public:
    Card(EditorPanel& owner, const Theme& theme, core::Id channelId, const core::Insert& insert)
        : owner_(owner), theme_(theme), channelId_(channelId), insertId_(insert.id), type_(insert.type),
          power_(theme, Icon::power, "Bypass"),
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
        open_.setButtonText("Open");
        open_.getProperties().set(AuralLookAndFeel::propertyVariant, "primary");
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
        if (dragging_ && e.mouseWasDraggedSinceMouseDown())
            owner_.reorderCard(insertId_, e.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragging_ && e.mouseWasDraggedSinceMouseDown())
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
            const auto text = "key: " + (source != nullptr ? juce::String::fromUTF8(source->name.c_str())
                                                           : juce::String("?"));
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
        auto footer = area.removeFromBottom(theme_.sizes.controlCompact).reduced(theme_.spacing.md, 0);
        footer.removeFromLeft(56);
        mix_.setBounds(footer);
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
    IconButton power_, menu_;
    juce::Slider mix_;
    juce::TextButton open_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    bool bypassed_ = false, updating_ = false, missing_ = false, dragging_ = false;
    core::Id sidechain_ = core::invalidId;
    int latency_ = 0;
    strip::CardHost host_;
    std::unique_ptr<strip::ModuleCard> body_; // a built-in's editor
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
    stripView_.onWidthChanged = [this] { resized(); };
    strip_.addChildComponent(stripView_);
    strip_.onPaint = [this](juce::Graphics& g)
    {
        if (channel() == nullptr)
            return;
        g.setFont(theme_.captionCaps());
        g.setColour(theme_.colours.textTertiary);
        g.drawText("INSERTS",
                   juce::Rectangle<int>(
                       insertsX_ + theme_.spacing.xxs, 0, 120, strip::ChannelStripView::captionHeight),
                   juce::Justification::centredLeft);
    };
    viewport_.setViewedComponent(&strip_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
    add_.setTooltip("Add an insert");
    add_.onClick = [this] { showAddMenu(); };
    strip_.addAndMakeVisible(add_);
    startTimerHz(60);
}

EditorPanel::~EditorPanel()
{
    stopTimer();
    cards_.clear();
    stripView_.setChannel(nullptr);
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
    // Only a change of shape lays the panel out again (each card repaints
    // what changed in it): a knob drag refreshes this many times a second.
    const int stripWidth = stripView_.preferredWidth();
    const bool stripShown = ch != nullptr && !ch->strip.empty();
    const bool preInserts = ch != nullptr && ch->stripPreInserts;
    stripView_.setChannel(ch);
    const bool shape = !same || add_.isVisible() != (ch != nullptr) || stripView_.isVisible() != stripShown ||
                       stripWidth != stripView_.preferredWidth() || preInserts != laidOutPreInserts_;
    add_.setVisible(ch != nullptr);
    stripView_.setVisible(stripShown);
    laidOutPreInserts_ = preInserts;
    if (shape)
    {
        resized();
        repaint();
    }
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
            for (const auto& card : cards_)
                card->stopWatching();
        wasShowing_ = false;
        return;
    }
    wasShowing_ = true;
    for (const auto& card : cards_)
        card->tick(elapsed);
    // Hosted plugins change their own values: keep the knobs current (10 Hz).
    if (++ticks_ % 6 != 0)
        return;
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
        g.drawText(
            "Select a track or bus to see its inserts", getLocalBounds(), juce::Justification::centred);
        return;
    }
    g.drawText(juce::String::fromUTF8(ch->name.c_str()) + juce::String::fromUTF8("  \xe2\x96\xb8"),
               title,
               juce::Justification::centredLeft);
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
    if (!type.empty())
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
    // The strip and the inserts in signal order, each under its caption.
    const auto placeStrip = [&]
    {
        if (!showStrip)
            return;
        stripView_.setBounds(x, theme_.spacing.xs, stripView_.preferredWidth(), h);
        x += stripView_.preferredWidth() + theme_.spacing.xl;
    };
    if (stripFirst)
        placeStrip();
    insertsX_ = x;
    for (auto& card : cards_)
    {
        card->setBounds(x, theme_.spacing.xs + caption, card->preferredWidth(), h - caption);
        x += card->preferredWidth() + theme_.spacing.sm;
    }
    // The add slot: an empty card the cards' height.
    const int addW = theme_.sizes.controlLarge * 3;
    add_.setBounds(x, theme_.spacing.xs + caption, addW, h - caption);
    x += addW + theme_.spacing.xl;
    if (!stripFirst)
        placeStrip();
    strip_.setSize(std::max(x, area.getWidth()), h + theme_.spacing.sm);
    strip_.repaint();
}

void EditorPanel::showAddMenu()
{
    // Recent, built-ins, favourites, then every scanned plugin by manufacturer.
    juce::PopupMenu menu;
    std::vector<std::string> types;
    int item = 1;
    const auto nameOf = [this](const std::string& type)
    { return displayName ? juce::String::fromUTF8(displayName(type).c_str()) : juce::String(type); };
    const auto known = [this](const std::string& type)
    {
        if (!hosting::PluginRegistry::isHostedType(type))
            return plugins::BuiltInFactory().create(type) != nullptr;
        return registry_ != nullptr && registry_->findByType(type) != nullptr;
    };
    const auto section = [&](const char* title, const std::vector<std::string>& list)
    {
        bool any = false;
        for (const auto& type : list)
        {
            if (!known(type))
                continue;
            if (!any)
                menu.addSectionHeader(title);
            any = true;
            menu.addItem(item++, nameOf(type));
            types.push_back(type);
        }
    };
    if (listSetting)
        section("Recent", listSetting("plugins.recent"));
    menu.addSectionHeader("Built-in");
    for (const auto& type : plugins::BuiltInFactory().typeNames())
    {
        menu.addItem(item++, plugins::BuiltInFactory::displayName(type));
        types.push_back(type);
    }
    if (listSetting)
        section("Favourites", listSetting("plugins.favourites"));
    if (registry_ != nullptr && !registry_->plugins().empty())
    {
        std::map<std::string, juce::PopupMenu> byManufacturer;
        for (const auto& plugin : registry_->plugins())
        {
            byManufacturer[plugin.manufacturer.empty() ? "Other" : plugin.manufacturer].addItem(
                item++,
                juce::String::fromUTF8(plugin.name.c_str()) + "  [" + juce::String(plugin.format) + "]");
            types.push_back(hosting::PluginRegistry::typeFor(plugin));
        }
        menu.addSeparator();
        for (auto& [manufacturer, sub] : byManufacturer)
            menu.addSubMenu(juce::String::fromUTF8(manufacturer.c_str()), sub);
    }
    juce::Component::SafePointer<EditorPanel> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add_),
                       [safe, types](int choice)
                       {
                           if (safe == nullptr || choice <= 0 || static_cast<size_t>(choice) > types.size())
                               return;
                           safe->addInsert(types[static_cast<size_t>(choice - 1)], -1);
                       });
}
} // namespace aural::ui
