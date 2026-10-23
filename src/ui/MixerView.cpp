#include "ui/MixerView.h"

#include "engine/Snapshot.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostedPlugin.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ClapEditor.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/InstrumentOutputs.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
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
constexpr int meterHz = 30;
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

    // The live gain reduction of the modules that are on (from their feedback).
    void updateReductions(const std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)>& live)
    {
        bool changed = false;
        for (auto& cell : cells_)
        {
            float reduction = 0.0f;
            if (cell.on && live)
                if (const auto p = live(cell.id))
                    if (auto* fb = p->feedback())
                        reduction = fb->latest().reduction;
            if (std::abs(reduction - cell.reduction) > 0.25f)
            {
                cell.reduction = reduction;
                changed = true;
            }
        }
        if (changed)
            repaint();
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
            g.setFont(
                theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.semibold));
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
};

class MixerView::Strip final : public juce::Component
{
public:
    Strip(MixerView& owner, const Theme& theme)
        : owner_(owner), theme_(theme), cap_(theme), stripRow_(theme), addInsert_(theme, "Insert"),
          addSend_(theme, "Send"), pan_(theme), fader_(theme), meter_(theme, 2, true), value_(theme)
    {
        setOpaque(true);
        stripRow_.onToggle = [this](core::Id moduleId, bool on)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetInsertBypass>(id_, moduleId, !on));
        };
        stripRow_.onEdit = [this]
        {
            if (owner_.onShowStrip)
                owner_.onShowStrip(id_);
        };
        addAndMakeVisible(stripRow_);
        setWantsKeyboardFocus(true);
        cap_.onRenamed = [this](const juce::String& name)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetTrackName>(id_, name.toStdString()));
        };
        cap_.onClicked = [this]
        {
            if (owner_.onSelectChannel)
                owner_.onSelectChannel(id_);
        };
        cap_.onMenu = [this] { showGroupMenu(); };
        addAndMakeVisible(cap_);

        input_.setTooltip("Input");
        input_.onChange = [this]
        {
            if (updating_)
                return;
            if (midi_)
            {
                const int index = input_.getSelectedId() - 2;
                owner_.onCommand(std::make_unique<core::SetTrackMidiInput>(
                    id_,
                    index >= 0 && static_cast<size_t>(index) < owner_.midiInputNames_.size()
                        ? owner_.midiInputNames_[static_cast<size_t>(index)]
                        : std::string{}));
            }
            else if (const auto choice = inputs::selected(input_, owner_.inputChoices_))
                owner_.onCommand(std::make_unique<core::SetTrackInput>(id_, choice->channel, choice->bus));
        };
        addAndMakeVisible(input_);

        insertsView_.setViewedComponent(&insertsHolder_, false);
        insertsView_.setScrollBarsShown(false, false, true, false);
        insertsView_.setScrollBarThickness(4);
        addAndMakeVisible(insertsView_);
        instrument_.setTooltip("The instrument this track's MIDI plays");
        addChildComponent(instrument_);
        addInstrument_.setTooltip("Choose the instrument this track's MIDI plays");
        addInstrument_.onClick = [this] { showInstrumentMenu(addInstrument_); };
        addChildComponent(addInstrument_);
        addInsert_.setTooltip("Add an insert");
        addInsert_.onClick = [this] { showAddInsertMenu(); };
        insertsHolder_.addAndMakeVisible(addInsert_);
        addSend_.setTooltip("Add a send");
        addSend_.onClick = [this] { showAddSendMenu(); };
        addAndMakeVisible(addSend_);

        pan_.setRange(-1.0, 1.0, 0.0);
        pan_.setShowValue(true);
        pan_.setTooltip("Pan");
        pan_.format = [](double v) { return panText(v); };
        pan_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Pan");
        };
        pan_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        pan_.onValueChanged = [this](double v)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetTrackPan>(id_, static_cast<float>(v)));
        };
        addAndMakeVisible(pan_);

        const auto toggle = [this](juce::TextButton& b,
                                   const char* text,
                                   const char* semantic,
                                   const char* tip,
                                   auto makeCommand)
        {
            b.setButtonText(juce::String::fromUTF8(text));
            b.setClickingTogglesState(true);
            b.getProperties().set(NorthlightLookAndFeel::propertySemantic, semantic);
            b.setTooltip(tip);
            b.onClick = [this, &b, makeCommand]
            {
                if (!updating_)
                    owner_.onCommand(makeCommand(id_, group_, b.getToggleState()));
            };
            addAndMakeVisible(b);
        };
        toggle(mute_,
               "M",
               "mute",
               "Mute",
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupMute>(group, on);
                   return std::make_unique<core::SetTrackMute>(id, on);
               });
        toggle(solo_,
               "S",
               "solo",
               "Solo",
               [](core::Id id, core::Id group, bool on) -> std::unique_ptr<core::Command>
               {
                   if (group != core::invalidId)
                       return std::make_unique<core::SetGroupSolo>(group, on);
                   return std::make_unique<core::SetTrackSolo>(id, on);
               });
        toggle(arm_,
               "R",
               "arm",
               "Arm for recording",
               [](core::Id id, core::Id, bool on) -> std::unique_ptr<core::Command>
               { return std::make_unique<core::SetTrackArmed>(id, on); });
        arm_.getProperties().set(NorthlightLookAndFeel::propertyHardOn, true);
        toggle(phase_,
               "\xC3\x98",
               "monitor",
               "Phase invert",
               [](core::Id id, core::Id, bool on) -> std::unique_ptr<core::Command>
               { return std::make_unique<core::SetPhaseInvert>(id, on); });
        phase_.getProperties().set(NorthlightLookAndFeel::propertySemantic, "phase");

        fader_.setMaxDb(static_cast<float>(maxFaderDb));
        fader_.onDragStart = [this]
        {
            breakLink_ = juce::ModifierKeys::getCurrentModifiers().isAltDown();
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Fader");
        };
        fader_.onDragEnd = [this]
        {
            breakLink_ = false;
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        fader_.onGainChanged = [this](float gain) { gainChanged(gain); };
        addAndMakeVisible(fader_);
        addAndMakeVisible(meter_);

        value_.setRange(db::silence, maxFaderDb, 0.1);
        value_.setDecimals(1);
        value_.setUnit("dB");
        value_.format = [](double v) { return db::text(v); };
        value_.parse = [](const juce::String& t) { return db::parse(t); };
        value_.setTooltip("Gain");
        value_.onDragStart = fader_.onDragStart;
        value_.onDragEnd = fader_.onDragEnd;
        value_.onValueChanged = [this](double v) { gainChanged(db::toGain(v)); };
        addAndMakeVisible(value_);

        output_.setTooltip("Output");
        output_.onChange = [this]
        {
            if (updating_)
                return;
            const int index = output_.getSelectedId() - 2; // id 1 = master
            const core::Id bus = index >= 0 && static_cast<size_t>(index) < busIds_.size()
                                     ? busIds_[static_cast<size_t>(index)]
                                     : core::invalidId;
            owner_.onCommand(std::make_unique<core::SetOutputBus>(id_, bus));
        };
        addAndMakeVisible(output_);
    }

    core::Id id() const noexcept { return id_; }
    int meterSlot() const noexcept { return meterSlot_; }
    bool isBus() const noexcept { return isBus_; }

    // A values refresh (a fader drag, 20 times a second): only what live
    // commands change (fader, pan, the toggles, bypass states), with no combo
    // refill, no new callbacks and no relayout.
    void setValues(const core::Channel& channel)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        pan_.setValue(channel.pan, juce::dontSendNotification);
        currentGain_ = channel.gain;
        fader_.setGain(channel.gain, juce::dontSendNotification);
        value_.setValue(db::fromGain(channel.gain), juce::dontSendNotification);
        showPlayed();
        mute_.setToggleState(channel.mute, juce::dontSendNotification);
        solo_.setToggleState(channel.solo, juce::dontSendNotification);
        phase_.setToggleState(channel.phaseInvert, juce::dontSendNotification);
        stripRow_.set(channel);
        if (channel.hasInstrument())
            instrument_.setBypassed(channel.instrument.bypass);
        for (size_t i = 0; i < inserts_.size() && i < channel.inserts.size(); ++i)
            inserts_[i]->setBypassed(channel.inserts[i].bypass);
    }

    void
    setChannel(const core::Channel& channel, bool isBus, const core::Project& project, int slot, int index)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        id_ = channel.id;
        isBus_ = isBus;
        meterSlot_ = slot;
        const auto* track = isBus ? nullptr : project.findTrack(channel.id);
        midi_ = track != nullptr && track->isMidi();
        aux_ = track != nullptr && track->isAux();
        const auto* busModel = isBus ? project.findBus(channel.id) : nullptr;
        fx_ = busModel != nullptr && busModel->fx;
        colour_ = isBus ? (fx_ && channel.colour == 0 ? strip::paletteColour(theme_, "Lichen")
                                                      : theme_.track.forBus(channel.colour))
                        : theme_.track.forTrack(channel.colour, index);
        fader_.setTrackColour(colour_);
        group_ = channel.group;
        const auto* group = project.findGroup(channel.group);
        int groupNumber = 0;
        for (size_t i = 0; i < project.groups.size(); ++i)
            if (project.groups[i].id == channel.group)
                groupNumber = static_cast<int>(i) + 1;
        cap_.set(
            juce::String::fromUTF8(channel.name.empty() ? (isBus ? "Bus" : "Track") : channel.name.c_str()),
            colour_,
            id_ == owner_.selected_,
            groupNumber,
            group != nullptr && group->colour != 0 ? juce::Colour(group->colour) : theme_.colours.accent);
        pan_.setValue(channel.pan, juce::dontSendNotification);
        currentGain_ = channel.gain;
        fader_.setGain(channel.gain, juce::dontSendNotification);
        value_.setValue(db::fromGain(channel.gain), juce::dontSendNotification);
        showPlayed();
        mute_.setToggleState(channel.mute, juce::dontSendNotification);
        solo_.setToggleState(channel.solo, juce::dontSendNotification);
        phase_.setToggleState(channel.phaseInvert, juce::dontSendNotification);
        arm_.setVisible(track != nullptr && !aux_ && !track->isInstrument()); // its MIDI tracks arm
        arm_.setToggleState(track != nullptr && track->armed, juce::dontSendNotification);

        // The input (tracks only): audio inputs or MIDI inputs.
        input_.setVisible(track != nullptr && !aux_ && !track->isInstrument());
        input_.clear(juce::dontSendNotification);
        if (track != nullptr && midi_)
        {
            input_.addItem("All MIDI", 1);
            int selected = 1;
            for (size_t i = 0; i < owner_.midiInputNames_.size(); ++i)
            {
                input_.addItem(juce::String::fromUTF8(owner_.midiInputNames_[i].c_str()),
                               static_cast<int>(i) + 2);
                if (owner_.midiInputNames_[i] == track->midiInput)
                    selected = static_cast<int>(i) + 2;
            }
            input_.setSelectedId(selected, juce::dontSendNotification);
        }
        else if (track != nullptr)
            inputs::fill(input_, owner_.inputChoices_, *track);

        busIds_.clear();
        output_.clear(juce::dontSendNotification);
        output_.addItem("Master", 1);
        int id = 2;
        for (const auto& b : project.buses)
        {
            busIds_.push_back(b.id);
            output_.addItem(juce::String::fromUTF8(b.name.empty() ? "Bus" : b.name.c_str()), id);
            // Itself, or a bus that already feeds this one, would make a loop: greyed.
            output_.setItemEnabled(id, !project.wouldCycle(channel.id, b.id));
            ++id;
        }
        int selected = 1;
        for (size_t i = 0; i < busIds_.size(); ++i)
            if (busIds_[i] == channel.outputBus)
                selected = static_cast<int>(i) + 2;
        output_.setSelectedId(selected, juce::dontSendNotification);

        stripRow_.set(channel);
        stripRow_.setVisible(stripRow_.hasModules());

        // The instrument slot (a MIDI track's): filled, or an add slot.
        const bool instrumentSlot = track != nullptr && (track->isInstrument() || track->hasInstrument());
        instrumentSlot_ = instrumentSlot;
        instrument_.setVisible(instrumentSlot && track->hasInstrument());
        addInstrument_.setVisible(instrumentSlot && !track->hasInstrument());
        if (instrumentSlot && track->hasInstrument())
        {
            const auto& inst = track->instrument;
            const core::Id instrumentId = inst.id;
            juce::String badge = "INST";
            if (hosting::PluginRegistry::isHostedType(inst.type))
                badge = inst.type.rfind("clap:", 0) == 0 ? "CLAP" : "VST3";
            instrument_.set(
                juce::String::fromUTF8(owner_.insertDisplayName(inst.type).c_str()), badge, inst.bypass);
            instrument_.onOpen = [this, instrumentId] { owner_.openInsertEditor(id_, instrumentId); };
            instrument_.onBypass = [this, instrumentId]
            {
                if (const auto* ch = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr)
                    if (const auto* ins = ch->findInsert(instrumentId))
                        owner_.onCommand(
                            std::make_unique<core::SetInsertBypass>(id_, instrumentId, !ins->bypass));
            };
            instrument_.onMenu = [this, instrumentId] { showInsertMenu(instrumentId); };
            instrument_.onReorder = nullptr;
        }

        // Insert slots.
        while (inserts_.size() > channel.inserts.size())
            inserts_.pop_back();
        while (inserts_.size() < channel.inserts.size())
        {
            auto made = std::make_unique<InsertSlot>(theme_);
            insertsHolder_.addAndMakeVisible(*made);
            inserts_.push_back(std::move(made));
        }
        for (size_t i = 0; i < channel.inserts.size(); ++i)
        {
            const auto& insert = channel.inserts[i];
            const core::Id insertId = insert.id;
            juce::String badge;
            if (hosting::PluginRegistry::isHostedType(insert.type))
            {
                const auto* info =
                    owner_.host_ != nullptr ? owner_.host_->registry().findByType(insert.type) : nullptr;
                badge = info != nullptr && info->isInstrument ? "INST"
                        : insert.type.rfind("clap:", 0) == 0  ? "CLAP"
                                                              : "VST3";
            }
            else if (plugins::BuiltInFactory::isInstrument(insert.type))
                badge = "INST";
            inserts_[i]->set(
                juce::String::fromUTF8(owner_.insertDisplayName(insert.type).c_str()), badge, insert.bypass);
            inserts_[i]->onOpen = [this, insertId] { owner_.openInsertEditor(id_, insertId); };
            inserts_[i]->onBypass = [this, insertId]
            {
                if (const auto* ch = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr)
                    if (const auto* ins = ch->findInsert(insertId))
                        owner_.onCommand(
                            std::make_unique<core::SetInsertBypass>(id_, insertId, !ins->bypass));
            };
            inserts_[i]->onMenu = [this, insertId] { showInsertMenu(insertId); };
            inserts_[i]->onReorder = [this, insertId](juce::Point<int> screen, bool released)
            { reorderInsert(insertId, screen, released); };
        }
        insertCount_ = static_cast<int>(channel.inserts.size());

        // Send rows.
        while (sends_.size() > channel.sends.size())
            sends_.pop_back();
        while (sends_.size() < channel.sends.size())
        {
            auto row = std::make_unique<SendRow>(theme_);
            addAndMakeVisible(*row);
            sends_.push_back(std::move(row));
        }
        for (size_t i = 0; i < channel.sends.size(); ++i)
        {
            const auto& send = channel.sends[i];
            const auto* bus = project.findBus(send.bus);
            const core::Id busId = send.bus;
            const bool pre = send.preFader;
            const float level = send.level;
            sends_[i]->set(
                juce::String::fromUTF8(bus != nullptr && !bus->name.empty() ? bus->name.c_str() : "Bus"),
                level,
                pre);
            sends_[i]->onLevel = [this, busId, pre](float value)
            { owner_.onCommand(std::make_unique<core::SetSend>(id_, busId, value, pre)); };
            sends_[i]->onTogglePre = [this, busId, level, pre]
            { owner_.onCommand(std::make_unique<core::SetSend>(id_, busId, level, !pre)); };
            sends_[i]->onMenu = [this, busId, row = sends_[i].get()]
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Remove send");
                juce::Component::SafePointer<Strip> safe(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(row),
                                   [safe, id = id_, busId](int choice)
                                   {
                                       if (safe != nullptr && choice == 1)
                                           safe->owner_.onCommand(
                                               std::make_unique<core::RemoveSend>(id, busId));
                                   });
            };
            sends_[i]->onGestureBegin = [this]
            {
                if (owner_.onGestureBegin)
                    owner_.onGestureBegin("Send level");
            };
            sends_[i]->onGestureEnd = [this]
            {
                if (owner_.onGestureEnd)
                    owner_.onGestureEnd();
            };
        }
        sendCount_ = static_cast<int>(channel.sends.size());
        addSend_.setEnabled(!busIds_.empty());
        feedsSoloedBus_ = false;
        for (const auto& b : project.buses)
            if (b.solo && (channel.outputBus == b.id ||
                           std::any_of(channel.sends.begin(),
                                       channel.sends.end(),
                                       [&](const core::Send& s) { return s.bus == b.id; })))
                feedsSoloedBus_ = true;
        resized();
        repaint();
    }

    void pushMeter(const rt::MeterBlock::Reading& r, double elapsedMs)
    {
        const float peaks[2] = {r.peakLeft, r.peakRight};
        meter_.push(peaks, 2, elapsedMs);
        stripRow_.updateReductions(owner_.liveInsert);
        showPlayed();
    }

    // An automated fader and pan show what their lanes play. Every refresh ends
    // here, so a model refresh (another strip's fader drag) never shows the static
    // value for a frame before the next meter tick moves it back.
    void showPlayed()
    {
        if (owner_.automatedValues)
            showAutomated(owner_.automatedValues(id_));
    }

    // What this strip needs of the shared row plan, and where its fader band
    // and gain field landed (for the master to match).
    void addTo(RowPlan& rows) const
    {
        rows.input = rows.input || input_.isVisible();
        rows.stripRow = rows.stripRow || stripRow_.isVisible();
        rows.instrument = rows.instrument || instrumentSlot_;
        rows.inserts = std::max(rows.inserts, insertCount_);
        rows.sends = std::max(rows.sends, sendCount_);
    }
    juce::Rectangle<int> faderBand() const { return fader_.getBounds().getUnion(meter_.getBounds()); }
    int valueY() const { return value_.getY(); }

    // The fader and pan follow what automation plays (unless the user holds them).
    void showAutomated(const MixerView::Automated& a)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        if (a.gain && !fader_.isMouseButtonDown() && !value_.isMouseButtonDown())
        {
            fader_.setGain(a.gainValue, juce::dontSendNotification);
            value_.setValue(db::fromGain(a.gainValue), juce::dontSendNotification);
        }
        if (a.pan && !pan_.isMouseButtonDown())
            pan_.setValue(a.panValue, juce::dontSendNotification);
        if (a.gain != automatedShown_)
        {
            automatedShown_ = a.gain;
            fader_.setTooltip(a.gain ? "Gain (automated: the fader follows its lane)" : "Gain");
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        auto bounds = getLocalBounds().toFloat().reduced(0.0f, 0.5f);
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.sm);
        if (id_ == owner_.selected_)
        {
            g.setColour(c.accent);
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
        }
        else if (feedsSoloedBus_)
        {
            g.setColour(c.solo.withAlpha(0.5f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
        }
        // Section labels and the collapsed summaries.
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        if (collapsed_)
        {
            g.drawText(juce::String(insertCount_) + (insertCount_ == 1 ? " insert" : " inserts"),
                       insertsSummary_,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(sendCount_) + (sendCount_ == 1 ? " send" : " sends"),
                       sendsSummary_,
                       juce::Justification::centredLeft);
        }
        if (hasKeyboardFocus(false))
            if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
                laf->drawFocusRing(g, *this, bounds.reduced(1.0f), theme_.radius.sm);
        if (reorderLineY_ >= 0)
        {
            g.setColour(c.accent);
            g.fillRect(insertsView_.getX(), reorderLineY_ - 1, insertsView_.getWidth(), 2);
        }
    }

    void resized() override
    {
        const int pad = theme_.spacing.xs;
        const int control = theme_.sizes.controlCompact;
        auto area = getLocalBounds().reduced(pad, 0);
        cap_.setBounds(getLocalBounds().withHeight(control));
        area.removeFromTop(control + pad);
        // Rows another strip has are reserved here too (empty), so every strip's
        // sections line up across the mixer.
        const auto& rows = owner_.rows_;
        if (rows.input || input_.isVisible())
        {
            input_.setBounds(area.removeFromTop(control));
            area.removeFromTop(pad);
        }
        if (rows.stripRow || stripRow_.isVisible())
        {
            stripRow_.setBounds(area.removeFromTop(16));
            area.removeFromTop(pad);
        }
        if (rows.instrument || instrumentSlot_)
        {
            const auto row = area.removeFromTop(slotHeight);
            instrument_.setBounds(row.withTrimmedBottom(1));
            addInstrument_.setBounds(row.withTrimmedBottom(1));
            area.removeFromTop(pad);
        }
        // The fixed bottom: pan, toggles, value, output; the fader takes the rest
        // between 120 and 200 px, and below 120 the sections collapse.
        // (The last term is the room under the output selector.)
        const int bottom =
            theme_.sizes.knob + 16 + pad + control + pad + pad + control + pad + control + theme_.spacing.sm;
        const int insertsFull =
            std::min(std::max(rows.inserts, insertCount_) + 1, maxVisibleSlots) * slotHeight + 4;
        const int sendsFull = (std::max(rows.sends, sendCount_) + 1) * slotHeight + 4;
        int faderTravel = area.getHeight() - bottom - insertsFull - sendsFull - 2 * pad;
        collapsed_ = faderTravel < minFaderTravel;
        const int insertsH = collapsed_ ? slotHeight : insertsFull;
        const int sendsH = collapsed_ ? slotHeight : sendsFull;
        faderTravel = std::clamp(area.getHeight() - bottom - insertsH - sendsH - 2 * pad, 24, maxFaderTravel);

        auto insertsArea = area.removeFromTop(insertsH);
        area.removeFromTop(pad);
        auto sendsArea = area.removeFromTop(sendsH);
        area.removeFromTop(pad);
        insertsSummary_ = insertsArea.withTrimmedLeft(pad);
        sendsSummary_ = sendsArea.withTrimmedLeft(pad);
        insertsView_.setVisible(!collapsed_);
        addSend_.setVisible(!collapsed_);
        for (auto& s : sends_)
            s->setVisible(!collapsed_);
        if (!collapsed_)
        {
            insertsView_.setBounds(insertsArea.reduced(0, 2));
            insertsHolder_.setSize(insertsView_.getWidth(), (insertCount_ + 1) * slotHeight);
            int y = 0;
            for (auto& slot : inserts_)
            {
                slot->setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
                y += slotHeight;
            }
            addInsert_.setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
            sendsArea.reduce(0, 2);
            for (auto& row : sends_)
                row->setBounds(sendsArea.removeFromTop(slotHeight).withTrimmedBottom(1));
            addSend_.setBounds(sendsArea.removeFromTop(slotHeight).withTrimmedBottom(1));
        }

        auto faderArea = area.removeFromTop(faderTravel + 2 * pad);
        auto meterArea = faderArea.removeFromRight(meter_.barsWidth());
        meter_.setBounds(meterArea.withTrimmedTop(pad));
        faderArea.removeFromRight(pad);
        fader_.setBounds(faderArea);
        area.removeFromTop(pad);
        pan_.setBounds(area.removeFromTop(theme_.sizes.knob + 16)
                           .withSizeKeepingCentre(theme_.sizes.knob + 16, theme_.sizes.knob + 16));
        area.removeFromTop(pad);
        auto toggles = area.removeFromTop(control);
        const int count = arm_.isVisible() ? 4 : 3;
        const int tw = (toggles.getWidth() - (count - 1) * theme_.spacing.xxs) / count;
        mute_.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(theme_.spacing.xxs);
        solo_.setBounds(toggles.removeFromLeft(tw));
        toggles.removeFromLeft(theme_.spacing.xxs);
        if (arm_.isVisible())
        {
            arm_.setBounds(toggles.removeFromLeft(tw));
            toggles.removeFromLeft(theme_.spacing.xxs);
        }
        phase_.setBounds(toggles);
        area.removeFromTop(pad);
        value_.setBounds(area.removeFromTop(control));
        area.removeFromTop(pad);
        output_.setBounds(area.removeFromTop(control));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress('m') || key == juce::KeyPress('M'))
            mute_.triggerClick();
        else if (key == juce::KeyPress('s') || key == juce::KeyPress('S'))
            solo_.triggerClick();
        else if ((key == juce::KeyPress('r') || key == juce::KeyPress('R')) && arm_.isVisible())
            arm_.triggerClick();
        else
            return fader_.keyPressed(key);
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    void gainChanged(float gain)
    {
        if (updating_)
            return;
        // In a group the move is relative: every member scales by the same ratio.
        // A member sitting at -inf has no ratio, so it moves alone; Alt breaks the link.
        if (group_ != core::invalidId && currentGain_ > 1.0e-6f && !breakLink_)
            owner_.onCommand(std::make_unique<core::SetGroupGain>(group_, gain / currentGain_));
        else
            owner_.onCommand(std::make_unique<core::SetTrackGain>(id_, gain));
    }

    void reorderInsert(core::Id insertId, juce::Point<int> screen, bool released)
    {
        const auto local = insertsHolder_.getLocalPoint(nullptr, screen);
        const int index = std::clamp(
            static_cast<int>(std::lround(static_cast<double>(local.y) / slotHeight)), 0, insertCount_);
        if (!released)
        {
            reorderLineY_ = getLocalPoint(&insertsHolder_, juce::Point<int>(0, index * slotHeight)).y;
            repaint();
            return;
        }
        reorderLineY_ = -1;
        repaint();
        const auto* channel = owner_.project_ != nullptr ? owner_.project_->findChannel(id_) : nullptr;
        if (channel == nullptr)
            return;
        const int from = channel->insertIndex(insertId);
        const int to = index > from ? index - 1 : index;
        if (from >= 0 && to != from)
            owner_.onCommand(std::make_unique<core::MoveInsert>(id_, insertId, to));
    }

    void showGroupMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        const auto& project = *owner_.project_;
        juce::PopupMenu menu;
        // The channel this menu is for, fixed now: a rebuild while it is open may
        // give this strip another channel.
        const core::Id id = id_;
        std::vector<std::function<void()>> actions;
        int item = 1;
        juce::PopupMenu groups;
        groups.addItem(item++, "None", true, group_ == core::invalidId);
        actions.push_back(
            [this, id] { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id, core::invalidId)); });
        for (const auto& g : project.groups)
        {
            groups.addItem(item++, g.name, true, g.id == group_);
            const core::Id groupId = g.id;
            actions.push_back([this, id, groupId]
                              { owner_.onCommand(std::make_unique<core::SetChannelGroup>(id, groupId)); });
        }
        groups.addSeparator();
        groups.addItem(item++, "New group");
        actions.push_back(
            [this, id]
            {
                if (owner_.project_ == nullptr)
                    return;
                const auto n = owner_.project_->groups.size();
                core::Group g;
                g.name = "Group " + std::to_string(n + 1);
                g.colour = groupColours[n % std::size(groupColours)];
                owner_.onCommand(std::make_unique<core::AddGroup>(g, std::vector<core::Id>{id}));
            });
        if (group_ != core::invalidId)
        {
            groups.addItem(item++, "Remove group");
            const core::Id groupId = group_;
            actions.push_back([this, groupId]
                              { owner_.onCommand(std::make_unique<core::RemoveGroup>(groupId)); });
        }
        menu.addSubMenu("Fader group", groups);
        juce::PopupMenu colours;
        const auto& palette = theme_.track.palette;
        for (size_t i = 0; i < palette.size(); ++i)
        {
            colours.addItem(item++, palette[i].name);
            const auto argb = palette[i].value.getARGB();
            actions.push_back([this, id, argb]
                              { owner_.onCommand(std::make_unique<core::SetColour>(id, argb)); });
        }
        colours.addSeparator();
        colours.addItem(item++, "Default");
        actions.push_back([this, id] { owner_.onCommand(std::make_unique<core::SetColour>(id, 0u)); });
        menu.addSubMenu("Colour", colours);
        if (isBus_)
        {
            menu.addSeparator();
            menu.addItem(item++, "Remove bus");
            actions.push_back(
                [this, id]
                {
                    // The removal rebuilds the strips, which may delete this one:
                    // run it once the menu's callback has unwound.
                    juce::Component::SafePointer<MixerView> owner(&owner_);
                    juce::MessageManager::callAsync(
                        [owner, id]
                        {
                            if (owner != nullptr && owner->onCommand)
                                owner->onCommand(std::make_unique<core::RemoveBus>(id));
                        });
                });
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(cap_),
                           [actions](int chosen)
                           {
                               if (chosen > 0 && static_cast<size_t>(chosen) <= actions.size())
                                   actions[static_cast<size_t>(chosen - 1)]();
                           });
    }

    void showInsertMenu(core::Id insertId)
    {
        if (owner_.project_ == nullptr)
            return;
        const auto* channel = owner_.project_->findChannel(id_);
        if (channel == nullptr || !channel->hasInstrument() || insertId != channel->instrument.id)
        {
            owner_.showInsertMenu(id_, insertId, !isBus_);
            return;
        }
        // The instrument slot: open and bypass, then its own items.
        const bool bypass = channel->instrument.bypass;
        juce::PopupMenu menu;
        std::vector<std::function<void()>> actions;
        int item = 1;
        menu.addItem(item++, "Open");
        actions.push_back([this, insertId] { owner_.openInsertEditor(id_, insertId); });
        menu.addItem(item++, "Bypass", true, bypass);
        actions.push_back(
            [this, insertId, bypass]
            { owner_.onCommand(std::make_unique<core::SetInsertBypass>(id_, insertId, !bypass)); });
        showInstrumentSlotMenu(menu, actions, item);
    }

    // The rest of the instrument slot's menu: its outputs as tracks, replace and
    // remove.
    void showInstrumentSlotMenu(juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, int item)
    {
        const auto* track = owner_.project_->findTrack(id_);
        if (track == nullptr)
            return;
        const auto live = owner_.liveInsert ? owner_.liveInsert(track->instrument.id) : nullptr;
        if (live != nullptr && live->auxOutputCount() > 0)
        {
            juce::PopupMenu outputs;
            for (int output = 1; output <= live->auxOutputCount(); ++output)
            {
                const bool exists = outputTrack(*owner_.project_, *track, output) != nullptr;
                outputs.addItem(item++, "Track for " + juce::String(outputPairName(output)), !exists, exists);
                const auto made = makeOutputTrack(*track, output, live->auxOutputName(output - 1));
                actions.push_back([this, made] { owner_.onCommand(std::make_unique<core::AddTrack>(made)); });
            }
            menu.addSubMenu("Outputs", outputs);
        }
        menu.addSeparator();
        auto replace = buildPluginMenu(PluginKind::instrument, owner_.pluginSources_, item);
        for (const auto& type : replace.types)
            actions.push_back([this, type] { setInstrument(type); });
        item = replace.nextItem();
        menu.addSubMenu("Replace instrument", replace.menu);
        menu.addItem(item++, "Remove instrument");
        actions.push_back([this]
                          { owner_.onCommand(std::make_unique<core::SetInstrument>(id_, core::Insert{})); });
        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [actions](int result)
                           {
                               if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                                   actions[static_cast<size_t>(result) - 1]();
                           });
    }

    void setInstrument(const std::string& type)
    {
        core::Insert instrument;
        instrument.type = type;
        instrument.params = plugins::defaultParameters(type);
        owner_.onCommand(std::make_unique<core::SetInstrument>(id_, instrument));
    }

    void showInstrumentMenu(juce::Component& target)
    {
        auto built = buildPluginMenu(PluginKind::instrument, owner_.pluginSources_);
        juce::Component::SafePointer<Strip> safe(this);
        built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                                 [safe, built](int choice)
                                 {
                                     if (safe != nullptr)
                                         if (const auto type = built.typeFor(choice); !type.empty())
                                             safe->setInstrument(type);
                                 });
    }

    void showAddInsertMenu() { owner_.showAddInsertMenu(id_, addInsert_); }

    void showAddSendMenu()
    {
        if (owner_.project_ == nullptr)
            return;
        auto built = buildSendMenu(*owner_.project_, id_);
        juce::Component::SafePointer<Strip> safe(this);
        built.menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(addSend_),
            [safe, built, id = id_](int choice)
            {
                if (const auto bus = built.busFor(choice); safe != nullptr && bus != core::invalidId)
                    safe->owner_.onCommand(std::make_unique<core::SetSend>(id, bus, 1.0f, false));
            });
    }

    MixerView& owner_;
    const Theme& theme_;
    core::Id id_ = core::invalidId;
    core::Id group_ = core::invalidId;
    float currentGain_ = 1.0f;
    bool isBus_ = false, midi_ = false, aux_ = false;
    int meterSlot_ = -1;
    int sendCount_ = 0;
    int insertCount_ = 0;
    bool updating_ = false, breakLink_ = false, collapsed_ = false, feedsSoloedBus_ = false;
    int reorderLineY_ = -1;
    juce::Colour colour_;
    std::vector<core::Id> busIds_;
    NameCap cap_;
    juce::ComboBox input_, output_;
    juce::Viewport insertsView_;
    juce::Component insertsHolder_;
    StripRow stripRow_;
    bool automatedShown_ = false;
    bool fx_ = false; // an FX track (a bus fed by sends)
    std::vector<std::unique_ptr<InsertSlot>> inserts_;
    std::vector<std::unique_ptr<SendRow>> sends_;
    AddSlot addInsert_, addSend_;
    InsertSlot instrument_{theme_};
    AddSlot addInstrument_{theme_, "Instrument"};
    bool instrumentSlot_ = false; // an instrument channel: the instrument row shows
    juce::Rectangle<int> insertsSummary_, sendsSummary_;
    Knob pan_;
    juce::TextButton mute_, solo_, arm_, phase_;
    Fader fader_;
    Meter meter_;
    NumericField value_;
};

// ----- master -------------------------------------------------------------------------------

class MixerView::MasterStrip final : public juce::Component
{
public:
    MasterStrip(MixerView& owner, const Theme& theme)
        : owner_(owner), theme_(theme), cap_(theme), addInsert_(theme, "Insert"), fader_(theme),
          meter_(theme, 2, true), value_(theme)
    {
        setOpaque(true);
        cap_.set("Master", theme_.colours.textPrimary, false, 0, {});
        cap_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(cap_);
        // The inserts: on the whole mix, before the master fader.
        insertsView_.setViewedComponent(&insertsHolder_, false);
        insertsView_.setScrollBarsShown(false, false, true, false);
        insertsView_.setScrollBarThickness(4);
        addAndMakeVisible(insertsView_);
        addInsert_.setTooltip("Add an insert on the whole mix, before the master fader");
        addInsert_.onClick = [this] { owner_.showAddInsertMenu(core::masterId, addInsert_); };
        insertsHolder_.addAndMakeVisible(addInsert_);
        fader_.setMaxDb(static_cast<float>(maxFaderDb));
        fader_.setTrackColour(theme_.colours.textPrimary);
        fader_.onDragStart = [this]
        {
            if (owner_.onGestureBegin)
                owner_.onGestureBegin("Master fader");
        };
        fader_.onDragEnd = [this]
        {
            if (owner_.onGestureEnd)
                owner_.onGestureEnd();
        };
        fader_.onGainChanged = [this](float gain)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetMasterGain>(gain));
        };
        addAndMakeVisible(fader_);
        meter_.setShowScale(true);
        addAndMakeVisible(meter_);
        value_.setRange(db::silence, maxFaderDb, 0.1);
        value_.setDecimals(1);
        value_.setUnit("dB");
        value_.format = [](double v) { return db::text(v); };
        value_.parse = [](const juce::String& t) { return db::parse(t); };
        value_.onDragStart = fader_.onDragStart;
        value_.onDragEnd = fader_.onDragEnd;
        value_.onValueChanged = [this](double db)
        {
            if (!updating_)
                owner_.onCommand(std::make_unique<core::SetMasterGain>(db::toGain(db)));
        };
        addAndMakeVisible(value_);
    }

    void setMaster(const core::Master& master)
    {
        const juce::ScopedValueSetter<bool> guard(updating_, true);
        fader_.setGain(master.gain, juce::dontSendNotification);
        value_.setValue(db::fromGain(master.gain), juce::dontSendNotification);

        const bool countChanged = inserts_.size() != master.inserts.size();
        while (inserts_.size() > master.inserts.size())
            inserts_.pop_back();
        while (inserts_.size() < master.inserts.size())
        {
            auto made = std::make_unique<InsertSlot>(theme_);
            insertsHolder_.addAndMakeVisible(*made);
            inserts_.push_back(std::move(made));
        }
        for (size_t i = 0; i < master.inserts.size(); ++i)
        {
            const auto& insert = master.inserts[i];
            const core::Id insertId = insert.id;
            juce::String badge;
            if (hosting::PluginRegistry::isHostedType(insert.type))
                badge = insert.type.rfind("clap:", 0) == 0 ? "CLAP" : "VST3";
            inserts_[i]->set(
                juce::String::fromUTF8(owner_.insertDisplayName(insert.type).c_str()), badge, insert.bypass);
            inserts_[i]->onOpen = [this, insertId] { owner_.openInsertEditor(core::masterId, insertId); };
            inserts_[i]->onBypass = [this, insertId]
            {
                if (const auto* ins =
                        owner_.project_ != nullptr ? owner_.project_->master.findInsert(insertId) : nullptr)
                    owner_.onCommand(
                        std::make_unique<core::SetInsertBypass>(core::masterId, insertId, !ins->bypass));
            };
            inserts_[i]->onMenu = [this, insertId]
            { owner_.showInsertMenu(core::masterId, insertId, false); };
            inserts_[i]->onReorder = [this, insertId](juce::Point<int> screen, bool released)
            { reorderInsert(insertId, screen, released); };
        }
        if (countChanged)
            resized();
    }

    void pushMeter(const rt::MeterBlock::Reading& r, double elapsedMs)
    {
        const float peaks[2] = {r.peakLeft, r.peakRight};
        meter_.push(peaks, 2, elapsedMs);
    }

    // The channel strips' fader band and gain field row, to line up with; an
    // empty band lays the master out on its own.
    void alignTo(juce::Rectangle<int> band, int valueY)
    {
        if (band == band_ && valueY == valueY_)
            return;
        band_ = band;
        valueY_ = valueY;
        resized();
    }

    void pushLoudness(const engine::LoudnessMeter::Reading& r)
    {
        loudness_ = r;
        repaint(loudnessArea_);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (reorderLineY_ >= 0)
        {
            g.setColour(theme_.colours.accent);
            g.fillRect(insertsView_.getX(), reorderLineY_ - 1, insertsView_.getWidth(), 2);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.fillAll(c.surface0);
        g.setColour(c.surface1);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.0f, 0.5f), theme_.radius.sm);
        // The loudness readout: M / S / I in value mono with the target marker by I.
        const auto text = [](float lufs)
        { return lufs <= -99.0f ? juce::String("-inf") : juce::String(lufs, 1); };
        auto area = loudnessArea_;
        g.setFont(theme_.value());
        const int rowH = area.getHeight() / 3;
        const auto row = [&](const char* label, float lufs, bool target)
        {
            auto r = area.removeFromTop(rowH);
            g.setColour(c.textTertiary);
            g.drawText(label, r.removeFromLeft(14), juce::Justification::centredLeft);
            g.setColour(target && lufs > -99.0f && std::abs(lufs - theme_.meter.loudnessTargetLufs) <= 1.0f
                            ? c.loudnessTarget
                            : c.textSecondary);
            g.drawText(text(lufs), r, juce::Justification::centredRight);
        };
        row("M", loudness_.momentaryLufs, false);
        row("S", loudness_.shortTermLufs, false);
        row("I", loudness_.integratedLufs, true);
    }

    void resized() override
    {
        const int pad = theme_.spacing.xs;
        const int control = theme_.sizes.controlCompact;
        auto area = getLocalBounds().reduced(pad, 0);
        cap_.setBounds(getLocalBounds().withHeight(control));
        area.removeFromTop(control + pad);
        const int loudnessH = 3 * (static_cast<int>(theme_.fonts.size.value) + 4);
        const bool aligned =
            !band_.isEmpty() && valueY_ + control <= getHeight() && band_.getBottom() + loudnessH <= valueY_;
        // The inserts fill the room above the fader band (lined up with the
        // channels), or take up to six slots of their own.
        {
            const int wanted = (static_cast<int>(inserts_.size()) + 1) * slotHeight + 4;
            const int room = aligned ? band_.getY() - area.getY() - pad
                                     : std::min(wanted, maxVisibleSlots * slotHeight + 4);
            auto insertsArea = area.removeFromTop(std::max(slotHeight + 4, std::min(room, wanted)));
            area.removeFromTop(pad);
            insertsView_.setBounds(insertsArea.reduced(0, 2));
            insertsHolder_.setSize(insertsView_.getWidth(),
                                   (static_cast<int>(inserts_.size()) + 1) * slotHeight);
            int y = 0;
            for (auto& slot : inserts_)
            {
                slot->setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
                y += slotHeight;
            }
            addInsert_.setBounds(0, y, insertsHolder_.getWidth(), slotHeight - 1);
        }
        if (aligned)
        {
            // Lined up with the channels: the same fader band and gain row, the
            // loudness readout between them.
            value_.setBounds(area.getX(), valueY_, area.getWidth(), control);
            loudnessArea_ = juce::Rectangle<int>(
                                area.getX(), band_.getBottom(), area.getWidth(), valueY_ - band_.getBottom())
                                .withSizeKeepingCentre(area.getWidth(), loudnessH);
            auto faderArea =
                juce::Rectangle<int>(area.getX(), band_.getY(), area.getWidth(), band_.getHeight());
            meter_.setBounds(faderArea.removeFromRight(meter_.barsWidth() + 18));
            faderArea.removeFromRight(pad);
            fader_.setBounds(faderArea);
            return;
        }
        value_.setBounds(area.removeFromBottom(control));
        area.removeFromBottom(pad);
        loudnessArea_ = area.removeFromBottom(loudnessH);
        area.removeFromBottom(pad);
        auto faderArea = area.withTrimmedTop(pad).withHeight(
            std::clamp(area.getHeight() - pad, 60, maxFaderTravel + 2 * pad));
        auto meterArea = faderArea.removeFromRight(meter_.barsWidth() + 18);
        meter_.setBounds(meterArea);
        faderArea.removeFromRight(pad);
        fader_.setBounds(faderArea);
    }

private:
    void reorderInsert(core::Id insertId, juce::Point<int> screen, bool released)
    {
        const auto local = insertsHolder_.getLocalPoint(nullptr, screen);
        const int count = static_cast<int>(inserts_.size());
        const int index =
            std::clamp(static_cast<int>(std::lround(static_cast<double>(local.y) / slotHeight)), 0, count);
        if (!released)
        {
            reorderLineY_ = getLocalPoint(&insertsHolder_, juce::Point<int>(0, index * slotHeight)).y;
            repaint();
            return;
        }
        reorderLineY_ = -1;
        repaint();
        if (owner_.project_ == nullptr)
            return;
        const int from = owner_.project_->master.insertIndex(insertId);
        const int to = index > from ? index - 1 : index;
        if (from >= 0 && to != from)
            owner_.onCommand(std::make_unique<core::MoveInsert>(core::masterId, insertId, to));
    }

    MixerView& owner_;
    const Theme& theme_;
    bool updating_ = false;
    NameCap cap_;
    juce::Viewport insertsView_;
    juce::Component insertsHolder_;
    std::vector<std::unique_ptr<InsertSlot>> inserts_;
    AddSlot addInsert_;
    int reorderLineY_ = -1;
    Fader fader_;
    Meter meter_;
    NumericField value_;
    juce::Rectangle<int> loudnessArea_;
    juce::Rectangle<int> band_;
    int valueY_ = 0;
    engine::LoudnessMeter::Reading loudness_;
};

// ----- insert editor windows ------------------------------------------------------------------

class MixerView::EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow(MixerView& owner, std::unique_ptr<InsertEditor> editor, const juce::String& title)
        : juce::DocumentWindow(title, Theme::dark().colours.background, juce::DocumentWindow::closeButton),
          owner_(owner), editor_(editor.get())
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor.release(), true);
        setResizable(false, false);
        setVisible(true);
    }

    InsertEditor& editor() noexcept { return *editor_; }

    void closeButtonPressed() override
    {
        juce::Component::SafePointer<MixerView> owner(&owner_);
        const auto id = editor_->insertId();
        juce::MessageManager::callAsync(
            [owner, id]
            {
                if (owner != nullptr)
                    std::erase_if(owner->editors_,
                                  [id](const auto& w) { return w->editor().insertId() == id; });
            });
    }

private:
    MixerView& owner_;
    InsertEditor* editor_;
};

// ----- view ---------------------------------------------------------------------------------

void MixerView::showAddInsertMenu(core::Id channelId, juce::Component& target)
{
    // Effects only: an instrument has its own slot.
    auto built = buildPluginMenu(PluginKind::effect, pluginSources_);
    auto& menu = built.menu;
    juce::Component::SafePointer<MixerView> safe(this);
    std::vector<std::function<void()>> actions;
    for (const auto& type : built.types)
        actions.push_back(
            [safe, channelId, type]
            {
                if (safe == nullptr || !safe->onCommand)
                    return;
                core::Insert insert;
                insert.type = type;
                insert.params = plugins::defaultParameters(type);
                safe->onCommand(std::make_unique<core::AddInsert>(channelId, insert));
            });
    int item = built.nextItem();
    menu.addSeparator();
    menu.addItem(item++, "Scan for plugins...");
    actions.push_back(
        [safe]
        {
            if (safe != nullptr && safe->onScanPlugins)
                safe->onScanPlugins();
        });
    menu.addItem(item++, "Plugin browser...");
    actions.push_back(
        [safe]
        {
            if (safe != nullptr && safe->onOpenPluginBrowser)
                safe->onOpenPluginBrowser();
        });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                       [actions](int result)
                       {
                           if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                               actions[static_cast<size_t>(result) - 1]();
                       });
}

void MixerView::showInsertMenu(core::Id channelId, core::Id insertId, bool offerOutputs)
{
    if (project_ == nullptr)
        return;
    const auto* channel = project_->findChannel(channelId);
    const auto* insert = channel != nullptr ? channel->findInsert(insertId) : nullptr;
    if (insert == nullptr)
        return;
    juce::Component::SafePointer<MixerView> safe(this);
    const auto command = [safe](std::unique_ptr<core::Command> c)
    {
        if (safe != nullptr && safe->onCommand)
            safe->onCommand(std::move(c));
    };
    juce::PopupMenu menu;
    std::vector<std::function<void()>> actions;
    int item = 1;
    menu.addItem(item++, "Open");
    actions.push_back(
        [safe, channelId, insertId]
        {
            if (safe != nullptr)
                safe->openInsertEditor(channelId, insertId);
        });
    menu.addItem(item++, "Bypass", true, insert->bypass);
    const bool bypass = insert->bypass;
    actions.push_back([command, channelId, insertId, bypass]
                      { command(std::make_unique<core::SetInsertBypass>(channelId, insertId, !bypass)); });
    const int index = channel->insertIndex(insertId);
    menu.addItem(item++, "Move up", index > 0);
    actions.push_back([command, channelId, insertId, index]
                      { command(std::make_unique<core::MoveInsert>(channelId, insertId, index - 1)); });
    menu.addItem(item++, "Move down", index + 1 < static_cast<int>(channel->inserts.size()));
    actions.push_back([command, channelId, insertId, index]
                      { command(std::make_unique<core::MoveInsert>(channelId, insertId, index + 1)); });
    juce::PopupMenu mix;
    for (const int percent : {0, 25, 50, 75, 100})
    {
        mix.addItem(
            item++, juce::String(percent) + "%", true, std::abs(insert->mix - percent / 100.0f) < 0.01f);
        actions.push_back(
            [command, channelId, insertId, percent]
            { command(std::make_unique<core::SetInsertMix>(channelId, insertId, percent / 100.0f)); });
    }
    menu.addSubMenu("Wet/dry", mix);
    // Key (sidechain) source, for inserts that take one: tracks for anyone, buses
    // too when the channel is a bus. The master's inserts run on the finished mix
    // and take no key.
    const auto live = liveInsert ? liveInsert(insertId) : nullptr;
    if (live != nullptr && live->acceptsSidechain() && channelId != core::masterId)
    {
        juce::PopupMenu key;
        addSidechainItems(key, actions, *project_, channelId, *insert, onCommand);
        item = static_cast<int>(actions.size()) + 1;
        menu.addSubMenu("Sidechain", key);
    }
    // Extra outputs of a multi-output instrument: one aux track per pair.
    if (live != nullptr && live->auxOutputCount() > 0 && offerOutputs)
    {
        juce::PopupMenu outputs;
        for (int output = 1; output <= live->auxOutputCount(); ++output)
        {
            bool exists = false;
            for (const auto& t : project_->tracks)
                if (t.isAux() && t.auxTrack == channelId && t.auxInsert == insertId && t.auxOutput == output)
                    exists = true;
            outputs.addItem(item++, "Track for output " + juce::String(output + 1), !exists, exists);
            const std::string name = channel->name;
            actions.push_back(
                [command, channelId, insertId, output, name]
                {
                    core::Track aux;
                    aux.kind = core::TrackKind::aux;
                    aux.name = name + " out " + std::to_string(output + 1);
                    aux.auxTrack = channelId;
                    aux.auxInsert = insertId;
                    aux.auxOutput = output;
                    command(std::make_unique<core::AddTrack>(aux));
                });
        }
        menu.addSubMenu("Outputs", outputs);
    }
    menu.addSeparator();
    menu.addItem(item++, "Remove");
    actions.push_back([command, channelId, insertId]
                      { command(std::make_unique<core::RemoveInsert>(channelId, insertId)); });
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [actions](int result)
                       {
                           if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                               actions[static_cast<size_t>(result) - 1]();
                       });
}

std::string MixerView::insertDisplayName(const std::string& type) const
{
    return host_ != nullptr ? host_->displayName(type) : plugins::BuiltInFactory::displayName(type);
}

void MixerView::openInsertEditor(core::Id channelId, core::Id insertId)
{
    for (auto& w : editors_)
        if (w->editor().insertId() == insertId)
        {
            w->toFront(true);
            return;
        }
    for (auto& w : pluginWindows_)
        if (w->insertId() == insertId)
        {
            w->toFront(true);
            return;
        }
    if (project_ == nullptr)
        return;
    const auto* channel = project_->findChannel(channelId);
    const auto* insert =
        channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(insertId) : nullptr;
    if (insert == nullptr)
        return;

    // A hosted plugin shows its own editor on its live instance.
    if (hosting::PluginRegistry::isHostedType(insert->type))
    {
        const auto live = liveInsert ? liveInsert(insertId) : nullptr;
        auto* vst3 = dynamic_cast<hosting::HostedPlugin*>(live.get());
        auto* clap = dynamic_cast<hosting::HostedClapPlugin*>(live.get());
        if (vst3 == nullptr && clap == nullptr)
            return; // not loaded (missing plugin): nothing to show
        const juce::String pluginName = juce::String::fromUTF8(insertDisplayName(insert->type).c_str());
        const juce::String trackName = juce::String::fromUTF8(channel->name.c_str());
        const juce::String format = hosting::PluginRegistry::isClapType(insert->type) ? "CLAP" : "VST3";
        const auto setting = [this](const std::string& key)
        { return loadSetting ? loadSetting(key) : std::string{}; };
        const bool pinned = setting("plugins.pin") == "1";

        // The editor component for the format: the VST3's own (or JUCE's generic
        // one), or the CLAP GUI embedded in a native child window.
        std::unique_ptr<juce::Component> editor;
        bool resizable = false;
        hosting::PluginPresets* presets = nullptr;
        ClapEditor* clapEditor = nullptr;
        if (vst3 != nullptr)
        {
            auto& instance = vst3->instance();
            juce::AudioProcessorEditor* raw =
                instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
            if (raw == nullptr)
                raw = new juce::GenericAudioProcessorEditor(instance);
            resizable = raw->isResizable();
            editor.reset(raw);
            presets = vst3;
        }
        else
        {
            if (clap->hasEditor())
            {
                auto owned = std::make_unique<ClapEditor>(*clap);
                clapEditor = owned.get();
                resizable = owned->isResizable();
                editor = std::move(owned);
            }
            else
            {
                // No GUI: the generic parameter editor over the model insert.
                auto generic =
                    std::make_unique<InsertEditor>(channelId, insertId, *clap, clap->displayName(), true);
                generic->onCommand = [this](std::unique_ptr<core::Command> c)
                {
                    if (onCommand)
                        onCommand(std::move(c));
                };
                generic->onGestureBegin = [this](const std::string& name)
                {
                    if (onGestureBegin)
                        onGestureBegin(name);
                };
                generic->onGestureEnd = [this]
                {
                    if (onGestureEnd)
                        onGestureEnd();
                };
                generic->onParameterGestureBegin = [live](int index) { live->beginParameterGesture(index); };
                generic->onParameterGestureEnd = [live](int index) { live->endParameterGesture(index); };
                generic->setInsert(*insert);
                editor = std::move(generic);
            }
            presets = clap;
        }
        auto window = std::make_unique<PluginWindow>(channelId,
                                                     insertId,
                                                     live,
                                                     *presets,
                                                     std::move(editor),
                                                     resizable,
                                                     pluginName,
                                                     trackName,
                                                     format,
                                                     pinned);
        juce::Component::SafePointer<MixerView> safe(this);
        if (clapEditor != nullptr)
        {
            juce::Component::SafePointer<PluginWindow> weak(window.get());
            clapEditor->onPluginResized = [weak](int w, int h)
            {
                if (weak != nullptr)
                    weak->editorSizeChanged(w, h);
            };
            clapEditor->onResizableKnown = [weak](bool canResize)
            {
                if (weak != nullptr)
                    weak->setResizable(canResize, false);
            };
            clapEditor->onPluginClosed = [safe, insertId]
            {
                // From the editor's timer: drop the window on the next message.
                juce::MessageManager::callAsync(
                    [safe, insertId]
                    {
                        if (safe != nullptr)
                            std::erase_if(safe->pluginWindows_,
                                          [insertId](const auto& w) { return w->insertId() == insertId; });
                    });
            };
        }
        window->onClose = [safe, insertId]
        {
            if (safe != nullptr)
                std::erase_if(safe->pluginWindows_,
                              [insertId](const auto& w) { return w->insertId() == insertId; });
        };
        window->onCommand = [safe](std::unique_ptr<core::Command> c)
        {
            if (safe != nullptr && safe->onCommand)
                safe->onCommand(std::move(c));
        };
        window->onPinChanged = [safe](bool pin)
        {
            if (safe != nullptr && safe->saveSetting)
                safe->saveSetting("plugins.pin", pin ? "1" : "0");
        };
        // Where this plugin's window was last left, and its size, per plugin type,
        // if still on screen.
        const std::string positionKey = "plugins.window." + insert->type;
        const auto saved = juce::StringArray::fromTokens(juce::String(setting(positionKey)), ",", "");
        const auto screen = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        if (saved.size() >= 2 && screen.contains(saved[0].getIntValue() + 20, saved[1].getIntValue() + 20))
        {
            window->setTopLeftPosition(saved[0].getIntValue(), saved[1].getIntValue());
            if (saved.size() == 4 && resizable && saved[2].getIntValue() > 100 && saved[3].getIntValue() > 50)
                window->setEditorSize(saved[2].getIntValue(), saved[3].getIntValue());
        }
        else
            window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
        juce::Component::SafePointer<PluginWindow> weakWindow(window.get());
        window->onBoundsChanged = [safe, weakWindow, positionKey](juce::Rectangle<int> b)
        {
            if (safe == nullptr || !safe->saveSetting || weakWindow == nullptr)
                return;
            const auto* content = weakWindow->getContentComponent();
            const int w = content != nullptr ? content->getWidth() : 0;
            const int h = content != nullptr ? content->getHeight() - PluginWindow::barHeight : 0;
            safe->saveSetting(positionKey,
                              std::to_string(b.getX()) + "," + std::to_string(b.getY()) + "," +
                                  std::to_string(w) + "," + std::to_string(h));
        };
        window->setInsert(*insert);
        pluginWindows_.push_back(std::move(window));
        return;
    }

    const auto processor = plugins::BuiltInFactory().create(insert->type);
    if (processor == nullptr)
        return;
    auto editor = std::make_unique<InsertEditor>(channelId, insertId, *processor);
    editor->onCommand = [this](std::unique_ptr<core::Command> c)
    {
        if (onCommand)
            onCommand(std::move(c));
    };
    editor->onGestureBegin = [this](const std::string& name)
    {
        if (onGestureBegin)
            onGestureBegin(name);
    };
    editor->onGestureEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    editor->project = [this] { return project_; };
    editor->showBuiltInBody(*insert, liveInsert);
    editor->setInsert(*insert);
    const juce::String title = juce::String(channel->name) + " - " +
                               juce::String(plugins::BuiltInFactory::displayName(insert->type));
    auto window = std::make_unique<EditorWindow>(*this, std::move(editor), title);
    window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
    editors_.push_back(std::move(window));
}

MixerView::MixerView(rt::MeterBlock& meters,
                     std::function<engine::LoudnessMeter::Reading()> loudness,
                     std::function<int()> masterMeterSlot)
    : meters_(meters), loudness_(std::move(loudness)), masterMeterSlot_(std::move(masterMeterSlot))
{
    setOpaque(true);
    viewport_.setViewedComponent(&stripsHolder_, false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(theme_.sizes.scrollbar);
    addAndMakeVisible(viewport_);
    master_ = std::make_unique<MasterStrip>(*this, theme_);
    addAndMakeVisible(*master_);
    addBus_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    addBus_.setTooltip("Add a bus or an FX track");
    addBus_.onClick = [this] { addBus(); };
    stripsHolder_.addAndMakeVisible(addBus_);
    startTimerHz(meterHz);
}
MixerView::~MixerView()
{
    stopTimer();
    closeEditors();
}

bool MixerView::isEditorOpen(core::Id insertId) const
{
    for (const auto& w : pluginWindows_)
        if (w->insertId() == insertId)
            return true;
    for (const auto& w : editors_)
        if (w->editor().insertId() == insertId)
            return true;
    return false;
}

void MixerView::closeEditors()
{
    pluginWindows_.clear();
    editors_.clear();
}

void MixerView::setInputChoices(std::vector<InputChoice> audio, std::vector<std::string> midi)
{
    if (audio == inputChoices_ && midi == midiInputNames_)
        return;
    inputChoices_ = std::move(audio);
    midiInputNames_ = std::move(midi);
    rebuildStrips();
}

void MixerView::setSelectedChannel(core::Id channelId)
{
    if (selected_ == channelId)
        return;
    selected_ = channelId;
    rebuildStrips();
}

void MixerView::setProject(const core::Project* project, bool valuesOnly)
{
    if (valuesOnly && project == project_ && project_ != nullptr)
    {
        for (auto& strip : strips_)
            if (const auto* channel = project_->findChannel(strip->id()))
                strip->setValues(*channel);
        master_->setMaster(project_->master);
        refreshEditors();
        return;
    }
    project_ = project;
    rebuildStrips();
    refreshEditors();
}

void MixerView::refreshEditors()
{
    // Open editors follow the model (undo, a removed insert closes its window).
    // A plugin window must go before its instance, which the engine drops once no
    // snapshot references it (after this call).
    std::erase_if(
        pluginWindows_,
        [this](const auto& w)
        {
            const auto* channel = project_ != nullptr ? project_->findChannel(w->channelId()) : nullptr;
            const auto* insert =
                channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(w->insertId()) : nullptr;
            if (insert == nullptr)
                return true;
            // A restarted plugin is a new instance: the window on the old one closes.
            if (liveInsert)
                if (const auto live = liveInsert(w->insertId());
                    live != nullptr && live.get() != w->instance())
                    return true;
            w->setInsert(*insert);
            return false;
        });
    std::erase_if(editors_,
                  [this](const auto& w)
                  {
                      const auto* channel =
                          project_ != nullptr ? project_->findChannel(w->editor().channelId()) : nullptr;
                      const auto* insert =
                          channel != nullptr
                              ? const_cast<core::Channel*>(channel)->findInsert(w->editor().insertId())
                              : nullptr;
                      if (insert == nullptr)
                          return true;
                      w->editor().setInsert(*insert);
                      if (liveInsert)
                          if (const auto live = liveInsert(w->editor().insertId()))
                              w->editor().setLatency(live->latencySamples());
                      return false;
                  });
}

void MixerView::addBus()
{
    if (!onCommand || project_ == nullptr)
        return;
    // A group bus, or an FX track that channels feed through sends.
    juce::PopupMenu menu;
    menu.addItem(1, "Bus");
    juce::PopupMenu fx;
    const auto choices = plugins::fxTrackEffects();
    for (size_t i = 0; i < choices.size(); ++i)
        fx.addItem(10 + static_cast<int>(i), juce::String(choices[i].name));
    menu.addSubMenu("FX track", fx);
    juce::Component::SafePointer<MixerView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addBus_),
                       [safe, choices](int choice)
                       {
                           if (safe == nullptr || safe->project_ == nullptr || !safe->onCommand)
                               return;
                           if (choice == 1)
                           {
                               core::Bus bus;
                               bus.name = "Bus " + std::to_string(safe->project_->buses.size() + 1);
                               safe->onCommand(std::make_unique<core::AddBus>(bus));
                           }
                           else if (choice >= 10 && static_cast<size_t>(choice - 10) < choices.size())
                           {
                               const auto& c = choices[static_cast<size_t>(choice - 10)];
                               // Only the AddBus: the strip has no sends to make.
                               safe->onCommand(std::make_unique<core::AddBus>(
                                   plugins::makeFxTrack(*safe->project_, c.type, c.name)));
                           }
                       });
}

void MixerView::rebuildStrips()
{
    // A strip per channel that makes audio: MIDI tracks only make notes (their
    // instrument channels have the strips).
    size_t audible = 0;
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
            audible += t.hasAudio() ? 1 : 0;
    const size_t count = project_ != nullptr ? audible + project_->buses.size() : 0;
    while (strips_.size() > count)
        strips_.pop_back();
    while (strips_.size() < count)
    {
        auto strip = std::make_unique<Strip>(*this, theme_);
        stripsHolder_.addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }
    if (project_ != nullptr)
    {
        size_t i = 0;
        for (size_t t = 0; t < project_->tracks.size(); ++t)
        {
            if (!project_->tracks[t].hasAudio())
                continue;
            strips_[i++]->setChannel(project_->tracks[t],
                                     false,
                                     *project_,
                                     engine::trackMeterSlot(static_cast<int>(t)),
                                     static_cast<int>(t));
        }
        for (size_t b = 0; b < project_->buses.size(); ++b, ++i)
            strips_[i]->setChannel(project_->buses[b],
                                   true,
                                   *project_,
                                   engine::busMeterSlot(*project_, static_cast<int>(b)),
                                   static_cast<int>(b));
        master_->setMaster(project_->master);
    }
    layoutStrips();
    repaint();
}
void MixerView::layoutStrips()
{
    // Strips at the strip width with 2 px gaps; the bus group after a lineSubtle
    // gap; the "+ Bus" ghost at the end.
    const int width = theme_.sizes.mixerStripWidth;
    const int gap = theme_.spacing.xxs;
    // When they are wider than the view its horizontal scrollbar shows along the
    // bottom: the strips stop above it, so it never covers the output selectors.
    const bool anyBus =
        std::any_of(strips_.begin(), strips_.end(), [](const auto& strip) { return strip->isBus(); });
    const int contentWidth =
        static_cast<int>(strips_.size()) * (width + gap) + (anyBus ? theme_.spacing.sm : 0) + 56 + gap;
    const int height =
        viewport_.getHeight() - (contentWidth > viewport_.getWidth() ? viewport_.getScrollBarThickness() : 0);
    RowPlan rows;
    for (const auto& strip : strips_)
        strip->addTo(rows);
    const bool rowsChanged = !(rows == rows_);
    rows_ = rows;
    int x = 0;
    bool busGap = false;
    for (auto& strip : strips_)
    {
        if (strip->isBus() && !busGap)
        {
            busGap = true;
            x += theme_.spacing.sm;
        }
        const bool moved = strip->getWidth() != width || strip->getHeight() != height;
        strip->setBounds(x, 0, width, height);
        if (rowsChanged && !moved)
            strip->resized();
        x += width + gap;
    }
    if (!strips_.empty())
        master_->alignTo(strips_.front()->faderBand(), strips_.front()->valueY());
    else
        master_->alignTo({}, 0);
    addBus_.setBounds(x, theme_.spacing.xs, 56, theme_.sizes.controlCompact);
    stripsHolder_.setSize(std::max(1, x + 56 + gap), std::max(1, height));
}
void MixerView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastMeterMs_ > 0.0 ? now - lastMeterMs_ : 1000.0 / meterHz;
    lastMeterMs_ = now;
    for (auto& strip : strips_)
    {
        const auto reading = meters_.take(strip->meterSlot());
        if (strip->meterSlot() >= 0)
        {
            if (static_cast<size_t>(strip->meterSlot()) >= readings_.size())
                readings_.resize(static_cast<size_t>(strip->meterSlot()) + 1);
            readings_[static_cast<size_t>(strip->meterSlot())] = reading;
        }
        strip->pushMeter(reading, elapsed);
    }
    if (project_ != nullptr)
    {
        const auto reading =
            meters_.take(masterMeterSlot_ ? masterMeterSlot_() : engine::masterMeterSlot(*project_));
        if (reading.peakLeft >= 1.0f || reading.peakRight >= 1.0f)
            masterClipped_ = true;
        master_->pushMeter(reading, elapsed);
    }
    if (loudness_)
        master_->pushLoudness(loudness_());
}
void MixerView::paint(juce::Graphics& g)
{
    const FrameStats::Scope timing;
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    // The separator before the master, and the bus group's slate line.
    g.setColour(c.lineSubtle);
    g.fillRect(master_->getX() - theme_.spacing.sm, 0, 1, getHeight());
    bool firstBus = true;
    for (const auto& strip : strips_)
        if (strip->isBus() && firstBus)
        {
            firstBus = false;
            const int x = viewport_.getX() + strip->getX() - viewport_.getViewPositionX() - theme_.spacing.xs;
            if (x > viewport_.getX() && x < viewport_.getRight())
                g.fillRect(x, 0, 1, viewport_.getHeight());
        }
    if (strips_.empty())
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.body());
        g.drawText("Add a track to start mixing", viewport_.getBounds(), juce::Justification::centred);
    }
}
void MixerView::resized()
{
    auto area = getLocalBounds();
    master_->setBounds(area.removeFromRight(theme_.sizes.mixerStripWideWidth));
    area.removeFromRight(theme_.spacing.lg);
    viewport_.setBounds(area);
    layoutStrips();
}
} // namespace northlight::ui
