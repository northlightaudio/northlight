#pragma once

#include "engine/pitch/Correction.h"
#include "ui/AudioEditor.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// The Correct sheet (screens/pitch-editing.md "Correct to key"): strength,
// tolerance, drift kept, vibrato and chord tones, previewed live inside one
// gesture; Apply (or a click away) keeps it, Cancel undoes it. Shown in a call-out
// from the audio editor's Correct button.
class AudioEditor::CorrectSheet final : public juce::Component
{
public:
    CorrectSheet(AudioEditor& owner, const Theme& theme, bool selectionOnly);
    ~CorrectSheet() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    engine::pitch::CorrectionOptions options() const;
    void preview();
    void close(bool undo);

    juce::Component::SafePointer<AudioEditor> owner_;
    const Theme& theme_;
    juce::Slider strength_, tolerance_, drift_, vibrato_;
    juce::ToggleButton chords_;
    juce::TextButton apply_, cancel_;
    juce::String title_, keyText_;
    bool finished_ = false;
};

} // namespace northlight::ui
