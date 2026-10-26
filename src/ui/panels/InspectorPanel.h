#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace northlight::ui
{

// The Inspector panel (screens/shell.md): the selected track or clip, as cards.
// A track: an identity card (its kind, the name to edit, a stripe and a swatch row
// in its colour) and a Mix card (gain, a pan slider, Mute/Solo/Arm); a MIDI track
// adds Recording (what its recorded passes become: takes, merged or replacing). A
// clip: its name and track, then Position (start, length in seconds) and Level
// (gain, fades in milliseconds).
//
// The editors' note selections (screens/piano-roll.md, pitch-editing.md): the piano
// roll's selected notes show as a Note card (pitch, start in bars.beats.ticks,
// length in beats, velocity, mute), the audio editor's as a Vocal note card (pitch
// as it plays, drift, vibrato, the slides in and out in milliseconds, gain, mute),
// each typeable. With several notes the fields show the first one's values and a
// change moves every selected note by the same amount. A note selection shows
// until the next track or clip selection, or until the editor's selection empties.
class InspectorPanel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InspectorPanel(const Theme& theme);

    void setProject(const core::Project* project);
    void setSelection(core::Id trackId, core::Id clipId); // clip invalid = the track alone
    // The piano roll's selection (MIDI notes) and the audio editor's (vocal notes);
    // empty: back to the track or clip.
    void setNotes(core::Id trackId, core::Id clipId, std::vector<core::Id> notes);
    void setVocalNotes(core::Id trackId, core::Id clipId, std::vector<core::Id> notes);
    core::Id trackId() const noexcept { return trackId_; }
    enum class Shows
    {
        nothing,
        track,
        clip,
        notes,
        vocalNotes,
    };
    Shows shows() const noexcept;

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    // Several commands as one step (an edit of several notes).
    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onCommands;
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
    const core::MidiClip* midiClip() const;   // the notes' clip
    const core::AudioClip* vocalClip() const; // the vocal notes' clip
    double sampleRate() const noexcept;
    // The notes still in the model, the first (earliest) first.
    std::vector<core::MidiNote> selectedNotes() const;
    std::vector<core::VocalNote> selectedVocalNotes() const;
    void editNotes(const std::string& name, const std::function<void(core::MidiNote&)>& change);
    void editVocalNotes(const std::string& name, const std::function<void(core::VocalNote&)>& change);
    void gesture(NumericField& field, const std::string& name);
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
    SegmentedControl recordMode_{theme_}; // a MIDI track's
    // The note cards.
    core::Id notesTrack_ = core::invalidId, notesClip_ = core::invalidId;
    std::vector<core::Id> notes_;
    bool vocal_ = false;
    NumericField notePitch_{theme_}, noteStart_{theme_}, noteLength_{theme_}, noteVelocity_{theme_};
    NumericField vocalPitch_{theme_}, vocalDrift_{theme_}, vocalVibrato_{theme_}, slideIn_{theme_},
        slideOut_{theme_}, vocalGain_{theme_};
    juce::TextButton noteMute_{"Mute"};
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
