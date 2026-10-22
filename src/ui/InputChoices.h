#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>
#include <vector>

// A track's audio input menu (screens/arrange.md "Track header", screens/mixer.md):
// "No input", then the stereo input buses set up in Audio Setup, then every enabled
// device channel as a mono input. A bus the track names that the device does not
// offer stays in the menu, marked missing, so the choice is not lost silently.

namespace northlight::ui
{

struct InputChoice
{
    std::string label;  // "Vocals" for a bus, "In 3" for a channel
    std::string detail; // the channels a bus reads ("In 1 + In 2"); empty for a channel
    int channel = -1;   // a mono input: index among the active inputs (callback order)
    std::string bus;    // a stereo input: the bus name
    bool operator==(const InputChoice&) const = default;
};

namespace inputs
{
constexpr int noInputId = 1;
constexpr int firstChoiceId = 2;

inline int missingId(const std::vector<InputChoice>& choices) noexcept
{
    return firstChoiceId + static_cast<int>(choices.size());
}

// Fills `box` for `track` and selects its input.
inline void fill(juce::ComboBox& box, const std::vector<InputChoice>& choices, const core::Track& track)
{
    box.clear(juce::dontSendNotification);
    box.addItem("No input", noInputId);
    int selected = noInputId;
    juce::String tooltip = "Input";
    bool stereoHeading = false, monoHeading = false;
    for (size_t i = 0; i < choices.size(); ++i)
    {
        const auto& choice = choices[i];
        const bool stereo = !choice.bus.empty();
        // Headings only when both kinds are listed; a plain list of channels needs none.
        if (stereo && !stereoHeading)
        {
            box.addSectionHeading("Stereo");
            stereoHeading = true;
        }
        if (!stereo && stereoHeading && !monoHeading)
        {
            box.addSectionHeading("Mono");
            monoHeading = true;
        }
        const int id = firstChoiceId + static_cast<int>(i);
        box.addItem(juce::String::fromUTF8(choice.label.c_str()), id);
        const bool chosen = stereo ? choice.bus == track.inputBus
                                   : track.inputBus.empty() && choice.channel == track.inputChannel;
        if (chosen)
        {
            selected = id;
            if (stereo)
                tooltip = "Input: " + juce::String::fromUTF8(choice.label.c_str()) + " (" +
                          juce::String::fromUTF8(choice.detail.c_str()) + ")";
        }
    }
    if (!track.inputBus.empty() && selected == noInputId)
    {
        box.addItem(juce::String::fromUTF8(track.inputBus.c_str()) + " (missing)", missingId(choices));
        selected = missingId(choices);
        tooltip = "This stereo input is not set up for the current device. Add it in Audio Setup or "
                  "choose another input.";
    }
    box.setSelectedId(selected, juce::dontSendNotification);
    box.setTooltip(tooltip);
}

// The input the box now selects: no input, a channel or a bus; nullopt for the
// missing bus, which is what the track already has.
inline std::optional<InputChoice> selected(const juce::ComboBox& box, const std::vector<InputChoice>& choices)
{
    const int id = box.getSelectedId();
    if (id == noInputId || id == 0)
        return InputChoice{};
    const int index = id - firstChoiceId;
    if (index >= 0 && static_cast<size_t>(index) < choices.size())
        return choices[static_cast<size_t>(index)];
    return std::nullopt;
}
} // namespace inputs

} // namespace northlight::ui
