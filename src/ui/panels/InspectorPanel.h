#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/NumericField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace northlight::ui
{

// The Inspector panel (screens/shell.md): the selected track or clip. A track shows
// its name (editable), colour swatches, gain, pan and the M/S/R toggles; a clip
// its name, start, length, gain and fades. Rows of a 120 px label and a control.
class InspectorPanel final : public juce::Component
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

private:
    void refresh();
    const core::Track* track() const;
    const core::AudioClip* clip() const;
    juce::Rectangle<int> swatchBounds(size_t index) const;

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    bool updating_ = false;
    juce::TextEditor name_;
    NumericField gain_, start_, length_, clipGain_, fadeIn_, fadeOut_;
    Knob pan_;
    juce::TextButton mute_{"M"}, solo_{"S"}, arm_{"R"};
    juce::Rectangle<int> swatchArea_;
};

} // namespace northlight::ui
