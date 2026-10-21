#include "ui/panels/InspectorPanel.h"

#include "ui/Db.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/TrackIcon.h"

#include <cmath>

namespace northlight::ui
{

InspectorPanel::InspectorPanel(const Theme& theme)
    : theme_(theme), gain_(theme), start_(theme), length_(theme), clipGain_(theme), fadeIn_(theme),
      fadeOut_(theme)
{
    setOpaque(true);
    name_.setFont(theme_.label());
    name_.setJustification(juce::Justification::centredLeft);
    name_.setIndents(theme_.spacing.sm, 0);
    name_.onReturnKey = [this]
    {
        if (updating_ || !onCommand)
            return;
        if (clipId_ != core::invalidId)
            return; // clip names are edited in the arrange view
        onCommand(std::make_unique<core::SetTrackName>(trackId_, name_.getText().toStdString()));
    };
    name_.onFocusLost = [this] { name_.onReturnKey(); };
    addAndMakeVisible(name_);

    gain_.setRange(-100.0, 6.0, 0.1);
    gain_.setDecimals(1);
    gain_.setUnit("dB");
    gain_.format = [](double v) { return v <= -100.0 ? juce::String("-inf") : juce::String(v, 1); };
    gain_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Gain");
    };
    gain_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    gain_.onValueChanged = [this](double db)
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackGain>(trackId_, db::toGain(db)));
    };
    addAndMakeVisible(gain_);

    pan_.setRange(-1.0, 1.0, 0.01);
    pan_.setDoubleClickReturnValue(true, 0.0);
    pan_.setTooltip("Pan: drag, double-click to centre");
    pan_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Pan");
    };
    pan_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    pan_.onValueChange = [this]
    {
        repaint(panValue_);
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackPan>(trackId_, static_cast<float>(pan_.getValue())));
    };
    addAndMakeVisible(pan_);

    const auto toggle = [this](juce::TextButton& b, const char* semantic, const char* tip)
    {
        b.setClickingTogglesState(true);
        b.getProperties().set(NorthlightLookAndFeel::propertySemantic, semantic);
        b.setTooltip(tip);
        addAndMakeVisible(b);
    };
    toggle(mute_, "mute", "Mute  Shift+M");
    toggle(solo_, "solo", "Solo  Shift+S");
    toggle(arm_, "arm", "Arm for recording  Shift+R");
    arm_.getProperties().set(NorthlightLookAndFeel::propertyHardOn, true);
    mute_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackMute>(trackId_, mute_.getToggleState()));
    };
    solo_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackSolo>(trackId_, solo_.getToggleState()));
    };
    arm_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetTrackArmed>(trackId_, arm_.getToggleState()));
    };

    for (auto* field : {&start_, &length_})
    {
        field->setRange(0.0, 1.0e7, 0.001);
        field->setDecimals(3);
        field->setUnit("s");
        field->setEnabled(false);
        addChildComponent(*field);
    }
    clipGain_.setRange(-60.0, 24.0, 0.1);
    clipGain_.setDecimals(1);
    clipGain_.setUnit("dB");
    clipGain_.onDragStart = [this]
    {
        if (onGestureBegin)
            onGestureBegin("Clip gain");
    };
    clipGain_.onDragEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    clipGain_.onValueChanged = [this](double db)
    {
        if (!updating_ && onCommand && clipId_ != core::invalidId)
            onCommand(std::make_unique<core::SetClipGain>(trackId_, clipId_, db::toGain(db)));
    };
    addChildComponent(clipGain_);
    for (auto* field : {&fadeIn_, &fadeOut_})
    {
        field->setRange(0.0, 600000.0, 1.0);
        field->setDecimals(0);
        field->setUnit("ms");
        field->onValueChanged = [this](double)
        {
            if (updating_ || !onCommand || clipId_ == core::invalidId)
                return;
            const auto toSamples = [this](double ms)
            { return static_cast<core::SamplePosition>(std::round(ms * sampleRate() / 1000.0)); };
            onCommand(std::make_unique<core::SetClipFades>(
                trackId_, clipId_, toSamples(fadeIn_.value()), toSamples(fadeOut_.value())));
        };
        addChildComponent(*field);
    }
}

void InspectorPanel::setProject(const core::Project* project)
{
    project_ = project;
    refresh();
}

void InspectorPanel::setSelection(core::Id trackId, core::Id clipId)
{
    trackId_ = trackId;
    clipId_ = clipId;
    refresh();
}

const core::Track* InspectorPanel::track() const
{
    return project_ != nullptr ? project_->findTrack(trackId_) : nullptr;
}

const core::AudioClip* InspectorPanel::clip() const
{
    const auto* t = track();
    return t != nullptr && clipId_ != core::invalidId ? t->findClip(clipId_) : nullptr;
}

double InspectorPanel::sampleRate() const noexcept
{
    return project_ != nullptr && project_->sampleRate > 0.0 ? project_->sampleRate : 48000.0;
}

void InspectorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const auto* t = track();
    const auto* c = clip();
    const bool clipMode = c != nullptr;
    name_.setVisible(t != nullptr && !clipMode);
    gain_.setVisible(t != nullptr && !clipMode);
    pan_.setVisible(t != nullptr && !clipMode);
    mute_.setVisible(t != nullptr && !clipMode);
    solo_.setVisible(t != nullptr && !clipMode);
    arm_.setVisible(t != nullptr && !clipMode && !t->isAux() && !t->isInstrument());
    for (auto* f : {&start_, &length_, &clipGain_, &fadeIn_, &fadeOut_})
        f->setVisible(clipMode);
    if (t == nullptr)
    {
        resized();
        repaint();
        return;
    }
    if (!clipMode)
        name_.setText(juce::String::fromUTF8(t->name.c_str()), juce::dontSendNotification);
    if (clipMode)
    {
        const double sr = sampleRate();
        start_.setValue(static_cast<double>(c->start) / sr, juce::dontSendNotification);
        length_.setValue(static_cast<double>(c->length) / sr, juce::dontSendNotification);
        clipGain_.setValue(db::fromGain(c->gain), juce::dontSendNotification);
        fadeIn_.setValue(static_cast<double>(c->fadeIn) * 1000.0 / sr, juce::dontSendNotification);
        fadeOut_.setValue(static_cast<double>(c->fadeOut) * 1000.0 / sr, juce::dontSendNotification);
    }
    else
    {
        gain_.setValue(db::fromGain(t->gain), juce::dontSendNotification);
        pan_.setValue(t->pan, juce::dontSendNotification);
        mute_.setToggleState(t->mute, juce::dontSendNotification);
        solo_.setToggleState(t->solo, juce::dontSendNotification);
        arm_.setToggleState(t->armed, juce::dontSendNotification);
    }
    resized();
    repaint();
}

juce::Rectangle<int> InspectorPanel::swatchBounds(size_t index) const
{
    const int i = static_cast<int>(index);
    const int col = i % swatchPerRow_, row = i / swatchPerRow_;
    const int x = swatchArea_.getX() + static_cast<int>(std::round(col * (swatchSize_ + swatchGap_)));
    const int y = swatchArea_.getY() + row * (swatchSize_ + theme_.spacing.md);
    return {x, y, swatchSize_, swatchSize_};
}

int InspectorPanel::swatchAt(juce::Point<int> p) const
{
    if (swatchArea_.isEmpty() || clip() != nullptr)
        return -1;
    const int slop = theme_.spacing.xxs;
    for (size_t i = 0; i < theme_.track.palette.size(); ++i)
        if (swatchBounds(i).expanded(slop).contains(p))
            return static_cast<int>(i);
    return -1;
}

void InspectorPanel::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    const auto* t = track();
    if (t == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("Select a track or clip",
                   getLocalBounds().reduced(theme_.spacing.md).removeFromTop(theme_.sizes.controlLarge * 2),
                   juce::Justification::centred);
        return;
    }
    const int slot = project_ != nullptr ? project_->trackIndex(trackId_) : 0;
    const auto colour = theme_.track.forTrack(t->colour, slot);

    // Cards, the identity card with a stripe in the track's colour.
    for (size_t i = 0; i < cards_.size(); ++i)
    {
        const auto bounds = cards_[i].toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        if (i == 0)
        {
            const juce::Graphics::ScopedSaveState state(g);
            juce::Path shape;
            shape.addRoundedRectangle(bounds, theme_.radius.md);
            g.reduceClipRegion(shape);
            g.setColour(colour);
            g.fillRect(bounds.withWidth(static_cast<float>(theme_.track.headerStripeWidth)));
        }
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
    }

    for (const auto& caption : captions_)
    {
        auto bounds = caption.bounds;
        if (caption.icon)
        {
            const int size = theme_.sizes.iconSmall;
            Icons::draw(g,
                        *caption.icon,
                        bounds.removeFromLeft(size).withSizeKeepingCentre(size, size).toFloat(),
                        c.textSecondary);
            bounds.removeFromLeft(theme_.spacing.xs + theme_.spacing.xxs);
        }
        g.setColour(c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText(caption.text, bounds, juce::Justification::centredLeft);
        if (caption.aside.isNotEmpty())
        {
            g.setFont(theme_.caption());
            const int captionW =
                juce::GlyphArrangement::getStringWidthInt(theme_.captionCaps(), caption.text) +
                theme_.spacing.md;
            g.drawFittedText(
                caption.aside, bounds.withTrimmedLeft(captionW), juce::Justification::centredRight, 1);
        }
    }

    g.setFont(theme_.body());
    g.setColour(c.textSecondary);
    for (const auto& label : labels_)
        g.drawText(label.text, label.bounds, juce::Justification::centredLeft);

    if (pan_.isVisible())
    {
        const double v = pan_.getValue();
        const int percent = static_cast<int>(std::round(std::abs(v) * 100.0));
        g.setFont(theme_.value());
        g.setColour(c.textPrimary);
        g.drawText(percent == 0 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(percent),
                   panValue_,
                   juce::Justification::centredRight);
    }

    if (const auto* cl = clip())
    {
        g.setFont(theme_.label());
        g.setColour(c.textPrimary);
        g.drawFittedText(
            juce::String::fromUTF8(cl->name.c_str()), clipName_, juce::Justification::centredLeft, 1);
        return;
    }

    // Swatches: round chips; the track's colour ringed, the hovered one outlined.
    const auto& palette = theme_.track.palette;
    for (size_t i = 0; i < palette.size(); ++i)
    {
        const auto bounds = swatchBounds(i).toFloat();
        g.setColour(palette[i].value);
        g.fillEllipse(bounds);
        const bool current = palette[i].value == colour;
        if (current || static_cast<int>(i) == hoverSwatch_)
        {
            g.setColour(current ? c.textPrimary : c.textTertiary);
            g.drawEllipse(bounds.expanded(2.5f), current ? 1.5f : 1.0f);
        }
    }
}

void InspectorPanel::resized()
{
    cards_.clear();
    captions_.clear();
    labels_.clear();
    clipName_ = {};
    panValue_ = {};
    swatchArea_ = {};
    const auto* t = track();
    if (t == nullptr)
        return;
    const auto* cl = clip();
    const auto& sp = theme_.spacing;
    const int rowH = theme_.sizes.control;
    const int captionH = sp.lg;
    const int fieldW = 96;

    const auto area = getLocalBounds().reduced(sp.md);
    const int x = area.getX() + sp.md, w = area.getWidth() - 2 * sp.md;
    int y = area.getY(), cardTop = 0;
    bool firstRow = true;

    const auto open = [&](const juce::String& caption, const juce::String& aside)
    {
        cardTop = y;
        y += sp.md;
        captions_.push_back({{x, y, w, captionH}, caption, aside});
        y += captionH + sp.sm;
        firstRow = true;
    };
    const auto close = [&]
    {
        y += sp.md;
        cards_.push_back({area.getX(), cardTop, area.getWidth(), y - cardTop});
        y += sp.sm;
    };
    // A row: the label on the left (centred on the first control line), the
    // control placed by the caller on the right of the returned area.
    const auto row = [&](const juce::String& label, int height)
    {
        if (!firstRow)
            y += sp.xs;
        firstRow = false;
        const juce::Rectangle<int> r{x, y, w, height};
        if (label.isNotEmpty())
            labels_.push_back({r.withHeight(rowH), label, {}});
        y += height;
        return r;
    };

    if (cl != nullptr)
    {
        open("CLIP", juce::String::fromUTF8(t->name.c_str()));
        captions_.back().icon = Icon::fileAudio;
        clipName_ = row({}, rowH);
        close();

        open("POSITION", {});
        const int sampleW = std::min(w - 72, 120);
        start_.setBounds(row("Start", rowH).removeFromRight(sampleW));
        length_.setBounds(row("Length", rowH).removeFromRight(sampleW));
        close();

        open("LEVEL", {});
        clipGain_.setBounds(row("Gain", rowH).removeFromRight(sampleW));
        fadeIn_.setBounds(row("Fade in", rowH).removeFromRight(sampleW));
        fadeOut_.setBounds(row("Fade out", rowH).removeFromRight(sampleW));
        close();
        return;
    }

    open(juce::String(trackKindName(t->kind)).toUpperCase(), {});
    captions_.back().icon = trackIcon(t->kind);
    name_.setBounds(row({}, rowH));
    // Small chips with clear air between them: one row of twelve when the gaps
    // stay at least `minGap`, else two of six.
    const int count = static_cast<int>(theme_.track.palette.size());
    const int minGap = sp.xs + sp.xxs;
    swatchSize_ = theme_.sizes.iconSmall;
    swatchPerRow_ = std::max(1, count);
    if (count > 1 && w < count * swatchSize_ + (count - 1) * minGap)
        swatchPerRow_ = (count + 1) / 2;
    swatchGap_ = swatchPerRow_ > 1
                     ? std::min(static_cast<float>(sp.md),
                                static_cast<float>(w - swatchPerRow_ * swatchSize_) / (swatchPerRow_ - 1))
                     : 0.0f;
    const int swatchRows = (count + swatchPerRow_ - 1) / swatchPerRow_;
    y += sp.md; // the swatches stand apart from the name field
    swatchArea_ = row({}, swatchRows * swatchSize_ + (swatchRows - 1) * sp.md);
    y += sp.xs;
    close();

    open("MIX", {});
    gain_.setBounds(row("Gain", rowH).removeFromRight(fieldW));
    // Pan as a slider across the row (a knob would sit alone in the space), its
    // value at the right edge in line with the gain field's.
    {
        auto r = row("Pan", rowH);
        r.removeFromLeft(sp.xxxl + sp.md);
        panValue_ = r.removeFromRight(sp.xl + sp.md).withTrimmedRight(sp.sm);
        r.removeFromRight(sp.xs);
        pan_.setBounds(r);
    }
    y += sp.xs;
    auto toggles = row({}, rowH);
    std::vector<juce::TextButton*> visible;
    for (auto* b : {&mute_, &solo_, &arm_})
        if (b->isVisible())
            visible.push_back(b);
    const int n = static_cast<int>(visible.size());
    for (int i = 0; i < n; ++i)
    {
        const int bw = (toggles.getWidth() - (n - 1 - i) * sp.xs) / (n - i);
        visible[static_cast<size_t>(i)]->setBounds(toggles.removeFromLeft(bw));
        toggles.removeFromLeft(sp.xs);
    }
    close();
}

void InspectorPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!onCommand || track() == nullptr)
        return;
    const int i = swatchAt(e.getPosition());
    if (i >= 0)
        onCommand(std::make_unique<core::SetColour>(
            trackId_, theme_.track.palette[static_cast<size_t>(i)].value.getARGB()));
}

void InspectorPanel::mouseMove(const juce::MouseEvent& e)
{
    const int i = swatchAt(e.getPosition());
    if (i == hoverSwatch_)
        return;
    hoverSwatch_ = i;
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip(i >= 0 ? juce::String(theme_.track.palette[static_cast<size_t>(i)].name) : juce::String());
    repaint(swatchArea_.expanded(theme_.spacing.xs));
}

void InspectorPanel::mouseExit(const juce::MouseEvent&)
{
    if (hoverSwatch_ < 0)
        return;
    hoverSwatch_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(swatchArea_.expanded(theme_.spacing.xs));
}

} // namespace northlight::ui
