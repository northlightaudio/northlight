#pragma once

#include "ui/Theme.h"
#include "ui/strip/EffectPresetLibrary.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui::strip
{

// The presets menu every preset control shows: the factory list, the user's
// under Yours (the current one ticked), then Save preset..., Delete and Show
// presets folder. `apply` gets the chosen preset's index in the library's list.
void showPresetMenu(juce::Component& target,
                    const Theme& theme,
                    const std::string& type,
                    const std::vector<float>& params,
                    std::function<void(int index)> apply);
// Asks for a name and saves `params` as one of the user's presets.
void showSavePresetSheet(juce::Component& from,
                         const Theme& theme,
                         const std::string& type,
                         const std::vector<float>& params);

// A built-in effect's preset bar (design-system.md "Preset bar"), in its card's
// header and its window's: previous and next around the current preset's name in
// a pill. The name reads the preset whose values the effect has right now, or
// "Custom"; a click on it lists the factory presets and the user's own ("Yours"),
// with Save preset..., Delete and Show presets folder. Previous and next step
// through the whole list, wrapping. Every bar for an effect follows the library,
// so a preset saved in one shows in all.
class PresetBar final : public juce::Component, private juce::ChangeListener
{
public:
    // In a face (an instrument's, which draws the bar's well in its own style),
    // the bar draws no well and its name no menu chevron.
    PresetBar(const Theme& theme, std::string type, bool inFace = false);
    ~PresetBar() override;

    // The effect's parameters now (the name follows them).
    void setValues(const std::vector<float>& params);

    // A preset was chosen: set the effect to these values as one undo step,
    // named for the preset.
    std::function<void(const std::vector<float>& values, const juce::String& name)> onApply;

    // The width the bar wants with a name of typical length.
    int preferredWidth() const noexcept { return 208; }
    // Narrowest it can be and still show a short name.
    int minimumWidth() const noexcept { return 112; }
    // The name shown ("Default", "Vocal Plate", "Custom").
    juce::String shownName() const { return name_.getButtonText(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // For the tests.
    void step(int direction);
    void apply(int index);
    void showMenu();

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { update(); }
    void update();

    const Theme& theme_;
    const std::string type_;
    const bool inFace_;
    std::vector<float> params_;
    int current_ = -1;
    juce::TextButton previous_, next_, name_;
};

} // namespace northlight::ui::strip
