#include "ui/strip/ModuleCard.h"

#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/strip/StripStyle.h"

#include <map>

namespace northlight::ui::strip
{

namespace
{
juce::String formatValue(const engine::ParameterInfo& info, double v)
{
    if (!info.choices.empty())
    {
        const auto i = static_cast<size_t>(
            juce::jlimit(0, static_cast<int>(info.choices.size()) - 1, static_cast<int>(std::lround(v))));
        return juce::String(info.choices[i]);
    }
    if (info.unit == "Hz")
        return formatHz(v);
    if (info.unit == "ms")
        return v >= 100.0 ? juce::String(static_cast<int>(std::lround(v))) + " ms"
                          : juce::String(v, v >= 10.0 ? 1 : 2) + " ms";
    if (info.unit == "dB")
        return juce::String(v, std::abs(v) >= 10.0 ? 1 : 1) + " dB";
    if (info.unit == ":1")
        return juce::String(v, v >= 10.0 ? 1 : 2) + ":1";
    if (info.unit == "%")
        return juce::String(static_cast<int>(std::lround(v))) + "%";
    return juce::String(v, 2);
}
} // namespace

// ----- pill toggle ------------------------------------------------------------------------------

PillToggle::PillToggle(const Theme& theme, const juce::String& text, juce::Colour onColour)
    : juce::Button(text), theme_(theme), onColour_(onColour)
{
    setClickingTogglesState(true);
}

void PillToggle::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? onColour_.withAlpha(isButtonDown ? 0.35f : 0.25f)
                   : (isMouseOver ? c.surface3 : c.surface2));
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    if (on)
    {
        g.setColour(onColour_.withAlpha(0.6f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
    }
    g.setColour(on ? onColour_ : c.textSecondary);
    g.setFont(theme_.caption());
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

// ----- collapse button --------------------------------------------------------------------------

namespace
{
// A small left chevron: collapse the card to its tab.
class CollapseButton final : public juce::Button
{
public:
    explicit CollapseButton(const Theme& theme) : juce::Button("Collapse"), theme_(theme)
    {
        setTooltip("Collapse");
    }
    void paintButton(juce::Graphics& g, bool isMouseOver, bool) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre(8.0f, 10.0f);
        juce::Path chevron;
        chevron.startNewSubPath(r.getRight() - 2.0f, r.getY());
        chevron.lineTo(r.getX() + 2.0f, r.getCentreY());
        chevron.lineTo(r.getRight() - 2.0f, r.getBottom());
        g.setColour(isMouseOver ? theme_.colours.textPrimary : theme_.colours.textTertiary);
        g.strokePath(chevron,
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    const Theme& theme_;
};
} // namespace

// ----- card -------------------------------------------------------------------------------------

ModuleCard::ModuleCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& module)
    : theme_(theme), host_(host), channelId_(channelId), moduleId_(module.id), type_(module.type),
      colour_(moduleColour(theme, module.type)), infos_(plugins::parameterInfo(module.type)),
      power_(theme, Icon::power, "Switch on"), ring_(static_cast<size_t>(historyCapacity))
{
    setOpaque(false);
    power_.setClickingTogglesState(true);
    power_.setOnColour(colour_);
    power_.onClick = [this]
    {
        if (!updating_ && host_.onCommand)
            host_.onCommand(
                std::make_unique<core::SetInsertBypass>(channelId_, moduleId_, !power_.getToggleState()));
    };
    addAndMakeVisible(power_);
    power_.setTitle(juce::String(plugins::strip::displayName(type_)) + " on");
    collapse_ = std::make_unique<CollapseButton>(theme_);
    collapse_->onClick = [this]
    {
        if (host_.onCollapse)
            host_.onCollapse(moduleId_, true);
    };
    addAndMakeVisible(*collapse_);
    // A slot that runs several models (the compressor: Clean or Opto) swaps
    // them from the header.
    if (const auto models = plugins::strip::alternatives(type_); models.size() > 1)
    {
        modelSwitch_ = std::make_unique<SegmentedControl>(theme_);
        std::vector<juce::String> labels;
        int selected = 0;
        for (size_t i = 0; i < models.size(); ++i)
        {
            labels.emplace_back(plugins::strip::modelName(models[i]));
            if (models[i] == type_)
                selected = static_cast<int>(i);
        }
        modelSwitch_->setSegments(labels);
        modelSwitch_->setSelected(selected, juce::dontSendNotification);
        modelSwitch_->setTitle("Compressor model");
        modelSwitch_->setTooltip("Clean or Opto");
        modelSwitch_->onChange = [this, models](int index)
        {
            if (!updating_ && host_.onSwapModel && index >= 0 && static_cast<size_t>(index) < models.size() &&
                models[static_cast<size_t>(index)] != type_)
                host_.onSwapModel(moduleId_, models[static_cast<size_t>(index)]);
        };
        addAndMakeVisible(*modelSwitch_);
    }
    params_.resize(infos_.size());
    for (size_t i = 0; i < infos_.size(); ++i)
        params_[i] = infos_[i].defaultValue;
    for (size_t i = 0; i < infos_.size() && i < module.params.size(); ++i)
        params_[i] = module.params[i];
    model_ = module.params;
    bypass_ = module.bypass;
    power_.setToggleState(!bypass_, juce::dontSendNotification);
}

ModuleCard::~ModuleCard()
{
    if (auto* fb = liveFeedback())
    {
        fb->setWatched(false);
        fb->setTapsEnabled(false);
    }
}

engine::ProcessorFeedback* ModuleCard::liveFeedback() const noexcept
{
    return live_ != nullptr ? live_->feedback() : nullptr;
}

double ModuleCard::sampleRate() const noexcept
{
    const auto* fb = liveFeedback();
    return fb != nullptr ? fb->sampleRate() : 48000.0;
}

double ModuleCard::tempoBpm() const noexcept
{
    const auto* fb = liveFeedback();
    return fb != nullptr ? fb->tempoBpm() : 120.0;
}

float ModuleCard::param(int index) const noexcept
{
    return index >= 0 && static_cast<size_t>(index) < params_.size() ? params_[static_cast<size_t>(index)]
                                                                     : 0.0f;
}

const engine::ParameterInfo& ModuleCard::info(int index) const
{
    static const engine::ParameterInfo none;
    return index >= 0 && static_cast<size_t>(index) < infos_.size() ? infos_[static_cast<size_t>(index)]
                                                                    : none;
}

void ModuleCard::setParam(int index, float value)
{
    if (index < 0 || static_cast<size_t>(index) >= params_.size())
        return;
    const auto& i = infos_[static_cast<size_t>(index)];
    value = juce::jlimit(i.minimum, i.maximum, value);
    if (params_[static_cast<size_t>(index)] == value)
        return;
    params_[static_cast<size_t>(index)] = value;
    for (const auto& b : bindings_)
        if (b.index == index)
            updateBinding(b);
    paramsChanged();
    if (host_.onCommand)
        host_.onCommand(std::make_unique<core::SetInsertParameter>(channelId_, moduleId_, index, value));
}

void ModuleCard::beginGesture(const juce::String& name)
{
    if (host_.onGestureBegin)
        host_.onGestureBegin(name.toStdString());
}

void ModuleCard::endGesture()
{
    if (host_.onGestureEnd)
        host_.onGestureEnd();
}

void ModuleCard::refresh(const core::Insert& module)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    bool changed = module.bypass != bypass_;
    bypass_ = module.bypass;
    power_.setToggleState(!bypass_, juce::dontSendNotification);
    power_.setTooltip(bypass_ ? "Switch on" : "Switch off");
    // Only the model's changes come in: a refresh that repeats a value would
    // pull a control off what automation is playing (tick follows the live
    // processor), flipping between the two every frame.
    model_.resize(module.params.size(), 0.0f);
    for (size_t i = 0; i < params_.size() && i < module.params.size(); ++i)
        if (model_[i] != module.params[i])
        {
            model_[i] = module.params[i];
            if (params_[i] != module.params[i])
            {
                params_[i] = module.params[i];
                changed = true;
            }
        }
    insertChanged(module);
    if (!changed)
        return;
    for (const auto& b : bindings_)
        updateBinding(b);
    paramsChanged();
    repaint();
}

void ModuleCard::updateBinding(const Binding& b)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const float v = param(b.index);
    if (b.knob != nullptr)
        b.knob->setValue(v, juce::dontSendNotification);
    if (b.choice != nullptr)
        b.choice->setSelected(static_cast<int>(std::lround(v)), juce::dontSendNotification);
    if (b.toggle != nullptr)
        b.toggle->setToggleState(v >= 0.5f, juce::dontSendNotification);
}

void ModuleCard::stopWatching()
{
    if (auto* fb = liveFeedback())
    {
        fb->setWatched(false);
        fb->setTapsEnabled(false);
    }
}

void ModuleCard::setCollapsed(bool collapsed)
{
    if (collapsed == collapsed_)
        return;
    collapsed_ = collapsed;
    if (collapsed_)
    {
        hiddenByCollapse_.clear();
        for (auto* child : getChildren())
            if (child != &power_ && child->isVisible())
            {
                hiddenByCollapse_.emplace_back(child);
                child->setVisible(false);
            }
        if (auto* fb = liveFeedback())
            fb->setWatched(false);
    }
    else
    {
        for (auto& child : hiddenByCollapse_)
            if (child != nullptr)
                child->setVisible(true);
        hiddenByCollapse_.clear();
    }
    setMouseCursor(collapsed_ ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    resized();
    repaint();
}

void ModuleCard::tick(double elapsedMs)
{
    const auto live = host_.liveInsert ? host_.liveInsert(moduleId_) : nullptr;
    if (live != live_)
    {
        if (auto* old = liveFeedback())
            old->setWatched(false);
        live_ = live;
        cursor_ = 0;
        if (auto* fb = liveFeedback())
            cursor_ = fb->framesWritten();
    }
    if (collapsed_)
    {
        // A collapsed card shows no levels; let a switched-off module stop measuring.
        if (auto* fb = liveFeedback())
        {
            fb->setWatched(false);
            cursor_ = fb->framesWritten();
        }
        return;
    }
    // Automation sets the processor's parameters: the controls follow.
    if (live_ != nullptr && !isMouseButtonDown(true))
    {
        bool moved = false;
        for (size_t i = 0; i < params_.size() && i < live_->parameters().size(); ++i)
        {
            const float v = live_->parameter(static_cast<int>(i));
            if (v != params_[i])
            {
                params_[i] = v;
                moved = true;
            }
        }
        if (moved)
        {
            for (const auto& b : bindings_)
                updateBinding(b);
            paramsChanged();
        }
    }
    float in = 0.0f, out = 0.0f, gr = 0.0f, extra = 0.0f;
    int n = 0;
    if (auto* fb = liveFeedback())
    {
        fb->setWatched(true);
        Frame frames[256];
        n = fb->readHistory(cursor_, frames, 256);
        for (int i = 0; i < n; ++i)
        {
            ring_[static_cast<size_t>(head_)] = frames[i];
            head_ = (head_ + 1) % historyCapacity;
            count_ = std::min(historyCapacity, count_ + 1);
            ++total_;
            in = std::max(in, frames[i].input);
            out = std::max(out, frames[i].output);
            gr = std::max(gr, frames[i].reduction);
            extra = std::max(extra, frames[i].extra);
        }
    }
    // The meters' ballistics (ui/MeterBallistics.h): levels rise over the
    // token time and fall at the token rate; gain reduction moves like a
    // compressor's needle, quick in and a third of a second out.
    const auto& motion = theme_.motion;
    const auto ballistic = [&](float& shown, float level)
    { shown = followLevel(shown, toDb(level), elapsedMs, motion.meterRiseMs, motion.meterFallDbPerSec); };
    ballistic(inDb_, in);
    ballistic(outDb_, out);
    ballistic(extraDb_, extra);
    grDb_ = followReduction(grDb_, gr, elapsedMs, motion.meterRiseMs * 0.5, 300.0);
    bool readoutsMoved = inReadout_.update(toDb(in), elapsedMs);
    readoutsMoved = outReadout_.update(toDb(out), elapsedMs) || readoutsMoved;
    readoutsMoved = grReadout_.update(gr, elapsedMs) || readoutsMoved;
    if (n > 0 || inDb_ > -119.0f || grDb_ > 0.0f || readoutsMoved)
        framesArrived(elapsedMs);
    const auto status = headerStatus();
    if (status != shownStatus_ && !embedded_)
    {
        shownStatus_ = status;
        repaint(getLocalBounds().removeFromTop(headerHeight));
    }
}

Knob& ModuleCard::addKnob(int index, const juce::String& label)
{
    const auto& i = info(index);
    auto knob = std::make_unique<Knob>(theme_);
    knob->setRange(i.minimum, i.maximum, i.defaultValue);
    knob->setLogarithmic(i.logarithmic);
    knob->setArcColour(colour_);
    knob->setShowValue(true);
    const auto copy = i;
    knob->format = [copy](double v) { return formatValue(copy, v); };
    knob->setValue(param(index), juce::dontSendNotification);
    knob->setTitle(i.name);
    knob->onDragStart = [this, index] { beginGesture(info(index).name); };
    knob->onDragEnd = [this] { endGesture(); };
    knob->onValueChanged = [this, index](double v)
    {
        if (!updating_)
            setParam(index, static_cast<float>(v));
    };
    addAndMakeVisible(*knob);
    auto text = std::make_unique<juce::Label>();
    text->setText(label.isNotEmpty() ? label : juce::String(i.name), juce::dontSendNotification);
    text->setFont(theme_.caption());
    text->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    text->setJustificationType(juce::Justification::centred);
    text->setMinimumHorizontalScale(0.75f);
    text->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*text);
    Binding b;
    b.index = index;
    b.knob = knob.get();
    b.label = text.get();
    bindings_.push_back(b);
    auto& result = *knob;
    owned_.push_back(std::move(knob));
    owned_.push_back(std::move(text));
    return result;
}

SegmentedControl& ModuleCard::addChoice(int index)
{
    const auto& i = info(index);
    auto control = std::make_unique<SegmentedControl>(theme_);
    std::vector<juce::String> labels;
    for (const auto& c : i.choices)
        labels.emplace_back(c);
    control->setSegments(labels);
    control->setSelected(static_cast<int>(std::lround(param(index))), juce::dontSendNotification);
    control->setTitle(i.name);
    control->onChange = [this, index](int selected)
    {
        if (!updating_)
            setParam(index, static_cast<float>(selected));
    };
    addAndMakeVisible(*control);
    Binding b;
    b.index = index;
    b.choice = control.get();
    bindings_.push_back(b);
    auto& result = *control;
    owned_.push_back(std::move(control));
    return result;
}

PillToggle& ModuleCard::addToggle(int index, const juce::String& text)
{
    auto toggle = std::make_unique<PillToggle>(theme_, text, colour_);
    toggle->setToggleState(param(index) >= 0.5f, juce::dontSendNotification);
    toggle->setTooltip(info(index).name);
    toggle->onClick = [this, index, t = toggle.get()]
    {
        if (!updating_)
            setParam(index, t->getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(*toggle);
    Binding b;
    b.index = index;
    b.toggle = toggle.get();
    bindings_.push_back(b);
    auto& result = *toggle;
    owned_.push_back(std::move(toggle));
    return result;
}

juce::Label* ModuleCard::labelFor(const Knob& knob) const
{
    for (const auto& b : bindings_)
        if (b.knob == &knob)
            return b.label;
    return nullptr;
}

int ModuleCard::knobCellHeight() const noexcept
{
    return theme_.sizes.knob + static_cast<int>(theme_.fonts.size.value) + 4 +
           static_cast<int>(theme_.fonts.size.caption) + 4;
}

void ModuleCard::layoutKnobs(juce::Rectangle<int> area, int perRow, const std::vector<Knob*>& knobs)
{
    perRow = std::max(1, perRow);
    const int cellW = area.getWidth() / perRow;
    const int cellH = knobCellHeight();
    const int labelH = static_cast<int>(theme_.fonts.size.caption) + 4;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = static_cast<int>(i) / perRow, col = static_cast<int>(i) % perRow;
        const auto cell =
            juce::Rectangle<int>(area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
        knobs[i]->setBounds(cell.withTrimmedBottom(labelH));
        if (auto* label = labelFor(*knobs[i]))
            label->setBounds(cell.withTop(cell.getBottom() - labelH));
    }
}

// The header row under the hue line, inset like the body.
juce::Rectangle<int> ModuleCard::headerArea() const
{
    return getLocalBounds().removeFromTop(headerHeight).withTrimmedTop(hueLine).reduced(theme_.spacing.sm, 0);
}

void ModuleCard::setEmbedded(bool embedded)
{
    embedded_ = embedded;
    power_.setVisible(!embedded);
    collapse_->setVisible(!embedded);
    if (modelSwitch_ != nullptr)
        modelSwitch_->setVisible(!embedded && !collapsed_);
    resized();
    repaint();
}

void ModuleCard::paint(juce::Graphics& g)
{
    if (embedded_)
        return; // the frame draws the surface and the header
    const auto& c = theme_.colours;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(c.surface1);
    g.fillRoundedRectangle(bounds, theme_.radius.md);
    g.setColour(c.lineSubtle);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
    if (collapsed_)
    {
        // The tab: the hue line, then the name running down under the power button.
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setColour(bypass_ ? colour_.withAlpha(0.25f) : colour_);
        g.fillRect(bounds.withHeight(static_cast<float>(hueLine)));
        const auto name = juce::String(plugins::strip::displayName(type_));
        g.setFont(theme_.label());
        g.setColour(bypass_ ? c.textSecondary : c.textPrimary);
        const float x = bounds.getCentreX();
        const float y = static_cast<float>(headerHeight + theme_.spacing.sm);
        g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::halfPi, x, y));
        g.drawText(name,
                   juce::Rectangle<float>(x, y - 10.0f, bounds.getHeight() - y - 8.0f, 20.0f),
                   juce::Justification::centredLeft);
        return;
    }
    // The hue line along the top: full when on, faint when off.
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setColour(bypass_ ? colour_.withAlpha(0.25f) : colour_);
        g.fillRect(bounds.withHeight(static_cast<float>(hueLine)));
    }
    auto header = headerArea();
    header.removeFromLeft(theme_.sizes.controlCompact + theme_.spacing.xs);
    header.removeFromRight(16 + theme_.spacing.xs); // the collapse chevron
    if (shownStatus_.isNotEmpty())
    {
        g.setFont(theme_.value());
        g.setColour(headerStatusColour());
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), shownStatus_) + 4;
        g.drawText(shownStatus_, header.removeFromRight(w), juce::Justification::centredRight);
    }
    g.setFont(theme_.label());
    g.setColour(bypass_ ? c.textSecondary : c.textPrimary);
    g.drawText(headerName(), header, juce::Justification::centredLeft, true);
}

juce::String ModuleCard::headerName() const
{
    const auto& type = modelSwitch_ != nullptr ? plugins::strip::slotOf(type_) : type_;
    return juce::String(plugins::strip::displayName(type));
}

void ModuleCard::paintOverChildren(juce::Graphics& g)
{
    if (!bypass_ || collapsed_ || embedded_)
        return;
    // Off: the body dims (and stays usable).
    auto body = getLocalBounds().withTrimmedTop(headerHeight).toFloat().reduced(1.0f);
    g.setColour(theme_.colours.surface1.withAlpha(0.42f));
    g.fillRoundedRectangle(body, theme_.radius.md);
}

void ModuleCard::resized()
{
    if (embedded_)
    {
        layoutBody(getLocalBounds().reduced(theme_.spacing.sm, theme_.spacing.xs));
        return;
    }
    auto header = headerArea();
    if (collapsed_)
    {
        power_.setBounds(
            header.withSizeKeepingCentre(theme_.sizes.controlCompact, theme_.sizes.controlCompact));
        return;
    }
    power_.setBounds(header.removeFromLeft(theme_.sizes.controlCompact)
                         .withSizeKeepingCentre(theme_.sizes.controlCompact, theme_.sizes.controlCompact));
    collapse_->setBounds(header.removeFromRight(16));
    if (modelSwitch_ != nullptr)
    {
        // Just after the name.
        header.removeFromLeft(theme_.spacing.xs);
        header.removeFromLeft(juce::GlyphArrangement::getStringWidthInt(theme_.label(), headerName()) +
                              theme_.spacing.sm);
        modelSwitch_->setBounds(
            header.removeFromLeft(std::min(header.getWidth(), modelSwitch_->preferredWidth()))
                .withSizeKeepingCentre(std::min(header.getWidth(), modelSwitch_->preferredWidth()),
                                       theme_.sizes.controlCompact - 2));
    }
    layoutBody(getLocalBounds().withTrimmedTop(headerHeight).reduced(theme_.spacing.sm, theme_.spacing.xs));
}

void ModuleCard::mouseDown(const juce::MouseEvent& e)
{
    if (collapsed_)
    {
        if (host_.onCollapse)
            host_.onCollapse(moduleId_, false);
        return;
    }
    dragging_ = !embedded_ && e.y < headerHeight && e.mods.isLeftButtonDown();
}

void ModuleCard::mouseMove(const juce::MouseEvent& e)
{
    if (!collapsed_ && !embedded_)
        setMouseCursor(e.y < headerHeight ? juce::MouseCursor::DraggingHandCursor
                                          : juce::MouseCursor::NormalCursor);
}

void ModuleCard::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging_ && e.mouseWasDraggedSinceMouseDown() && host_.onReorder)
        host_.onReorder(moduleId_, e.getScreenPosition(), false);
}

void ModuleCard::mouseUp(const juce::MouseEvent& e)
{
    if (dragging_ && e.mouseWasDraggedSinceMouseDown() && host_.onReorder)
        host_.onReorder(moduleId_, e.getScreenPosition(), true);
    dragging_ = false;
}

} // namespace northlight::ui::strip
