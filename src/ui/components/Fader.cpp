#include "ui/components/Fader.h"

#include "ui/Db.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/Accessibility.h"

#include <cmath>
#include <limits>

namespace northlight::ui
{

namespace
{
constexpr float zeroDbFraction = 0.75f;
constexpr float lowDb = -48.0f; // the bottom of the log region; below is a short run to -inf
constexpr float infFraction = 0.03f;

float dbToGain(float value) noexcept
{
    return db::toGain(value);
}

float gainToDb(float gain) noexcept
{
    return static_cast<float>(db::fromGain(gain));
}
} // namespace

// Above 0 dB the top quarter is linear in dB; below, the travel maps to dB on a
// curve that spends most of its length between -24 and 0 (Logic's feel), with a
// short run at the bottom to -inf.
float Fader::dbForFraction(float fraction, float maxDb) noexcept
{
    fraction = juce::jlimit(0.0f, 1.0f, fraction);
    if (fraction >= zeroDbFraction)
        return (fraction - zeroDbFraction) / (1.0f - zeroDbFraction) * maxDb;
    if (fraction <= infFraction)
        return minusInfinityDb;
    const float t = (fraction - infFraction) / (zeroDbFraction - infFraction); // 0..1 over -48..0
    return lowDb * (1.0f - t) * (1.0f - t); // quadratic: slow near 0, fast near the bottom
}

float Fader::fractionForDb(float db, float maxDb) noexcept
{
    if (db >= 0.0f)
        return zeroDbFraction + juce::jlimit(0.0f, 1.0f, db / maxDb) * (1.0f - zeroDbFraction);
    if (db <= minusInfinityDb || db < lowDb)
        return db <= minusInfinityDb ? 0.0f : infFraction;
    const float t = 1.0f - std::sqrt(db / lowDb);
    return infFraction + t * (zeroDbFraction - infFraction);
}

Fader::Fader(const Theme& theme) : theme_(theme)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
}

void Fader::setGain(float linear, juce::NotificationType notify)
{
    gain_ = std::max(0.0f, linear);
    repaint();
    if (notify != juce::dontSendNotification && onGainChanged)
        onGainChanged(gain_);
}

void Fader::setTrackColour(juce::Colour colour)
{
    trackColour_ = colour;
    repaint();
}

float Fader::currentDb() const noexcept
{
    return gainToDb(gain_);
}

void Fader::setDb(float db, bool notify)
{
    db = juce::jlimit(minusInfinityDb, maxDb_, db);
    gain_ = dbToGain(db);
    setTooltip(db <= minusInfinityDb ? juce::String("-inf dB") : juce::String(db, 1) + " dB");
    repaint();
    if (notify && onGainChanged)
        onGainChanged(gain_);
}

juce::Rectangle<int> Fader::trackBounds() const noexcept
{
    const int capH = theme_.sizes.faderCapHeight;
    return getLocalBounds().withTrimmedTop(capH / 2).withTrimmedBottom(capH / 2);
}

float Fader::fractionForY(int y) const noexcept
{
    const auto track = trackBounds();
    if (track.getHeight() <= 0)
        return 0.0f;
    return 1.0f - static_cast<float>(y - track.getY()) / static_cast<float>(track.getHeight());
}

int Fader::yForFraction(float fraction) const noexcept
{
    const auto track = trackBounds();
    return track.getY() +
           static_cast<int>(std::round((1.0f - fraction) * static_cast<float>(track.getHeight())));
}

void Fader::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto track = trackBounds();
    const float trackW = static_cast<float>(theme_.spacing.xs);
    const float cx = static_cast<float>(getWidth()) * 0.5f;
    auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel());
    const auto& m = theme_.material;
    // The track (a groove when the material is on) and the scale to its left.
    const auto groove = juce::Rectangle<float>(
        cx - trackW * 0.5f, static_cast<float>(track.getY()), trackW, static_cast<float>(track.getHeight()));
    if (m.enabled && lnf != nullptr)
    {
        g.setColour(m.grooveLip);
        g.fillRoundedRectangle(groove.translated(0.0f, 1.0f), trackW * 0.5f);
        g.setColour(m.groove);
        g.fillRoundedRectangle(groove, trackW * 0.5f);
        lnf->drawInset(g, groove, trackW * 0.5f, m.grooveInset);
    }
    else
    {
        g.setColour(c.surface2);
        g.fillRoundedRectangle(groove, trackW * 0.5f);
    }
    g.setFont(theme_.caption());
    // From the bottom up, so where two labels would touch on a short fader the
    // end stop keeps its label and the one above it drops (its tick stays).
    const int lineH = theme_.lineHeight(theme_.fonts.size.caption);
    int labelTop = std::numeric_limits<int>::max();
    for (const float db : {minusInfinityDb, -48.0f, -24.0f, -12.0f, -6.0f, 0.0f, maxDb_})
    {
        const int y = yForFraction(fractionForDb(db, maxDb_));
        const bool zero = db == 0.0f;
        g.setColour(zero ? c.lineStrong : c.lineSubtle);
        const int tickW = zero ? theme_.spacing.sm : theme_.spacing.xs;
        g.fillRect(static_cast<int>(cx) - tickW / 2, y, tickW, 1);
        g.setColour(c.textTertiary);
        const auto label = db <= minusInfinityDb ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x9e"))
                                                 : juce::String(static_cast<int>(db));
        const int top = y - lineH / 2;
        if (top + lineH > labelTop)
            continue;
        labelTop = top;
        g.drawText(label,
                   0,
                   top,
                   static_cast<int>(cx) - theme_.spacing.sm,
                   lineH,
                   juce::Justification::centredRight);
    }
    // The cap.
    const int capW = theme_.sizes.faderCapWidth, capH = theme_.sizes.faderCapHeight;
    const auto cap =
        juce::Rectangle<float>(cx - static_cast<float>(capW) * 0.5f,
                               static_cast<float>(yForFraction(fractionForDb(currentDb(), maxDb_))) -
                                   static_cast<float>(capH) * 0.5f,
                               static_cast<float>(capW),
                               static_cast<float>(capH));
    if (m.enabled && isEnabled())
    {
        if (!m.capShadow.empty())
        {
            g.setColour(m.capShadow.colour);
            g.fillRoundedRectangle(cap.translated(m.capShadow.x, m.capShadow.y), theme_.radius.sm);
        }
        g.setGradientFill(juce::ColourGradient::vertical(m.capTop, cap.getY(), m.capBottom, cap.getBottom()));
        g.fillRoundedRectangle(cap, theme_.radius.sm);
        g.setColour(m.capHighlight);
        g.fillRect(cap.getX() + theme_.radius.sm * 0.5f, cap.getY(), cap.getWidth() - theme_.radius.sm, 1.0f);
    }
    else
    {
        g.setColour(isEnabled() ? c.surface3 : c.surface1);
        g.fillRoundedRectangle(cap, theme_.radius.sm);
    }
    if (isMouseOverOrDragging())
    {
        g.setColour(c.textSecondary);
        g.drawRoundedRectangle(cap.reduced(0.5f), theme_.radius.sm, 1.0f);
    }
    const auto line = trackColour_.isTransparent() ? c.textPrimary : trackColour_;
    const auto lineBounds =
        juce::Rectangle<float>(cap.getX() + 2.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 4.0f, 1.0f);
    if (m.enabled && lnf != nullptr && !trackColour_.isTransparent())
        lnf->drawGlow(g, lineBounds, 0.5f, Theme::Shadow{0.0f, 0.0f, 4.0f, line.withAlpha(0.6f), false});
    g.setColour(line);
    g.fillRect(lineBounds);
    if (lnf != nullptr)
        lnf->drawFocusRing(g, *this, cap, theme_.radius.sm);
}

void Fader::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStartDb_ = currentDb();
    dragStartY_ = e.y;
    dragging_ = true;
    if (onDragStart)
        onDragStart();
    // A click away from the cap jumps there.
    const int capY = yForFraction(fractionForDb(currentDb(), maxDb_));
    if (std::abs(e.y - capY) > theme_.sizes.faderCapHeight)
    {
        setDb(dbForFraction(fractionForY(e.y), maxDb_), true);
        dragStartDb_ = currentDb();
    }
}

void Fader::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    const float startFraction = fractionForDb(dragStartDb_, maxDb_);
    const float scale = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const float delta = -static_cast<float>(e.y - dragStartY_) /
                        static_cast<float>(std::max(1, trackBounds().getHeight())) * scale;
    setDb(dbForFraction(startFraction + delta, maxDb_), true);
}

void Fader::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onDragEnd)
        onDragEnd();
}

void Fader::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onDragStart)
        onDragStart();
    setDb(0.0f, true);
    if (onDragEnd)
        onDragEnd();
}

void Fader::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    stepDb((wheel.deltaY > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.1f : 1.0f));
}

void Fader::stepDb(float delta)
{
    if (onDragStart)
        onDragStart();
    setDb(std::max(lowDb - 1.0f, currentDb()) + delta, true);
    if (onDragEnd)
        onDragEnd();
}

bool Fader::keyPressed(const juce::KeyPress& key)
{
    const float step = key.getModifiers().isShiftDown() ? 0.1f : 1.0f;
    if (key == juce::KeyPress::upKey)
        stepDb(step);
    else if (key == juce::KeyPress::downKey)
        stepDb(-step);
    else if (key == juce::KeyPress::homeKey)
        stepDb(maxDb_ - currentDb());
    else if (key == juce::KeyPress::endKey)
        stepDb(minusInfinityDb - currentDb());
    else
        return false;
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> Fader::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::slider,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<AccessibleValue<Fader>>(*this)});
}

double Fader::accessibleValue() const noexcept
{
    return currentDb();
}

juce::String Fader::accessibleValueText() const
{
    const float db = currentDb();
    return db <= minusInfinityDb + 0.5f ? juce::String("-inf dB") : juce::String(db, 1) + " dB";
}

void Fader::setAccessibleValue(double db)
{
    setDb(static_cast<float>(db), true);
}

} // namespace northlight::ui
