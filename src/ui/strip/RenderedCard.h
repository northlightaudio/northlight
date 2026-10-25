#pragma once

#include "ui/components/RenderedFace.h"
#include "ui/strip/HardwareLever.h"
#include "ui/strip/ModuleCard.h"

#include <array>
#include <map>
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
    // Half the face's size: below that even the compact view's names are too small.
    int minimumBodyHeight() const override;
    int preferredWidthAt(int bodyHeight) const override;

protected:
    // A knob bound to its parameter: its own label hidden (the face prints
    // names and values), drawn as `pointer` turning over the body the face
    // draws, `skirt` across, its bounds `imageSize` square round `centre`.
    Knob& bindRenderedKnob(
        int index, juce::Point<float> centre, float skirt, float imageSize, const juce::Image& pointer);
    // The same placement for a knob the card drives itself (one that switches
    // between parameters); `control` is what the face calls it while it is
    // hovered or turned (a parameter index; -1 for none). Set the knob's drag
    // callbacks before placing it: the placement chains onto them.
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

    // A rendered knob was turned (default: the face repaints round the knob,
    // where its name and value are printed).
    virtual void controlMoved(Knob& knob) { face_.repaint(printArea(knob)); }
    // How far round a knob its name and value can be printed.
    static constexpr int controlReach = 72;
    // Where a knob's name and value are printed, with the knob (in the face's
    // pixels): what repaints when it is turned or hovered. Default: controlReach
    // round it; a small face whose print sits closer says so.
    virtual juce::Rectangle<int> printArea(const Knob& knob) const
    {
        return knob.getBounds().expanded(controlReach);
    }

    void layoutBody(juce::Rectangle<int> body) override { face_.placeIn(body); }

    // Repaints what the parameters changed since the last call: round each bound
    // knob whose value moved (its name and value print there), and the whole face
    // for one printsWidely() says reaches further, or on the first call. Call it
    // from paramsChanged() instead of repainting the face.
    void repaintChanged();
    bool repaintsChangedParameters() const noexcept override { return true; }
    // Whether a parameter's change shows beyond its own knob (another control's
    // value or name, the art). Default: a parameter with no rendered knob does.
    virtual bool printsWidely(int index) const { return knobFor(index) == nullptr; }
    // Other parameters whose knobs' print a parameter's change alters (a unit
    // that follows a mode): those knobs repaint round themselves too.
    virtual std::vector<int> alsoChanges(int index) const
    {
        juce::ignoreUnused(index);
        return {};
    }
    // The rendered knob bound to a parameter, or null.
    Knob* knobFor(int index) const;
    // A knob the card drives itself that shows a parameter (a knob switching
    // between two): its changes repaint round it.
    void showOnKnob(int index, Knob& knob) { renderedKnobs_[index] = &knob; }

    RenderedFace& face_;

private:
    // Tells the face which knob is hovered or turned.
    void watchActive(Knob& knob, int control);
    std::vector<std::unique_ptr<HardwareLever>> levers_;
    std::map<int, Knob*> renderedKnobs_;
    std::vector<float> shown_; // the parameters as last painted
};

} // namespace northlight::ui::strip
