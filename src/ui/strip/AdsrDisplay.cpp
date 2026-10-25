#include "ui/strip/AdsrDisplay.h"

#include "ui/strip/SynthParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip
{

AdsrDisplay::AdsrDisplay(ModuleCard& card) : card_(card)
{
    setTooltip("Drag the points: attack, decay and sustain, release");
}

std::array<juce::Point<float>, 4> AdsrDisplay::points(int attack) const
{
    const auto p = plot();
    const float quarter = p.getWidth() / 4.0f;
    const auto along = [&](int stage, float ms, float maxMs)
    {
        const float f = std::log(std::max(1.0f, ms)) / std::log(maxMs);
        return p.getX() + quarter * static_cast<float>(stage) + quarter * juce::jlimit(0.0f, 1.0f, f);
    };
    const float sustainY = p.getBottom() - card_.param(attack + 2) * p.getHeight();
    return {juce::Point<float>{along(0, card_.param(attack), maxMs(attack)), p.getY()},
            juce::Point<float>{along(1, card_.param(attack + 1), maxMs(attack + 1)), sustainY},
            juce::Point<float>{p.getX() + quarter * 3.0f, sustainY},
            juce::Point<float>{along(3, card_.param(attack + 3), maxMs(attack + 3)), p.getBottom()}};
}

juce::Path AdsrDisplay::path(const std::array<juce::Point<float>, 4>& pts) const
{
    const auto p = plot();
    juce::Path env;
    env.startNewSubPath(p.getX(), p.getBottom());
    for (const auto& pt : pts)
        env.lineTo(pt);
    return env;
}

void AdsrDisplay::drawHandles(juce::Graphics& g, juce::Colour hue) const
{
    const auto pts = points(editedAttack());
    for (const int i : {0, 1, 3})
        drawHandle(g, card_.theme(), hue, pts[static_cast<size_t>(i)], drag_ == i);
}

void AdsrDisplay::mouseDown(const juce::MouseEvent& e)
{
    const auto pts = points(editedAttack());
    drag_ = 3;
    float best = 1.0e9f;
    for (const int i : {0, 1, 3})
        if (const float d = pts[static_cast<size_t>(i)].getDistanceFrom(e.position); d < best)
        {
            best = d;
            drag_ = i;
        }
    card_.beginGesture(drag_ == 0 ? "Attack" : drag_ == 1 ? "Decay" : "Release");
    mouseDrag(e);
}

void AdsrDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const int attack = editedAttack();
    const auto p = plot();
    const float quarter = p.getWidth() / 4.0f;
    const auto msIn = [&](int stage, float x, float maxMs)
    {
        const float f =
            juce::jlimit(0.0f, 1.0f, (x - (p.getX() + quarter * static_cast<float>(stage))) / quarter);
        return std::pow(maxMs, f);
    };
    if (drag_ == 0)
        card_.setParam(attack, msIn(0, e.position.x, maxMs(attack)));
    else if (drag_ == 1)
    {
        card_.setParam(attack + 1, msIn(1, e.position.x, maxMs(attack + 1)));
        card_.setParam(attack + 2, juce::jlimit(0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight()));
    }
    else if (drag_ == 3)
        card_.setParam(attack + 3, msIn(3, e.position.x, maxMs(attack + 3)));
    repaint();
}

void AdsrDisplay::mouseUp(const juce::MouseEvent&)
{
    if (drag_ >= 0)
        card_.endGesture();
    drag_ = -1;
    repaint();
}

} // namespace northlight::ui::strip
