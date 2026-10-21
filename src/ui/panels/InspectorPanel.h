#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace northlight::ui
{

// The Inspector panel (screens/shell.md): the selected track or clip, as cards.
// A track: an identity card (its kind, the name to edit, a stripe and a swatch row
// in its colour) and a Mix card (gain, a pan slider, Mute/Solo/Arm). A clip: its
// name and track, then Position (start, length in seconds) and Level (gain, fades
// in milliseconds).
class InspectorPanel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InspectorPanel(const Theme& theme);

    void setProject(const core::Project* project);
    void setSelection(core::Id trackId, core::Id clipId); // clip invalid = the track alone
    core::Id trackId() const noexcept { return trackId_; }

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void refresh();
    const core::Track* track() const;
    const core::AudioClip* clip() const;
    double sampleRate() const noexcept;
    juce::Rectangle<int> swatchBounds(size_t index) const;
    int swatchAt(juce::Point<int> p) const; // -1 when none

    struct Text
    {
        juce::Rectangle<int> bounds;
        juce::String text, aside; // aside: right aligned (captions only)
        std::optional<Icon> icon; // before the text (captions only)
    };

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    bool updating_ = false;
    juce::TextEditor name_;
    NumericField gain_, start_, length_, clipGain_, fadeIn_, fadeOut_;
    juce::Slider pan_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::TextButton mute_{"Mute"}, solo_{"Solo"}, arm_{"Arm"};
    // Laid out by resized(), drawn by paint().
    std::vector<juce::Rectangle<int>> cards_; // the first is the identity card
    std::vector<Text> captions_, labels_;
    juce::Rectangle<int> clipName_, panValue_;
    juce::Rectangle<int> swatchArea_;
    int swatchSize_ = 0, swatchPerRow_ = 12;
    float swatchGap_ = 0.0f;
    int hoverSwatch_ = -1;
};

} // namespace northlight::ui
