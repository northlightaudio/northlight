#include "ui/AutomationLanes.h"

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
constexpr float gainTopDb = 6.0f, gainBottomDb = -60.0f;
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

AutomationLanes::AutomationLanes(const Theme& theme) : theme_(theme)
{
    setInterceptsMouseClicks(true, false);
    setRepaintsOnMouseActivity(false);
}

void AutomationLanes::setProject(const core::Project* project)
{
    project_ = project;
    shownValues_.clear();
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
    return slot.bounds.withWidth(geometry.headerWidth());
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
    if (p.x >= geometry.headerWidth())
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
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            if (value <= 1.0e-6f)
                return 0.0f;
            return juce::jlimit(0.0f,
                                1.0f,
                                (static_cast<float>(db::fromGain(value)) - gainBottomDb) /
                                    (gainTopDb - gainBottomDb));
        case core::AutomationTarget::pan:
            return juce::jlimit(0.0f, 1.0f, (value + 1.0f) * 0.5f);
        case core::AutomationTarget::insertParameter:
            if (const auto* info = infoFor(track, key))
            {
                if (info->logarithmic && info->minimum > 0.0f)
                    return juce::jlimit(0.0f,
                                        1.0f,
                                        std::log(std::max(info->minimum, value) / info->minimum) /
                                            std::log(info->maximum / info->minimum));
                return juce::jlimit(0.0f, 1.0f, (value - info->minimum) / (info->maximum - info->minimum));
            }
            return 0.5f;
    }
    return 0.0f;
}

float AutomationLanes::fromNorm(const core::Channel& track, const Key& key, float norm) const
{
    norm = juce::jlimit(0.0f, 1.0f, norm);
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            if (norm <= 0.002f)
                return 0.0f;
            return db::toGain(gainBottomDb + norm * (gainTopDb - gainBottomDb));
        case core::AutomationTarget::pan:
            return norm * 2.0f - 1.0f;
        case core::AutomationTarget::insertParameter:
            if (const auto* info = infoFor(track, key))
            {
                float v = info->logarithmic && info->minimum > 0.0f
                              ? info->minimum * std::pow(info->maximum / info->minimum, norm)
                              : info->minimum + norm * (info->maximum - info->minimum);
                if (!info->choices.empty())
                    v = std::round(v);
                return v;
            }
            return 0.0f;
    }
    return 0.0f;
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
    return std::max(static_cast<float>(geometry.xForSample(position)),
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
        const int y = area.getBottom() - static_cast<int>(toNorm(*track, key, pt.value) * area.getHeight());
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
    shownValues_[lane->id] = value;
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
    const float h = static_cast<float>(area.getHeight());
    const bool dragging = dragLane_ == lane->id && dragIndex_ >= 0;
    const auto& points = dragging ? dragPoints_ : lane->points;
    const auto yOf = [&](float value)
    { return static_cast<float>(area.getBottom()) - toNorm(*track, key, value) * h; };
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
        const float v =
            core::automationValueAt(points.data(), points.size(), key.target, geometry.sampleForX(x));
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
        const float r = hot ? pointRadius + 1.5f : static_cast<float>(pointRadius);
        const auto at = juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre({x, yOf(points[i].value)});
        g.setColour(c.canvas);
        g.fillEllipse(at.expanded(1.0f));
        g.setColour(colour);
        g.fillEllipse(at);
        if (hot)
        {
            g.setColour(c.textPrimary);
            g.drawEllipse(at.expanded(1.5f), 1.0f);
        }
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

void AutomationLanes::playheadMoved()
{
    if (project_ == nullptr || !geometry.rowCount)
        return;
    for (int row = 0; row < geometry.rowCount(); ++row)
    {
        const auto block = geometry.block(row);
        if (block.isEmpty() || !block.intersects(geometry.lanesArea().withX(0)))
            continue;
        const auto* track = geometry.channelAt(row);
        for (const auto& slot : slotsOf(row))
            if (const auto* lane = laneOf(slot))
            {
                auto value = valueAtPlayhead(*track, *lane);
                if (auto& shown = shownValues_[lane->id]; shown != value)
                {
                    shown = std::move(value);
                    repaint(valueOf(slot));
                }
            }
    }
}

// ----- the mouse --------------------------------------------------------------------------

void AutomationLanes::mouseMove(const juce::MouseEvent& e)
{
    const auto slot = slotAt(e.getPosition());
    int point = -1;
    core::Id lane = core::invalidId;
    Part part = Part::none;
    auto pointer = juce::MouseCursor::NormalCursor;
    if (slot.has_value())
    {
        part = partAt(*slot, e.getPosition());
        const auto* l = laneOf(*slot);
        if (onResizeEdge(*slot, e.getPosition()))
            pointer = juce::MouseCursor::UpDownResizeCursor;
        else if (part != Part::none)
            pointer = juce::MouseCursor::PointingHandCursor;
        else if (l != nullptr && e.x >= geometry.headerWidth())
        {
            lane = l->id;
            point = pointAt(*slot, e.getPosition());
            pointer = l->view.collapsed ? juce::MouseCursor::PointingHandCursor
                      : point >= 0      ? juce::MouseCursor::DraggingHandCursor
                                        : juce::MouseCursor::CrosshairCursor;
        }
    }
    setMouseCursor(pointer);
    const int row = slot.has_value() ? slot->row : -1;
    const int slotLane = slot.has_value() ? slot->lane : -1;
    if (part != hoverPart_ || row != hoverRow_ || slotLane != hoverSlotLane_)
    {
        if (hoverPart_ != Part::none || part != Part::none)
            repaint(0, 0, geometry.headerWidth(), getHeight());
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

    if (e.x < geometry.headerWidth())
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
    dragChannel_ = track->id;
    dragLane_ = lane->id;
    dragPoints_ = lane->points;
    if (index >= 0)
    {
        dragIndex_ = index;
        dragNew_ = false;
        dragPosition_ = dragPoints_[static_cast<size_t>(index)].position;
    }
    else
    {
        const auto area = curveArea(*slot);
        core::AutomationPoint p;
        p.position = std::max<core::SamplePosition>(0, geometry.snap(geometry.sampleForX(e.x)));
        p.value = fromNorm(
            *track, key, static_cast<float>(area.getBottom() - e.y) / static_cast<float>(area.getHeight()));
        auto at = std::upper_bound(dragPoints_.begin(),
                                   dragPoints_.end(),
                                   p.position,
                                   [](core::SamplePosition s, const core::AutomationPoint& q)
                                   { return s < q.position; });
        dragIndex_ = static_cast<int>(at - dragPoints_.begin());
        dragPoints_.insert(at, p);
        dragNew_ = true;
        dragPosition_ = p.position;
    }
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
    if (dragIndex_ < 0 || project_ == nullptr)
        return;
    const auto* track = project_->findChannel(dragChannel_);
    const auto* lane = track != nullptr ? track->findLane(dragLane_) : nullptr;
    if (lane == nullptr)
        return;
    std::optional<Slot> slot;
    for (int row = 0; row < geometry.rowCount() && !slot.has_value(); ++row)
        if (geometry.channelAt(row)->id == dragChannel_)
            for (const auto& s : slotsOf(row))
                if (laneOf(s) == lane)
                    slot = s;
    if (!slot.has_value())
        return;
    const auto key = keyOf(*lane);
    const auto area = curveArea(*slot);
    auto& p = dragPoints_[static_cast<size_t>(dragIndex_)];
    if (!e.mods.isShiftDown())
    {
        auto position = std::max<core::SamplePosition>(0, geometry.snap(geometry.sampleForX(e.x)));
        // Points keep their order: a point stays between its neighbours.
        if (dragIndex_ > 0)
            position = std::max(position, dragPoints_[static_cast<size_t>(dragIndex_ - 1)].position + 1);
        if (dragIndex_ + 1 < static_cast<int>(dragPoints_.size()))
            position = std::min(position, dragPoints_[static_cast<size_t>(dragIndex_ + 1)].position - 1);
        p.position = position;
    }
    else
        p.position = dragPosition_;
    p.value = fromNorm(
        *track, key, static_cast<float>(area.getBottom() - e.y) / static_cast<float>(area.getHeight()));
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
    if (dragIndex_ < 0 || project_ == nullptr)
        return;
    const auto* track = project_->findChannel(dragChannel_);
    const auto* lane = track != nullptr ? track->findLane(dragLane_) : nullptr;
    auto points = std::move(dragPoints_);
    const bool isNew = dragNew_;
    dragIndex_ = -1;
    dragPoints_.clear();
    dragChannel_ = dragLane_ = core::invalidId;
    if (lane != nullptr && (isNew || e.mouseWasDraggedSinceMouseDown()))
        emitPoints(
            *track, *lane, std::move(points), isNew ? "Add automation point" : "Move automation point");
    repaint();
}

void AutomationLanes::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto slot = slotAt(e.getPosition());
    if (!slot.has_value() || e.x < geometry.headerWidth())
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
