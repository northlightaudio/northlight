#pragma once

// The mixer strips' parts (internal to the mixer: included by MixerView.cpp only):
// the name cap, an insert slot, a send row, the channel strip's module row and
// the values they share.

#include "core/Command.h"
#include "engine/Snapshot.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostedPlugin.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ClapEditor.h"
#include "ui/ColourMenu.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/InstrumentOutputs.h"
#include "ui/MixerView.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PluginMenu.h"
#include "ui/SendMenu.h"
#include "ui/SidechainMenu.h"
#include "ui/components/AddSlot.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

namespace northlight::ui
{

namespace
{
constexpr double minFaderDb = -60.0;
constexpr double maxFaderDb = 6.0; // matches core::maxChannelGain

// Colours handed to new fader groups in turn.
constexpr std::uint32_t groupColours[] = {
    0xff4f8fe8u, 0xffe8a84fu, 0xff5fc96fu, 0xffd65f9eu, 0xff9b6fe8u, 0xff4fc9c9u};
constexpr int slotHeight = 20;     // an insert slot or a send row
constexpr int maxVisibleSlots = 6; // inserts shown before the box scrolls
constexpr int minFaderTravel = 120, maxFaderTravel = 200;

// The send slider's travel stops at minFaderDb (silence there); the value boxes
// take any gain down to db::silence, so one typed elsewhere is not clipped.
float sendGain(double db) noexcept
{
    return db::toGain(db, minFaderDb);
}
double sendDb(float gain) noexcept
{
    return db::fromGain(gain, minFaderDb, maxFaderDb);
}

juce::String panText(double v)
{
    const int percent = static_cast<int>(std::lround(std::abs(v) * 100.0));
    return percent == 0 ? juce::String("C") : juce::String(v < 0 ? "L" : "R") + juce::String(percent);
}

// The name cap at the top of a strip: the colour stripe, the name (double-click
// renames), the group bar; a click selects the channel.
class NameCap final : public juce::Component
{
public:
    explicit NameCap(const Theme& theme) : theme_(theme)
    {
        name_.setFont(theme_.label());
        name_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
        name_.setJustificationType(juce::Justification::centredLeft);
        name_.setMinimumHorizontalScale(1.0f);
        name_.setEditable(false, true, false);
        name_.setInterceptsMouseClicks(false, false);
        name_.onTextChange = [this]
        {
            if (onRenamed)
                onRenamed(name_.getText().trim());
        };
        addAndMakeVisible(name_);
    }
    std::function<void(const juce::String&)> onRenamed;
    std::function<void()> onClicked, onMenu;

    void
    set(const juce::String& name, juce::Colour colour, bool selected, int group, juce::Colour groupColour)
    {
        name_.setText(name, juce::dontSendNotification);
        colour_ = colour;
        selected_ = selected;
        group_ = group;
        groupColour_ = groupColour;
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.setColour(colour_.withAlpha(selected_ ? 1.0f : theme_.track.clipHeaderAlpha));
        g.fillRect(getLocalBounds());
        g.setColour(colour_);
        g.fillRect(getLocalBounds().removeFromLeft(theme_.track.headerStripeWidth));
        if (group_ > 0)
        {
            auto bar = getLocalBounds().removeFromTop(3);
            g.setColour(groupColour_);
            g.fillRect(bar);
            g.setFont(theme_.caption());
            g.drawText(juce::String(group_),
                       getLocalBounds().removeFromRight(14),
                       juce::Justification::centredRight);
        }
        juce::ignoreUnused(c);
    }
    void resized() override
    {
        name_.setBounds(getLocalBounds()
                            .withTrimmedLeft(theme_.track.headerStripeWidth + theme_.spacing.xs)
                            .withTrimmedRight(group_ > 0 ? 14 : 2));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (onClicked)
            onClicked();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { name_.showEditor(); }

private:
    const Theme& theme_;
    juce::Label name_;
    juce::Colour colour_, groupColour_;
    bool selected_ = false;
    int group_ = 0;
};

// An insert slot: name in small, the format badge, a strike when bypassed, the
// power icon (left) and drag handle (right) on hover.
class InsertSlot final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InsertSlot(const Theme& theme) : theme_(theme) {}
    std::function<void()> onOpen, onBypass, onMenu;

    // Hover eases in the power button and the drag handle (the name moves over
    // to make room) and eases them out again, over motion.hoverMs.
    void mouseEnter(const juce::MouseEvent&) override { hoverTo(1.0f); }
    void mouseExit(const juce::MouseEvent&) override { hoverTo(0.0f); }
    void hoverTo(float target)
    {
        const float from = hover_;
        juce::Component::SafePointer<InsertSlot> safe(this);
        motion::Tween::cancel(*this);
        motion::Tween::run(*this,
                           theme_.motion.ms(theme_.motion.hoverMs),
                           [safe, from, target](float t)
                           {
                               if (safe == nullptr)
                                   return;
                               safe->hover_ = from + (target - from) * t;
                               safe->repaint();
                           });
    }
    std::function<void(juce::Point<int> screen, bool released)> onReorder;

    void setBypassed(bool bypassed)
    {
        if (bypassed == bypassed_)
            return;
        bypassed_ = bypassed;
        setTooltip(name_ + (bypassed ? " (bypassed)" : ""));
        repaint();
    }
    void set(const juce::String& name, const juce::String& badge, bool bypassed)
    {
        name_ = name;
        badge_ = badge;
        bypassed_ = bypassed;
        setTooltip(name + (bypassed ? " (bypassed)" : ""));
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.setColour(c.surface2.interpolatedWith(c.surface3, hover_));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 0.5f), theme_.radius.sm);
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        if (hover_ > 0.0f)
        {
            // The controls fade in where they sit; the name gives them room as they come.
            const auto power = area.withWidth(12).toFloat().reduced(0.0f, 4.0f);
            const auto handle = area.withTrimmedLeft(area.getWidth() - 10).toFloat().reduced(0.0f, 4.0f);
            Icons::draw(
                g, Icon::power, power, (bypassed_ ? c.textTertiary : c.success).withMultipliedAlpha(hover_));
            Icons::draw(g, Icon::dragHandle, handle, c.textTertiary.withMultipliedAlpha(hover_));
            area.removeFromLeft(juce::roundToInt((12.0f + static_cast<float>(theme_.spacing.xxs)) * hover_));
            area.removeFromRight(juce::roundToInt(10.0f * hover_));
        }
        if (badge_.isNotEmpty() && hover_ < 1.0f)
        {
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary.withMultipliedAlpha(1.0f - hover_));
            const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), badge_);
            g.drawText(badge_, area.removeFromRight(w), juce::Justification::centredRight);
            area.removeFromRight(theme_.spacing.xxs);
        }
        g.setFont(theme_.small());
        g.setColour(bypassed_ ? c.textTertiary : c.textPrimary);
        g.drawText(name_, area, juce::Justification::centredLeft, true);
        if (bypassed_)
        {
            const int w =
                std::min(area.getWidth(), juce::GlyphArrangement::getStringWidthInt(theme_.small(), name_));
            g.fillRect(area.getX(), area.getCentreY(), w, 1);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = false;
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (e.x < theme_.spacing.xs + 12)
        {
            if (onBypass)
                onBypass();
            return;
        }
        handle_ = e.x >= getWidth() - theme_.spacing.xs - 10;
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!handle_ || !e.mouseWasDraggedSinceMouseDown())
            return;
        dragging_ = true;
        if (onReorder)
            onReorder(e.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragging_)
        {
            if (onReorder)
                onReorder(e.getScreenPosition(), true);
        }
        else if (!e.mods.isPopupMenu() && e.x >= theme_.spacing.xs + 12 && onOpen)
            onOpen();
        dragging_ = false;
        handle_ = false;
    }

private:
    const Theme& theme_;
    juce::String name_, badge_;
    bool bypassed_ = false, handle_ = false, dragging_ = false;
    float hover_ = 0.0f; // 0 at rest, 1 hovered
};

// A send row: the bus name, a 40 px mini slider and the pre/post dot.
class SendRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SendRow(const Theme& theme) : theme_(theme)
    {
        slider_.setSliderStyle(juce::Slider::LinearHorizontal);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider_.setRange(minFaderDb, maxFaderDb, 0.1);
        slider_.setDoubleClickReturnValue(true, 0.0);
        slider_.onDragStart = [this]
        {
            if (onGestureBegin)
                onGestureBegin();
        };
        slider_.onDragEnd = [this]
        {
            if (onGestureEnd)
                onGestureEnd();
        };
        slider_.onValueChange = [this]
        {
            slider_.setTooltip(
                db::text(slider_.getValue() <= minFaderDb + 0.05 ? db::silence : slider_.getValue()) + " dB");
            if (!updating_ && onLevel)
                onLevel(sendGain(slider_.getValue()));
        };
        addAndMakeVisible(slider_);
    }
    std::function<void(float)> onLevel;
    std::function<void()> onTogglePre, onMenu, onGestureBegin, onGestureEnd;

    void set(const juce::String& name, float level, bool pre)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        name_ = name;
        pre_ = pre;
        slider_.setValue(sendDb(level), juce::dontSendNotification);
        slider_.setTooltip(db::gainText(level) + " dB");
        setTooltip(name + (pre ? " (pre-fader)" : " (post-fader)"));
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        area.removeFromRight(40 + theme_.spacing.xs);
        const auto dot = area.removeFromRight(10).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        g.setColour(c.accent);
        if (pre_)
            g.fillEllipse(dot);
        else
            g.drawEllipse(dot, 1.0f);
        g.setFont(theme_.small());
        g.setColour(c.textSecondary);
        g.drawText(name_, area.withTrimmedRight(theme_.spacing.xxs), juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        slider_.setBounds(getLocalBounds().reduced(theme_.spacing.xs, 0).removeFromRight(40));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (e.x >= getWidth() - theme_.spacing.xs - 40 - theme_.spacing.xs - 12 && onTogglePre)
            onTogglePre();
    }

private:
    const Theme& theme_;
    juce::String name_;
    bool pre_ = false, updating_ = false;
    juce::Slider slider_;
};

} // namespace

// ----- strip --------------------------------------------------------------------------------

// The channel strip's row in a mixer strip (screens/channel-strip.md 12.9): a cell
// per module with its initial, lit in the module's hue when on; a click
// switches the module in place, right-click offers the same plus "Edit channel
// strip". Gain-reducing modules show their reduction as a bar along the bottom.
class StripRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StripRow(const Theme& theme) : theme_(theme) { setRepaintsOnMouseActivity(true); }

    std::function<void(core::Id moduleId, bool on)> onToggle;
    std::function<void()> onEdit;

    bool hasModules() const noexcept { return !cells_.empty(); }

    void set(const core::Channel& channel)
    {
        std::vector<Cell> cells;
        for (const auto& m : channel.strip)
        {
            Cell cell;
            cell.id = m.id;
            cell.type = m.type;
            cell.on = !m.bypass;
            for (const auto& old : cells_)
                if (old.id == m.id)
                    cell.reduction = old.reduction;
            cells.push_back(cell);
        }
        cells_ = std::move(cells);
        repaint();
    }

    ~StripRow() override { stopWatching(); }

    // The live gain reduction of the modules that are on (from their feedback).
    // The row watches the processors it reads, so they meter while it shows.
    void updateReductions(const std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)>& live)
    {
        bool changed = false;
        std::vector<std::shared_ptr<engine::InsertProcessor>> watching;
        for (auto& cell : cells_)
        {
            float reduction = 0.0f;
            if (cell.on && live)
                if (const auto p = live(cell.id))
                    if (auto* fb = p->feedback())
                    {
                        fb->setWatched(true, engine::ProcessorFeedback::Watcher::mixer);
                        watching.push_back(p);
                        reduction = fb->latest().reduction;
                    }
            if (std::abs(reduction - cell.reduction) > 0.25f)
            {
                cell.reduction = reduction;
                changed = true;
            }
        }
        for (const auto& weak : watching_)
            if (const auto p = weak.lock();
                p != nullptr && std::find(watching.begin(), watching.end(), p) == watching.end())
                p->feedback()->setWatched(false, engine::ProcessorFeedback::Watcher::mixer);
        watching_.assign(watching.begin(), watching.end());
        if (changed)
            repaint();
    }

    // Hidden (the mixer closed): the processors need not meter for it.
    void visibilityChanged() override
    {
        if (!isShowing())
            stopWatching();
    }
    void parentHierarchyChanged() override
    {
        if (!isShowing())
            stopWatching();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto mouse = getMouseXYRelative();
        for (size_t i = 0; i < cells_.size(); ++i)
        {
            const auto& cell = cells_[i];
            const auto r = cellBounds(static_cast<int>(i)).toFloat().reduced(0.5f, 0.0f);
            const auto hue = strip::moduleColour(theme_, cell.type);
            const bool hover = isMouseOver() && r.contains(mouse.toFloat());
            g.setColour(cell.on ? hue.withAlpha(hover ? 0.42f : 0.3f) : (hover ? c.surface3 : c.surface2));
            g.fillRoundedRectangle(r, 2.0f);
            if (cell.on && cell.reduction > 0.5f && cell.type != plugins::strip::types::gate)
            {
                const float f = juce::jlimit(0.0f, 1.0f, cell.reduction / 12.0f);
                g.setColour(c.meterHot);
                g.fillRect(r.withTop(r.getBottom() - 2.0f).withWidth(r.getWidth() * f));
            }
            g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.micro, theme_.fonts.weight.semibold));
            g.setColour(cell.on ? c.textPrimary : c.textTertiary);
            g.drawText(juce::String(plugins::strip::displayName(cell.type)).substring(0, 1),
                       r,
                       juce::Justification::centred);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int i = cellAt(e.x);
        if (i >= 0)
        {
            const auto& cell = cells_[static_cast<size_t>(i)];
            setTooltip(juce::String(plugins::strip::displayName(cell.type)) +
                       (cell.on ? ": on, click to switch off" : ": off, click to switch on") +
                       " (right-click to edit the strip)");
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        const int i = cellAt(e.x);
        if (i >= 0 && onToggle)
            onToggle(cells_[static_cast<size_t>(i)].id, !cells_[static_cast<size_t>(i)].on);
    }

private:
    struct Cell
    {
        core::Id id = core::invalidId;
        std::string type;
        bool on = false;
        float reduction = 0.0f;
    };

    juce::Rectangle<int> cellBounds(int i) const
    {
        const int n = std::max<int>(1, static_cast<int>(cells_.size()));
        const int x0 = getWidth() * i / n, x1 = getWidth() * (i + 1) / n;
        return {x0, 0, x1 - x0, getHeight()};
    }

    int cellAt(int x) const
    {
        for (size_t i = 0; i < cells_.size(); ++i)
            if (cellBounds(static_cast<int>(i)).getHorizontalRange().contains(x))
                return static_cast<int>(i);
        return -1;
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Edit channel strip");
        menu.addSeparator();
        for (size_t i = 0; i < cells_.size(); ++i)
            menu.addItem(static_cast<int>(i) + 2,
                         juce::String(plugins::strip::displayName(cells_[i].type)),
                         true,
                         cells_[i].on);
        juce::Component::SafePointer<StripRow> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe](int choice)
                           {
                               if (safe == nullptr || choice <= 0)
                                   return;
                               if (choice == 1)
                               {
                                   if (safe->onEdit)
                                       safe->onEdit();
                                   return;
                               }
                               const auto i = static_cast<size_t>(choice - 2);
                               if (i < safe->cells_.size() && safe->onToggle)
                                   safe->onToggle(safe->cells_[i].id, !safe->cells_[i].on);
                           });
    }

    const Theme& theme_;
    std::vector<Cell> cells_;
    std::vector<std::weak_ptr<engine::InsertProcessor>> watching_; // the processors read, watched for the row
    void stopWatching()
    {
        for (const auto& weak : watching_)
            if (const auto p = weak.lock())
                if (auto* fb = p->feedback())
                    fb->setWatched(false, engine::ProcessorFeedback::Watcher::mixer);
        watching_.clear();
    }
};

} // namespace northlight::ui
