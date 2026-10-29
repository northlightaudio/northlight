#include "ui/AutomationLanes.h"

#include "core/Command.h"
#include "plugins/BuiltIns.h"
#include "ui/Db.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"

#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int pointRadius = 4;
// Menu ids past the parameters' (which count from 1).
constexpr int deletePointId = 9001, collapseId = 9002, hideId = 9003, clearId = 9004, deleteLaneId = 9005;

bool isHosted(const std::string& type)
{
    return type.rfind("vst3:", 0) == 0 || type.rfind("clap:", 0) == 0;
}

const char* modeName(core::AutomationMode mode)
{
    switch (mode)
    {
        case core::AutomationMode::off:
            return "Off";
        case core::AutomationMode::read:
            return "Read";
        case core::AutomationMode::touch:
            return "Touch";
        case core::AutomationMode::latch:
            return "Latch";
        case core::AutomationMode::write:
            return "Write";
    }
    return "Read";
}

juce::String ellipsis()
{
    return juce::String::fromUTF8(" \xe2\x80\xa6 ");
}

} // namespace

AutomationLanes::AutomationLanes(const Theme& theme, const TimelineViewport& view)
    : theme_(theme), view_(view)
{
    setInterceptsMouseClicks(true, false);
    setRepaintsOnMouseActivity(false);
}

void AutomationLanes::setProject(const core::Project* project)
{
    project_ = project;
    // A selection whose lane went, or whose points no longer exist, is dropped.
    const auto* channel = project_ != nullptr ? project_->findChannel(selChannel_) : nullptr;
    const auto* lane = channel != nullptr ? channel->findLane(selLane_) : nullptr;
    if (lane == nullptr)
    {
        selChannel_ = selLane_ = core::invalidId;
        selected_.clear();
        rangeStart_ = rangeEnd_ = 0;
    }
    else
        std::erase_if(selected_,
                      [lane](int i) { return i < 0 || static_cast<size_t>(i) >= lane->points.size(); });
    repaint();
}

// ----- layout -----------------------------------------------------------------------------

int AutomationLanes::laneHeight(const core::AutomationLane& lane) const noexcept
{
    if (lane.view.collapsed)
        return theme_.sizes.automationLaneCollapsed;
    if (lane.id == resizeLane_)
        return resizeHeight_;
    if (lane.view.height > 0)
        return std::clamp(lane.view.height, theme_.sizes.trackRowMin, theme_.sizes.trackRowMax);
    return theme_.sizes.automationLane;
}

int AutomationLanes::blockHeight(const core::Channel& channel) const noexcept
{
    if (!channel.automationOpen)
        return 0;
    int h = theme_.sizes.automationLaneCollapsed; // the footer
    for (const auto& lane : channel.automation)
        if (!lane.view.hidden)
            h += laneHeight(lane);
    return h;
}

std::vector<AutomationLanes::Slot> AutomationLanes::slotsOf(int row) const
{
    std::vector<Slot> slots;
    const auto* channel = geometry.channelAt ? geometry.channelAt(row) : nullptr;
    if (channel == nullptr || !channel->automationOpen)
        return slots;
    const auto block = geometry.block(row);
    if (block.isEmpty())
        return slots;
    int y = block.getY();
    for (size_t i = 0; i < channel->automation.size(); ++i)
    {
        const auto& lane = channel->automation[i];
        if (lane.view.hidden)
            continue;
        const int h = laneHeight(lane);
        slots.push_back({row, static_cast<int>(i), {block.getX(), y, block.getWidth(), h}});
        y += h;
    }
    slots.push_back({row, -1, {block.getX(), y, block.getWidth(), theme_.sizes.automationLaneCollapsed}});
    return slots;
}

std::optional<AutomationLanes::Slot> AutomationLanes::slotAt(juce::Point<int> p) const
{
    if (project_ == nullptr || !geometry.rowCount)
        return std::nullopt;
    for (int row = 0; row < geometry.rowCount(); ++row)
    {
        const auto block = geometry.block(row);
        if (block.isEmpty() || !block.getVerticalRange().contains(p.y))
            continue;
        for (const auto& slot : slotsOf(row))
            if (slot.bounds.contains(p))
                return slot;
    }
    return std::nullopt;
}

const core::Channel* AutomationLanes::channelOf(const Slot& slot) const
{
    return geometry.channelAt ? geometry.channelAt(slot.row) : nullptr;
}

const core::AutomationLane* AutomationLanes::laneOf(const Slot& slot) const
{
    const auto* channel = channelOf(slot);
    if (channel == nullptr || slot.lane < 0 || static_cast<size_t>(slot.lane) >= channel->automation.size())
        return nullptr;
    return &channel->automation[static_cast<size_t>(slot.lane)];
}

AutomationLanes::Key AutomationLanes::keyOf(const core::AutomationLane& lane) noexcept
{
    return {lane.target, lane.insert, lane.parameter};
}

juce::Rectangle<int> AutomationLanes::curveArea(const Slot& slot) const
{
    const auto lanes = geometry.lanesArea();
    return slot.bounds.withLeft(lanes.getX()).withRight(lanes.getRight()).reduced(0, theme_.spacing.xs);
}

juce::Rectangle<int> AutomationLanes::headerOf(const Slot& slot) const
{
    return slot.bounds.withWidth(view_.originX);
}

// The header's top line: a control's height, or the whole of a collapsed lane.
static juce::Rectangle<int> topLine(juce::Rectangle<int> header, int control)
{
    return header.withHeight(std::min(header.getHeight(), control));
}

juce::Rectangle<int> AutomationLanes::chevronOf(const Slot& slot) const
{
    const auto line = topLine(headerOf(slot), theme_.sizes.controlCompact);
    const int left = theme_.track.headerStripeWidth + theme_.spacing.md;
    const int size = theme_.sizes.iconSmall + theme_.spacing.xxs;
    return juce::Rectangle<int>(line.getX() + left, line.getY(), size, line.getHeight())
        .withSizeKeepingCentre(size, std::min(size, line.getHeight()));
}

juce::Rectangle<int> AutomationLanes::eyeOf(const Slot& slot) const
{
    const auto line = topLine(headerOf(slot), theme_.sizes.controlCompact);
    const int size = theme_.sizes.iconSmall + theme_.spacing.xs;
    return juce::Rectangle<int>(
               line.getRight() - theme_.spacing.sm - size, line.getY(), size, line.getHeight())
        .withSizeKeepingCentre(size, std::min(size, line.getHeight()));
}

juce::Rectangle<int> AutomationLanes::valueOf(const Slot& slot) const
{
    const auto line = topLine(headerOf(slot), theme_.sizes.controlCompact);
    const auto eye = eyeOf(slot);
    const int w = 72;
    return {eye.getX() - theme_.spacing.xs - w, line.getY(), w, line.getHeight()};
}

juce::Rectangle<int> AutomationLanes::nameOf(const Slot& slot) const
{
    const auto line = topLine(headerOf(slot), theme_.sizes.controlCompact);
    const int left = chevronOf(slot).getRight() + theme_.spacing.xs;
    return {
        left, line.getY(), std::max(0, valueOf(slot).getX() - theme_.spacing.sm - left), line.getHeight()};
}

juce::Rectangle<int> AutomationLanes::addOf(const Slot& slot) const
{
    const auto header = headerOf(slot);
    const int left = chevronOf(slot).getX();
    const int w =
        juce::GlyphArrangement::getStringWidthInt(theme_.caption(), "+ Add lane") + theme_.spacing.sm;
    return {left - theme_.spacing.xs, header.getY(), w, header.getHeight()};
}

juce::Rectangle<int> AutomationLanes::hiddenOf(const Slot& slot) const
{
    const auto header = headerOf(slot);
    const int w = 84;
    return {header.getRight() - theme_.spacing.sm - w, header.getY(), w, header.getHeight()};
}

bool AutomationLanes::onResizeEdge(const Slot& slot, juce::Point<int> p) const
{
    const auto* lane = laneOf(slot);
    return lane != nullptr && !lane->view.collapsed && p.y >= slot.bounds.getBottom() - 4 &&
           p.y < slot.bounds.getBottom();
}

AutomationLanes::Part AutomationLanes::partAt(const Slot& slot, juce::Point<int> p) const
{
    if (p.x >= view_.originX)
        return Part::none;
    if (slot.lane < 0)
    {
        if (addOf(slot).contains(p))
            return Part::add;
        const auto* channel = channelOf(slot);
        const bool anyHidden =
            channel != nullptr && std::any_of(channel->automation.begin(),
                                              channel->automation.end(),
                                              [](const core::AutomationLane& l) { return l.view.hidden; });
        return anyHidden && hiddenOf(slot).contains(p) ? Part::hidden : Part::none;
    }
    if (chevronOf(slot).contains(p))
        return Part::chevron;
    if (eyeOf(slot).contains(p))
        return Part::eye;
    if (nameOf(slot).contains(p))
        return Part::name;
    return Part::none;
}

// ----- values -----------------------------------------------------------------------------

const std::vector<engine::ParameterInfo>* AutomationLanes::parametersOf(const core::Insert& insert) const
{
    if (isHosted(insert.type))
        return hostedParameters ? hostedParameters(insert.id) : nullptr;
    return &plugins::parameterInfo(insert.type);
}

const engine::ParameterInfo* AutomationLanes::infoFor(const core::Channel& track, const Key& key) const
{
    const auto* insert = track.findInsert(key.insert);
    if (insert == nullptr)
        return nullptr;
    const auto* infos = parametersOf(*insert);
    return infos != nullptr && key.parameter >= 0 && static_cast<size_t>(key.parameter) < infos->size()
               ? &(*infos)[static_cast<size_t>(key.parameter)]
               : nullptr;
}

float AutomationLanes::toNorm(const core::Channel& track, const Key& key, float value) const
{
    return scaleFor(track, key).toNorm(value);
}

float AutomationLanes::fromNorm(const core::Channel& track, const Key& key, float norm) const
{
    return scaleFor(track, key).fromNorm(norm);
}

AutomationScale AutomationLanes::scaleFor(const core::Channel& track, const Key& key) const
{
    return {key.target,
            key.target == core::AutomationTarget::insertParameter ? infoFor(track, key) : nullptr};
}

float AutomationLanes::staticValue(const core::Channel& track, const Key& key) const
{
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            return track.gain;
        case core::AutomationTarget::pan:
            return track.pan;
        case core::AutomationTarget::insertParameter:
            if (const auto* insert = track.findInsert(key.insert);
                insert != nullptr && key.parameter >= 0 &&
                static_cast<size_t>(key.parameter) < insert->params.size())
                return insert->params[static_cast<size_t>(key.parameter)];
            if (const auto* info = infoFor(track, key))
                return info->defaultValue;
            return 0.0f;
    }
    return 0.0f;
}

juce::String AutomationLanes::keyName(const core::Channel& track, const Key& key) const
{
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            return "Volume";
        case core::AutomationTarget::pan:
            return "Pan";
        case core::AutomationTarget::insertParameter:
        {
            const auto* insert = track.findInsert(key.insert);
            const auto* info = infoFor(track, key);
            if (insert == nullptr || info == nullptr)
                return "Missing parameter";
            const auto module = displayName ? displayName(insert->type) : insert->type;
            return juce::String(module) + ": " + juce::String(info->name);
        }
    }
    return {};
}

juce::String AutomationLanes::rangeText(const core::Channel& track, const Key& key) const
{
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            return juce::String::fromUTF8("\xe2\x88\x92") + "60" + ellipsis() + "+6 dB";
        case core::AutomationTarget::pan:
            return "L100" + ellipsis() + "R100";
        case core::AutomationTarget::insertParameter:
            if (const auto* info = infoFor(track, key))
                return valueText(track, key, info->minimum) + ellipsis() +
                       valueText(track, key, info->maximum);
            return {};
    }
    return {};
}

juce::String AutomationLanes::valueText(const core::Channel& track, const Key& key, float value) const
{
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            return db::gainText(value) + " dB";
        case core::AutomationTarget::pan:
        {
            const int amount = static_cast<int>(std::lround(std::abs(value) * 100.0f));
            return amount == 0 ? juce::String("C") : (value < 0.0f ? "L" : "R") + juce::String(amount);
        }
        case core::AutomationTarget::insertParameter:
            if (const auto* info = infoFor(track, key))
            {
                if (!info->choices.empty())
                {
                    const auto i = static_cast<size_t>(juce::jlimit(
                        0, static_cast<int>(info->choices.size()) - 1, static_cast<int>(std::lround(value))));
                    return juce::String(info->choices[i]);
                }
                return fixedText(value, std::abs(value) >= 100.0f ? 0 : 2) +
                       (info->unit.empty() ? juce::String() : " " + juce::String(info->unit));
            }
            return juce::String(value, 2);
    }
    return {};
}

juce::String AutomationLanes::valueAtPlayhead(const core::Channel& track,
                                              const core::AutomationLane& lane) const
{
    const auto key = keyOf(lane);
    const auto at = geometry.playhead ? geometry.playhead() : 0;
    return valueText(track, key, lane.points.empty() ? staticValue(track, key) : lane.valueAt(at));
}

float AutomationLanes::pointX(const Slot& slot, core::SamplePosition position) const
{
    // A point at the lane's left edge (the song's start) is drawn whole.
    const auto area = curveArea(slot);
    return std::max(static_cast<float>(view_.xOf(position)),
                    static_cast<float>(area.getX() + pointRadius + 1));
}

int AutomationLanes::pointAt(const Slot& slot, juce::Point<int> p) const
{
    const auto* lane = laneOf(slot);
    const auto* track = channelOf(slot);
    if (lane == nullptr || track == nullptr || lane->view.collapsed)
        return -1;
    const auto key = keyOf(*lane);
    const auto area = curveArea(slot);
    int best = -1;
    int bestDistance = pointRadius + 4;
    for (size_t i = 0; i < lane->points.size(); ++i)
    {
        const auto& pt = lane->points[i];
        const int x = juce::roundToInt(pointX(slot, pt.position));
        const int y = static_cast<int>(
            AutomationScale::yFor(toNorm(*track, key, pt.value), area.getY(), area.getHeight()));
        const int d = std::max(std::abs(x - p.x), std::abs(y - p.y));
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// ----- painting ---------------------------------------------------------------------------

void AutomationLanes::paintBackgrounds(juce::Graphics& g) const
{
    if (project_ == nullptr || !geometry.rowCount)
        return;
    const auto& c = theme_.colours;
    const auto lanes = geometry.lanesArea();
    const auto clip = g.getClipBounds();
    for (int row = 0; row < geometry.rowCount(); ++row)
    {
        const auto block = geometry.block(row);
        if (block.isEmpty() || !block.intersects(clip))
            continue;
        for (const auto& slot : slotsOf(row))
        {
            const auto r = slot.bounds.withLeft(lanes.getX()).withRight(lanes.getRight());
            g.setColour(c.surface0);
            g.fillRect(r);
            g.setColour(c.lineSubtle);
            g.fillRect(r.withTop(r.getBottom() - 1));
        }
    }
}

bool AutomationLanes::hitTest(int x, int y)
{
    if (project_ == nullptr || !geometry.lanesArea)
        return false;
    const auto lanes = geometry.lanesArea();
    if (y < lanes.getY() || y >= lanes.getBottom() || x >= lanes.getRight())
        return false;
    const auto slot = slotAt({x, y});
    if (!slot.has_value())
        return false;
    // The top pixels of a block belong to the row above it, whose bottom edge resizes it.
    const auto slots = slotsOf(slot->row);
    return !(slots.front().bounds == slot->bounds && y < slot->bounds.getY() + 2);
}

void AutomationLanes::paint(juce::Graphics& g)
{
    if (project_ == nullptr || !geometry.rowCount)
        return;
    const auto lanes = geometry.lanesArea();
    const auto clip = g.getClipBounds();
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(0, lanes.getY(), lanes.getRight(), lanes.getHeight());
    for (int row = 0; row < geometry.rowCount(); ++row)
    {
        const auto block = geometry.block(row);
        if (block.isEmpty() || !block.intersects(clip))
            continue;
        for (const auto& slot : slotsOf(row))
        {
            if (!slot.bounds.intersects(clip))
                continue;
            if (slot.lane < 0)
                paintFooter(g, slot);
            else
            {
                paintHeader(g, slot);
                juce::Graphics::ScopedSaveState inLanes(g);
                g.reduceClipRegion(lanes);
                paintLane(g, slot);
            }
        }
    }
}

void AutomationLanes::paintHeader(juce::Graphics& g, const Slot& slot) const
{
    const auto& c = theme_.colours;
    const auto* track = channelOf(slot);
    const auto* lane = laneOf(slot);
    if (track == nullptr || lane == nullptr)
        return;
    const auto header = headerOf(slot);
    g.setColour(c.surface0);
    g.fillRect(header);
    g.setColour((geometry.colour ? geometry.colour(slot.row) : c.textTertiary).withAlpha(0.38f));
    g.fillRect(header.withWidth(theme_.track.headerStripeWidth));
    g.setColour(c.lineSubtle);
    g.fillRect(header.withTop(header.getBottom() - 1));

    const bool collapsed = lane->view.collapsed;
    const auto key = keyOf(*lane);
    // Hovered controls get a surface behind them, as icon buttons do.
    const auto control = [&](juce::Rectangle<int> r, Part part, Icon icon)
    {
        const bool hot = hovered(slot, part);
        if (hot)
        {
            g.setColour(c.surface2);
            g.fillRoundedRectangle(r.toFloat(), theme_.radius.sm);
        }
        const float size = static_cast<float>(std::min(theme_.sizes.iconSmall, r.getHeight()));
        Icons::draw(
            g, icon, r.toFloat().withSizeKeepingCentre(size, size), hot ? c.textPrimary : c.textTertiary);
    };
    control(chevronOf(slot), Part::chevron, collapsed ? Icon::chevronRight : Icon::chevronDown);
    control(eyeOf(slot), Part::eye, Icon::visible);

    // The parameter's name, which opens its menu, with a small chevron after it.
    const auto name = nameOf(slot);
    const auto font = collapsed ? theme_.caption() : theme_.small();
    const auto text = keyName(*track, key);
    const bool nameHot = hovered(slot, Part::name);
    g.setFont(font);
    g.setColour(nameHot ? c.textPrimary : (collapsed ? c.textTertiary : c.textSecondary));
    const int chevron = theme_.sizes.iconSmall - theme_.spacing.xs;
    const int textW = std::min(name.getWidth() - chevron - theme_.spacing.xxs,
                               juce::GlyphArrangement::getStringWidthInt(font, text));
    g.drawText(text, name.withWidth(std::max(0, textW)), juce::Justification::centredLeft, true);
    if (!collapsed)
        Icons::draw(g,
                    Icon::chevronDown,
                    juce::Rectangle<int>(
                        name.getX() + textW + theme_.spacing.xxs, name.getY(), chevron, name.getHeight())
                        .toFloat()
                        .withSizeKeepingCentre(static_cast<float>(chevron), static_cast<float>(chevron)),
                    nameHot ? c.textPrimary : c.textTertiary);

    // The value at the playhead.
    const auto value = valueAtPlayhead(*track, *lane);
    g.setFont(collapsed ? theme_.caption() : theme_.value());
    g.setColour(collapsed ? c.textTertiary : c.textSecondary);
    g.drawText(value, valueOf(slot), juce::Justification::centredRight, true);

    // The range, where there is room.
    if (!collapsed && header.getHeight() >= 44)
    {
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        g.drawText(rangeText(*track, key),
                   juce::Rectangle<int>(name.getX(),
                                        name.getBottom(),
                                        header.getRight() - theme_.spacing.sm - name.getX(),
                                        theme_.lineHeight(theme_.fonts.size.caption)),
                   juce::Justification::centredLeft,
                   true);
    }
}

void AutomationLanes::paintFooter(juce::Graphics& g, const Slot& slot) const
{
    const auto& c = theme_.colours;
    const auto* track = channelOf(slot);
    if (track == nullptr)
        return;
    const auto header = headerOf(slot);
    g.setColour(c.surface0);
    g.fillRect(header);
    g.setColour((geometry.colour ? geometry.colour(slot.row) : c.textTertiary).withAlpha(0.38f));
    g.fillRect(header.withWidth(theme_.track.headerStripeWidth));
    g.setColour(c.lineSubtle);
    g.fillRect(header.withTop(header.getBottom() - 1));

    g.setFont(theme_.caption());
    const auto button =
        [&](juce::Rectangle<int> r, Part part, const juce::String& text, juce::Justification just)
    {
        const bool hot = hovered(slot, part);
        if (hot)
        {
            const int w =
                juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + theme_.spacing.sm;
            auto pill = r.reduced(0, theme_.spacing.xxs);
            pill =
                just == juce::Justification::centredRight ? pill.removeFromRight(w) : pill.removeFromLeft(w);
            g.setColour(c.surface2);
            g.fillRoundedRectangle(pill.toFloat(), theme_.radius.sm);
        }
        g.setColour(hot ? c.textPrimary : (part == Part::add ? c.textTertiary : c.textSecondary));
        g.drawText(text, r.reduced(theme_.spacing.xs, 0), just, true);
    };
    button(addOf(slot), Part::add, "+ Add lane", juce::Justification::centredLeft);
    int hidden = 0;
    for (const auto& lane : track->automation)
        hidden += lane.view.hidden ? 1 : 0;
    if (hidden > 0)
        button(hiddenOf(slot),
               Part::hidden,
               juce::String(hidden) + " hidden" + juce::String::fromUTF8(" \xe2\x96\xbe"),
               juce::Justification::centredRight);
}

void AutomationLanes::paintLane(juce::Graphics& g, const Slot& slot) const
{
    const auto& c = theme_.colours;
    const auto* track = channelOf(slot);
    const auto* lane = laneOf(slot);
    if (track == nullptr || lane == nullptr)
        return;
    const auto key = keyOf(*lane);
    const auto area = curveArea(slot);
    const bool collapsed = lane->view.collapsed;
    const bool previewing = previewing_ && dragLane_ == lane->id && dragChannel_ == track->id;
    const bool dragging = previewing && mode_ == Mode::point && dragIndex_ >= 0;
    const auto& points = previewing ? dragPoints_ : lane->points;
    // The range tool's stretch of time in this lane, under the curve.
    if (selLane_ == lane->id && hasRange() && !collapsed)
    {
        const int x0 = view_.xOf(rangeStart_), x1 = view_.xOf(rangeEnd_);
        const auto band = juce::Rectangle<int>(x0, area.getY(), std::max(1, x1 - x0), area.getHeight());
        g.setColour(c.selectionFill);
        g.fillRect(band);
        g.setColour(c.selectionOutline);
        g.fillRect(band.withWidth(1));
        g.fillRect(band.withLeft(band.getRight() - 1));
    }
    const auto yOf = [&](float value)
    { return AutomationScale::yFor(toNorm(*track, key, value), area.getY(), area.getHeight()); };
    const bool active = track->automationMode != core::AutomationMode::off;
    // The track's own colour, so a lane reads as its track's; grey when the mode is Off.
    const auto colour = !active ? c.textTertiary : geometry.colour ? geometry.colour(slot.row) : c.automation;
    const float dashes[] = {4.0f, 4.0f};

    // The reference: unity gain or the centre.
    if (!collapsed &&
        (key.target == core::AutomationTarget::gain || key.target == core::AutomationTarget::pan))
    {
        const float y = yOf(key.target == core::AutomationTarget::gain ? 1.0f : 0.0f);
        g.setColour(c.lineStrong);
        g.drawDashedLine(
            juce::Line<float>(static_cast<float>(area.getX()), y, static_cast<float>(area.getRight()), y),
            dashes,
            2,
            1.0f);
    }

    if (points.empty())
    {
        // No points yet: the static value as a dashed line.
        const float y = yOf(staticValue(*track, key));
        g.setColour(colour.withAlpha(0.6f));
        g.drawDashedLine(
            juce::Line<float>(static_cast<float>(area.getX()), y, static_cast<float>(area.getRight()), y),
            dashes,
            2,
            1.0f);
        if (!collapsed && area.getHeight() >= 24)
        {
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary);
            g.drawText("Click to add a point",
                       area.withTrimmedLeft(theme_.spacing.sm)
                           .withHeight(theme_.lineHeight(theme_.fonts.size.caption) + theme_.spacing.xs),
                       juce::Justification::centredLeft);
        }
        return;
    }

    juce::Path curve;
    for (int x = area.getX(); x <= area.getRight(); x += 2)
    {
        const float v = core::automationValueAt(points.data(), points.size(), key.target, view_.sampleAt(x));
        const float y = yOf(v);
        if (x == area.getX())
            curve.startNewSubPath(static_cast<float>(x), y);
        else
            curve.lineTo(static_cast<float>(x), y);
    }
    if (collapsed)
    {
        g.setColour(colour.withAlpha(0.75f));
        g.strokePath(curve, juce::PathStrokeType(1.2f));
        return;
    }
    juce::Path fill(curve);
    fill.lineTo(static_cast<float>(area.getRight()), static_cast<float>(area.getBottom()));
    fill.lineTo(static_cast<float>(area.getX()), static_cast<float>(area.getBottom()));
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(1.5f));
    for (size_t i = 0; i < points.size(); ++i)
    {
        const float x = pointX(slot, points[i].position);
        if (x < area.getX() - 8 || x > area.getRight() + 8)
            continue;
        const bool hot = (dragging && static_cast<int>(i) == dragIndex_) ||
                         (!dragging && hoverLane_ == lane->id && hoverPoint_ == static_cast<int>(i));
        // Selected: ringed in selectionOutline (while a selection moves, the moving ones).
        const bool chosen = previewing && !previewMoved_.empty()
                                ? i < previewMoved_.size() && previewMoved_[i] != 0
                                : !previewing && isSelected(lane->id, static_cast<int>(i));
        const float r = hot || chosen ? pointRadius + 1.5f : static_cast<float>(pointRadius);
        const auto at = juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre({x, yOf(points[i].value)});
        g.setColour(c.canvas);
        g.fillEllipse(at.expanded(1.0f));
        g.setColour(colour);
        g.fillEllipse(at);
        if (chosen)
        {
            g.setColour(c.selectionOutline);
            g.drawEllipse(at.expanded(1.5f), 1.5f);
        }
        else if (hot)
        {
            g.setColour(c.textPrimary);
            g.drawEllipse(at.expanded(1.5f), 1.0f);
        }
    }
    // The marquee being drawn in this lane.
    if (mode_ == Mode::marquee && dragLane_ == lane->id && !marquee_.isEmpty())
    {
        g.setColour(c.selectionFill);
        g.fillRect(marquee_);
        g.setColour(c.selectionOutline);
        g.drawRect(marquee_, 1);
    }
    if (dragging)
    {
        const auto& p = points[static_cast<size_t>(dragIndex_)];
        const auto text = valueText(*track, key, p.value);
        g.setFont(theme_.value());
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 10;
        const auto at =
            juce::Point<int>(juce::roundToInt(pointX(slot, p.position)), static_cast<int>(yOf(p.value)));
        // Beside the point, on the side with room.
        auto box = juce::Rectangle<int>(w, 16).withCentre(at);
        box.setX(at.x + w + 16 < area.getRight() ? at.x + 10 : at.x - 10 - w);
        box = box.constrainedWithin(slot.bounds.withLeft(area.getX()));
        g.setColour(c.surface3);
        g.fillRoundedRectangle(box.toFloat(), theme_.radius.sm);
        g.setColour(c.textPrimary);
        g.drawText(text, box, juce::Justification::centred);
    }
}

// ----- the mouse --------------------------------------------------------------------------

void AutomationLanes::mouseMove(const juce::MouseEvent& e)
{
    const auto slot = slotAt(e.getPosition());
    int point = -1;
    core::Id lane = core::invalidId;
    Part part = Part::none;
    juce::MouseCursor pointer = juce::MouseCursor::NormalCursor;
    if (slot.has_value())
    {
        part = partAt(*slot, e.getPosition());
        const auto* l = laneOf(*slot);
        if (onResizeEdge(*slot, e.getPosition()))
            pointer = juce::MouseCursor::UpDownResizeCursor;
        else if (part != Part::none)
            pointer = juce::MouseCursor::PointingHandCursor;
        else if (l != nullptr && e.x >= view_.originX)
        {
            lane = l->id;
            point = pointAt(*slot, e.getPosition());
            const int tool = geometry.tool ? geometry.tool() : 0;
            if (l->view.collapsed)
                pointer = juce::MouseCursor::PointingHandCursor;
            else if (tool == 1)
                pointer = juce::MouseCursor::IBeamCursor;
            else if (tool == 2)
                pointer = Icons::toolCursor(theme_, Icon::pencil);
            else if (tool == 3)
                pointer = Icons::toolCursor(theme_, Icon::eraser);
            else
                pointer =
                    point >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor;
        }
    }
    setMouseCursor(pointer);
    const int row = slot.has_value() ? slot->row : -1;
    const int slotLane = slot.has_value() ? slot->lane : -1;
    if (part != hoverPart_ || row != hoverRow_ || slotLane != hoverSlotLane_)
    {
        if (hoverPart_ != Part::none || part != Part::none)
            repaint(0, 0, view_.originX, getHeight());
        hoverPart_ = part;
        hoverRow_ = row;
        hoverSlotLane_ = slotLane;
    }
    if (lane != hoverLane_ || point != hoverPoint_)
    {
        hoverLane_ = lane;
        hoverPoint_ = point;
        repaint();
    }
}

void AutomationLanes::mouseExit(const juce::MouseEvent&)
{
    if (hoverPart_ != Part::none || hoverPoint_ >= 0)
        repaint();
    hoverPart_ = Part::none;
    hoverRow_ = hoverSlotLane_ = -1;
    hoverLane_ = core::invalidId;
    hoverPoint_ = -1;
}

void AutomationLanes::mouseDown(const juce::MouseEvent& e)
{
    const auto slot = slotAt(e.getPosition());
    if (!slot.has_value() || project_ == nullptr)
        return;
    const auto* track = channelOf(*slot);
    const auto* lane = laneOf(*slot);
    if (track == nullptr)
        return;
    const auto toScreen = [this](juce::Rectangle<int> r) { return localAreaToGlobal(r); };

    if (lane != nullptr && onResizeEdge(*slot, e.getPosition()) && !e.mods.isPopupMenu())
    {
        resizeLane_ = lane->id;
        resizeChannel_ = track->id;
        resizeStartHeight_ = resizeHeight_ = slot->bounds.getHeight();
        return;
    }

    if (e.x < view_.originX)
    {
        const auto part = partAt(*slot, e.getPosition());
        if (slot->lane < 0)
        {
            if (part == Part::add)
                showAddMenu(track->id, toScreen(addOf(*slot)));
            else if (part == Part::hidden)
                showHiddenMenu(track->id, toScreen(hiddenOf(*slot)));
            return;
        }
        if (lane == nullptr)
            return;
        if (e.mods.isPopupMenu())
        {
            showLaneMenu(*slot, juce::Rectangle<int>(e.x, e.y, 1, 1), -1);
            return;
        }
        auto view = lane->view;
        if (part == Part::chevron)
        {
            view.collapsed = !view.collapsed;
            setView(track->id,
                    lane->id,
                    view,
                    view.collapsed ? "Collapse automation lane" : "Expand automation lane");
        }
        else if (part == Part::eye)
        {
            view.hidden = true;
            setView(track->id, lane->id, view, "Hide automation lane");
        }
        else if (part == Part::name)
            showLaneMenu(*slot, nameOf(*slot), -1);
        return;
    }

    if (lane == nullptr)
        return;
    const int index = pointAt(*slot, e.getPosition());
    if (e.mods.isPopupMenu())
    {
        showLaneMenu(*slot, juce::Rectangle<int>(e.x, e.y, 1, 1), index);
        return;
    }
    if (lane->view.collapsed)
    {
        // A collapsed lane is read-only: a click opens it out.
        auto view = lane->view;
        view.collapsed = false;
        setView(track->id, lane->id, view, "Expand automation lane");
        return;
    }
    const auto key = keyOf(*lane);
    const auto area = curveArea(*slot);
    activeChannel_ = track->id;
    activeLane_ = lane->id;
    dragChannel_ = track->id;
    dragLane_ = lane->id;
    dragOrigin_ = lane->points;
    dragPoints_ = lane->points;
    pressAt_ = e.getPosition();
    previewing_ = false;
    const int tool = geometry.tool ? geometry.tool() : 0;
    const auto at = std::max<core::SamplePosition>(0, view_.sampleAt(e.x));
    if (tool == 1)
    {
        // The range tool: a stretch of time in this lane.
        mode_ = Mode::range;
        select(track->id, lane->id, {});
        rangeStart_ = rangeEnd_ = geometry.snap(at);
        repaint();
        return;
    }
    if (tool == 2)
    {
        // The pencil: freehand from here.
        mode_ = Mode::draw;
        drawn_.clear();
        previewDraw(e, *slot, *track, key);
        return;
    }
    if (tool == 3)
    {
        // The eraser: a click removes the point, a drag every point it passes.
        mode_ = Mode::erase;
        erased_.clear();
        if (index >= 0)
            erased_.push_back(index);
        previewing_ = true;
        dragPoints_.clear();
        for (int i = 0; i < static_cast<int>(dragOrigin_.size()); ++i)
            if (std::find(erased_.begin(), erased_.end(), i) == erased_.end())
                dragPoints_.push_back(dragOrigin_[static_cast<size_t>(i)]);
        repaint();
        return;
    }
    const bool sameLane = selChannel_ == track->id && selLane_ == lane->id && !hasRange();
    if (index >= 0)
    {
        auto points = sameLane ? selected_ : std::vector<int>{};
        const bool in = std::find(points.begin(), points.end(), index) != points.end();
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            // Ctrl+click: in or out of the selection, no drag.
            if (in)
                std::erase(points, index);
            else
                points.push_back(index);
            select(track->id, lane->id, std::move(points));
            mode_ = Mode::none;
            repaint();
            return;
        }
        if (e.mods.isShiftDown() && !in)
            points.push_back(index);
        else if (!in)
            points = {index};
        select(track->id, lane->id, points);
        if (points.size() > 1)
        {
            // A point of a larger selection: they move together.
            mode_ = Mode::selection;
            grabbed_ = points;
            grabIndex_ = index;
            repaint();
            return;
        }
        mode_ = Mode::point;
        dragIndex_ = index;
        dragNew_ = false;
        dragPosition_ = dragPoints_[static_cast<size_t>(index)].position;
        previewing_ = true;
        repaint();
        return;
    }
    if (e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        // A marquee over empty space adds what it takes in to the selection.
        mode_ = Mode::marquee;
        grabbed_ = sameLane ? selected_ : std::vector<int>{};
        marquee_ = {e.x, e.y, 0, 0};
        select(track->id, lane->id, grabbed_);
        repaint();
        return;
    }
    // A new point where clicked (dragged into place before the release).
    core::AutomationPoint p;
    p.position = std::max<core::SamplePosition>(0, geometry.snap(at));
    p.value = fromNorm(
        *track, key, AutomationScale::normAt(static_cast<float>(e.y), area.getY(), area.getHeight()));
    auto it = std::upper_bound(dragPoints_.begin(),
                               dragPoints_.end(),
                               p.position,
                               [](core::SamplePosition s, const core::AutomationPoint& q)
                               { return s < q.position; });
    dragIndex_ = static_cast<int>(it - dragPoints_.begin());
    dragPoints_.insert(it, p);
    dragNew_ = true;
    dragPosition_ = p.position;
    mode_ = Mode::point;
    previewing_ = true;
    select(track->id, lane->id, {});
    repaint();
}

std::optional<AutomationLanes::Slot> AutomationLanes::slotOf(core::Id channel, core::Id laneId) const
{
    if (!geometry.rowCount)
        return std::nullopt;
    for (int row = 0; row < geometry.rowCount(); ++row)
        if (const auto* c = geometry.channelAt(row); c != nullptr && c->id == channel)
            for (const auto& s : slotsOf(row))
                if (const auto* l = laneOf(s); l != nullptr && l->id == laneId)
                    return s;
    return std::nullopt;
}

juce::Point<float> AutomationLanes::pointPosition(const Slot& slot,
                                                  const core::Channel& track,
                                                  const Key& key,
                                                  const core::AutomationPoint& point) const
{
    const auto area = curveArea(slot);
    return {pointX(slot, point.position),
            AutomationScale::yFor(toNorm(track, key, point.value), area.getY(), area.getHeight())};
}

void AutomationLanes::previewMove(const juce::MouseEvent& e,
                                  const Slot& slot,
                                  const core::Channel& track,
                                  const Key& key)
{
    // The selected points move by the pressed one's change in time (snapped;
    // Shift keeps their times) and in value; they stay between the points either
    // side of the selection.
    if (grabIndex_ < 0 || static_cast<size_t>(grabIndex_) >= dragOrigin_.size())
        return;
    const auto area = curveArea(slot);
    const auto& grabbed = dragOrigin_[static_cast<size_t>(grabIndex_)];
    core::SamplePosition delta = 0;
    if (!e.mods.isShiftDown())
        delta = geometry.snap(std::max<core::SamplePosition>(0, view_.sampleAt(e.x))) - grabbed.position;
    core::SamplePosition first = std::numeric_limits<core::SamplePosition>::max(), last = 0;
    for (const int i : grabbed_)
    {
        first = std::min(first, dragOrigin_[static_cast<size_t>(i)].position);
        last = std::max(last, dragOrigin_[static_cast<size_t>(i)].position);
    }
    const auto isGrabbed = [&](int i)
    { return std::find(grabbed_.begin(), grabbed_.end(), i) != grabbed_.end(); };
    core::SamplePosition lowest = -first, highest = std::numeric_limits<core::SamplePosition>::max();
    for (int i = 0; i < static_cast<int>(dragOrigin_.size()); ++i)
    {
        if (isGrabbed(i))
            continue;
        const auto at = dragOrigin_[static_cast<size_t>(i)].position;
        if (at < first)
            lowest = std::max(lowest, at + 1 - first);
        else if (at > last)
            highest = std::min(highest, at - 1 - last);
    }
    delta = std::clamp(delta, lowest, std::max(lowest, highest));
    const float dNorm = AutomationScale::normAt(static_cast<float>(e.y), area.getY(), area.getHeight()) -
                        toNorm(track, key, grabbed.value);
    dragPoints_.clear();
    previewMoved_.clear();
    for (int i = 0; i < static_cast<int>(dragOrigin_.size()); ++i)
    {
        auto p = dragOrigin_[static_cast<size_t>(i)];
        const bool moving = isGrabbed(i);
        if (moving)
        {
            p.position += delta;
            p.value = fromNorm(track, key, std::clamp(toNorm(track, key, p.value) + dNorm, 0.0f, 1.0f));
        }
        dragPoints_.push_back(p);
        previewMoved_.push_back(moving ? 1 : 0);
    }
    // In time order (the moved block stays between its neighbours, so only
    // points inside its span reorder).
    std::vector<size_t> order(dragPoints_.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(),
                     order.end(),
                     [&](size_t a, size_t b) { return dragPoints_[a].position < dragPoints_[b].position; });
    std::vector<core::AutomationPoint> sorted;
    std::vector<char> moved;
    for (const auto i : order)
    {
        sorted.push_back(dragPoints_[i]);
        moved.push_back(previewMoved_[i]);
    }
    dragPoints_ = std::move(sorted);
    previewMoved_ = std::move(moved);
    previewing_ = true;
}

void AutomationLanes::previewDraw(const juce::MouseEvent& e,
                                  const Slot& slot,
                                  const core::Channel& track,
                                  const Key& key)
{
    // Freehand: a point wherever the pointer has been, at least 4 px apart, the
    // lane's points under the drawn stretch replaced.
    const auto area = curveArea(slot);
    core::AutomationPoint p;
    p.position = std::max<core::SamplePosition>(0, view_.sampleAt(std::max(e.x, area.getX())));
    p.value =
        fromNorm(track, key, AutomationScale::normAt(static_cast<float>(e.y), area.getY(), area.getHeight()));
    const auto minGap = static_cast<core::SamplePosition>(4.0 * view_.samplesPerPixel);
    const bool far = std::none_of(drawn_.begin(),
                                  drawn_.end(),
                                  [&](const core::AutomationPoint& q)
                                  { return std::abs(q.position - p.position) < minGap; });
    if (far)
        drawn_.push_back(p);
    std::stable_sort(drawn_.begin(),
                     drawn_.end(),
                     [](const core::AutomationPoint& a, const core::AutomationPoint& b)
                     { return a.position < b.position; });
    const auto from = drawn_.front().position, to = drawn_.back().position;
    dragPoints_.clear();
    previewMoved_.clear();
    for (const auto& q : dragOrigin_)
        if (q.position < from || q.position > to)
            dragPoints_.push_back(q);
    dragPoints_.insert(dragPoints_.end(), drawn_.begin(), drawn_.end());
    std::stable_sort(dragPoints_.begin(),
                     dragPoints_.end(),
                     [](const core::AutomationPoint& a, const core::AutomationPoint& b)
                     { return a.position < b.position; });
    previewing_ = true;
    repaint();
}

void AutomationLanes::mouseDrag(const juce::MouseEvent& e)
{
    if (resizeLane_ != core::invalidId)
    {
        const int h = std::clamp(resizeStartHeight_ + e.getDistanceFromDragStartY(),
                                 theme_.sizes.trackRowMin,
                                 theme_.sizes.trackRowMax);
        if (h != resizeHeight_)
        {
            resizeHeight_ = h;
            if (onLayoutChanged)
                onLayoutChanged();
        }
        return;
    }
    if (mode_ == Mode::none || project_ == nullptr)
        return;
    const auto* track = project_->findChannel(dragChannel_);
    const auto* lane = track != nullptr ? track->findLane(dragLane_) : nullptr;
    const auto slot = lane != nullptr ? slotOf(dragChannel_, dragLane_) : std::nullopt;
    if (!slot.has_value())
        return;
    const auto key = keyOf(*lane);
    const auto area = curveArea(*slot);
    switch (mode_)
    {
        case Mode::point:
        {
            if (dragIndex_ < 0 || static_cast<size_t>(dragIndex_) >= dragPoints_.size())
                return;
            auto& p = dragPoints_[static_cast<size_t>(dragIndex_)];
            if (!e.mods.isShiftDown())
            {
                auto position = std::max<core::SamplePosition>(0, geometry.snap(view_.sampleAt(e.x)));
                // Points keep their order: a point stays between its neighbours.
                if (dragIndex_ > 0)
                    position =
                        std::max(position, dragPoints_[static_cast<size_t>(dragIndex_ - 1)].position + 1);
                if (dragIndex_ + 1 < static_cast<int>(dragPoints_.size()))
                    position =
                        std::min(position, dragPoints_[static_cast<size_t>(dragIndex_ + 1)].position - 1);
                p.position = position;
            }
            else
                p.position = dragPosition_;
            p.value = fromNorm(
                *track, key, AutomationScale::normAt(static_cast<float>(e.y), area.getY(), area.getHeight()));
            break;
        }
        case Mode::selection:
            if (e.mouseWasDraggedSinceMouseDown())
                previewMove(e, *slot, *track, key);
            break;
        case Mode::marquee:
        {
            marquee_ = juce::Rectangle<int>(pressAt_, e.getPosition());
            auto points = grabbed_;
            for (int i = 0; i < static_cast<int>(lane->points.size()); ++i)
                if (marquee_.toFloat().contains(
                        pointPosition(*slot, *track, key, lane->points[static_cast<size_t>(i)])) &&
                    std::find(points.begin(), points.end(), i) == points.end())
                    points.push_back(i);
            select(track->id, lane->id, std::move(points));
            break;
        }
        case Mode::range:
        {
            const auto anchor = geometry.snap(std::max<core::SamplePosition>(0, view_.sampleAt(pressAt_.x)));
            const auto now = geometry.snap(std::max<core::SamplePosition>(0, view_.sampleAt(e.x)));
            selected_.clear();
            rangeStart_ = std::min(anchor, now);
            rangeEnd_ = std::max(anchor, now);
            for (int i = 0; i < static_cast<int>(lane->points.size()); ++i)
                if (const auto at = lane->points[static_cast<size_t>(i)].position;
                    at >= rangeStart_ && at <= rangeEnd_)
                    selected_.push_back(i);
            break;
        }
        case Mode::draw:
            previewDraw(e, *slot, *track, key);
            break;
        case Mode::erase:
        {
            for (int i = 0; i < static_cast<int>(dragOrigin_.size()); ++i)
            {
                const auto at = pointPosition(*slot, *track, key, dragOrigin_[static_cast<size_t>(i)]);
                if (at.getDistanceFrom(e.position) <= static_cast<float>(pointRadius + 4) &&
                    std::find(erased_.begin(), erased_.end(), i) == erased_.end())
                    erased_.push_back(i);
            }
            dragPoints_.clear();
            for (int i = 0; i < static_cast<int>(dragOrigin_.size()); ++i)
                if (std::find(erased_.begin(), erased_.end(), i) == erased_.end())
                    dragPoints_.push_back(dragOrigin_[static_cast<size_t>(i)]);
            previewing_ = true;
            break;
        }
        case Mode::none:
            break;
    }
    repaint();
}

void AutomationLanes::mouseUp(const juce::MouseEvent& e)
{
    if (resizeLane_ != core::invalidId)
    {
        const auto laneId = resizeLane_, channelId = resizeChannel_;
        const int height = resizeHeight_;
        const bool changed = height != resizeStartHeight_;
        if (changed && project_ != nullptr)
            if (const auto* track = project_->findChannel(channelId))
                if (const auto* lane = track->findLane(laneId))
                {
                    auto view = lane->view;
                    view.height = height;
                    setView(channelId, laneId, view, "Resize automation lane");
                }
        resizeLane_ = resizeChannel_ = core::invalidId;
        if (onLayoutChanged)
            onLayoutChanged();
        return;
    }
    const auto mode = std::exchange(mode_, Mode::none);
    const bool previewed = std::exchange(previewing_, false);
    auto points = std::move(dragPoints_);
    auto moved = std::move(previewMoved_);
    const int index = dragIndex_;
    const bool isNew = dragNew_;
    dragIndex_ = -1;
    dragPoints_.clear();
    previewMoved_.clear();
    const auto* track = project_ != nullptr ? project_->findChannel(dragChannel_) : nullptr;
    const auto* lane = track != nullptr ? track->findLane(dragLane_) : nullptr;
    dragChannel_ = dragLane_ = core::invalidId;
    repaint();
    if (lane == nullptr)
        return;
    switch (mode)
    {
        case Mode::point:
            if (previewed && (isNew || e.mouseWasDraggedSinceMouseDown()))
            {
                const auto at = points[static_cast<size_t>(index)].position;
                emitPoints(*track, *lane, points, isNew ? "Add automation point" : "Move automation point");
                // The point just placed or moved is the selection.
                for (int i = 0; i < static_cast<int>(points.size()); ++i)
                    if (points[static_cast<size_t>(i)].position == at)
                        select(track->id, lane->id, {i});
            }
            break;
        case Mode::selection:
            if (previewed && e.mouseWasDraggedSinceMouseDown())
            {
                std::vector<int> now;
                for (int i = 0; i < static_cast<int>(moved.size()); ++i)
                    if (moved[static_cast<size_t>(i)] != 0)
                        now.push_back(i);
                const auto channelId = track->id, laneId = lane->id;
                emitPoints(*track, *lane, std::move(points), "Move automation points");
                select(channelId, laneId, std::move(now));
            }
            else if (!e.mouseWasDraggedSinceMouseDown() && !e.mods.isShiftDown())
                select(track->id, lane->id, {grabIndex_}); // a click on one of several: that one alone
            break;
        case Mode::draw:
            if (previewed)
                emitPoints(*track, *lane, std::move(points), "Draw automation");
            break;
        case Mode::erase:
            if (previewed && points.size() != lane->points.size())
            {
                emitPoints(*track, *lane, std::move(points), "Erase automation points");
                clearSelection();
            }
            break;
        case Mode::range:
            if (!hasRange())
                clearSelection();
            break;
        case Mode::marquee:
        case Mode::none:
            break;
    }
    repaint();
}

// ----- selection and clipboard ------------------------------------------------------------

bool AutomationLanes::isSelected(core::Id lane, int index) const noexcept
{
    return lane == selLane_ && std::find(selected_.begin(), selected_.end(), index) != selected_.end();
}

void AutomationLanes::select(core::Id channel, core::Id lane, std::vector<int> points)
{
    selChannel_ = channel;
    selLane_ = lane;
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    selected_ = std::move(points);
    rangeStart_ = rangeEnd_ = 0;
    repaint();
}

void AutomationLanes::clearSelection()
{
    if (selLane_ == core::invalidId && selected_.empty() && !hasRange())
        return;
    selChannel_ = selLane_ = core::invalidId;
    selected_.clear();
    rangeStart_ = rangeEnd_ = 0;
    repaint();
}

void AutomationLanes::selectFirstLanePoints(int first, int last)
{
    if (project_ == nullptr || !geometry.rowCount)
        return;
    for (int row = 0; row < geometry.rowCount(); ++row)
        for (const auto& slot : slotsOf(row))
            if (const auto* lane = laneOf(slot); lane != nullptr && !lane->points.empty())
            {
                std::vector<int> points;
                for (int i = std::max(0, first); i <= last && i < static_cast<int>(lane->points.size()); ++i)
                    points.push_back(i);
                select(channelOf(slot)->id, lane->id, std::move(points));
                activeChannel_ = selChannel_;
                activeLane_ = selLane_;
                return;
            }
}

bool AutomationLanes::hasSelection() const noexcept
{
    return selLane_ != core::invalidId && (!selected_.empty() || hasRange());
}

std::vector<core::AutomationPoint> AutomationLanes::withoutRange(const core::AutomationLane& lane,
                                                                 core::SamplePosition start,
                                                                 core::SamplePosition end)
{
    std::vector<core::AutomationPoint> points;
    if (lane.points.empty())
        return points;
    const auto atStart = lane.valueAt(start), atEnd = lane.valueAt(end);
    for (const auto& p : lane.points)
        if (p.position < start)
            points.push_back(p);
    if (lane.points.front().position < start)
        points.push_back({start, atStart});
    if (lane.points.back().position > end)
        points.push_back({end, atEnd});
    for (const auto& p : lane.points)
        if (p.position > end)
            points.push_back(p);
    return points;
}

bool AutomationLanes::deleteSelection()
{
    if (!hasSelection() || project_ == nullptr)
        return false;
    const auto* track = project_->findChannel(selChannel_);
    const auto* lane = track != nullptr ? track->findLane(selLane_) : nullptr;
    if (lane == nullptr)
    {
        clearSelection();
        return false;
    }
    std::vector<core::AutomationPoint> points;
    std::string name = "Delete automation points";
    if (hasRange())
    {
        points = withoutRange(*lane, rangeStart_, rangeEnd_);
        name = "Delete automation range";
    }
    else
        for (int i = 0; i < static_cast<int>(lane->points.size()); ++i)
            if (!isSelected(lane->id, i))
                points.push_back(lane->points[static_cast<size_t>(i)]);
    const auto channelId = track->id, laneId = lane->id;
    const bool range = hasRange();
    const auto start = rangeStart_, end = rangeEnd_;
    emitPoints(*track, *lane, std::move(points), name);
    if (range)
    {
        // The range stays (now holding no points) so it can be pasted over.
        select(channelId, laneId, {});
        rangeStart_ = start;
        rangeEnd_ = end;
    }
    else
        clearSelection();
    return true;
}

bool AutomationLanes::copySelection()
{
    if (!hasSelection() || project_ == nullptr)
        return false;
    const auto* track = project_->findChannel(selChannel_);
    const auto* lane = track != nullptr ? track->findLane(selLane_) : nullptr;
    if (lane == nullptr)
        return false;
    clipboard_.clear();
    if (hasRange())
    {
        // The range's shape exactly: its points, and the value at each end.
        if (!lane->points.empty())
        {
            clipboard_.push_back({0, lane->valueAt(rangeStart_)});
            for (const auto& p : lane->points)
                if (p.position > rangeStart_ && p.position < rangeEnd_)
                    clipboard_.push_back({p.position - rangeStart_, p.value});
            clipboard_.push_back({rangeEnd_ - rangeStart_, lane->valueAt(rangeEnd_)});
        }
        clipboardLength_ = rangeEnd_ - rangeStart_;
        return true;
    }
    const auto first = lane->points[static_cast<size_t>(selected_.front())].position;
    for (const int i : selected_)
        if (static_cast<size_t>(i) < lane->points.size())
            clipboard_.push_back({lane->points[static_cast<size_t>(i)].position - first,
                                  lane->points[static_cast<size_t>(i)].value});
    clipboardLength_ = clipboard_.empty() ? 0 : clipboard_.back().position;
    return true;
}

bool AutomationLanes::paste(core::SamplePosition at)
{
    if (clipboard_.empty() || project_ == nullptr)
        return false;
    // Into the selected lane, else the one last pressed.
    const auto channelId = selLane_ != core::invalidId ? selChannel_ : activeChannel_;
    const auto laneId = selLane_ != core::invalidId ? selLane_ : activeLane_;
    const auto* track = project_->findChannel(channelId);
    const auto* lane = track != nullptr ? track->findLane(laneId) : nullptr;
    if (lane == nullptr)
        return false;
    at = std::max<core::SamplePosition>(0, at);
    auto points = withoutRange(*lane, at, at + clipboardLength_);
    // Points that land exactly on the edges replace the edge values kept.
    std::erase_if(points,
                  [&](const core::AutomationPoint& p)
                  { return p.position >= at && p.position <= at + clipboardLength_; });
    std::vector<int> pasted;
    for (const auto& p : clipboard_)
        points.push_back({at + p.position, p.value});
    std::stable_sort(points.begin(),
                     points.end(),
                     [](const core::AutomationPoint& a, const core::AutomationPoint& b)
                     { return a.position < b.position; });
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
        if (points[static_cast<size_t>(i)].position >= at &&
            points[static_cast<size_t>(i)].position <= at + clipboardLength_)
            pasted.push_back(i);
    emitPoints(*track, *lane, std::move(points), "Paste automation");
    select(channelId, laneId, std::move(pasted));
    return true;
}

void AutomationLanes::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto slot = slotAt(e.getPosition());
    if (!slot.has_value() || e.x < view_.originX)
        return;
    const auto* track = channelOf(*slot);
    const auto* lane = laneOf(*slot);
    const int index = pointAt(*slot, e.getPosition());
    if (track == nullptr || lane == nullptr || index < 0)
        return;
    auto points = lane->points;
    points.erase(points.begin() + index);
    emitPoints(*track, *lane, std::move(points), "Remove automation point");
}

// ----- edits ------------------------------------------------------------------------------

void AutomationLanes::emit(std::unique_ptr<core::Command> command, const std::string& name)
{
    if (!onEdit)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::move(command));
    onEdit(std::move(commands), name);
}

void AutomationLanes::emitPoints(const core::Channel& track,
                                 const core::AutomationLane& lane,
                                 std::vector<core::AutomationPoint> points,
                                 const std::string& name)
{
    // A lane keeps its row when its last point goes; Delete lane removes it.
    emit(std::make_unique<core::SetAutomationPoints>(track.id, lane.id, std::move(points), name), name);
}

void AutomationLanes::setView(core::Id channelId,
                              core::Id laneId,
                              core::AutomationLaneView view,
                              const std::string& name)
{
    emit(std::make_unique<core::SetAutomationLaneView>(channelId, laneId, view, name), name);
}

void AutomationLanes::revealOrAdd(core::Id channelId, const Key& key)
{
    if (project_ == nullptr || !onEdit)
        return;
    const auto* track = project_->findChannel(channelId);
    if (track == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    if (const auto* lane = track->findLane(key.target, key.insert, key.parameter))
    {
        if (lane->view.hidden || lane->view.collapsed)
        {
            auto view = lane->view;
            view.hidden = view.collapsed = false;
            commands.push_back(std::make_unique<core::SetAutomationLaneView>(channelId, lane->id, view));
        }
        if (!track->automationOpen)
            commands.push_back(std::make_unique<core::SetAutomationOpen>(channelId, true));
        if (!commands.empty())
            onEdit(std::move(commands), "Show automation lane");
        return;
    }
    core::AutomationLane added;
    added.target = key.target;
    added.insert = key.insert;
    added.parameter = key.parameter;
    commands.push_back(std::make_unique<core::AddAutomationLane>(channelId, std::move(added)));
    onEdit(std::move(commands), "Add automation lane");
}

// ----- menus ------------------------------------------------------------------------------

void AutomationLanes::addParameters(juce::PopupMenu& menu,
                                    const core::Channel& track,
                                    const Key* current,
                                    std::vector<Key>& keys) const
{
    // A dot marks a parameter that has a lane.
    const auto has = [&](const Key& k) { return track.findLane(k.target, k.insert, k.parameter) != nullptr; };
    const auto dot = juce::String::fromUTF8("  \xe2\x80\xa2");
    const auto add = [&](juce::PopupMenu& into, const juce::String& name, const Key& k)
    {
        keys.push_back(k);
        into.addItem(static_cast<int>(keys.size()),
                     has(k) ? name + dot : name,
                     true,
                     current != nullptr && k == *current);
    };
    add(menu, "Volume", Key{core::AutomationTarget::gain});
    if (track.id != core::masterId) // the master has no pan
        add(menu, "Pan", Key{core::AutomationTarget::pan});
    bool separator = false;
    for (const auto* insert : track.processingChain())
    {
        const auto* found = parametersOf(*insert);
        if (found == nullptr || found->empty())
            continue; // (a VST3 plugin exposes no parameters yet)
        const auto& infos = *found;
        if (!separator)
            menu.addSeparator();
        separator = true;
        juce::PopupMenu sub;
        bool any = false;
        for (size_t p = 0; p < infos.size(); ++p)
        {
            const Key k{core::AutomationTarget::insertParameter, insert->id, static_cast<int>(p)};
            any = any || has(k);
            add(sub, juce::String(infos[p].name), k);
        }
        auto name = juce::String(displayName ? displayName(insert->type) : insert->type);
        if (insert->bypass)
            name << " (off)";
        menu.addSubMenu(
            any ? name + dot : name, sub, true, nullptr, current != nullptr && current->insert == insert->id);
    }
}

void AutomationLanes::showLaneMenu(const Slot& slot, juce::Rectangle<int> menuArea, int point)
{
    const auto* track = channelOf(slot);
    const auto* lane = laneOf(slot);
    if (track == nullptr || lane == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader("This lane shows");
    std::vector<Key> keys;
    const auto current = keyOf(*lane);
    addParameters(menu, *track, &current, keys);
    menu.addSeparator();
    if (point >= 0)
        menu.addItem(deletePointId, "Delete point");
    menu.addItem(collapseId, lane->view.collapsed ? "Expand lane" : "Collapse lane");
    menu.addItem(hideId, "Hide lane");
    menu.addItem(clearId, "Clear points", !lane->points.empty());
    menu.addItem(deleteLaneId, "Delete lane");
    juce::Component::SafePointer<AutomationLanes> safe(this);
    const auto channelId = track->id, laneId = lane->id;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(menuArea)),
        [safe, keys, channelId, laneId, point](int choice)
        {
            if (safe == nullptr || choice <= 0 || safe->project_ == nullptr)
                return;
            const auto* t = safe->project_->findChannel(channelId);
            const auto* l = t != nullptr ? t->findLane(laneId) : nullptr;
            if (l == nullptr)
                return;
            if (choice <= static_cast<int>(keys.size()))
            {
                const auto& k = keys[static_cast<size_t>(choice - 1)];
                if (k == keyOf(*l))
                    return;
                // An empty lane changes parameter; one with points keeps them and
                // the other parameter's lane shows (or is added).
                if (l->points.empty() && t->findLane(k.target, k.insert, k.parameter) == nullptr)
                    safe->emit(std::make_unique<core::SetAutomationLaneTarget>(
                                   channelId, laneId, k.target, k.insert, k.parameter),
                               "Change automation parameter");
                else
                    safe->revealOrAdd(channelId, k);
                return;
            }
            auto view = l->view;
            switch (choice)
            {
                case deletePointId:
                    if (point >= 0 && static_cast<size_t>(point) < l->points.size())
                    {
                        auto points = l->points;
                        points.erase(points.begin() + point);
                        safe->emitPoints(*t, *l, std::move(points), "Remove automation point");
                    }
                    break;
                case collapseId:
                    view.collapsed = !view.collapsed;
                    safe->setView(channelId,
                                  laneId,
                                  view,
                                  view.collapsed ? "Collapse automation lane" : "Expand automation lane");
                    break;
                case hideId:
                    view.hidden = true;
                    safe->setView(channelId, laneId, view, "Hide automation lane");
                    break;
                case clearId:
                    safe->emitPoints(*t, *l, {}, "Clear automation");
                    break;
                case deleteLaneId:
                    safe->emit(std::make_unique<core::RemoveAutomationLane>(channelId, laneId),
                               "Delete automation lane");
                    break;
                default:
                    break;
            }
        });
}

void AutomationLanes::showAddMenu(core::Id channelId, juce::Rectangle<int> screenArea)
{
    const auto* track = project_ != nullptr ? project_->findChannel(channelId) : nullptr;
    if (track == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader("Add a lane for");
    std::vector<Key> keys;
    addParameters(menu, *track, nullptr, keys);
    juce::Component::SafePointer<AutomationLanes> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
                       [safe, keys, channelId](int choice)
                       {
                           if (safe != nullptr && choice > 0 && choice <= static_cast<int>(keys.size()))
                               safe->revealOrAdd(channelId, keys[static_cast<size_t>(choice - 1)]);
                       });
}

void AutomationLanes::showHiddenMenu(core::Id channelId, juce::Rectangle<int> screenArea)
{
    const auto* track = project_ != nullptr ? project_->findChannel(channelId) : nullptr;
    if (track == nullptr)
        return;
    juce::PopupMenu menu;
    std::vector<core::Id> hidden;
    for (const auto& lane : track->automation)
        if (lane.view.hidden)
        {
            hidden.push_back(lane.id);
            menu.addItem(static_cast<int>(hidden.size()), "Show " + keyName(*track, keyOf(lane)));
        }
    if (hidden.empty())
        return;
    menu.addSeparator();
    menu.addItem(10000, "Show all");
    juce::Component::SafePointer<AutomationLanes> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
                       [safe, hidden, channelId](int choice)
                       {
                           if (safe == nullptr || choice <= 0 || safe->project_ == nullptr || !safe->onEdit)
                               return;
                           const auto* t = safe->project_->findChannel(channelId);
                           if (t == nullptr)
                               return;
                           std::vector<std::unique_ptr<core::Command>> commands;
                           for (size_t i = 0; i < hidden.size(); ++i)
                               if (choice == 10000 || choice == static_cast<int>(i) + 1)
                                   if (const auto* l = t->findLane(hidden[i]))
                                   {
                                       auto view = l->view;
                                       view.hidden = false;
                                       commands.push_back(std::make_unique<core::SetAutomationLaneView>(
                                           channelId, l->id, view));
                                   }
                           if (!commands.empty())
                               safe->onEdit(std::move(commands), "Show automation lane");
                       });
}

void AutomationLanes::showChannelMenu(core::Id channelId, juce::Rectangle<int> screenArea)
{
    const auto* track = project_ != nullptr ? project_->findChannel(channelId) : nullptr;
    if (track == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader("Mode");
    const core::AutomationMode modes[] = {core::AutomationMode::off,
                                          core::AutomationMode::read,
                                          core::AutomationMode::touch,
                                          core::AutomationMode::latch,
                                          core::AutomationMode::write};
    const char* hints[] = {"lanes ignored",
                           "lanes play",
                           "writes while a control is held",
                           "writes from a touch until stop",
                           "overwrites while playing"};
    for (int i = 0; i < 5; ++i)
        menu.addItem(i + 1,
                     juce::String(modeName(modes[i])) + juce::String::fromUTF8("  \xc2\xb7  ") + hints[i],
                     true,
                     track->automationMode == modes[i]);
    menu.addSeparator();
    int hidden = 0, collapsed = 0, expanded = 0;
    for (const auto& lane : track->automation)
    {
        hidden += lane.view.hidden ? 1 : 0;
        collapsed += !lane.view.hidden && lane.view.collapsed ? 1 : 0;
        expanded += !lane.view.hidden && !lane.view.collapsed ? 1 : 0;
    }
    menu.addItem(100, track->automationOpen ? "Hide lanes" : "Show lanes");
    if (hidden > 0)
        menu.addItem(101,
                     hidden == 1 ? juce::String("Show the hidden lane")
                                 : "Show " + juce::String(hidden) + " hidden lanes");
    menu.addItem(102, "Expand all lanes", collapsed > 0);
    menu.addItem(103, "Collapse all lanes", expanded > 0);
    juce::PopupMenu add;
    std::vector<Key> keys;
    addParameters(add, *track, nullptr, keys);
    for (auto it = juce::PopupMenu::MenuItemIterator(add, true); it.next();)
        if (it.getItem().itemID > 0)
            it.getItem().itemID += 1000;
    menu.addSubMenu("Add lane", add);
    juce::Component::SafePointer<AutomationLanes> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(screenArea),
        [safe, channelId, modes, keys](int choice)
        {
            if (safe == nullptr || choice <= 0 || safe->project_ == nullptr || !safe->onEdit)
                return;
            const auto* t = safe->project_->findChannel(channelId);
            if (t == nullptr)
                return;
            if (choice <= 5)
            {
                safe->emit(std::make_unique<core::SetAutomationMode>(channelId, modes[choice - 1]),
                           "Set automation mode");
                return;
            }
            if (choice > 1000 && choice <= 1000 + static_cast<int>(keys.size()))
            {
                safe->revealOrAdd(channelId, keys[static_cast<size_t>(choice - 1001)]);
                return;
            }
            if (choice == 100)
            {
                safe->emit(std::make_unique<core::SetAutomationOpen>(channelId, !t->automationOpen),
                           t->automationOpen ? "Hide automation" : "Show automation");
                return;
            }
            std::vector<std::unique_ptr<core::Command>> commands;
            for (const auto& l : t->automation)
            {
                auto view = l.view;
                if (choice == 101)
                    view.hidden = false;
                else if (choice == 102 && !view.hidden)
                    view.collapsed = false;
                else if (choice == 103 && !view.hidden)
                    view.collapsed = true;
                if (view != l.view)
                    commands.push_back(std::make_unique<core::SetAutomationLaneView>(channelId, l.id, view));
            }
            if (choice == 101 && !t->automationOpen)
                commands.push_back(std::make_unique<core::SetAutomationOpen>(channelId, true));
            if (!commands.empty())
                safe->onEdit(std::move(commands),
                             choice == 101   ? "Show automation lanes"
                             : choice == 102 ? "Expand automation lanes"
                                             : "Collapse automation lanes");
        });
}

} // namespace northlight::ui
