#pragma once

#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// A stepped parameter's control in a generic editor (screens/plugins.md 7.3): a
// segmented control for up to four choices, a combo (a menu) for more. The value
// is the choice's index, as the parameter stores it.
class ChoiceControl final : public juce::Component
{
public:
    static constexpr int maxSegments = 4;

    ChoiceControl(const Theme& theme, const std::vector<std::string>& choices, const juce::String& tooltip);

    // The parameter's value; it shows the nearest choice.
    void setValue(float value);
    int selected() const noexcept { return selected_; }
    bool isSegmented() const noexcept { return segments_ != nullptr; }
    // The width it needs to show every choice (a combo: its longest).
    int preferredWidth() const;
    // On a surface1 card the segmented track is surface0, so it reads as a well.
    void setOnCard(bool onCard);

    // A choice picked by the user, as the parameter's value.
    std::function<void(float)> onChange;

    void resized() override;

private:
    void picked(int index);

    const Theme& theme_;
    std::unique_ptr<SegmentedControl> segments_;
    std::unique_ptr<juce::ComboBox> combo_;
    std::vector<juce::String> labels_;
    int selected_ = 0;
    bool updating_ = false;
};

} // namespace northlight::ui
