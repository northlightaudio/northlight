#include "ui/arrange/MarkerController.h"

#include "core/Command.h"

#include <algorithm>

namespace northlight::ui
{

MarkerController::MarkerController(juce::Component& owner, const Theme& theme, const TimelineViewport& view)
    : owner_(owner), theme_(theme), view_(view)
{
}

void MarkerController::setProject(const core::Project* project)
{
    project_ = project;
    if (project_ == nullptr || project_->findMarker(selected_) == nullptr)
        selected_ = core::invalidId;
}

core::SamplePosition MarkerController::shownPosition(const core::Marker& marker) const noexcept
{
    return dragging_ && marker.id == dragged_.id ? preview_ : marker.position;
}

juce::Rectangle<int> MarkerController::flagBounds(const core::Marker& marker) const noexcept
{
    const auto position = shownPosition(marker);
    const int x = view_.xOf(position);
    const int nameWidth = 10 +
                          juce::GlyphArrangement::getStringWidthInt(
                              theme_.caption(), juce::String::fromUTF8(marker.name.c_str())) +
                          6;
    const int width =
        marker.isRegion() ? std::max(nameWidth, view_.xOf(position + marker.length) - x) : nameWidth;
    return {x, 0, std::max(12, std::min(width, 240)), rowHeight};
}

const core::Marker* MarkerController::markerAt(juce::Point<int> p) const noexcept
{
    if (project_ == nullptr || p.y >= rowHeight)
        return nullptr;
    // Later markers draw on top: test them first.
    for (auto it = project_->markers.rbegin(); it != project_->markers.rend(); ++it)
        if (flagBounds(*it).contains(p))
            return &*it;
    return nullptr;
}

void MarkerController::paintFlags(juce::Graphics& g, juce::Rectangle<int> row, int left, int right) const
{
    if (project_ == nullptr)
        return;
    const auto& c = theme_.colours;
    g.saveState();
    g.reduceClipRegion(left, row.getY(), right - left, row.getHeight());
    g.setFont(theme_.caption());
    for (size_t i = 0; i < project_->markers.size(); ++i)
    {
        const auto& m = project_->markers[i];
        const auto flag = flagBounds(m);
        if (flag.getRight() < left || flag.getX() > owner_.getWidth())
            continue;
        const bool selected = m.id == selected_;
        const bool hovered = m.id == hover_;
        const auto fill = selected ? c.accent : c.markerFlag;
        if (m.isRegion())
        {
            g.setColour(c.regionFill);
            g.fillRect(flag);
        }
        // The flag: a triangle on a stem, 10 px.
        juce::Path shape;
        const float x = static_cast<float>(flag.getX()) + 0.5f;
        const float top = static_cast<float>(row.getY()) + 2.0f;
        shape.addTriangle(x, top, x + 7.0f, top + 4.0f, x, top + 8.0f);
        shape.addRectangle(x - 0.5f, top, 1.0f, static_cast<float>(row.getHeight()) - 2.0f);
        g.setColour(fill);
        g.fillPath(shape);
        // The name, clipped at the next marker.
        int clipRight = flag.getRight();
        if (!m.isRegion() && i + 1 < project_->markers.size())
            clipRight = std::min(clipRight, flagBounds(project_->markers[i + 1]).getX() - 2);
        g.setColour(selected ? c.accent : hovered ? c.textPrimary : c.textSecondary);
        g.drawText(
            juce::String::fromUTF8(m.name.c_str()),
            juce::Rectangle<int>(
                flag.getX() + 10, row.getY(), std::max(0, clipRight - flag.getX() - 10), row.getHeight()),
            juce::Justification::centredLeft,
            true);
    }
    g.restoreState();
}

void MarkerController::paintLines(juce::Graphics& g, juce::Rectangle<int> lanes) const
{
    if (project_ == nullptr)
        return;
    const auto& c = theme_.colours;
    g.saveState();
    g.reduceClipRegion(lanes);
    for (const auto& m : project_->markers)
    {
        const auto position = shownPosition(m);
        const int x = view_.xOf(position);
        g.setColour((m.id == selected_ ? c.accent : c.markerFlag).withAlpha(0.5f));
        g.fillRect(x, lanes.getY(), 1, lanes.getHeight());
        if (m.isRegion())
        {
            const int x1 = view_.xOf(position + m.length);
            g.setColour(c.regionFill.withMultipliedAlpha(0.5f));
            g.fillRect(x, lanes.getY(), std::max(1, x1 - x), lanes.getHeight());
            g.setColour(c.markerFlag.withAlpha(0.35f));
            g.fillRect(x1, lanes.getY(), 1, lanes.getHeight());
        }
    }
    g.restoreState();
}

bool MarkerController::setHover(core::Id marker) noexcept
{
    if (marker == hover_)
        return false;
    hover_ = marker;
    return true;
}

void MarkerController::press(const core::Marker& marker, core::SamplePosition at)
{
    selected_ = marker.id;
    dragged_ = marker;
    preview_ = marker.position;
    dragStartAt_ = at;
    dragging_ = true;
    if (onLocate)
        onLocate(marker.position);
}

void MarkerController::dragTo(core::SamplePosition at)
{
    const auto wanted = dragged_.position + (at - dragStartAt_);
    preview_ = snap ? snap(wanted) : std::max<core::SamplePosition>(0, wanted);
}

void MarkerController::finishDrag(bool moved)
{
    dragging_ = false;
    if (!moved || project_ == nullptr || project_->findMarker(dragged_.id) == nullptr ||
        preview_ == dragged_.position)
        return;
    emit(std::make_unique<core::SetMarker>(dragged_.id, preview_, dragged_.length, dragged_.name),
         "Move marker");
}

core::Id MarkerController::addMarker(core::SamplePosition at,
                                     bool region,
                                     core::SamplePosition regionStart,
                                     core::SamplePosition regionEnd)
{
    if (project_ == nullptr || !onEdit)
        return core::invalidId;
    core::Marker marker;
    if (region && regionEnd > regionStart)
    {
        marker.position = regionStart;
        marker.length = regionEnd - regionStart;
        marker.name = "Region " + std::to_string(project_->markers.size() + 1);
    }
    else
    {
        marker.position = at;
        marker.name = "Marker " + std::to_string(project_->markers.size() + 1);
    }
    auto command = std::make_unique<core::AddMarker>(marker);
    const auto* added = command.get(); // the undo history keeps it alive
    emit(std::move(command), region ? "Add region" : "Add marker");
    const auto id = added->markerId(); // allocated on apply
    if (project_->findMarker(id) == nullptr)
        return core::invalidId;
    selected_ = id;
    return id;
}

void MarkerController::jump(int direction, core::SamplePosition at)
{
    if (project_ == nullptr || !onLocate)
        return;
    const auto* target = direction < 0 ? project_->markerBefore(at) : project_->markerAfter(at);
    if (target == nullptr)
    {
        if (direction < 0)
            onLocate(0);
        return;
    }
    selected_ = target->id;
    onLocate(target->position);
}

bool MarkerController::removeSelected()
{
    if (selected_ == core::invalidId || project_ == nullptr || project_->findMarker(selected_) == nullptr)
        return false;
    const auto id = std::exchange(selected_, core::invalidId);
    emit(std::make_unique<core::RemoveMarker>(id), "Remove marker");
    return true;
}

void MarkerController::rename(core::Id markerId)
{
    const auto* marker = project_ != nullptr ? project_->findMarker(markerId) : nullptr;
    if (marker == nullptr)
        return;
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setFont(theme_.caption());
    editor_->setText(juce::String::fromUTF8(marker->name.c_str()), false);
    editor_->setSelectAllWhenFocused(true);
    const auto flag = flagBounds(*marker);
    editor_->setBounds(flag.getX() + 8, 0, std::max(80, flag.getWidth()), rowHeight);
    const core::Marker copy = *marker;
    editor_->onReturnKey = [this, copy]
    {
        const auto name = editor_->getText().trim().toStdString();
        editor_.reset();
        if (name.empty() || name == copy.name)
            return;
        emit(std::make_unique<core::SetMarker>(copy.id, copy.position, copy.length, name), "Rename marker");
    };
    editor_->onEscapeKey = [this] { editor_.reset(); };
    editor_->onFocusLost = editor_->onEscapeKey;
    owner_.addAndMakeVisible(*editor_);
    editor_->grabKeyboardFocus();
}

void MarkerController::emit(std::unique_ptr<core::Command> command, const std::string& name)
{
    if (!onEdit)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::move(command));
    onEdit(std::move(commands), name);
}

} // namespace northlight::ui
