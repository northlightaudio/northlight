#pragma once

#include "engine/Insert.h"
#include "ui/components/Knob.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace northlight::ui::strip
{

// A card's parameters and the controls bound to them (ModuleCard's): the values
// the card shows, each held to its range; the model's values at the last
// refresh; and every bound control set from its value without sending it back.
// Only the model's changes come in, so a refresh that repeats a value never
// pulls a control off what automation is playing.
class ParamBinder
{
public:
    ParamBinder(const std::string& type, const std::vector<float>& model);

    const std::vector<float>& values() const noexcept { return values_; }
    float value(int index) const noexcept;
    bool has(int index) const noexcept { return index >= 0 && static_cast<size_t>(index) < values_.size(); }
    const engine::ParameterInfo& info(int index) const;

    // A user's edit, held to its range, and its controls set; false when the
    // value was that already.
    bool set(int index, float value);
    // A whole set of values (a preset), each held to its range, and every control
    // set; false when nothing moved.
    bool setAll(const std::vector<float>& values);
    // The model's values: those that changed since the last call come in. True
    // when a value the card shows moved (the controls are not set yet: updateAll).
    bool takeModel(const std::vector<float>& model);
    // The live processor's values (automation playing), and every control set;
    // true when one moved.
    bool follow(const engine::InsertProcessor& live);

    // Controls bound to a parameter (any of them null).
    struct Binding
    {
        int index = -1;
        Knob* knob = nullptr;
        SegmentedControl* choice = nullptr;
        juce::Button* toggle = nullptr; // a two-state pill
        juce::Label* label = nullptr;
    };
    void bind(const Binding& binding) { bindings_.push_back(binding); }
    // Sets every bound control from its value.
    void updateAll();
    juce::Label* labelFor(const Knob& knob) const;

    // While the controls are being set from the values (or the model is coming
    // in), their callbacks send nothing.
    bool updating() const noexcept { return updating_; }
    [[nodiscard]] juce::ScopedValueSetter<bool> quiet()
    {
        return juce::ScopedValueSetter<bool>(updating_, true);
    }

private:
    void update(const Binding& b);

    std::vector<engine::ParameterInfo> infos_;
    std::vector<float> values_;
    std::vector<float> model_; // the model's values at the last refresh
    std::vector<Binding> bindings_;
    bool updating_ = false;
};

} // namespace northlight::ui::strip
