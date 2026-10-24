#include "ui/TempoLane.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int tagRow = 16;                // the time signature tags along the top
constexpr float handleR = 4.0f;           // a handle's radius
constexpr double minimumGap = 1.0 / 16.0; // quarters between neighbouring changes

core::TempoMap mapOf(const core::TempoState& s, double sampleRate)
{
    return core::TempoMap(sampleRate, s.bpm, s.signature, s.tempoChanges, s.meterChanges);
}

const std::vector<core::TimeSignature>& signatureChoices()
{
    static const std::vector<core::TimeSignature> choices{
        {2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4}, {3, 8}, {5, 8}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};
    return choices;
}

juce::String signatureText(core::TimeSignature s)
{
    return juce::String(s.numerator) + "/" + juce::String(s.denominator);
}

juce::String bpmText(double bpm)
{
    // Whole tempos read without decimals; others to two places.
    return std::abs(bpm - std::round(bpm)) < 0.005 ? juce::String(static_cast<int>(std::lround(bpm)))
                                                   : juce::String(bpm, 2);
}
} // namespace

TempoLane::TempoLane(const Theme& theme) : theme_(theme)
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
}

void TempoLane::setProject(const core::Project* project)
{
    project_ = project;
    if (project_ != nullptr)
        state_ = core::TempoState::of(*project_);
    if (selected_ >= static_cast<int>(state_.tempoChanges.size()))
        selected_ = -2;
    if (!editing_)
        updateRange();
    repaint();
}

void TempoLane::updateRange()
{
    const auto& s = shown();
    double low = s.bpm, high = s.bpm;
    for (const auto& c : s.tempoChanges)
    {
        low = std::min(low, c.bpm);
        high = std::max(high, c.bpm);
    }
    const double span = std::max(40.0, high - low);
    minBpm_ = std::max(1.0, low - span * 0.3);
    maxBpm_ = high + span * 0.3;
}

core::TempoState TempoLane::shown() const
{
    return editing_ ? *editing_ : state_;
}

juce::Rectangle<int> TempoLane::lane() const noexcept
{
    return getLocalBounds().withTrimmedLeft(headerWidth);
}

float TempoLane::yForBpm(double bpm) const noexcept
{
    const auto area = getLocalBounds().withTrimmedTop(tagRow + 3).withTrimmedBottom(4);
    const double t = (bpm - minBpm_) / std::max(1.0e-6, maxBpm_ - minBpm_);
    return static_cast<float>(area.getBottom()) -
           static_cast<float>(t) * static_cast<float>(area.getHeight());
}

double TempoLane::bpmForY(float y) const noexcept
{
    const auto area = getLocalBounds().withTrimmedTop(tagRow + 3).withTrimmedBottom(4);
    const double t = (static_cast<double>(area.getBottom()) - y) / std::max(1, area.getHeight());
    return minBpm_ + t * (maxBpm_ - minBpm_);
}

double TempoLane::bpmOf(const core::TempoState& state, int index) const noexcept
{
    if (index < 0)
        return state.bpm;
    return state.tempoChanges[static_cast<size_t>(index)].bpm;
}

juce::Point<float> TempoLane::handlePosition(int index) const
{
    const auto s = shown();
    if (!xForSample || project_ == nullptr)
        return {};
    if (index < 0)
        return {static_cast<float>(xForSample(0)), yForBpm(s.bpm)};
    const auto map = mapOf(s, project_->sampleRate);
    const auto& change = s.tempoChanges[static_cast<size_t>(index)];
    return {static_cast<float>(xForSample(map.positionAt(change.quarter))), yForBpm(change.bpm)};
}

std::optional<int> TempoLane::handleAt(juce::Point<int> p) const
{
    if (p.x < headerWidth)
        return std::nullopt;
    const auto s = shown();
    // The nearest within 7 px, changes before the start (they sit later).
    std::optional<int> best;
    float bestDistance = 7.0f;
    for (int i = -1; i < static_cast<int>(s.tempoChanges.size()); ++i)
    {
        const auto at = handlePosition(i);
        const float d = at.getDistanceFrom(p.toFloat());
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

std::optional<int> TempoLane::meterAt(juce::Point<int> p) const
{
    if (p.y >= tagRow || p.x < headerWidth || project_ == nullptr || !xForSample)
        return std::nullopt;
    const auto map = mapOf(shown(), project_->sampleRate);
    const auto& points = map.meterPoints();
    for (int i = static_cast<int>(points.size()) - 1; i >= 0; --i)
    {
        const int x = xForSample(map.positionAt(points[static_cast<size_t>(i)].quarter));
        if (p.x >= x && p.x <= x + 30)
            return i - 1; // -1: the song's start
    }
    return std::nullopt;
}

// ----- painting ------------------------------------------------------------------------------

void TempoLane::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto area = lane();
    g.setColour(c.laneAlt);
    g.fillRect(area);
    g.setColour(c.surface0);
    g.fillRect(0, 0, headerWidth, getHeight());
    g.setColour(c.lineSubtle);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    g.fillRect(headerWidth - 1, 0, 1, getHeight());

    // The header: the lane's name and the tempo at the playhead.
    const auto s = shown();
    const double rate = project_ != nullptr ? project_->sampleRate : 48000.0;
    const auto map = mapOf(s, rate);
    {
        auto header = juce::Rectangle<int>(0, 0, headerWidth - 1, getHeight()).reduced(theme_.spacing.sm, 0);
        g.setColour(c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText("TEMPO",
                   header.removeFromTop(getHeight() / 2).withTrimmedTop(4),
                   juce::Justification::bottomLeft);
        const auto at = playhead ? std::max<core::SamplePosition>(0, playhead()) : 0;
        const auto signature = map.signatureAt(at);
        g.setColour(c.textSecondary);
        g.setFont(theme_.caption());
        g.drawText(bpmText(map.bpmAt(at)) + " BPM   " + signatureText(signature),
                   header.withTrimmedBottom(4),
                   juce::Justification::topLeft);
    }
    if (project_ == nullptr || !xForSample || !sampleForX)
        return;

    g.saveState();
    g.reduceClipRegion(area);
    // The curve: flat between changes, gliding over ramps.
    juce::Path curve;
    const int left = area.getX();
    const int right = area.getRight();
    bool started = false;
    for (int x = left - 2; x <= right + 2; x += 2)
    {
        const auto at = std::max<core::SamplePosition>(0, sampleForX(x));
        const float y = yForBpm(map.bpmAt(at));
        if (!started)
        {
            curve.startNewSubPath(static_cast<float>(x), y);
            started = true;
        }
        else
            curve.lineTo(static_cast<float>(x), y);
    }
    juce::Path fill = curve;
    fill.lineTo(static_cast<float>(right + 2), static_cast<float>(getHeight()));
    fill.lineTo(static_cast<float>(left - 2), static_cast<float>(getHeight()));
    fill.closeSubPath();
    g.setColour(c.automation.withAlpha(0.12f));
    g.fillPath(fill);
    g.setColour(c.automation);
    g.strokePath(curve, juce::PathStrokeType(1.5f));

    // The time signature tags.
    g.setFont(theme_.caption());
    for (const auto& m : map.meterPoints())
    {
        const int x = xForSample(map.positionAt(m.quarter));
        if (x < left - 30 || x > right)
            continue;
        g.setColour(c.markerFlag);
        g.fillRect(x, 0, 1, tagRow);
        g.drawText(signatureText(m.signature), x + 3, 0, 40, tagRow, juce::Justification::centredLeft);
    }

    // The handles, with their tempos beside them where there is room.
    for (int i = -1; i < static_cast<int>(s.tempoChanges.size()); ++i)
    {
        const auto at = handlePosition(i);
        if (at.x < static_cast<float>(left) - 40.0f || at.x > static_cast<float>(right) + handleR)
            continue;
        const bool selected = i == selected_;
        const bool hovered = i == hover_ || i == dragging_;
        const float r = hovered ? handleR + 1.0f : handleR;
        g.setColour(selected ? c.selectionOutline : c.automation);
        g.fillEllipse(at.x - r, at.y - r, 2.0f * r, 2.0f * r);
        g.setColour(c.canvas);
        g.drawEllipse(at.x - r, at.y - r, 2.0f * r, 2.0f * r, 1.0f);
        const float nextX = i + 1 < static_cast<int>(s.tempoChanges.size()) ? handlePosition(i + 1).x
                                                                            : static_cast<float>(right);
        if (nextX - at.x > 44.0f || hovered || selected)
        {
            g.setColour(c.rulerText);
            const auto label = bpmText(bpmOf(s, i)) + (i >= 0 && s.tempoChanges[static_cast<size_t>(i)].ramp
                                                           ? juce::String(" ramp")
                                                           : juce::String());
            const int lineH = theme_.lineHeight(theme_.fonts.size.caption);
            const float y = at.y < static_cast<float>(getHeight()) / 2.0f + 4.0f
                                ? at.y + 3.0f
                                : at.y - 3.0f - static_cast<float>(lineH);
            g.drawText(label,
                       static_cast<int>(at.x) + 6,
                       static_cast<int>(y),
                       80,
                       lineH,
                       juce::Justification::centredLeft);
        }
    }
    // The playhead runs through the lane as it does through the tracks.
    if (playhead)
    {
        const int x = xForSample(playhead());
        if (x >= left && x < right)
        {
            g.setColour(c.playhead);
            g.fillRect(x, 0, 1, getHeight());
        }
    }
    g.restoreState();
}

// ----- editing -------------------------------------------------------------------------------

void TempoLane::commit(core::TempoState state, const std::string& name)
{
    if (state == state_)
        return;
    if (onEdit)
        onEdit(std::move(state), name);
}

void TempoLane::mouseDown(const juce::MouseEvent& e)
{
    if (project_ == nullptr || e.x < headerWidth)
        return;
    draggedFar_ = false;
    movedSideways_ = false;
    dragStart_ = e.getPosition();
    if (const auto meter = meterAt(e.getPosition()))
    {
        meterMenu(*meter, e.getScreenPosition());
        return;
    }
    if (const auto handle = handleAt(e.getPosition()))
    {
        selected_ = *handle;
        if (e.mods.isPopupMenu())
        {
            repaint();
            handleMenu(*handle, e.getScreenPosition());
            return;
        }
        dragging_ = *handle;
        editing_ = state_;
        dragStartBpm_ = bpmOf(state_, *handle);
        dragStartQuarter_ = *handle >= 0 ? state_.tempoChanges[static_cast<size_t>(*handle)].quarter : 0.0;
        repaint();
        return;
    }
    selected_ = -2;
    repaint();
    if (e.mods.isPopupMenu() && sampleForX)
        laneMenu(std::max<core::SamplePosition>(0, sampleForX(e.x)), e.getScreenPosition());
}

void TempoLane::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ == -2 || !editing_ || project_ == nullptr)
        return;
    const auto delta = e.getPosition() - dragStart_;
    if (!draggedFar_ && std::abs(delta.x) + std::abs(delta.y) < 3)
        return;
    draggedFar_ = true;
    // Up and down: the tempo, a pixel's worth of the lane's range (a tenth with Shift).
    const double perPixel = (maxBpm_ - minBpm_) / std::max(1, getHeight() - tagRow - 7);
    const double bpm =
        std::clamp(dragStartBpm_ - delta.y * perPixel * (e.mods.isShiftDown() ? 0.1 : 1.0), 1.0, 999.0);
    const double rounded =
        std::round(bpm * (e.mods.isShiftDown() ? 100.0 : 10.0)) / (e.mods.isShiftDown() ? 100.0 : 10.0);
    auto state = state_;
    if (dragging_ < 0)
        state.bpm = rounded;
    else
    {
        auto& change = state.tempoChanges[static_cast<size_t>(dragging_)];
        change.bpm = rounded;
        // Sideways: once the drag has gone 6 px across, the change moves along the
        // song, snapped to the grid of the tempo before it.
        if (!movedSideways_ && std::abs(delta.x) > 6 && std::abs(delta.x) > std::abs(delta.y))
            movedSideways_ = true;
        if (movedSideways_ && sampleForX)
        {
            auto without = state_;
            without.tempoChanges.erase(without.tempoChanges.begin() + dragging_);
            const auto before = mapOf(without, project_->sampleRate);
            auto at = std::max<core::SamplePosition>(0, sampleForX(e.x));
            if (!e.mods.isAltDown())
            {
                const double samplesPerPixel =
                    static_cast<double>(sampleForX(e.x + 100) - sampleForX(e.x)) / 100.0;
                at = before.snap(at, before.adaptiveStep(samplesPerPixel, 16, at));
            }
            const double previous =
                dragging_ > 0 ? state_.tempoChanges[static_cast<size_t>(dragging_ - 1)].quarter : 0.0;
            const double next = dragging_ + 1 < static_cast<int>(state_.tempoChanges.size())
                                    ? state_.tempoChanges[static_cast<size_t>(dragging_ + 1)].quarter
                                    : 1.0e12;
            change.quarter = std::clamp(
                before.quartersAt(static_cast<double>(at)), previous + minimumGap, next - minimumGap);
        }
        else
            change.quarter = dragStartQuarter_;
    }
    editing_ = state;
    repaint();
}

void TempoLane::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ != -2 && editing_ && draggedFar_)
    {
        const auto state = *editing_;
        editing_.reset();
        dragging_ = -2;
        commit(state, movedSideways_ ? "Move tempo change" : "Set tempo");
    }
    editing_.reset();
    dragging_ = -2;
    updateRange();
    repaint();
}

void TempoLane::mouseMove(const juce::MouseEvent& e)
{
    const auto handle = handleAt(e.getPosition());
    const int hover = handle ? *handle : -2;
    setMouseCursor(handle ? (*handle >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                          : juce::MouseCursor::UpDownResizeCursor)
                          : juce::MouseCursor::NormalCursor);
    if (hover != hover_)
    {
        hover_ = hover;
        repaint();
    }
}

void TempoLane::mouseExit(const juce::MouseEvent&)
{
    if (hover_ != -2)
    {
        hover_ = -2;
        repaint();
    }
}

void TempoLane::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (project_ == nullptr || e.x < headerWidth)
        return;
    if (const auto handle = handleAt(e.getPosition()))
        typeTempo(*handle);
    else if (sampleForX && e.y >= tagRow)
        addChange(std::max<core::SamplePosition>(0, sampleForX(e.x)));
}

bool TempoLane::keyPressed(const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected_ >= 0 &&
        selected_ < static_cast<int>(state_.tempoChanges.size()))
    {
        auto state = state_;
        state.tempoChanges.erase(state.tempoChanges.begin() + selected_);
        selected_ = -2;
        commit(state, "Delete tempo change");
        return true;
    }
    return false;
}

void TempoLane::addChange(core::SamplePosition at)
{
    if (project_ == nullptr)
        return;
    const auto map = mapOf(state_, project_->sampleRate);
    if (sampleForX && xForSample)
    {
        const int x = xForSample(at);
        const double samplesPerPixel = static_cast<double>(sampleForX(x + 100) - sampleForX(x)) / 100.0;
        at = map.snap(at, map.adaptiveStep(samplesPerPixel, 16, at));
    }
    const double quarter = map.quartersAt(static_cast<double>(at));
    if (quarter <= minimumGap)
        return;
    for (const auto& c : state_.tempoChanges)
        if (std::abs(c.quarter - quarter) < minimumGap)
            return;
    auto state = state_;
    state.tempoChanges.push_back({quarter, std::round(map.bpmAtQuarter(quarter) * 100.0) / 100.0, false});
    std::sort(state.tempoChanges.begin(),
              state.tempoChanges.end(),
              [](const core::TempoChange& a, const core::TempoChange& b) { return a.quarter < b.quarter; });
    for (int i = 0; i < static_cast<int>(state.tempoChanges.size()); ++i)
        if (std::abs(state.tempoChanges[static_cast<size_t>(i)].quarter - quarter) < 1.0e-9)
            selected_ = i;
    commit(state, "Add tempo change");
}

void TempoLane::typeTempo(int index)
{
    const auto at = handlePosition(index);
    editor_ = std::make_unique<juce::TextEditor>();
    addAndMakeVisible(*editor_);
    editor_->setInputRestrictions(7, "0123456789.");
    editor_->setText(bpmText(bpmOf(state_, index)), false);
    editor_->setBounds(
        juce::Rectangle<int>(static_cast<int>(at.x) + 6, std::max(0, static_cast<int>(at.y) - 11), 64, 22)
            .constrainedWithin(getLocalBounds()));
    editor_->selectAll();
    editor_->grabKeyboardFocus();
    juce::Component::SafePointer<TempoLane> safe(this);
    const auto finish = [safe, index](bool apply)
    {
        if (safe == nullptr || safe->editor_ == nullptr)
            return;
        const double bpm = safe->editor_->getText().getDoubleValue();
        // Removing the editor inside its own callback is unsafe: later.
        juce::MessageManager::callAsync(
            [safe]
            {
                if (safe != nullptr)
                    safe->editor_.reset();
            });
        if (!apply || bpm < 1.0 || bpm > 999.0)
            return;
        auto state = safe->state_;
        if (index < 0)
            state.bpm = bpm;
        else if (index < static_cast<int>(state.tempoChanges.size()))
            state.tempoChanges[static_cast<size_t>(index)].bpm = bpm;
        safe->commit(state, "Set tempo");
    };
    editor_->onReturnKey = [finish] { finish(true); };
    editor_->onEscapeKey = [finish] { finish(false); };
    editor_->onFocusLost = [finish] { finish(true); };
}

void TempoLane::handleMenu(int index, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Set tempo...");
    if (index >= 0)
    {
        menu.addItem(
            2, "Ramp from the previous tempo", true, state_.tempoChanges[static_cast<size_t>(index)].ramp);
        menu.addSeparator();
        menu.addItem(3, "Delete tempo change");
    }
    juce::Component::SafePointer<TempoLane> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
        [safe, index](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            auto state = safe->state_;
            if (choice == 1)
                safe->typeTempo(index);
            else if (choice == 2 && index >= 0 && index < static_cast<int>(state.tempoChanges.size()))
            {
                auto& change = state.tempoChanges[static_cast<size_t>(index)];
                change.ramp = !change.ramp;
                safe->commit(state, change.ramp ? "Ramp tempo" : "Step tempo");
            }
            else if (choice == 3 && index >= 0 && index < static_cast<int>(state.tempoChanges.size()))
            {
                state.tempoChanges.erase(state.tempoChanges.begin() + index);
                safe->selected_ = -2;
                safe->commit(state, "Delete tempo change");
            }
        });
}

void TempoLane::laneMenu(core::SamplePosition at, juce::Point<int> screen)
{
    if (project_ == nullptr)
        return;
    const auto map = mapOf(state_, project_->sampleRate);
    // A signature change goes at the start of the bar under the pointer.
    const int bar = map.barAtQuarter(map.quartersAt(static_cast<double>(at)));
    juce::PopupMenu menu;
    menu.addItem(1, "Add tempo change here");
    juce::PopupMenu meters;
    const auto current = map.signatureAtBar(bar);
    const auto& choices = signatureChoices();
    for (size_t i = 0; i < choices.size(); ++i)
        meters.addItem(100 + static_cast<int>(i), signatureText(choices[i]), true, choices[i] == current);
    menu.addSubMenu("Time signature from bar " + juce::String(bar + 1), meters);
    menu.addSeparator();
    menu.addItem(2, "Hide tempo track");
    juce::Component::SafePointer<TempoLane> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
                       [safe, at, bar](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               safe->addChange(at);
                           else if (choice == 2 && safe->onHide)
                               safe->onHide();
                           else if (choice >= 100)
                           {
                               const auto signature = signatureChoices()[static_cast<size_t>(choice - 100)];
                               auto state = safe->state_;
                               if (bar <= 0)
                                   state.signature = signature;
                               else
                               {
                                   std::erase_if(state.meterChanges,
                                                 [bar](const core::MeterChange& m) { return m.bar == bar; });
                                   state.meterChanges.push_back({bar, signature});
                                   std::sort(state.meterChanges.begin(),
                                             state.meterChanges.end(),
                                             [](const core::MeterChange& a, const core::MeterChange& b)
                                             { return a.bar < b.bar; });
                               }
                               safe->commit(state, "Set time signature");
                           }
                       });
}

void TempoLane::meterMenu(int index, juce::Point<int> screen)
{
    juce::PopupMenu menu;
    const auto current =
        index < 0 ? state_.signature : state_.meterChanges[static_cast<size_t>(index)].signature;
    const auto& choices = signatureChoices();
    for (size_t i = 0; i < choices.size(); ++i)
        menu.addItem(100 + static_cast<int>(i), signatureText(choices[i]), true, choices[i] == current);
    if (index >= 0)
    {
        menu.addSeparator();
        menu.addItem(1, "Remove time signature change");
    }
    juce::Component::SafePointer<TempoLane> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
                       [safe, index](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           auto state = safe->state_;
                           if (choice == 1 && index >= 0)
                           {
                               state.meterChanges.erase(state.meterChanges.begin() + index);
                               safe->commit(state, "Remove time signature change");
                               return;
                           }
                           const auto signature = signatureChoices()[static_cast<size_t>(choice - 100)];
                           if (index < 0)
                               state.signature = signature;
                           else
                               state.meterChanges[static_cast<size_t>(index)].signature = signature;
                           safe->commit(state, "Set time signature");
                       });
}

} // namespace northlight::ui
