#include "ui/AutomationOverlay.h"

#include "plugins/BuiltIns.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/Db.h"

#include <cmath>

namespace aural::ui
{

namespace
{
constexpr float gainTopDb = 6.0f, gainBottomDb = -60.0f;
constexpr int chipHeight = 18;
constexpr int pointRadius = 4;

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

} // namespace

const std::vector<engine::ParameterInfo>* AutomationOverlay::parametersOf(const core::Insert& insert) const
{
    if (isHosted(insert.type))
        return hostedParameters ? hostedParameters(insert.id) : nullptr;
    return &plugins::parameterInfo(insert.type);
}

const engine::ParameterInfo* AutomationOverlay::infoFor(const core::Channel& track, const Key& key) const
{
    const auto* insert = track.findInsert(key.insert);
    if (insert == nullptr)
        return nullptr;
    const auto* infos = parametersOf(*insert);
    return infos != nullptr && key.parameter >= 0 && static_cast<size_t>(key.parameter) < infos->size()
               ? &(*infos)[static_cast<size_t>(key.parameter)]
               : nullptr;
}

AutomationOverlay::AutomationOverlay(const Theme& theme) : theme_(theme)
{
    setInterceptsMouseClicks(true, false);
    setRepaintsOnMouseActivity(false);
}

void AutomationOverlay::setProject(const core::Project* project)
{
    project_ = project;
    // Forget tracks that are gone.
    if (project_ != nullptr)
        std::erase_if(shown_, [this](core::Id id) { return project_->findChannel(id) == nullptr; });
    repaint();
}

void AutomationOverlay::setShown(core::Id trackId, bool shown)
{
    if (shown)
        shown_.insert(trackId);
    else
        shown_.erase(trackId);
    repaint();
}

AutomationOverlay::Key AutomationOverlay::keyFor(core::Id trackId) const
{
    const auto it = keys_.find(trackId);
    return it != keys_.end() ? it->second : Key{};
}

void AutomationOverlay::setKey(core::Id trackId, Key key)
{
    keys_[trackId] = key;
    repaint();
}

int AutomationOverlay::rowOfTrack(core::Id trackId) const noexcept
{
    if (project_ == nullptr || !geometry.rowCount)
        return -1;
    for (int i = 0; i < geometry.rowCount(); ++i)
        if (geometry.channelAt(i)->id == trackId)
            return i;
    return -1;
}

int AutomationOverlay::rowAt(int y) const noexcept
{
    if (project_ == nullptr || !geometry.rowBounds || !geometry.rowCount)
        return -1;
    for (int i = 0; i < geometry.rowCount(); ++i)
        if (geometry.rowBounds(i).getVerticalRange().contains(y))
            return i;
    return -1;
}

juce::Rectangle<int> AutomationOverlay::laneArea(int row) const
{
    const auto lanes = geometry.lanesArea();
    auto r = geometry.rowBounds(row);
    r.setLeft(lanes.getX());
    r.setRight(lanes.getRight());
    return r.reduced(0, 3);
}

juce::Rectangle<int> AutomationOverlay::parameterChip(int row) const
{
    const auto r = geometry.rowBounds(row);
    return {geometry.lanesArea().getX() + 6, r.getY() + 4, 132, chipHeight};
}

juce::Rectangle<int> AutomationOverlay::modeChip(int row) const
{
    const auto p = parameterChip(row);
    return {p.getRight() + 4, p.getY(), 54, chipHeight};
}

bool AutomationOverlay::hitTest(int x, int y)
{
    if (project_ == nullptr || shown_.empty() || !geometry.lanesArea || !geometry.lanesArea().contains(x, y))
        return false;
    const int row = rowAt(y);
    return row >= 0 && shown_.contains(geometry.channelAt(row)->id);
}

const core::AutomationLane* AutomationOverlay::laneFor(const core::Channel& track, const Key& key) const
{
    return track.findLane(key.target, key.insert, key.parameter);
}

float AutomationOverlay::toNorm(const core::Channel& track, const Key& key, float value) const
{
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            if (value <= 1.0e-6f)
                return 0.0f;
            return juce::jlimit(
                0.0f, 1.0f, (20.0f * std::log10(value) - gainBottomDb) / (gainTopDb - gainBottomDb));
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

float AutomationOverlay::fromNorm(const core::Channel& track, const Key& key, float norm) const
{
    norm = juce::jlimit(0.0f, 1.0f, norm);
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            if (norm <= 0.002f)
                return 0.0f;
            return std::pow(10.0f, (gainBottomDb + norm * (gainTopDb - gainBottomDb)) / 20.0f);
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

float AutomationOverlay::staticValue(const core::Channel& track, const Key& key) const
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

juce::String AutomationOverlay::keyName(const core::Channel& track, const Key& key) const
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

juce::String AutomationOverlay::valueText(const core::Channel& track, const Key& key, float value) const
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
                return juce::String(value, std::abs(value) >= 100.0f ? 0 : 2) +
                       (info->unit.empty() ? juce::String() : " " + juce::String(info->unit));
            }
            return juce::String(value, 2);
    }
    return {};
}

int AutomationOverlay::pointAt(const core::Channel& track, int row, juce::Point<int> p) const
{
    const auto key = keyFor(track.id);
    const auto* lane = laneFor(track, key);
    if (lane == nullptr)
        return -1;
    const auto area = laneArea(row);
    int best = -1;
    int bestDistance = pointRadius + 4;
    for (size_t i = 0; i < lane->points.size(); ++i)
    {
        const auto& pt = lane->points[i];
        const int x = geometry.xForSample(pt.position);
        const int y = area.getBottom() - static_cast<int>(toNorm(track, key, pt.value) * area.getHeight());
        const int d = std::max(std::abs(x - p.x), std::abs(y - p.y));
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void AutomationOverlay::paint(juce::Graphics& g)
{
    if (project_ == nullptr || shown_.empty() || !geometry.lanesArea)
        return;
    const auto& c = theme_.colours;
    const auto lanes = geometry.lanesArea();
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(lanes);
    for (int row = 0; row < geometry.rowCount(); ++row)
    {
        const auto& track = *geometry.channelAt(row);
        if (!shown_.contains(track.id))
            continue;
        const auto rowArea = geometry.rowBounds(row).getIntersection(lanes);
        if (rowArea.isEmpty())
            continue;
        const auto area = laneArea(row);
        const float h = static_cast<float>(area.getHeight());
        // The clips step back under the lane.
        g.setColour(c.canvas.withAlpha(0.55f));
        g.fillRect(rowArea);
        const auto key = keyFor(track.id);
        const auto* lane = laneFor(track, key);
        const bool dragging = dragTrack_ == track.id && dragIndex_ >= 0;
        const auto& points = dragging ? dragPoints_ : (lane != nullptr ? lane->points : dragPoints_);
        const bool hasPoints = (dragging || lane != nullptr) && !points.empty();
        const auto yOf = [&](float value)
        { return static_cast<float>(area.getBottom()) - toNorm(track, key, value) * h; };
        const bool active = track.automationMode != core::AutomationMode::off;
        const auto colour = active ? c.automation : c.textTertiary;
        if (hasPoints)
        {
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
                const float x = static_cast<float>(geometry.xForSample(points[i].position));
                if (x < area.getX() - 8 || x > area.getRight() + 8)
                    continue;
                const bool hot = (dragging && static_cast<int>(i) == dragIndex_) ||
                                 (!dragging && hoverRow_ == row && hoverPoint_ == static_cast<int>(i));
                const float r = hot ? pointRadius + 1.5f : static_cast<float>(pointRadius);
                const auto at =
                    juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre({x, yOf(points[i].value)});
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
                const auto text = valueText(track, key, p.value);
                g.setFont(theme_.value());
                const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 10;
                const auto at =
                    juce::Point<int>(geometry.xForSample(p.position), static_cast<int>(yOf(p.value)));
                auto box = juce::Rectangle<int>(w, 16).withCentre(
                    at.translated(0, at.y - area.getY() > 22 ? -16 : 16));
                g.setColour(c.surface3);
                g.fillRoundedRectangle(box.toFloat(), theme_.radius.sm);
                g.setColour(c.textPrimary);
                g.drawText(text, box, juce::Justification::centred);
            }
        }
        else
        {
            // No lane yet: the static value as a dashed line.
            const float y = yOf(staticValue(track, key));
            const float dashes[] = {4.0f, 4.0f};
            g.setColour(colour.withAlpha(0.6f));
            g.drawDashedLine(
                juce::Line<float>(static_cast<float>(area.getX()), y, static_cast<float>(area.getRight()), y),
                dashes,
                2,
                1.0f);
            if (area.getHeight() >= 40)
            {
                g.setFont(theme_.caption());
                g.setColour(c.textTertiary);
                g.drawText("Click to add a point",
                           area.withLeft(modeChip(row).getRight() + 10).withHeight(chipHeight + 8),
                           juce::Justification::centredLeft);
            }
        }
        // The chips.
        const auto chip = [&](juce::Rectangle<int> r, const juce::String& text, juce::Colour textColour)
        {
            g.setColour(c.surface2.withAlpha(0.95f));
            g.fillRoundedRectangle(r.toFloat(), theme_.radius.sm);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
            g.setFont(theme_.caption());
            g.setColour(textColour);
            g.drawText(text + juce::String::fromUTF8(" \xe2\x96\xbe"),
                       r.reduced(6, 0),
                       juce::Justification::centredLeft,
                       true);
        };
        chip(parameterChip(row), keyName(track, key), c.textPrimary);
        juce::Colour modeColour = c.textTertiary;
        switch (track.automationMode)
        {
            case core::AutomationMode::read:
                modeColour = c.success;
                break;
            case core::AutomationMode::touch:
            case core::AutomationMode::latch:
                modeColour = c.warning;
                break;
            case core::AutomationMode::write:
                modeColour = c.record;
                break;
            case core::AutomationMode::off:
                break;
        }
        chip(modeChip(row), modeName(track.automationMode), modeColour);
    }
}

void AutomationOverlay::mouseMove(const juce::MouseEvent& e)
{
    const int row = rowAt(e.y);
    int point = -1;
    if (row >= 0)
        point = pointAt(*geometry.channelAt(row), row, e.getPosition());
    const bool onChip =
        row >= 0 && (parameterChip(row).contains(e.getPosition()) || modeChip(row).contains(e.getPosition()));
    setMouseCursor(onChip       ? juce::MouseCursor::PointingHandCursor
                   : point >= 0 ? juce::MouseCursor::DraggingHandCursor
                                : juce::MouseCursor::CrosshairCursor);
    if (row != hoverRow_ || point != hoverPoint_)
    {
        hoverRow_ = row;
        hoverPoint_ = point;
        repaint();
    }
}

void AutomationOverlay::mouseExit(const juce::MouseEvent&)
{
    hoverRow_ = hoverPoint_ = -1;
    repaint();
}

void AutomationOverlay::mouseDown(const juce::MouseEvent& e)
{
    const int row = rowAt(e.y);
    if (row < 0 || project_ == nullptr)
        return;
    const auto& track = *geometry.channelAt(row);
    if (parameterChip(row).contains(e.getPosition()))
    {
        showParameterMenu(track, row);
        return;
    }
    if (modeChip(row).contains(e.getPosition()))
    {
        showModeMenu(track, row);
        return;
    }
    const auto key = keyFor(track.id);
    const auto* lane = laneFor(track, key);
    const int index = pointAt(track, row, e.getPosition());
    if (e.mods.isPopupMenu())
    {
        showPointMenu(track, row, index);
        return;
    }
    dragTrack_ = track.id;
    dragPoints_ = lane != nullptr ? lane->points : std::vector<core::AutomationPoint>{};
    if (index >= 0)
    {
        dragIndex_ = index;
        dragNew_ = false;
        dragPosition_ = dragPoints_[static_cast<size_t>(index)].position;
    }
    else
    {
        const auto area = laneArea(row);
        core::AutomationPoint p;
        p.position = std::max<core::SamplePosition>(0, geometry.snap(geometry.sampleForX(e.x)));
        p.value = fromNorm(
            track, key, static_cast<float>(area.getBottom() - e.y) / static_cast<float>(area.getHeight()));
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

void AutomationOverlay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIndex_ < 0 || project_ == nullptr)
        return;
    const auto* track = project_->findChannel(dragTrack_);
    if (track == nullptr)
        return;
    const int row = rowOfTrack(dragTrack_);
    const auto key = keyFor(dragTrack_);
    const auto area = laneArea(row);
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

void AutomationOverlay::mouseUp(const juce::MouseEvent& e)
{
    if (dragIndex_ < 0 || project_ == nullptr)
        return;
    const auto* track = project_->findChannel(dragTrack_);
    const auto key = keyFor(dragTrack_);
    auto points = std::move(dragPoints_);
    const bool isNew = dragNew_;
    dragIndex_ = -1;
    dragPoints_.clear();
    dragTrack_ = core::invalidId;
    if (track != nullptr && (isNew || e.mouseWasDraggedSinceMouseDown()))
        emitPoints(*track, key, std::move(points), isNew ? "Add automation point" : "Move automation point");
    repaint();
}

void AutomationOverlay::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int row = rowAt(e.y);
    if (row < 0 || project_ == nullptr)
        return;
    const auto& track = *geometry.channelAt(row);
    const auto key = keyFor(track.id);
    const auto* lane = laneFor(track, key);
    const int index = pointAt(track, row, e.getPosition());
    if (lane == nullptr || index < 0)
        return;
    auto points = lane->points;
    points.erase(points.begin() + index);
    emitPoints(track, key, std::move(points), "Remove automation point");
}

void AutomationOverlay::emitPoints(const core::Channel& track,
                                   const Key& key,
                                   std::vector<core::AutomationPoint> points,
                                   const std::string& name)
{
    if (!onEdit)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    if (const auto* lane = laneFor(track, key))
    {
        if (points.empty())
            commands.push_back(std::make_unique<core::RemoveAutomationLane>(track.id, lane->id));
        else
            commands.push_back(
                std::make_unique<core::SetAutomationPoints>(track.id, lane->id, std::move(points), name));
    }
    else if (!points.empty())
    {
        core::AutomationLane added;
        added.target = key.target;
        added.insert = key.insert;
        added.parameter = key.parameter;
        added.points = std::move(points);
        commands.push_back(std::make_unique<core::AddAutomationLane>(track.id, std::move(added)));
    }
    if (!commands.empty())
        onEdit(std::move(commands), name);
}

void AutomationOverlay::showParameterMenu(const core::Channel& track, int row)
{
    juce::PopupMenu menu;
    const auto current = keyFor(track.id);
    const auto has = [&](const Key& k)
    {
        const auto* lane = laneFor(track, k);
        return lane != nullptr && !lane->points.empty();
    };
    const auto label = [&](const juce::String& name, const Key& k)
    { return has(k) ? name + juce::String::fromUTF8("  \xe2\x80\xa2") : name; };
    std::vector<Key> keys;
    const auto add = [&](juce::PopupMenu& into, const juce::String& name, const Key& k)
    {
        keys.push_back(k);
        into.addItem(static_cast<int>(keys.size()), label(name, k), true, k == current);
    };
    add(menu, "Volume", Key{core::AutomationTarget::gain});
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
        menu.addSubMenu(any ? name + juce::String::fromUTF8("  \xe2\x80\xa2") : name,
                        sub,
                        true,
                        nullptr,
                        current.insert == insert->id);
    }
    menu.addSeparator();
    menu.addItem(10000, "Hide automation");
    juce::Component::SafePointer<AutomationOverlay> safe(this);
    const auto trackId = track.id;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(parameterChip(row))),
                       [safe, keys, trackId](int choice)
                       {
                           if (safe == nullptr || choice <= 0)
                               return;
                           if (choice == 10000)
                           {
                               safe->setShown(trackId, false);
                               if (safe->onShownChanged)
                                   safe->onShownChanged(trackId, false);
                               return;
                           }
                           if (static_cast<size_t>(choice) <= keys.size())
                               safe->setKey(trackId, keys[static_cast<size_t>(choice - 1)]);
                       });
}

void AutomationOverlay::showModeMenu(const core::Channel& track, int row)
{
    juce::PopupMenu menu;
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
                     track.automationMode == modes[i]);
    juce::Component::SafePointer<AutomationOverlay> safe(this);
    const auto trackId = track.id;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(modeChip(row))),
                       [safe, trackId, modes](int choice)
                       {
                           if (safe == nullptr || choice <= 0 || !safe->onEdit)
                               return;
                           std::vector<std::unique_ptr<core::Command>> commands;
                           commands.push_back(
                               std::make_unique<core::SetAutomationMode>(trackId, modes[choice - 1]));
                           safe->onEdit(std::move(commands), "Set automation mode");
                       });
}

void AutomationOverlay::showPointMenu(const core::Channel& track, int row, int point)
{
    juce::ignoreUnused(row);
    const auto key = keyFor(track.id);
    const auto* lane = laneFor(track, key);
    juce::PopupMenu menu;
    menu.addItem(1, "Delete point", lane != nullptr && point >= 0);
    menu.addItem(2, "Clear lane", lane != nullptr);
    menu.addSeparator();
    menu.addItem(3, "Hide automation");
    juce::Component::SafePointer<AutomationOverlay> safe(this);
    const auto trackId = track.id;
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safe, trackId, key, point](int choice)
                       {
                           if (safe == nullptr || choice <= 0 || safe->project_ == nullptr)
                               return;
                           const auto* t = safe->project_->findChannel(trackId);
                           if (t == nullptr)
                               return;
                           const auto* l = safe->laneFor(*t, key);
                           if (choice == 1 && l != nullptr && point >= 0 &&
                               static_cast<size_t>(point) < l->points.size())
                           {
                               auto points = l->points;
                               points.erase(points.begin() + point);
                               safe->emitPoints(*t, key, std::move(points), "Remove automation point");
                           }
                           else if (choice == 2 && l != nullptr)
                               safe->emitPoints(*t, key, {}, "Clear automation");
                           else if (choice == 3)
                           {
                               safe->setShown(trackId, false);
                               if (safe->onShownChanged)
                                   safe->onShownChanged(trackId, false);
                           }
                       });
}

} // namespace aural::ui
