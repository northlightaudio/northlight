#pragma once

// The mixer strips' parts (internal to the mixer: included by MixerView.cpp only):
// the name cap, an insert slot, a send row, the channel strip's module row and
// the values they share.

#include "core/Command.h"
#include "engine/Snapshot.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ColourMenu.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/InsertMenu.h"
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

constexpr int hardwareBase = 1000; // output selector ids: hardwareBase + pair is a device output pair
constexpr int latencyRow = 14;     // the strip's latency line (a caption)

// A delay in milliseconds as a strip shows it: "0.7 ms", "12 ms".
juce::String latencyMs(int samples, double sampleRate)
{
    const double ms = sampleRate > 0.0 ? 1000.0 * samples / sampleRate : 0.0;
    return juce::String(ms, ms >= 10.0 ? 0 : 1) + " ms";
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
// power icon (left) and drag handle (right) on hover. A slot whose plugin is
// missing, failed or crashed shows a warning icon in its colour (warning for
// missing, danger otherwise) before its dimmed name; a click shows its menu,
// which says what is wrong and offers the way out. Dragging the handle moves
// the insert (to another strip too, with the window's drag and drop; Ctrl at the
// drop copies it); a click on the badge shows the plugin in the browser.
class InsertSlot final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InsertSlot(const Theme& theme) : theme_(theme) {}
    std::function<void()> onOpen, onBypass, onMenu, onBadge;
    // What a drag carries ("insert:<channel>:<insert>"); empty: the slot only
    // reorders within its strip (onReorder).
    std::function<juce::String()> dragDescription;

    void setTrouble(InsertTrouble trouble)
    {
        if (trouble.kind == trouble_.kind && trouble.reason == trouble_.reason)
            return;
        trouble_ = std::move(trouble);
        updateTooltip();
        repaint();
    }
    bool troubled() const noexcept { return trouble_.any(); }

    // Hover eases in the power button and the drag handle (the name moves over
    // to make room) and eases them out again, over motion.hoverMs.
    void mouseEnter(const juce::MouseEvent&) override { hoverTo(1.0f); }
    void mouseExit(const juce::MouseEvent&) override
    {
        badgeHover_ = false;
        hoverTo(0.0f);
    }
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
        updateTooltip();
        repaint();
    }
    void set(const juce::String& name, const juce::String& badge, bool bypassed)
    {
        name_ = name;
        badge_ = badge;
        bypassed_ = bypassed;
        updateTooltip();
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.setColour(c.surface2.interpolatedWith(c.surface3, hover_));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 0.5f), theme_.radius.sm);
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        const float shown = trouble_.any() ? 0.0f : hover_; // a troubled slot has no power or handle
        if (shown > 0.0f)
        {
            // The controls fade in where they sit; the name gives them room as they come.
            const auto power = area.withWidth(12).toFloat().reduced(0.0f, 4.0f);
            const auto handle = area.withTrimmedLeft(area.getWidth() - 10).toFloat().reduced(0.0f, 4.0f);
            Icons::draw(
                g, Icon::power, power, (bypassed_ ? c.textTertiary : c.success).withMultipliedAlpha(shown));
            Icons::draw(g, Icon::dragHandle, handle, c.textTertiary.withMultipliedAlpha(shown));
            area.removeFromLeft(juce::roundToInt((12.0f + static_cast<float>(theme_.spacing.xxs)) * shown));
            area.removeFromRight(juce::roundToInt(10.0f * shown));
        }
        if (trouble_.any())
        {
            const auto colour = trouble_.kind == InsertTrouble::Kind::missing ? c.warning : c.danger;
            Icons::draw(g, Icon::warning, area.removeFromLeft(12).toFloat().reduced(0.0f, 4.0f), colour);
            area.removeFromLeft(theme_.spacing.xxs);
        }
        // The badge stays while hovered (it opens the browser), left of the handle;
        // a slot in trouble gives its room to the name.
        if (badge_.isNotEmpty() && !trouble_.any())
        {
            g.setFont(theme_.caption());
            g.setColour(badgeHover_ ? c.accent : c.textTertiary);
            const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), badge_);
            badgeArea_ = area.removeFromRight(w);
            g.drawText(badge_, badgeArea_, juce::Justification::centredRight);
            area.removeFromRight(theme_.spacing.xxs);
        }
        else
            badgeArea_ = {};
        g.setFont(theme_.small());
        g.setColour(trouble_.any() ? c.textTertiary : bypassed_ ? c.textTertiary : c.textPrimary);
        g.drawText(name_, area, juce::Justification::centredLeft, true);
        if (bypassed_ && !trouble_.any())
        {
            const int w =
                std::min(area.getWidth(), juce::GlyphArrangement::getStringWidthInt(theme_.small(), name_));
            g.fillRect(area.getX(), area.getCentreY(), w, 1);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        const bool over = onBadge && badgeArea_.contains(e.getPosition());
        if (over != badgeHover_)
        {
            badgeHover_ = over;
            setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = false;
        if (e.mods.isPopupMenu() || trouble_.any())
        {
            // A slot in trouble shows what is wrong and the way out on any click.
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
        if (!handle_ || e.getDistanceFromDragStart() < 3)
            return;
        // Through the window's drag and drop when there is one: the slot can go to
        // another strip (or be copied there with Ctrl at the drop).
        if (!dragging_ && dragDescription)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                dragging_ = true;
                handle_ = false;
                container->startDragging(
                    dragDescription(), this, juce::ScaledImage(createComponentSnapshot(getLocalBounds())));
                return;
            }
        dragging_ = true;
        if (onReorder)
            onReorder(e.getScreenPosition(), false);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (dragging_)
        {
            if (handle_ && onReorder)
                onReorder(e.getScreenPosition(), true);
        }
        else if (!e.mods.isPopupMenu() && onBadge && badgeArea_.contains(e.getPosition()))
            onBadge();
        else if (!e.mods.isPopupMenu() && e.x >= theme_.spacing.xs + 12 && onOpen)
            onOpen();
        dragging_ = false;
        handle_ = false;
    }

private:
    void updateTooltip()
    {
        if (trouble_.any())
            setTooltip(name_ + ": " + trouble_.label() + ". " +
                       juce::String::fromUTF8(trouble_.reason.c_str()));
        else
            setTooltip(name_ + (bypassed_ ? " (bypassed)" : ""));
    }

    const Theme& theme_;
    juce::String name_, badge_;
    InsertTrouble trouble_;
    juce::Rectangle<int> badgeArea_;
    bool bypassed_ = false, handle_ = false, dragging_ = false, badgeHover_ = false;
    float hover_ = 0.0f; // 0 at rest, 1 hovered
};

// A short strip's collapsed section ("3 inserts", "2 sends"): a slot-sized
// button with a chevron; a click lists the section's slots with their actions.
// A slot in trouble among them puts a warning icon before the count.
class SectionSummary final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SectionSummary(const Theme& theme) : theme_(theme)
    {
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    std::function<void()> onClick;
    void set(const juce::String& text, bool troubled)
    {
        if (text == text_ && troubled == troubled_)
            return;
        text_ = text;
        troubled_ = troubled;
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const bool hot = isMouseOver() || isMouseButtonDown();
        g.setColour(hot ? c.surface3 : c.surface2);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 0.5f), theme_.radius.sm);
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        Icons::draw(
            g, Icon::chevronDown, area.removeFromRight(10).toFloat().reduced(0.0f, 4.0f), c.textTertiary);
        if (troubled_)
        {
            Icons::draw(g, Icon::warning, area.removeFromLeft(12).toFloat().reduced(0.0f, 4.0f), c.warning);
            area.removeFromLeft(theme_.spacing.xxs);
        }
        g.setFont(theme_.small());
        g.setColour(hot ? c.textPrimary : c.textSecondary);
        g.drawText(text_, area, juce::Justification::centredLeft, true);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onClick && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

private:
    const Theme& theme_;
    juce::String text_;
    bool troubled_ = false;
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
    std::function<void(float)> onPan; // the wide strip's pan column
    std::function<void()> onTogglePre, onMenu, onGestureBegin, onGestureEnd;

    // A muted send shows its name struck and its slider dimmed; a wide strip
    // adds a pan column (drag up and down, double-click centres) after the slider.
    void set(const juce::String& name, float level, bool pre, float pan = 0.0f, bool mute = false)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        name_ = name;
        pre_ = pre;
        pan_ = pan;
        mute_ = mute;
        slider_.setValue(sendDb(level), juce::dontSendNotification);
        slider_.setTooltip(db::gainText(level) + " dB" + (mute ? " (muted)" : ""));
        slider_.setAlpha(mute ? 0.4f : 1.0f);
        setTooltip(name + (pre ? " (pre-fader" : " (post-fader") +
                   (pan != 0.0f ? ", pan " + panText(pan) : "") + (mute ? ", muted)" : ")"));
        repaint();
    }
    void setShowPan(bool show)
    {
        if (show == showPan_)
            return;
        showPan_ = show;
        resized();
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        if (showPan_)
        {
            const auto column = area.removeFromRight(panWidth);
            g.setFont(theme_.caption());
            g.setColour(c.textSecondary);
            g.drawText(panText(pan_), column, juce::Justification::centredRight);
            area.removeFromRight(theme_.spacing.xxs);
        }
        area.removeFromRight(40 + theme_.spacing.xs);
        const auto dot = area.removeFromRight(10).toFloat().withSizeKeepingCentre(6.0f, 6.0f);
        g.setColour(c.accent.withMultipliedAlpha(mute_ ? 0.5f : 1.0f));
        if (pre_)
            g.fillEllipse(dot);
        else
            g.drawEllipse(dot, 1.0f);
        g.setFont(theme_.small());
        g.setColour(mute_ ? c.textTertiary : c.textSecondary);
        const auto text = area.withTrimmedRight(theme_.spacing.xxs);
        g.drawText(name_, text, juce::Justification::centredLeft, true);
        if (mute_)
        {
            const int w =
                std::min(text.getWidth(), juce::GlyphArrangement::getStringWidthInt(theme_.small(), name_));
            g.fillRect(text.getX(), text.getCentreY(), w, 1);
        }
    }
    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.xs, 0);
        if (showPan_)
        {
            area.removeFromRight(panWidth);
            area.removeFromRight(theme_.spacing.xxs);
        }
        slider_.setBounds(area.removeFromRight(40));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (inPan(e.x))
        {
            panAtDown_ = pan_;
            if (onGestureBegin)
                onGestureBegin();
            panning_ = true;
            return;
        }
        if (e.x >= slider_.getX() - theme_.spacing.xs - 12 && e.x < slider_.getX() && onTogglePre)
            onTogglePre();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!panning_ || !onPan)
            return;
        const float to =
            std::clamp(panAtDown_ - static_cast<float>(e.getDistanceFromDragStartY()) / 100.0f, -1.0f, 1.0f);
        onPan(std::abs(to) < 0.02f ? 0.0f : to);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (panning_ && onGestureEnd)
            onGestureEnd();
        panning_ = false;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (inPan(e.x) && onPan)
            onPan(0.0f);
    }

private:
    static constexpr int panWidth = 24;
    bool inPan(int x) const noexcept { return showPan_ && x >= getWidth() - theme_.spacing.xs - panWidth; }

    const Theme& theme_;
    juce::String name_;
    bool pre_ = false, updating_ = false, mute_ = false, showPan_ = false, panning_ = false;
    float pan_ = 0.0f, panAtDown_ = 0.0f;
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
