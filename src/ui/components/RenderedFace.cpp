#include "ui/components/RenderedFace.h"

#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

RenderedFace::RenderedFace(float designWidth, float designHeight) : width_(designWidth), height_(designHeight)
{
    setSize(juce::roundToInt(width_), juce::roundToInt(height_));
    setOpaque(true);
}

void RenderedFace::invalidate()
{
    cacheValid_ = false;
    repaint();
}

void RenderedFace::placeIn(juce::Rectangle<int> body)
{
    const float s = hardware::fitScale(
        static_cast<float>(body.getWidth()), static_cast<float>(body.getHeight()), width_, height_);
    setVisible(s > 0.0f);
    if (s <= 0.0f)
        return;
    setCompact(s < compactBelow);
    setBounds(0, 0, juce::roundToInt(width_), juce::roundToInt(height_));
    const float x =
        static_cast<float>(body.getX()) + (static_cast<float>(body.getWidth()) - width_ * s) * 0.5f;
    setTransform(juce::AffineTransform::scale(s).translated(x, static_cast<float>(body.getY())));
}

void RenderedFace::setCompact(bool compact)
{
    if (compact == compact_)
        return;
    compact_ = compact;
    invalidate();
}

void RenderedFace::setActiveControl(int control)
{
    if (control == activeControl_)
        return;
    activeControl_ = control;
    if (compact_) // only the compact face shows the control in hand (its value for its name)
        repaint();
}

int RenderedFace::widthForHeight(int bodyHeight) const noexcept
{
    return hardware::widthForHeight(bodyHeight, width_, height_);
}

void RenderedFace::paint(juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int key = cacheKey();
    if (!cacheValid_ || cacheScale_ != scale || cacheKey_ != key)
    {
        cacheScale_ = scale;
        cacheKey_ = key;
        cacheValid_ = true;
        cache_ = juce::Image(juce::Image::RGB,
                             std::max(1, juce::roundToInt(width_ * scale)),
                             std::max(1, juce::roundToInt(height_ * scale)),
                             true);
        juce::Graphics cg(cache_);
        cg.addTransform(juce::AffineTransform::scale(scale));
        cg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        pass_ = Pass::art;
        paintStatic(cg);
    }
    g.drawImage(cache_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    pass_ = Pass::words;
    paintStatic(g);
    pass_ = Pass::live;
    paintLive(g);
}

void RenderedFace::print(juce::Graphics& g,
                         const juce::String& text,
                         juce::Rectangle<float> area,
                         juce::Justification justification,
                         bool centreInk) const
{
    if (pass_ != Pass::art)
        hardware::drawCrispText(g, text, area, justification, centreInk);
}

} // namespace northlight::ui
