#include "ui/audioeditor/CorrectSheet.h"

#include "core/NoteNames.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/KeyMenu.h"

namespace northlight::ui
{

namespace pitch = engine::pitch;

namespace
{
constexpr int rowTop = 36, rowStep = 30, labelWidth = 84;
}

AudioEditor::CorrectSheet::CorrectSheet(AudioEditor& owner, const Theme& theme, bool selectionOnly)
    : owner_(&owner), theme_(theme)
{
    // The key: the project's, or the one the vocal suggests, which Apply confirms.
    detected_ = owner.project_ == nullptr || !owner.project_->key.isSet();
    key_ = owner.suggestedKey();
    keyButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    keyButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    keyButton_.setTooltip("The key the notes are corrected towards; it becomes the project's key");
    keyButton_.onClick = [this] { showKeyMenu(); };
    addAndMakeVisible(keyButton_);
    refreshKeyButton();

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
    keepVibrato_.setButtonText("Keep vibrato");
    keepVibrato_.setToggleState(true, juce::dontSendNotification);
    keepVibrato_.setTooltip("Keep each note's vibrato as sung; off, the slider sets how much is kept");
    keepVibrato_.onClick = [this]
    {
        vibrato_.setEnabled(!keepVibrato_.getToggleState());
        preview();
    };
    vibrato_.setEnabled(false);
    addAndMakeVisible(keepVibrato_);
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
    setSize(300, 318);
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

void AudioEditor::CorrectSheet::setKey(core::MusicalKey key)
{
    key_ = key;
    detected_ = false;
    refreshKeyButton();
    preview();
}

void AudioEditor::CorrectSheet::refreshKeyButton()
{
    keyButton_.setButtonText(keymenu::name(key_) + (detected_ && key_.isSet() ? "  (detected)" : ""));
}

void AudioEditor::CorrectSheet::showKeyMenu()
{
    auto menu = keymenu::build(key_, false, false);
    juce::Component::SafePointer<CorrectSheet> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(keyButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           if (const auto key = keymenu::keyFor(choice); key && key->isSet())
                               safe->setKey(*key);
                       });
}

void AudioEditor::CorrectSheet::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.setColour(c.textPrimary);
    g.setFont(theme_.label());
    g.drawText(title_, 12, 8, getWidth() - 24, 20, juce::Justification::centredLeft);
    g.setFont(theme_.caption());
    g.setColour(c.textSecondary);
    const char* labels[] = {"Key", "Strength", "Tolerance", "Drift kept", "Vibrato"};
    for (int i = 0; i < 5; ++i)
        g.drawText(labels[i], 12, rowTop + i * rowStep, labelWidth, 24, juce::Justification::centredLeft);
    g.setColour(c.textTertiary);
    g.drawText(keyText_, 12, getHeight() - 72, getWidth() - 24, 20, juce::Justification::centredLeft);
}

void AudioEditor::CorrectSheet::resized()
{
    const int x = 12 + labelWidth, w = getWidth() - x - 12;
    keyButton_.setBounds(x, rowTop, w, 24);
    juce::Slider* sliders[] = {&strength_, &tolerance_, &drift_, &vibrato_};
    for (int i = 0; i < 4; ++i)
        sliders[i]->setBounds(x, rowTop + (i + 1) * rowStep, w, 24);
    const int toggles = rowTop + 5 * rowStep;
    keepVibrato_.setBounds(10, toggles, getWidth() - 20, 22);
    chords_.setBounds(10, toggles + 26, getWidth() - 20, 22);
    apply_.setBounds(getWidth() - 88, getHeight() - 40, 76, 28);
    cancel_.setBounds(getWidth() - 172, getHeight() - 40, 76, 28);
}

pitch::CorrectionOptions AudioEditor::CorrectSheet::options() const
{
    pitch::CorrectionOptions o;
    o.strength = static_cast<float>(strength_.getValue() / 100.0);
    o.toleranceCents = static_cast<float>(tolerance_.getValue());
    o.drift = static_cast<float>(drift_.getValue() / 100.0);
    o.vibrato = keepVibrato_.getToggleState() ? 1.0f : static_cast<float>(vibrato_.getValue() / 100.0);
    o.chordAware = chords_.getToggleState();
    return o;
}

void AudioEditor::CorrectSheet::preview()
{
    if (owner_ == nullptr)
        return;
    owner_->correctToKey(options(), key_, "Correct pitch");
    keyText_.clear();
    if (!owner_->flagged_.empty())
        keyText_ = juce::String(static_cast<int>(owner_->flagged_.size())) +
                   " notes left alone: more than a semitone from the key";
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
