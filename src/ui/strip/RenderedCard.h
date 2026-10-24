#pragma once

#include "ui/components/RenderedFace.h"
#include "ui/strip/HardwareLever.h"
#include "ui/strip/ModuleCard.h"

#include <array>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

// A module card whose body is a rendered hardware face (the tape echo's
// console, Corona's amp): the card is as wide as the face at its body's height,
// the face scaled to fit, and the face's controls are rendered knobs and
// levers bound to the module's parameters, placed in the face's design pixels.
class RenderedCard : public ModuleCard
{
public:
    // `face` is the subclass's member: stored, not touched until layout.
    RenderedCard(const Theme& theme,
                 CardHost& host,
                 core::Id channelId,
                 const core::Insert& insert,
                 RenderedFace& face);

    ~RenderedCard() override;

    int preferredWidth() const override;
    int preferredBodyHeight() const override;
    int preferredWidthAt(int bodyHeight) const override;

protected:
    // A knob bound to its parameter: its own label hidden (the face prints
    // names and values), drawn as `pointer` turning over the body the face
    // draws, `skirt` across, its bounds `imageSize` square round `centre`.
    Knob& bindRenderedKnob(
        int index, juce::Point<float> centre, float skirt, float imageSize, const juce::Image& pointer);
    // The same placement for a knob the card drives itself (one that switches
    // between parameters); `control` is what the face calls it while it is
    // hovered or turned (a parameter index; -1 for none).
    void placeRenderedKnob(Knob& knob,
                           juce::Point<float> centre,
                           float skirt,
                           float imageSize,
                           const juce::Image& pointer,
                           int control = -1);
    // A lever on the face, its renders `files` under assets/<folder>, drawn at
    // `scale` of the render's size (119 x 178.5) with its nut at `nut`.
    HardwareLever& addLever(const juce::String& name,
                            std::vector<juce::String> positions,
                            const juce::String& folder,
                            std::array<juce::String, 3> files,
                            juce::Point<float> nut,
                            float scale,
                            float nutSize);

    // A rendered knob was turned (default: the face repaints its values).
    virtual void controlMoved() { face_.repaint(); }

    void layoutBody(juce::Rectangle<int> body) override { face_.placeIn(body); }

    RenderedFace& face_;

private:
    // Tells the face which knob is hovered or turned.
    struct Watch;
    std::vector<std::unique_ptr<HardwareLever>> levers_;
    std::vector<std::unique_ptr<Watch>> watches_;
};

} // namespace northlight::ui::strip
