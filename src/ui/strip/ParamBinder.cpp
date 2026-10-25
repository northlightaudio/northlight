#include "ui/strip/ParamBinder.h"

#include "plugins/BuiltIns.h"

#include <cmath>

namespace northlight::ui::strip
{

ParamBinder::ParamBinder(const std::string& type, const std::vector<float>& model)
    : infos_(plugins::parameterInfo(type)), model_(model)
{
    values_.resize(infos_.size());
    for (size_t i = 0; i < infos_.size(); ++i)
        values_[i] = i < model.size() ? model[i] : infos_[i].defaultValue;
}

float ParamBinder::value(int index) const noexcept
{
    return has(index) ? values_[static_cast<size_t>(index)] : 0.0f;
}

const engine::ParameterInfo& ParamBinder::info(int index) const
{
    static const engine::ParameterInfo none;
    return index >= 0 && static_cast<size_t>(index) < infos_.size() ? infos_[static_cast<size_t>(index)]
                                                                    : none;
}

bool ParamBinder::set(int index, float value)
{
    if (!has(index))
        return false;
    const auto& i = infos_[static_cast<size_t>(index)];
    value = juce::jlimit(i.minimum, i.maximum, value);
    if (values_[static_cast<size_t>(index)] == value)
        return false;
    values_[static_cast<size_t>(index)] = value;
    for (const auto& b : bindings_)
        if (b.index == index)
            update(b);
    return true;
}

bool ParamBinder::setAll(const std::vector<float>& values)
{
    auto next = values_;
    for (size_t i = 0; i < values.size() && i < next.size(); ++i)
        next[i] = juce::jlimit(infos_[i].minimum, infos_[i].maximum, values[i]);
    if (next == values_)
        return false;
    values_ = std::move(next);
    updateAll();
    return true;
}

bool ParamBinder::takeModel(const std::vector<float>& model)
{
    bool changed = false;
    model_.resize(model.size(), 0.0f);
    for (size_t i = 0; i < values_.size() && i < model.size(); ++i)
        if (model_[i] != model[i])
        {
            model_[i] = model[i];
            if (values_[i] != model[i])
            {
                values_[i] = model[i];
                changed = true;
            }
        }
    return changed;
}

bool ParamBinder::follow(const engine::InsertProcessor& live)
{
    bool moved = false;
    for (size_t i = 0; i < values_.size() && i < live.parameters().size(); ++i)
    {
        const float v = live.parameter(static_cast<int>(i));
        if (v != values_[i])
        {
            values_[i] = v;
            moved = true;
        }
    }
    if (moved)
        updateAll();
    return moved;
}

void ParamBinder::updateAll()
{
    for (const auto& b : bindings_)
        update(b);
}

void ParamBinder::update(const Binding& b)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const float v = value(b.index);
    if (b.knob != nullptr)
        b.knob->setValue(v, juce::dontSendNotification);
    if (b.choice != nullptr)
        b.choice->setSelected(static_cast<int>(std::lround(v)), juce::dontSendNotification);
    if (b.toggle != nullptr)
        b.toggle->setToggleState(v >= 0.5f, juce::dontSendNotification);
}

juce::Label* ParamBinder::labelFor(const Knob& knob) const
{
    for (const auto& b : bindings_)
        if (b.knob == &knob)
            return b.label;
    return nullptr;
}

} // namespace northlight::ui::strip
