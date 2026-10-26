#include "ui/pianoroll/ControllerLane.h"

#include "core/Command.h"
#include "ui/components/DragTip.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace northlight::ui
{

namespace
{
// The names of the common controllers, for the lane menu and label.
const char* controllerName(int number) noexcept
{
    switch (number)
    {
        case 0:
            return "Bank select";
        case 1:
            return "Mod wheel";
        case 2:
            return "Breath";
        case 4:
            return "Foot";
        case 5:
            return "Portamento time";
        case 7:
            return "Volume";
        case 8:
            return "Balance";
        case 10:
            return "Pan";
        case 11:
            return "Expression";
        case 64:
            return "Sustain";
        case 65:
            return "Portamento";
        case 66:
            return "Sostenuto";
        case 67:
            return "Soft pedal";
        case 71:
            return "Resonance";
        case 74:
            return "Cutoff";
        case 91:
            return "Reverb";
        case 93:
            return "Chorus";
        default:
            return "";
    }
}
} // namespace

juce::String MidiLane::name(const MidiLane& lane, bool brief)
{
    if (lane.velocity)
        return brief ? "VEL" : "Velocity";
    switch (lane.type)
    {
        case core::MidiControllerType::pitchBend:
            return brief ? "BEND" : "Pitch bend";
        case core::MidiControllerType::channelPressure:
            return brief ? "PRESS" : "Channel pressure";
        case core::MidiControllerType::polyPressure:
            return brief ? "POLY" : "Poly pressure";
        case core::MidiControllerType::controller:
            break;
    }
    if (brief)
        switch (lane.number)
        {
            case 1:
                return "MOD";
            case 11:
                return "EXPR";
            case 64:
                return "SUS";
            default:
                return "CC " + juce::String(lane.number);
        }
    const juce::String text = controllerName(lane.number);
    return "CC " + juce::String(lane.number) + (text.isEmpty() ? juce::String() : " " + text);
}

MidiLane MidiLane::forKey(const juce::String& key)
{
    const auto k = key.trim().toLowerCase();
    if (k == "bend")
        return controller(core::MidiControllerType::pitchBend);
    if (k == "pressure")
        return controller(core::MidiControllerType::channelPressure);
    if (k == "mod")
        return controller(core::MidiControllerType::controller, 1);
    if (k == "expression")
        return controller(core::MidiControllerType::controller, 11);
    if (k == "sustain")
        return controller(core::MidiControllerType::controller, 64);
    if (k.startsWith("cc") && k.substring(2).containsOnly("0123456789") && k.length() > 2)
        return controller(core::MidiControllerType::controller,
                          std::clamp(k.substring(2).getIntValue(), 0, 127));
    return velocities();
}

ControllerLane::ControllerLane(const Theme& theme) : theme_(theme), colour_(theme.colours.midiNote)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void ControllerLane::setClip(const core::Project* project, core::Id trackId, core::Id clipId)
{
    project_ = project;
    trackId_ = trackId;
    clipId_ = clipId;
    if (const auto* c = clip())
        std::erase_if(selection_, [c](core::Id id) { return c->findController(id) == nullptr; });
    else
        selection_.clear();
    repaint();
}

void ControllerLane::setLane(const MidiLane& lane)
{
    if (lane == lane_)
        return;
    lane_ = lane;
    selection_.clear();
    repaint();
}

void ControllerLane::setColour(juce::Colour colour)
{
    if (colour == colour_)
        return;
    colour_ = colour;
    repaint();
}

const core::MidiClip* ControllerLane::clip() const noexcept
{
    const auto* t = project_ != nullptr ? project_->findTrack(trackId_) : nullptr;
    return t != nullptr ? t->findMidiClip(clipId_) : nullptr;
}

TimelineViewport ControllerLane::axis() const
{
    return view ? view() : TimelineViewport{};
}

int ControllerLane::yForValue(int value) const noexcept
{
    const int low = core::midiControllerMinimum(lane_.type), high = core::midiControllerMaximum(lane_.type);
    const int top = 6, bottom = getHeight() - 4;
    const double t =
        static_cast<double>(std::clamp(value, low, high) - low) / static_cast<double>(high - low);
    return bottom - static_cast<int>(std::lround(t * (bottom - top)));
}

int ControllerLane::valueForY(int y) const noexcept
{
    const int low = core::midiControllerMinimum(lane_.type), high = core::midiControllerMaximum(lane_.type);
    const int top = 6, bottom = getHeight() - 4;
    const double t = static_cast<double>(bottom - y) / static_cast<double>(std::max(1, bottom - top));
    return std::clamp(low + static_cast<int>(std::lround(t * (high - low))), low, high);
}

core::SamplePosition ControllerLane::timeForX(int x) const noexcept
{
    const auto* c = clip();
    const auto last = c != nullptr ? std::max<core::SamplePosition>(0, c->length - 1) : 0;
    return std::clamp<core::SamplePosition>(axis().sampleAt(x), 0, last);
}

core::SamplePosition ControllerLane::drawStep() const noexcept
{
    const bool alt = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    if (snapEnabled && snapEnabled() && !alt && gridSamples)
        return gridSamples();
    return std::max<core::SamplePosition>(
        1, static_cast<core::SamplePosition>(std::llround(4.0 * axis().samplesPerPixel)));
}

std::vector<core::MidiControllerEvent> ControllerLane::events() const
{
    std::vector<core::MidiControllerEvent> out;
    if (const auto* c = clip(); c != nullptr && !lane_.velocity)
        for (const auto& e : c->controllers)
            if (e.sameLane(lane_.type, lane_.number))
                out.push_back(e);
    return out;
}

std::vector<core::MidiControllerEvent> ControllerLane::previewEvents() const
{
    auto list = events();
    if (drag_ == Drag::move && draggedFar_)
    {
        for (auto& e : list)
            if (isSelected(e.id))
            {
                e.time = std::max<core::SamplePosition>(0, e.time + moveDelta_);
                e.value = std::clamp(e.value + valueDelta_,
                                     core::midiControllerMinimum(e.type),
                                     core::midiControllerMaximum(e.type));
            }
    }
    else if (drag_ == Drag::draw && !drawPoints_.empty())
    {
        const auto from = drawPoints_.begin()->first, to = drawPoints_.rbegin()->first;
        std::erase_if(list,
                      [&](const core::MidiControllerEvent& e) { return e.time >= from && e.time <= to; });
        for (const auto& [time, value] : drawPoints_)
            list.push_back({core::invalidId, lane_.type, lane_.number, value, time});
    }
    else if (drag_ == Drag::erase)
        std::erase_if(list,
                      [&](const core::MidiControllerEvent& e)
                      { return std::find(erased_.begin(), erased_.end(), e.id) != erased_.end(); });
    std::stable_sort(list.begin(),
                     list.end(),
                     [](const core::MidiControllerEvent& a, const core::MidiControllerEvent& b)
                     { return a.time < b.time; });
    return list;
}

const core::MidiControllerEvent* ControllerLane::eventAt(juce::Point<int> p) const noexcept
{
    const auto* c = clip();
    if (c == nullptr || lane_.velocity || !getLocalBounds().contains(p))
        return nullptr;
    const auto v = axis();
    const core::MidiControllerEvent* best = nullptr;
    int bestDistance = 1 << 30;
    for (const auto& e : c->controllers)
    {
        if (!e.sameLane(lane_.type, lane_.number) || e.time < 0)
            continue;
        const int dx = std::abs(v.xOf(e.time) - p.x), dy = std::abs(yForValue(e.value) - p.y);
        if (dx > 5 || dy > 8)
            continue;
        if (dx + dy < bestDistance)
        {
            best = &e;
            bestDistance = dx + dy;
        }
    }
    return best;
}

bool ControllerLane::isSelected(core::Id id) const noexcept
{
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

juce::String ControllerLane::valueText(int value) const
{
    if (lane_.type == core::MidiControllerType::pitchBend)
        return (value > 0 ? "+" : "") + juce::String(value) + "  (" + (value > 0 ? "+" : "") +
               juce::String(value / 8192.0 * 2.0, 2) + " st)";
    if (lane_.type == core::MidiControllerType::controller && lane_.number >= 64 && lane_.number <= 69)
        return juce::String(value) + (value >= 64 ? "  (on)" : "  (off)");
    return juce::String(value);
}

void ControllerLane::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto* clipModel = clip();
    if (clipModel == nullptr)
        return;
    const auto area = getLocalBounds();
    const auto v = axis();
    const auto list = previewEvents();
    const bool bend = lane_.type == core::MidiControllerType::pitchBend;
    const int base = bend ? yForValue(0) : area.getBottom() - 4;
    // Guides: the bend's centre, or half way.
    g.setColour(c.gridBeat);
    g.fillRect(area.getX(), bend ? base : yForValue(64), area.getWidth(), 1);
    const int end = v.xOf(clipModel->length);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& e = list[i];
        if (e.time < 0)
            continue;
        const int x0 = v.xOf(e.time);
        const int x1 = i + 1 < list.size() ? v.xOf(list[i + 1].time) : end;
        if (x1 < area.getX() || x0 > area.getRight())
            continue;
        const int y = yForValue(e.value);
        // The value holds as a step until the next event.
        g.setColour(colour_.withAlpha(0.22f));
        g.fillRect(x0, std::min(y, base), std::max(1, x1 - x0), std::abs(base - y));
        g.setColour(colour_.withAlpha(0.9f));
        g.fillRect(x0, y - 1, std::max(1, x1 - x0), 2);
        if (i + 1 < list.size())
        {
            const int next = yForValue(list[i + 1].value);
            g.fillRect(x1, std::min(y, next) - 1, 1, std::abs(next - y) + 2);
        }
    }
    for (const auto& e : list)
    {
        if (e.time < 0)
            continue;
        const int x = v.xOf(e.time), y = yForValue(e.value);
        if (x < area.getX() - 4 || x > area.getRight() + 4)
            continue;
        const bool selected = e.id != core::invalidId && isSelected(e.id);
        g.setColour(colour_.withAlpha(selected ? 1.0f : 0.85f));
        g.fillRect(x - 3, y - 3, 6, 6);
        g.setColour(selected ? c.textPrimary : c.canvas);
        g.drawRect(x - 3, y - 3, 6, 6, 1);
    }
    if (drag_ == Drag::lasso && draggedFar_)
    {
        g.setColour(c.selectionFill);
        g.fillRect(lasso_);
        g.setColour(c.selectionOutline);
        g.drawRect(lasso_, 1);
    }
    if ((drag_ == Drag::move || drag_ == Drag::draw) && draggedFar_)
    {
        const int value = drag_ == Drag::move ? std::clamp(original_.value + valueDelta_,
                                                           core::midiControllerMinimum(lane_.type),
                                                           core::midiControllerMaximum(lane_.type))
                                              : drawLast_.value;
        paintDragTip(g, theme_, valueText(value), dragPoint_.translated(0, -40), getLocalBounds());
    }
}

void ControllerLane::mouseDown(const juce::MouseEvent& e)
{
    if (forwardsToOwner(e))
    {
        if (auto* owner = getParentComponent())
            owner->mouseDown(e.getEventRelativeTo(owner));
        return;
    }
    if (onPressed)
        onPressed();
    draggedFar_ = false;
    dragStart_ = dragPoint_ = e.getPosition();
    if (clip() == nullptr || e.mods.isPopupMenu())
        return;
    const auto p = e.getPosition();
    if (tool_ == Tool::erase)
    {
        drag_ = Drag::erase;
        erased_.clear();
        if (const auto* hit = eventAt(p))
            erased_.push_back(hit->id);
        repaint();
        return;
    }
    if (tool_ == Tool::draw)
    {
        drag_ = Drag::draw;
        draggedFar_ = true; // a click draws a point
        line_ = e.mods.isShiftDown();
        drawPoints_.clear();
        drawStart_ = {timeForX(p.x), valueForY(p.y)};
        drawLast_ = drawStart_;
        addSegment(drawStart_, drawStart_);
        repaint();
        return;
    }
    if (const auto* hit = eventAt(p))
    {
        if (e.mods.isShiftDown() || e.mods.isCtrlDown())
        {
            if (!isSelected(hit->id))
                selection_.push_back(hit->id);
            else if (e.mods.isCtrlDown())
                std::erase(selection_, hit->id);
        }
        else if (!isSelected(hit->id))
            selection_ = {hit->id};
        dragId_ = hit->id;
        original_ = *hit;
        moveDelta_ = 0;
        valueDelta_ = 0;
        drag_ = isSelected(hit->id) ? Drag::move : Drag::none;
        repaint();
        return;
    }
    if (!e.mods.isShiftDown())
        selection_.clear();
    drag_ = Drag::lasso;
    lasso_ = juce::Rectangle<int>(p.x, p.y, 0, 0);
    repaint();
}

void ControllerLane::mouseDrag(const juce::MouseEvent& e)
{
    if (forwardsToOwner(e))
    {
        if (auto* owner = getParentComponent())
            owner->mouseDrag(e.getEventRelativeTo(owner));
        return;
    }
    if (drag_ == Drag::none || clip() == nullptr)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    dragPoint_ = e.getPosition();
    const int x = std::clamp(e.x, 0, getWidth());
    const auto v = axis();
    const auto at = v.sampleAt(x);
    switch (drag_)
    {
        case Drag::erase:
            if (const auto* hit = eventAt(e.getPosition()))
                if (std::find(erased_.begin(), erased_.end(), hit->id) == erased_.end())
                    erased_.push_back(hit->id);
            break;
        case Drag::draw:
        {
            const Point point{timeForX(x), valueForY(e.y)};
            if (line_)
            {
                drawPoints_.clear();
                addSegment(drawStart_, point);
            }
            else
                addSegment(drawLast_, point);
            drawLast_ = point;
            break;
        }
        case Drag::move:
        {
            const auto wanted = original_.time + (at - v.sampleAt(dragStart_.x));
            moveDelta_ = (snap ? snap(wanted) : std::max<core::SamplePosition>(0, wanted)) - original_.time;
            core::SamplePosition earliest = original_.time;
            for (const auto& ev : events())
                if (isSelected(ev.id))
                    earliest = std::min(earliest, ev.time);
            moveDelta_ = std::max(moveDelta_, -earliest);
            valueDelta_ = valueForY(e.y) - original_.value;
            if (e.mods.isShiftDown()) // one axis: whichever moved more
            {
                if (std::abs(e.x - dragStart_.x) > std::abs(e.y - dragStart_.y))
                    valueDelta_ = 0;
                else
                    moveDelta_ = 0;
            }
            break;
        }
        case Drag::lasso:
            lasso_ = juce::Rectangle<int>::leftTopRightBottom(std::min(dragStart_.x, x),
                                                              std::min(dragStart_.y, e.y),
                                                              std::max(dragStart_.x, x),
                                                              std::max(dragStart_.y, e.y));
            break;
        case Drag::none:
            break;
    }
    repaint();
}

void ControllerLane::mouseUp(const juce::MouseEvent& e)
{
    if (forwardsToOwner(e))
    {
        if (auto* owner = getParentComponent())
            owner->mouseUp(e.getEventRelativeTo(owner));
        return;
    }
    const auto drag = std::exchange(drag_, Drag::none);
    switch (drag)
    {
        case Drag::draw:
            finishDraw();
            drawPoints_.clear();
            break;
        case Drag::move:
            if (draggedFar_)
                finishMove();
            else if (!e.mods.isShiftDown() && !e.mods.isCtrlDown())
                selection_ = {dragId_};
            break;
        case Drag::lasso:
            if (draggedFar_)
                finishLasso();
            break;
        case Drag::erase:
            if (!erased_.empty())
            {
                for (const auto id : erased_)
                    std::erase(selection_, id);
                emit(std::make_unique<core::EditMidiControllers>(trackId_,
                                                                 clipId_,
                                                                 std::vector<core::MidiControllerEvent>{},
                                                                 erased_,
                                                                 "Erase controller points"),
                     "Erase controller points");
            }
            erased_.clear();
            break;
        case Drag::none:
            break;
    }
    repaint();
}

void ControllerLane::mouseDoubleClick(const juce::MouseEvent& e)
{
    // With the select tool a double-click on empty lane adds a point.
    if (tool_ == Tool::select && eventAt(e.getPosition()) == nullptr)
        addAt(e.getPosition());
}

void ControllerLane::addSegment(Point from, Point to)
{
    const auto step = drawStep();
    const auto align = [step](core::SamplePosition t)
    { return core::snapToGrid(std::max<core::SamplePosition>(0, t), step); };
    const auto* c = clip();
    const auto last = c != nullptr ? std::max<core::SamplePosition>(0, c->length - 1) : 0;
    if (from.time > to.time)
        std::swap(from, to);
    const auto value = [&](core::SamplePosition t)
    {
        if (to.time == from.time)
            return to.value;
        const double f = static_cast<double>(t - from.time) / static_cast<double>(to.time - from.time);
        return static_cast<int>(std::lround(from.value + (to.value - from.value) * std::clamp(f, 0.0, 1.0)));
    };
    for (auto t = align(from.time); t <= align(to.time); t += step)
        if (t <= last)
            drawPoints_[t] = value(t);
}

void ControllerLane::finishDraw()
{
    if (clip() == nullptr || drawPoints_.empty())
        return;
    const auto from = drawPoints_.begin()->first, to = drawPoints_.rbegin()->first;
    std::vector<core::Id> remove;
    for (const auto& e : events())
        if (e.time >= from && e.time <= to)
            remove.push_back(e.id);
    std::vector<core::MidiControllerEvent> add;
    int previous = std::numeric_limits<int>::min();
    for (const auto& [time, value] : drawPoints_)
    {
        if (value == previous)
            continue; // the step holds it already
        add.push_back({core::invalidId, lane_.type, lane_.number, value, time});
        previous = value;
    }
    const auto name = line_ ? "Draw controller line" : "Draw controller";
    auto command = std::make_unique<core::EditMidiControllers>(trackId_, clipId_, add, remove, name);
    const auto* raw = command.get();
    emit(std::move(command), name);
    // The drawn points become the selection (their ids were given on apply).
    selection_.clear();
    for (const auto& e : raw->events())
        if (e.id != core::invalidId)
            selection_.push_back(e.id);
}

void ControllerLane::finishMove()
{
    const auto* c = clip();
    if (c == nullptr || (moveDelta_ == 0 && valueDelta_ == 0))
        return;
    std::vector<core::MidiControllerEvent> moved;
    for (const auto& e : events())
        if (isSelected(e.id))
        {
            auto m = e;
            m.time = std::clamp<core::SamplePosition>(
                e.time + moveDelta_, 0, std::max<core::SamplePosition>(0, c->length - 1));
            m.value = std::clamp(e.value + valueDelta_,
                                 core::midiControllerMinimum(e.type),
                                 core::midiControllerMaximum(e.type));
            moved.push_back(m);
        }
    emit(std::make_unique<core::EditMidiControllers>(
             trackId_, clipId_, std::move(moved), std::vector<core::Id>{}, "Move controller points"),
         "Move controller points");
}

void ControllerLane::finishLasso()
{
    const auto v = axis();
    for (const auto& e : events())
        if (e.time >= 0 && lasso_.expanded(3).contains(v.xOf(e.time), yForValue(e.value)) &&
            !isSelected(e.id))
            selection_.push_back(e.id);
}

void ControllerLane::addAt(juce::Point<int> p)
{
    if (clip() == nullptr || lane_.velocity)
        return;
    const auto t = timeForX(p.x);
    const auto time = snap ? snap(t) : t;
    std::vector<core::MidiControllerEvent> add{
        {core::invalidId, lane_.type, lane_.number, valueForY(p.y), time}};
    auto command = std::make_unique<core::EditMidiControllers>(
        trackId_, clipId_, add, std::vector<core::Id>{}, "Add controller point");
    const auto* raw = command.get();
    emit(std::move(command), "Add controller point");
    selection_.clear();
    if (!raw->events().empty() && raw->events()[0].id != core::invalidId)
        selection_.push_back(raw->events()[0].id);
}

void ControllerLane::selectAll()
{
    selection_.clear();
    for (const auto& e : events())
        if (e.time >= 0) // not the values a trim left before the clip
            selection_.push_back(e.id);
    repaint();
}

void ControllerLane::clearSelection()
{
    selection_.clear();
    repaint();
}

void ControllerLane::deleteSelection()
{
    if (clip() == nullptr || selection_.empty())
        return;
    auto ids = std::exchange(selection_, {});
    emit(std::make_unique<core::EditMidiControllers>(trackId_,
                                                     clipId_,
                                                     std::vector<core::MidiControllerEvent>{},
                                                     std::move(ids),
                                                     "Delete controller points"),
         "Delete controller points");
}

void ControllerLane::emit(std::unique_ptr<core::Command> command, const std::string& name)
{
    if (!onEdit)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::move(command));
    onEdit(std::move(commands), name);
}

} // namespace northlight::ui
