#include "ui/TakeLanes.h"

#include "core/Command.h"
#include "core/Comping.h"
#include "ui/Icons.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int boundaryReach = 4; // px either side of a comp boundary that drag it

juce::String takeBadge(const core::Take& take)
{
    return "T" + juce::String(take.number);
}

// Whether a take plays anywhere in the comp.
bool inComp(const core::TakeGroup& group, core::Id take) noexcept
{
    return std::any_of(
        group.comp.begin(), group.comp.end(), [take](const core::CompSegment& s) { return s.take == take; });
}
} // namespace

TakeLanes::TakeLanes(const Theme& theme, audio::PeakStore& peaks, juce::Component& owner)
    : theme_(theme), peaks_(peaks), owner_(owner)
{
}

TakeLanes::~TakeLanes() = default;

int TakeLanes::laneCount(const core::Track& track) const noexcept
{
    size_t lanes = 0;
    for (const auto& g : track.takeGroups)
        if (g.expanded)
            lanes = std::max(lanes, g.takes.size());
    return static_cast<int>(lanes);
}

int TakeLanes::lanesHeight(const core::Track& track) const noexcept
{
    return laneCount(track) * theme_.sizes.takeLane;
}

const core::Take* TakeLanes::takeInLane(const core::TakeGroup& group, int lane) noexcept
{
    const int index = static_cast<int>(group.takes.size()) - 1 - lane; // newest on top
    return group.expanded && lane >= 0 && index >= 0 ? &group.takes[static_cast<size_t>(index)] : nullptr;
}

int TakeLanes::laneAt(const Row& row, int y) const noexcept
{
    if (row.track == nullptr || y < row.main.getBottom())
        return -1;
    const int lane = (y - row.main.getBottom()) / std::max(1, theme_.sizes.takeLane);
    return lane < laneCount(*row.track) ? lane : -1;
}

juce::Rectangle<int> TakeLanes::laneBounds(const Row& row, int lane) const noexcept
{
    return {row.main.getX(),
            row.main.getBottom() + lane * theme_.sizes.takeLane,
            row.main.getWidth(),
            theme_.sizes.takeLane};
}

const core::TakeGroup* TakeLanes::headerGroup(const Row& row, int lane) const noexcept
{
    const core::TakeGroup* first = nullptr;
    for (const auto& g : row.track->takeGroups)
    {
        if (takeInLane(g, lane) == nullptr)
            continue;
        if (first == nullptr)
            first = &g;
        if (geometry.xForSample(g.end()) > row.headerWidth) // the first one still in view
            return &g;
    }
    return first;
}

juce::Rectangle<int> TakeLanes::chipBounds(const core::TakeGroup& group, const Row& row) const
{
    const int x0 = std::max(geometry.xForSample(group.start()), row.main.getX()) + theme_.spacing.xs;
    const auto text = juce::String(group.takes.size()) + (group.takes.size() == 1 ? " take" : " takes");
    const int w = theme_.sizes.iconSmall + theme_.spacing.xs +
                  juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + 2 * theme_.spacing.sm;
    const int h = std::min(theme_.sizes.clipHeader - 2, row.main.getHeight() - 8);
    return {x0, row.main.getBottom() - 4 - h, w, h};
}

std::optional<core::SamplePosition>
TakeLanes::boundaryAt(const core::TakeGroup& group, const Row& row, juce::Point<int> p) const
{
    const bool header = row.main.getHeight() >= 24;
    if (!row.main.contains(p) || (header && p.y < row.main.getY() + 2 + theme_.sizes.clipHeader))
        return std::nullopt;
    std::optional<core::SamplePosition> best;
    int bestDistance = boundaryReach + 1;
    for (size_t k = 1; k < group.comp.size(); ++k)
    {
        const int d = std::abs(geometry.xForSample(group.comp[k].start) - p.x);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = group.comp[k].start;
        }
    }
    return best;
}

// ----- painting --------------------------------------------------------------------------------

void TakeLanes::drawBadge(juce::Graphics& g,
                          const juce::String& text,
                          juce::Rectangle<int> area,
                          bool lit) const
{
    // design-system.md "Badge": pill, caption 600, the colour at 20% behind it.
    const auto& c = theme_.colours;
    const auto font = theme_.font(Theme::Face::ui, theme_.fonts.size.caption, theme_.fonts.weight.semibold);
    const int w = juce::GlyphArrangement::getStringWidthInt(font, text) + 2 * 6;
    const auto pill = area.withWidth(std::min(area.getWidth(), w));
    const auto colour = lit ? c.textPrimary : c.textSecondary;
    g.setColour(colour.withAlpha(0.2f));
    g.fillRoundedRectangle(pill.toFloat(), static_cast<float>(pill.getHeight()) * 0.5f);
    g.setColour(colour);
    g.setFont(font);
    g.drawText(text, pill, juce::Justification::centred, false);
}

void TakeLanes::drawWave(juce::Graphics& g,
                         const core::AudioClip& clip,
                         juce::Rectangle<int> area,
                         juce::Colour colour,
                         const core::TakeGroup& group) const
{
    const auto peaks = peaks_.peaksFor(clip.mediaPath, geometry.sampleRate());
    if (peaks == nullptr || peaks->empty() || area.isEmpty())
        return;
    const auto first = clip.sourceAt(geometry.sampleForX(area.getX()) - clip.start);
    const auto last = clip.sourceAt(geometry.sampleForX(area.getRight()) - clip.start);
    const auto view = peaks_.viewFor(clip.mediaPath,
                                     geometry.sampleRate(),
                                     first,
                                     last + 1,
                                     static_cast<double>(last - first) / std::max(1, area.getWidth()));
    const int channels = std::min(2, peaks->numChannels());
    const float laneHeight = static_cast<float>(area.getHeight()) / static_cast<float>(channels);
    const float scale = std::min(1.0f, clip.gain);
    const auto lit = colour.withAlpha(theme_.track.waveformAlpha);
    const auto dim = colour.withAlpha(theme_.track.waveformMutedAlpha);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float midY = static_cast<float>(area.getY()) + laneHeight * (static_cast<float>(ch) + 0.5f);
        const float half = laneHeight * 0.48f * scale;
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto at = geometry.sampleForX(x);
            const auto* segment = core::segmentAt(group, at);
            g.setColour(segment != nullptr && segment->take == clip.id ? lit : dim);
            const auto from = clip.sourceAt(at - clip.start);
            const auto to = clip.sourceAt(geometry.sampleForX(x + 1) - clip.start);
            const auto r = view.lookup(ch, from, std::max(to, from + 1));
            const float top = midY - r.maximum * half;
            const float bottom = midY - r.minimum * half;
            g.fillRect(
                static_cast<float>(x), std::min(top, bottom), 1.0f, std::max(1.0f, std::abs(bottom - top)));
        }
    }
}

void TakeLanes::paintMain(juce::Graphics& g, const Row& row) const
{
    if (row.track == nullptr || row.track->takeGroups.empty())
        return;
    const auto& c = theme_.colours;
    const auto& track = *row.track;
    const bool header = row.main.getHeight() >= 24;
    for (const auto& group : track.takeGroups)
    {
        // The take badge on each comp clip's header, at its right.
        if (header)
            for (const auto& clip : track.clips)
            {
                if (!group.ownsClip(clip.id))
                    continue;
                const auto* segment = core::segmentAt(group, clip.start + clip.length / 2);
                const auto* take = segment != nullptr ? group.findTake(segment->take) : nullptr;
                if (take == nullptr)
                    continue;
                const int x0 = geometry.xForSample(clip.start), x1 = geometry.xForSample(clip.end());
                if (x1 - x0 < 64)
                    continue;
                const auto strip =
                    juce::Rectangle<int>(x0, row.main.getY() + 2, x1 - x0, theme_.sizes.clipHeader)
                        .reduced(0, 3);
                const auto text = takeBadge(*take);
                const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + 12;
                drawBadge(g, text, strip.withTrimmedRight(theme_.spacing.xs).removeFromRight(w), true);
            }
        // Comp boundaries: a faint line through the body; the one under the pointer lit.
        const int top = row.main.getY() + 2 + (header ? theme_.sizes.clipHeader : 0);
        const int bottom = row.main.getBottom() - 3;
        for (size_t k = 1; k < group.comp.size(); ++k)
        {
            const auto at = press_.kind == PressKind::boundary && press_.group == group.id &&
                                    press_.anchor == group.comp[k].start
                                ? press_.current
                                : group.comp[k].start;
            const bool lit = (hoverGroup_ == group.id && hoverBoundary_ == group.comp[k].start) ||
                             (press_.kind == PressKind::boundary && press_.group == group.id &&
                              press_.anchor == group.comp[k].start);
            const int x = geometry.xForSample(at);
            g.setColour(lit ? c.accent : c.textPrimary.withAlpha(0.35f));
            g.fillRect(lit ? x - 1 : x, top, lit ? 2 : 1, std::max(0, bottom - top));
        }
        // The disclosure chip at the group's lower left.
        if (row.main.getHeight() >= 24)
        {
            const auto chip = chipBounds(group, row);
            g.setColour(c.surface2.withAlpha(0.9f));
            g.fillRoundedRectangle(chip.toFloat(), theme_.radius.sm);
            auto inner = chip.reduced(theme_.spacing.xs, 0);
            const auto icon = inner.removeFromLeft(theme_.sizes.iconSmall);
            Icons::draw(
                g,
                group.expanded ? Icon::chevronDown : Icon::chevronRight,
                icon.toFloat().withSizeKeepingCentre(static_cast<float>(theme_.sizes.iconSmall) - 2.0f,
                                                     static_cast<float>(theme_.sizes.iconSmall) - 2.0f),
                c.textPrimary);
            g.setColour(c.textPrimary);
            g.setFont(theme_.caption());
            g.drawText(juce::String(group.takes.size()) + (group.takes.size() == 1 ? " take" : " takes"),
                       inner.withTrimmedLeft(theme_.spacing.xs),
                       juce::Justification::centredLeft,
                       false);
        }
    }
}

void TakeLanes::paintLanes(juce::Graphics& g, const Row& row) const
{
    if (row.track == nullptr)
        return;
    const auto& c = theme_.colours;
    const auto& track = *row.track;
    const int lanes = laneCount(track);
    for (int lane = 0; lane < lanes; ++lane)
    {
        const auto area = laneBounds(row, lane);
        g.setColour(c.surface0);
        g.fillRect(area);
        g.setColour(c.lineSubtle);
        g.fillRect(area.withTrimmedTop(area.getHeight() - 1));
        for (const auto& group : track.takeGroups)
        {
            const auto* take = takeInLane(group, lane);
            if (take == nullptr)
                continue;
            const auto& clip = take->clip;
            const int x0 = geometry.xForSample(clip.start);
            const int x1 = std::max(x0 + 2, geometry.xForSample(clip.end()));
            const auto block = juce::Rectangle<int>(x0, area.getY() + 2, x1 - x0, area.getHeight() - 5);
            if (block.getRight() < area.getX() || block.getX() > area.getRight())
                continue;
            // The take, dim; the stretches the comp plays from it, lit.
            g.setColour(row.colour.withAlpha(theme_.track.clipBodyAlpha * 0.5f));
            g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);
            for (const auto& s : group.comp)
                if (s.take == take->id())
                {
                    const int s0 = std::max(block.getX(), geometry.xForSample(std::max(s.start, clip.start)));
                    const int s1 =
                        std::min(block.getRight(), geometry.xForSample(std::min(s.end, clip.end())));
                    if (s1 <= s0)
                        continue;
                    const auto lit = juce::Rectangle<int>(s0, block.getY(), s1 - s0, block.getHeight());
                    g.setColour(row.colour.withAlpha(theme_.track.clipBodySelectedAlpha));
                    g.fillRect(lit);
                    g.setColour(row.colour);
                    g.drawRect(lit, 1);
                }
            drawWave(g, clip, block.reduced(0, 2).getIntersection(area), row.colour, group);
            g.setColour(row.colour.withAlpha(0.4f));
            g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
            // The swipe in progress: the range the take will play over.
            if (press_.kind == PressKind::swipe && press_.group == group.id && press_.take == take->id() &&
                press_.moved)
            {
                const auto from = std::max(std::min(press_.anchor, press_.current), clip.start);
                const auto to = std::min(std::max(press_.anchor, press_.current), clip.end());
                const int s0 = geometry.xForSample(from), s1 = geometry.xForSample(to);
                if (s1 > s0)
                {
                    const auto swipe = juce::Rectangle<int>(s0, block.getY(), s1 - s0, block.getHeight());
                    g.setColour(c.selectionFill);
                    g.fillRect(swipe);
                    g.setColour(c.selectionOutline);
                    g.drawRect(swipe, 1);
                }
            }
            // The take's badge and name at its left, kept in view.
            const int labelX = std::max(block.getX(), area.getX()) + theme_.spacing.xs;
            const auto label = juce::Rectangle<int>(labelX,
                                                    block.getY() + 3,
                                                    block.getRight() - labelX - 4,
                                                    theme_.lineHeight(theme_.fonts.size.caption) + 2);
            if (label.getWidth() > 24)
            {
                const auto badge = takeBadge(*take);
                const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), badge) + 12;
                drawBadge(g, badge, label.withWidth(w), inComp(group, take->id()));
                g.setColour(inComp(group, take->id()) ? c.textPrimary : c.textSecondary);
                g.setFont(theme_.caption());
                g.drawText(juce::String::fromUTF8(clip.name.c_str()),
                           label.withTrimmedLeft(w + theme_.spacing.xs),
                           juce::Justification::centredLeft,
                           true);
            }
        }
    }
}

void TakeLanes::paintLaneHeaders(juce::Graphics& g, const Row& row) const
{
    if (row.track == nullptr)
        return;
    const auto& c = theme_.colours;
    const int lanes = laneCount(*row.track);
    for (int lane = 0; lane < lanes; ++lane)
    {
        const auto lb = laneBounds(row, lane);
        const auto area = juce::Rectangle<int>(0, lb.getY(), row.headerWidth, lb.getHeight());
        g.setColour(c.surface0);
        g.fillRect(area);
        g.setColour(row.colour.withAlpha(0.4f));
        g.fillRect(area.withWidth(theme_.track.headerStripeWidth));
        g.setColour(c.lineSubtle);
        g.fillRect(area.withTrimmedTop(area.getHeight() - 1));
        g.fillRect(area.withTrimmedLeft(area.getWidth() - 1));
        const auto* group = headerGroup(row, lane);
        const auto* take = group != nullptr ? takeInLane(*group, lane) : nullptr;
        if (take == nullptr)
            continue;
        auto inner = area.withTrimmedLeft(theme_.track.headerStripeWidth + theme_.spacing.sm)
                         .withTrimmedRight(theme_.spacing.sm)
                         .withSizeKeepingCentre(area.getWidth(), 16);
        inner.setX(area.getX() + theme_.track.headerStripeWidth + theme_.spacing.sm);
        inner.setWidth(area.getWidth() - theme_.track.headerStripeWidth - 2 * theme_.spacing.sm);
        const bool used = inComp(*group, take->id());
        const bool whole = group->comp.size() == 1 && group->comp.front().take == take->id();
        const auto badge = takeBadge(*take);
        const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), badge) + 12;
        drawBadge(g, badge, inner.removeFromLeft(w), used);
        inner.removeFromLeft(theme_.spacing.sm);
        g.setColour(used ? c.textPrimary : c.textSecondary);
        g.setFont(theme_.label());
        g.drawText(
            juce::String::fromUTF8(take->clip.name.c_str()), inner, juce::Justification::centredLeft, true);
        if (whole)
        {
            g.setColour(c.accent);
            g.setFont(theme_.caption());
            g.drawText("Comp", inner, juce::Justification::centredRight, false);
        }
    }
}

// ----- input -----------------------------------------------------------------------------------

bool TakeLanes::mouseDown(const juce::MouseEvent& e, const Row& row)
{
    press_ = Press{};
    if (row.track == nullptr || row.track->takeGroups.empty())
        return false;
    const auto& track = *row.track;
    const auto p = e.getPosition();
    const auto at = geometry.sampleForX(p.x);

    // The lanes and their headers.
    if (const int lane = laneAt(row, p.y); lane >= 0)
    {
        if (p.x < row.headerWidth)
        {
            const auto* group = headerGroup(row, lane);
            const auto* take = group != nullptr ? takeInLane(*group, lane) : nullptr;
            if (take == nullptr)
                return true;
            const auto header =
                juce::Rectangle<int>(0, laneBounds(row, lane).getY(), row.headerWidth, theme_.sizes.takeLane);
            if (e.mods.isPopupMenu())
                laneMenu(track, *group, *take, header);
            else
                emit(std::make_unique<core::SelectTake>(track.id, group->id, take->id()), "Use take");
            return true;
        }
        for (const auto& group : track.takeGroups)
        {
            const auto* take = takeInLane(group, lane);
            if (take == nullptr || at < take->clip.start || at >= take->clip.end())
                continue;
            if (e.mods.isPopupMenu())
            {
                const auto header = juce::Rectangle<int>(
                    p.x, laneBounds(row, lane).getY(), row.headerWidth, theme_.sizes.takeLane);
                laneMenu(track, group, *take, header);
                return true;
            }
            press_.kind = PressKind::swipe;
            press_.track = track.id;
            press_.group = group.id;
            press_.take = take->id();
            press_.lane = lane;
            press_.anchor = press_.current = geometry.snap(at);
            press_.start = p;
            return true;
        }
        return true; // empty lane space: nothing to lasso here
    }

    // The main lane: the disclosure chip, a comp boundary, a comp's menu.
    if (!row.main.contains(p))
        return false;
    for (const auto& group : track.takeGroups)
    {
        if (row.main.getHeight() >= 24 && chipBounds(group, row).contains(p) && !e.mods.isPopupMenu())
        {
            emit(std::make_unique<core::SetTakeLanesExpanded>(track.id, group.id, !group.expanded),
                 group.expanded ? "Hide take lanes" : "Show take lanes");
            return true;
        }
        if (at < group.start() || at >= group.end())
            continue;
        if (e.mods.isPopupMenu())
        {
            compMenu(track, group);
            return true;
        }
        if (const auto boundary = boundaryAt(group, row, p))
        {
            press_.kind = PressKind::boundary;
            press_.track = track.id;
            press_.group = group.id;
            press_.anchor = press_.current = *boundary;
            press_.start = p;
            return true;
        }
    }
    return false;
}

void TakeLanes::mouseDrag(const juce::MouseEvent& e)
{
    if (press_.kind == PressKind::none)
        return;
    press_.moved = press_.moved || std::abs(e.x - press_.start.x) >= 3;
    const auto at = geometry.snap(geometry.sampleForX(e.x));
    if (at != press_.current)
    {
        press_.current = at;
        if (onRepaint)
            onRepaint();
    }
}

void TakeLanes::mouseUp(const juce::MouseEvent&)
{
    const auto press = std::exchange(press_, Press{});
    if (press.kind == PressKind::swipe)
    {
        if (press.moved && press.current != press.anchor)
            emit(std::make_unique<core::SelectTake>(press.track,
                                                    press.group,
                                                    press.take,
                                                    std::min(press.anchor, press.current),
                                                    std::max(press.anchor, press.current)),
                 "Swipe take");
        else if (!press.moved)
        {
            // A click: the take plays over the comp segment under the pointer.
            core::SamplePosition from = 0, to = 0;
            const auto* track = project_ != nullptr ? project_->findTrack(press.track) : nullptr;
            if (const auto* group = track != nullptr ? track->findTakeGroup(press.group) : nullptr)
                if (const auto* segment = core::segmentAt(*group, press.anchor))
                {
                    from = segment->start;
                    to = segment->end;
                }
            if (to > from)
                emit(std::make_unique<core::SelectTake>(press.track, press.group, press.take, from, to),
                     "Use take here");
        }
    }
    else if (press.kind == PressKind::boundary && press.current != press.anchor)
        emit(std::make_unique<core::MoveCompBoundary>(press.track, press.group, press.anchor, press.current),
             "Move comp boundary");
    if (onRepaint)
        onRepaint();
}

std::optional<juce::MouseCursor> TakeLanes::hover(juce::Point<int> p, const Row* row)
{
    core::Id group = core::invalidId;
    core::SamplePosition boundary = -1;
    std::optional<juce::MouseCursor> cursor;
    if (row != nullptr && row->track != nullptr)
    {
        if (laneAt(*row, p.y) >= 0)
            cursor = p.x < row->headerWidth ? juce::MouseCursor::PointingHandCursor
                                            : juce::MouseCursor::IBeamCursor;
        else
            for (const auto& g : row->track->takeGroups)
            {
                if (row->main.getHeight() >= 24 && chipBounds(g, *row).contains(p))
                {
                    cursor = juce::MouseCursor::PointingHandCursor;
                    break;
                }
                if (const auto b = boundaryAt(g, *row, p))
                {
                    group = g.id;
                    boundary = *b;
                    cursor = juce::MouseCursor::LeftRightResizeCursor;
                    break;
                }
            }
    }
    if (group != hoverGroup_ || boundary != hoverBoundary_)
    {
        hoverGroup_ = group;
        hoverBoundary_ = boundary;
        if (onRepaint)
            onRepaint();
    }
    return cursor;
}

bool TakeLanes::cycleTake(const core::Track& track, core::SamplePosition at, int direction)
{
    const auto* group = core::takeGroupAt(track, at);
    const auto* segment = group != nullptr ? core::segmentAt(*group, at) : nullptr;
    if (segment == nullptr || group->takes.size() < 2)
        return false;
    const int count = static_cast<int>(group->takes.size());
    int index = group->takeIndex(segment->take);
    // The next take that has audio at the position, wrapping.
    for (int step = 0; step < count - 1; ++step)
    {
        index = (index + direction + count) % count;
        const auto& clip = group->takes[static_cast<size_t>(index)].clip;
        if (at >= clip.start && at < clip.end())
        {
            emit(std::make_unique<core::SelectTake>(
                     track.id, group->id, clip.id, segment->start, segment->end),
                 direction > 0 ? "Next take" : "Previous take");
            return true;
        }
    }
    return true;
}

bool TakeLanes::toggleLanes(const core::Track& track)
{
    if (track.takeGroups.empty())
        return false;
    const bool show = laneCount(track) == 0;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& g : track.takeGroups)
        if (g.expanded != show)
            commands.push_back(std::make_unique<core::SetTakeLanesExpanded>(track.id, g.id, show));
    if (!commands.empty() && onEdit)
        onEdit(std::move(commands), show ? "Show take lanes" : "Hide take lanes");
    return true;
}

void TakeLanes::emit(std::unique_ptr<core::Command> command, const std::string& name)
{
    if (!onEdit)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::move(command));
    onEdit(std::move(commands), name);
}

void TakeLanes::laneMenu(const core::Track& track,
                         const core::TakeGroup& group,
                         const core::Take& take,
                         juce::Rectangle<int> header)
{
    juce::PopupMenu menu;
    menu.addSectionHeader(takeBadge(take) + "  " + juce::String::fromUTF8(take.clip.name.c_str()));
    menu.addItem(1, "Use whole take");
    menu.addItem(2, "Rename take...");
    menu.addItem(3, "Move take to new track");
    menu.addItem(4, "Delete take");
    menu.addSeparator();
    menu.addItem(5, "Flatten comp");
    menu.addItem(6, "Hide take lanes");
    const auto trackId = track.id, groupId = group.id, takeId = take.id();
    const core::Take copy = take;
    juce::Component::SafePointer<juce::Component> safe(&owner_);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&owner_).withTargetScreenArea(
            owner_.localAreaToGlobal(header.withHeight(1).translated(0, header.getHeight() / 2))),
        [this, safe, trackId, groupId, takeId, copy, header](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            switch (choice)
            {
                case 1:
                    emit(std::make_unique<core::SelectTake>(trackId, groupId, takeId), "Use take");
                    break;
                case 2:
                    rename(trackId, groupId, copy, header);
                    break;
                case 3:
                    if (project_ != nullptr && onEdit)
                        onEdit(core::moveTakeToNewTrack(*project_, trackId, groupId, takeId),
                               "Move take to new track");
                    break;
                case 4:
                    emit(std::make_unique<core::RemoveTake>(trackId, groupId, takeId), "Delete take");
                    break;
                case 5:
                    emit(std::make_unique<core::FlattenComp>(trackId, groupId), "Flatten comp");
                    break;
                case 6:
                    emit(std::make_unique<core::SetTakeLanesExpanded>(trackId, groupId, false),
                         "Hide take lanes");
                    break;
                default:
                    break;
            }
        });
}

void TakeLanes::compMenu(const core::Track& track, const core::TakeGroup& group)
{
    juce::PopupMenu menu;
    menu.addItem(1, group.expanded ? "Hide take lanes" : "Show take lanes");
    juce::PopupMenu takes;
    for (size_t i = group.takes.size(); i-- > 0;)
    {
        const auto& take = group.takes[i];
        const bool whole = group.comp.size() == 1 && group.comp.front().take == take.id();
        takes.addItem(100 + static_cast<int>(i),
                      takeBadge(take) + "  " + juce::String::fromUTF8(take.clip.name.c_str()),
                      true,
                      whole);
    }
    menu.addSubMenu("Use take", takes);
    menu.addSeparator();
    menu.addItem(2, "Flatten comp");
    const auto trackId = track.id, groupId = group.id;
    const bool expanded = group.expanded;
    std::vector<core::Id> ids;
    for (const auto& take : group.takes)
        ids.push_back(take.id());
    juce::Component::SafePointer<juce::Component> safe(&owner_);
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [this, safe, trackId, groupId, expanded, ids](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               emit(std::make_unique<core::SetTakeLanesExpanded>(trackId, groupId, !expanded),
                                    expanded ? "Hide take lanes" : "Show take lanes");
                           else if (choice == 2)
                               emit(std::make_unique<core::FlattenComp>(trackId, groupId), "Flatten comp");
                           else if (choice >= 100 && static_cast<size_t>(choice - 100) < ids.size())
                               emit(std::make_unique<core::SelectTake>(
                                        trackId, groupId, ids[static_cast<size_t>(choice - 100)]),
                                    "Use take");
                       });
}

void TakeLanes::rename(core::Id track, core::Id group, const core::Take& take, juce::Rectangle<int> area)
{
    // An inline field over the lane header: Enter commits, Escape or focus loss cancels.
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setFont(theme_.label());
    editor_->setText(juce::String::fromUTF8(take.clip.name.c_str()), false);
    editor_->setSelectAllWhenFocused(true);
    editor_->setBounds(area.reduced(theme_.spacing.xs).withHeight(theme_.sizes.controlCompact));
    const auto takeId = take.id();
    editor_->onReturnKey = [this, track, group, takeId]
    {
        const auto name = editor_->getText().trim().toStdString();
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<juce::Component>(&owner_), this]
            {
                if (safe != nullptr)
                    editor_.reset();
            });
        if (!name.empty())
            emit(std::make_unique<core::RenameTake>(track, group, takeId, name), "Rename take");
    };
    editor_->onEscapeKey = [this]
    {
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<juce::Component>(&owner_), this]
            {
                if (safe != nullptr)
                    editor_.reset();
            });
    };
    editor_->onFocusLost = editor_->onEscapeKey;
    owner_.addAndMakeVisible(*editor_);
    editor_->grabKeyboardFocus();
}

} // namespace northlight::ui
