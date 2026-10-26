#pragma once

#include "engine/pitch/Correction.h"
#include "ui/AudioEditor.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// The Correct sheet (screens/pitch-editing.md "Correct to key"): the key (the
// project's, or the one detected from the vocal for the user to confirm or
// change), strength, tolerance, drift kept, keep vibrato (off: the amount to keep)
// and chord tones, previewed live inside one gesture; Apply (or a click away)
// keeps it, Cancel undoes it. Shown in a call-out from the audio editor's Correct
// button.
class AudioEditor::CorrectSheet final : public juce::Component
{
public:
    CorrectSheet(AudioEditor& owner, const Theme& theme, bool selectionOnly);
    ~CorrectSheet() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    core::MusicalKey key() const noexcept { return key_; }
    void setKey(core::MusicalKey key);

private:
    engine::pitch::CorrectionOptions options() const;
    void preview();
    void close(bool undo);
    void showKeyMenu();
    void refreshKeyButton();

    juce::Component::SafePointer<AudioEditor> owner_;
    const Theme& theme_;
    core::MusicalKey key_;
    bool detected_ = false; // the key was detected, not the project's
    juce::TextButton keyButton_;
    juce::Slider strength_, tolerance_, drift_, vibrato_;
    juce::ToggleButton keepVibrato_, chords_;
    juce::TextButton apply_, cancel_;
    juce::String title_, keyText_;
    bool finished_ = false;
};

} // namespace northlight::ui
