#include "ui/audioeditor/CorrectSheet.h"

#include "core/NoteNames.h"
#include "ui/NorthlightLookAndFeel.h"

namespace northlight::ui
{

namespace pitch = engine::pitch;

AudioEditor::CorrectSheet::CorrectSheet(AudioEditor& owner, const Theme& theme, bool selectionOnly)
    : owner_(&owner), theme_(theme)
{
    const auto slider =
        [this](juce::Slider& s, double lo, double hi, double value, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setRange(lo, hi, 1.0);
        s.setValue(value, juce::dontSendNotification);
        s.setTextValueSuffix(suffix);
        s.onValueChange = [this] { preview(); };
        addAndMakeVisible(s);
    };
    slider(strength_, 0, 100, 85, "%");
    slider(tolerance_, 0, 50, 10, " ct");
    slider(drift_, 0, 100, 60, "%");
    slider(vibrato_, 0, 200, 100, "%");
    chords_.setButtonText("Prefer chord tones");
    chords_.onClick = [this] { preview(); };
    addAndMakeVisible(chords_);
    apply_.setButtonText("Apply");
    apply_.onClick = [this] { close(false); };
    cancel_.setButtonText("Cancel");
    cancel_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    cancel_.onClick = [this] { close(true); };
    addAndMakeVisible(apply_);
    addAndMakeVisible(cancel_);
    title_ = selectionOnly ? "Correct the selected notes" : "Correct every note";
    setSize(300, 236);
    if (owner_->onGestureBegin)
        owner_->onGestureBegin("Correct pitch");
    preview();
}

AudioEditor::CorrectSheet::~CorrectSheet()
{
    // Dismissed by a click outside: what is heard is kept.
    if (!finished_ && owner_ != nullptr && owner_->onGestureEnd)
        owner_->onGestureEnd();
}

void AudioEditor::CorrectSheet::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.setColour(c.textPrimary);
    g.setFont(theme_.label());
    g.drawText(title_, 12, 8, getWidth() - 24, 20, juce::Justification::centredLeft);
    g.setFont(theme_.caption());
    g.setColour(c.textSecondary);
    const char* labels[] = {"Strength", "Tolerance", "Drift kept", "Vibrato"};
    for (int i = 0; i < 4; ++i)
        g.drawText(labels[i], 12, 36 + i * 30, 80, 24, juce::Justification::centredLeft);
    g.setColour(c.textTertiary);
    g.drawText(keyText_, 12, getHeight() - 72, getWidth() - 24, 20, juce::Justification::centredLeft);
}

void AudioEditor::CorrectSheet::resized()
{
    juce::Slider* sliders[] = {&strength_, &tolerance_, &drift_, &vibrato_};
    for (int i = 0; i < 4; ++i)
        sliders[i]->setBounds(96, 36 + i * 30, getWidth() - 108, 24);
    chords_.setBounds(10, 156, getWidth() - 20, 22);
    apply_.setBounds(getWidth() - 88, getHeight() - 40, 76, 28);
    cancel_.setBounds(getWidth() - 172, getHeight() - 40, 76, 28);
}

pitch::CorrectionOptions AudioEditor::CorrectSheet::options() const
{
    pitch::CorrectionOptions o;
    o.strength = static_cast<float>(strength_.getValue() / 100.0);
    o.toleranceCents = static_cast<float>(tolerance_.getValue());
    o.drift = static_cast<float>(drift_.getValue() / 100.0);
    o.vibrato = static_cast<float>(vibrato_.getValue() / 100.0);
    o.chordAware = chords_.getToggleState();
    return o;
}

void AudioEditor::CorrectSheet::preview()
{
    if (owner_ == nullptr)
        return;
    owner_->correctToKey(options());
    if (owner_->project_ != nullptr && owner_->project_->key.isSet())
        keyText_ = juce::String("Key: ") + core::pitchClassName(owner_->project_->key.root) + " " +
                   core::scaleName(owner_->project_->key.scale);
    if (!owner_->flagged_.empty())
        keyText_ += "  \xc2\xb7  " + juce::String(static_cast<int>(owner_->flagged_.size())) + " left alone";
    repaint();
}

void AudioEditor::CorrectSheet::close(bool undo)
{
    finished_ = true;
    if (owner_ != nullptr)
    {
        if (owner_->onGestureEnd)
            owner_->onGestureEnd();
        if (undo && owner_->onUndo)
            owner_->onUndo();
    }
    if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
        box->dismiss();
}

} // namespace northlight::ui
