#include "ui/shell/HeaderBar.h"

#include "ui/AuralLookAndFeel.h"

#include <algorithm>

namespace aural::ui
{

// ----- time display ---------------------------------------------------------------------------

TimeDisplay::TimeDisplay(const Theme& theme, bool primary) : theme_(theme), primary_(primary)
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTooltip("Click to type a position, drag a segment to nudge it");
}

void TimeDisplay::setText(const juce::String& text)
{
    if (text == text_)
        return;
    text_ = text;
    repaint();
}

void TimeDisplay::setTone(Tone tone)
{
    if (tone == tone_)
        return;
    tone_ = tone;
    repaint();
}

void TimeDisplay::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto& m = theme_.material;
    const auto bounds = getLocalBounds().toFloat();
    auto* lnf = dynamic_cast<AuralLookAndFeel*>(&getLookAndFeel());
    g.setColour(m.enabled ? m.readoutWell : c.surface2);
    g.fillRoundedRectangle(bounds, theme_.radius.sm);
    if (m.enabled && lnf != nullptr)
        lnf->drawInset(g, bounds, theme_.radius.sm, m.grooveInset);
    if (editor_ != nullptr)
        return;
    const juce::Colour text = tone_ == Tone::recording ? (m.enabled ? m.readoutRecord : c.record)
                              : tone_ == Tone::countIn ? c.warning
                              : m.enabled              ? m.readoutText
                                                       : c.textPrimary;
    g.setFont(primary_ ? theme_.timeLarge() : theme_.timeSmall());
    if (m.enabled && lnf != nullptr && tone_ == Tone::normal)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text_);
        lnf->drawGlow(g,
                      juce::Rectangle<float>(bounds.getCentreX() - w * 0.5f,
                                             bounds.getCentreY() - g.getCurrentFont().getHeight() * 0.4f,
                                             static_cast<float>(w),
                                             g.getCurrentFont().getHeight() * 0.8f),
                      theme_.radius.sm,
                      m.readoutGlow);
    }
    g.setColour(text);
    g.drawText(text_, getLocalBounds().reduced(theme_.spacing.sm, 0), juce::Justification::centred);
}

void TimeDisplay::resized()
{
    if (editor_ != nullptr)
        editor_->setBounds(getLocalBounds());
}

int TimeDisplay::segmentAt(int x) const
{
    // Segments are separated by '.' or ':'; the drag applies to the one under x.
    const auto font = primary_ ? theme_.timeLarge() : theme_.timeSmall();
    const int total = juce::GlyphArrangement::getStringWidthInt(font, text_);
    const int left = (getWidth() - total) / 2;
    int segment = 0;
    juce::String seen;
    for (int i = 0; i < text_.length(); ++i)
    {
        seen += text_[i];
        if (text_[i] == '.' || text_[i] == ':')
        {
            if (x < left + juce::GlyphArrangement::getStringWidthInt(font, seen))
                return segment;
            ++segment;
        }
    }
    return segment;
}

void TimeDisplay::mouseDown(const juce::MouseEvent& e)
{
    dragSegment_ = segmentAt(e.x);
    dragStartY_ = e.y;
    dragSteps_ = 0;
    dragged_ = false;
}

void TimeDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragSegment_ < 0 || editor_ != nullptr)
        return;
    dragged_ = dragged_ || e.mouseWasDraggedSinceMouseDown();
    const int steps = (dragStartY_ - e.y) / 6;
    if (steps != dragSteps_)
    {
        if (onNudge)
            onNudge(dragSegment_, steps - dragSteps_);
        dragSteps_ = steps;
    }
}

void TimeDisplay::mouseUp(const juce::MouseEvent&)
{
    if (!dragged_ && dragSegment_ >= 0 && editor_ == nullptr)
        beginTyping();
    dragSegment_ = -1;
}

void TimeDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    beginTyping();
}

void TimeDisplay::beginTyping()
{
    if (editor_ != nullptr)
        return;
    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setFont(primary_ ? theme_.timeLarge() : theme_.timeSmall());
    editor_->setJustification(juce::Justification::centred);
    editor_->setText(text_, juce::dontSendNotification);
    editor_->setSelectAllWhenFocused(true);
    editor_->onReturnKey = [this] { endTyping(true); };
    editor_->onEscapeKey = [this] { endTyping(false); };
    editor_->onFocusLost = [this] { endTyping(false); };
    addAndMakeVisible(*editor_);
    resized();
    editor_->grabKeyboardFocus();
}

void TimeDisplay::endTyping(bool commit)
{
    if (editor_ == nullptr)
        return;
    const auto typed = editor_->getText();
    auto editor = std::move(editor_);
    removeChildComponent(editor.get());
    editor.reset();
    if (commit && onTyped)
        onTyped(typed);
    repaint();
}

// ----- clip indicator --------------------------------------------------------------------------

// ----- header bar -----------------------------------------------------------------------------

HeaderBar::HeaderBar(const Theme& theme)
    : theme_(theme), app_(theme, Icon::mark, "Aural: About, Preferences, Audio Setup, Plugins"),
      undo_(theme, Icon::undo, "Undo  Ctrl+Z"), redo_(theme, Icon::redo, "Redo  Ctrl+Shift+Z"),
      goToStart_(theme, Icon::goToStart, "Go to start  Enter"), play_(theme, Icon::play, "Play  Space"),
      stop_(theme, Icon::stop, "Stop  Space"), record_(theme, Icon::record, "Record  R"),
      loop_(theme, Icon::loop, "Loop  L"), punch_(theme, Icon::punch, "Punch  P"),
      click_(theme, Icon::metronome, "Click  C"), countIn_(theme, Icon::countIn, "Count-in: 1 bar"),
      bars_(theme, true), time_(theme, false), tempo_(theme), snap_(theme, Icon::snap, "Snap: Adaptive  N"),
      ripple_(theme, Icon::ripple, "Ripple  Shift+N"), zoomFit_(theme, Icon::zoomFit, "Zoom to fit  Z"),
      mixer_(theme, Icon::mixer, "Mixer  F2"), editor_(theme, Icon::inspector, "Editor  F3"),
      browser_(theme, Icon::browser, "Browser  F4")
{
    setOpaque(true);
    const auto bind = [](IconButton& button, std::function<void()>& action)
    {
        button.onClick = [&action]
        {
            if (action)
                action();
        };
    };
    bind(app_, onAppMenu);
    bind(undo_, onUndo);
    bind(redo_, onRedo);
    bind(goToStart_, onGoToStart);
    bind(play_, onPlayStop);
    bind(stop_, onStop);
    bind(record_, onRecord);
    bind(loop_, onToggleLoop);
    bind(punch_, onTogglePunch);
    bind(click_, onToggleClick);
    bind(snap_, onToggleSnap);
    bind(ripple_, onToggleRipple);
    bind(zoomFit_, onZoomFit);
    bind(mixer_, onToggleMixer);
    bind(editor_, onToggleEditor);
    bind(browser_, onToggleBrowser);
    for (auto* b : {&goToStart_, &play_, &stop_, &record_})
    {
        b->setSize(theme_.sizes.controlLarge, theme_.sizes.controlLarge);
        b->setIconSize(theme_.sizes.iconLarge);
    }
    record_.setOnColour(theme_.colours.record);
    record_.setRestColour(theme_.colours.record);
    record_.setSolidWhenOn(true);
    punch_.setOnColour(theme_.colours.record);
    countIn_.onClick = [this]
    {
        if (onCountInBars)
            onCountInBars((state_.countInBars + 1) % 3);
    };
    snap_.setShowsMenuChevron(true);
    snap_.setSize(theme_.sizes.control + theme_.spacing.sm, theme_.sizes.control);

    project_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    project_.getProperties().set(AuralLookAndFeel::propertyMenu, true);
    project_.onClick = [this]
    {
        if (onProjectMenu)
            onProjectMenu();
    };

    bars_.onTyped = [this](const juce::String& text)
    {
        if (onBarsTyped)
            onBarsTyped(text);
    };
    time_.onTyped = [this](const juce::String& text)
    {
        if (onTimeTyped)
            onTimeTyped(text);
    };
    bars_.onNudge = [this](int segment, int steps)
    {
        if (onBarsNudged)
            onBarsNudged(segment, steps);
    };
    time_.onNudge = [this](int segment, int steps)
    {
        if (onTimeNudged)
            onTimeNudged(segment, steps);
    };

    tempo_.setRange(20.0, 400.0, 0.01);
    tempo_.setDecimals(2);
    tempo_.setTooltip("Tempo: drag or type; T taps while focused");
    tempo_.onValueChanged = [this](double value)
    {
        if (onTempo)
            onTempo(value);
    };
    // Tap tempo: T while the field is focused; the average of the last taps within
    // two seconds becomes the tempo once there are two or more.
    tempo_.onKey = [this](const juce::KeyPress& key)
    {
        if (key.getTextCharacter() != 't' && key.getTextCharacter() != 'T')
            return false;
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (!tapTimesMs_.empty() && now - tapTimesMs_.back() > 2000.0)
            tapTimesMs_.clear();
        tapTimesMs_.push_back(now);
        if (tapTimesMs_.size() > 8)
            tapTimesMs_.erase(tapTimesMs_.begin());
        if (tapTimesMs_.size() >= 2)
        {
            const double interval =
                (tapTimesMs_.back() - tapTimesMs_.front()) / static_cast<double>(tapTimesMs_.size() - 1);
            if (interval > 0.0 && onTempo)
                onTempo(std::clamp(60000.0 / interval, 20.0, 400.0));
        }
        return true;
    };
    for (const char* s : {"2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8"})
        signature_.addItem(s, signature_.getNumItems() + 1);
    signature_.setTooltip("Time signature");
    signature_.onChange = [this]
    {
        const auto text = signature_.getText();
        if (onSignature && text.containsChar('/'))
            onSignature(text.upToFirstOccurrenceOf("/", false, false).getIntValue(),
                        text.fromFirstOccurrenceOf("/", false, false).getIntValue());
    };

    for (auto* c : std::initializer_list<juce::Component*>{
             &app_,  &project_, &undo_,    &redo_,    &goToStart_, &play_,   &stop_,  &record_,
             &loop_, &punch_,   &click_,   &countIn_, &bars_,      &time_,   &tempo_, &signature_,
             &snap_, &ripple_,  &zoomFit_, &mixer_,   &editor_,    &browser_})
        addAndMakeVisible(c);
    refreshFromState();
}

void HeaderBar::setState(const State& state)
{
    state_ = state;
    refreshFromState();
}

void HeaderBar::refreshFromState()
{
    const auto& s = state_;
    project_.setButtonText(s.projectName);
    project_.setTooltip(s.projectPath.isNotEmpty() ? s.projectPath : juce::String("Unsaved project"));
    undo_.setEnabled(s.canUndo);
    undo_.setTooltip(s.canUndo ? "Undo " + s.undoName + "  Ctrl+Z" : "Undo  Ctrl+Z");
    redo_.setEnabled(s.canRedo);
    redo_.setTooltip(s.canRedo ? "Redo " + s.redoName + "  Ctrl+Shift+Z" : "Redo  Ctrl+Shift+Z");
    play_.setIcon(s.playing ? Icon::pause : Icon::play);
    play_.setTooltip(s.playing ? "Stop  Space" : "Play  Space");
    play_.setToggleState(s.playing, juce::dontSendNotification);
    play_.setRestColour(s.playing ? theme_.colours.accent : theme_.colours.textSecondary);
    stop_.setRestColour(s.playing ? theme_.colours.textSecondary : theme_.colours.textPrimary);
    record_.setToggleState(s.recording, juce::dontSendNotification);
    record_.setEnabled(s.deviceReady);
    play_.setEnabled(s.deviceReady);
    record_.setWarningDot(!s.deviceReady);
    record_.setTooltip(s.deviceReady ? (s.recording ? "Stop recording  R" : "Record  R")
                                     : "No audio device: click to open Audio Setup");
    loop_.setToggleState(s.loop, juce::dontSendNotification);
    loop_.setEnabled(s.loopPossible);
    loop_.setTooltip(s.loopPossible ? juce::String(s.loop ? "Loop: on  L" : "Loop: off  L")
                                    : juce::String("Set a loop region in the ruler"));
    punch_.setToggleState(s.punch, juce::dontSendNotification);
    punch_.setTooltip(s.punch ? "Punch: on  P" : "Punch: off  P");
    click_.setToggleState(s.click, juce::dontSendNotification);
    click_.setTooltip(s.click ? "Click: on  C" : "Click: off  C");
    countIn_.setToggleState(s.countInBars > 0, juce::dontSendNotification);
    countIn_.setCornerText(s.countInBars > 0 ? juce::String(s.countInBars) : juce::String());
    countIn_.setTooltip("Count-in: " + juce::String(s.countInBars) + (s.countInBars == 1 ? " bar" : " bars") +
                        " (click to change)");
    bars_.setText(s.barsText);
    time_.setText(s.timeText);
    const auto tone = s.recording    ? TimeDisplay::Tone::recording
                      : s.countingIn ? TimeDisplay::Tone::countIn
                                     : TimeDisplay::Tone::normal;
    bars_.setTone(tone);
    time_.setTone(tone);
    if (std::abs(tempo_.value() - s.tempo) > 1.0e-6)
        tempo_.setValue(s.tempo, juce::dontSendNotification);
    const auto signature = juce::String(s.signatureNumerator) + "/" + juce::String(s.signatureDenominator);
    if (signature_.getText() != signature)
    {
        bool found = false;
        for (int i = 0; i < signature_.getNumItems(); ++i)
            if (signature_.getItemText(i) == signature)
            {
                signature_.setSelectedItemIndex(i, juce::dontSendNotification);
                found = true;
            }
        if (!found)
        {
            signature_.addItem(signature, signature_.getNumItems() + 1);
            signature_.setSelectedItemIndex(signature_.getNumItems() - 1, juce::dontSendNotification);
        }
    }
    snap_.setToggleState(s.snap, juce::dontSendNotification);
    snap_.setTooltip("Snap: " + (s.snap ? s.snapName : juce::String("off")) + "  N");
    ripple_.setToggleState(s.ripple, juce::dontSendNotification);
    ripple_.setTooltip(s.ripple ? "Ripple: on  Shift+N" : "Ripple: off  Shift+N");
    mixer_.setToggleState(s.mixerVisible, juce::dontSendNotification);
    editor_.setToggleState(s.editorVisible, juce::dontSendNotification);
    browser_.setToggleState(s.browserVisible, juce::dontSendNotification);
    if (s.masterClipped != clipLit_)
    {
        clipLit_ = s.masterClipped;
        repaint(clipIndicatorBounds());
    }
    repaint(getLocalBounds().removeFromLeft(app_.getRight()));
}

juce::Rectangle<int> HeaderBar::clipIndicatorBounds() const
{
    const int size = theme_.sizes.meterWidth;
    return {signature_.getRight() + theme_.spacing.sm, (getHeight() - size) / 2, size, size};
}

void HeaderBar::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    // The unsaved dot after the project name.
    if (state_.modified)
    {
        g.setColour(c.accent);
        const int d = theme_.spacing.sm - theme_.spacing.xxs;
        g.fillEllipse(static_cast<float>(project_.getRight() - theme_.spacing.sm),
                      static_cast<float>(project_.getBounds().getCentreY() - d / 2),
                      static_cast<float>(d),
                      static_cast<float>(d));
    }
    // The divider before the panel toggles.
    g.setColour(c.lineStrong);
    g.fillRect(mixer_.getX() - theme_.spacing.md, theme_.spacing.md, 1, getHeight() - 2 * theme_.spacing.md);
    // The master clip indicator, only while lit.
    if (clipLit_)
    {
        g.setColour(c.meterClip);
        g.fillRect(clipIndicatorBounds());
    }
}

void HeaderBar::mouseDown(const juce::MouseEvent& e)
{
    if (clipLit_ && clipIndicatorBounds().expanded(theme_.spacing.xs).contains(e.getPosition()) &&
        onMasterClipClicked)
        onMasterClipClicked();
}

void HeaderBar::resized()
{
    const int h = getHeight();
    const int control = theme_.sizes.control;
    const int large = theme_.sizes.controlLarge;
    const int md = theme_.spacing.md, sm = theme_.spacing.sm, xl = theme_.spacing.xl;
    auto bar = getLocalBounds().reduced(md, 0);
    const auto place = [&](juce::Component& c, int w, int height, int gapAfter)
    {
        c.setBounds(bar.removeFromLeft(w).withSizeKeepingCentre(w, height));
        bar.removeFromLeft(gapAfter);
    };
    // Left: app, project, undo/redo.
    place(app_, control, control, md);
    const int projectW =
        std::min(220,
                 juce::GlyphArrangement::getStringWidthInt(theme_.label(), project_.getButtonText()) +
                     2 * md + theme_.sizes.icon + sm);
    place(project_, projectW, control, md);
    place(undo_, control, control, sm);
    place(redo_, control, control, xl);
    // Right cluster, placed from the right edge.
    auto right = bar;
    const auto placeRight = [&](juce::Component& c, int w, int gapBefore)
    {
        c.setBounds(right.removeFromRight(w).withSizeKeepingCentre(w, control));
        right.removeFromRight(gapBefore);
    };
    placeRight(browser_, control, sm);
    placeRight(editor_, control, sm);
    placeRight(mixer_, control, md + 1 + md); // the divider sits in this gap
    placeRight(zoomFit_, control, sm);
    placeRight(ripple_, control, sm);
    placeRight(snap_, control + sm, xl);
    bar.setRight(right.getRight());
    // Centre: the transport cluster.
    const int clusterW = 4 * large + 3 * md + xl + 4 * control + 3 * sm + xl + 150 + sm + 110 + xl + 72 + sm +
                         64 + sm + theme_.sizes.meterWidth;
    auto cluster = bar.withSizeKeepingCentre(std::min(clusterW, bar.getWidth()), h);
    if (cluster.getX() < bar.getX())
        cluster.setX(bar.getX());
    bar = cluster;
    place(goToStart_, large, large, md);
    place(play_, large, large, md);
    place(stop_, large, large, md);
    place(record_, large, large, xl);
    place(loop_, control, control, sm);
    place(punch_, control, control, sm);
    place(click_, control, control, sm);
    place(countIn_, control, control, xl);
    place(bars_, 150, control, sm);
    place(time_, 110, control, xl);
    place(tempo_, 72, control, sm);
    place(signature_, 64, control, sm);
}

} // namespace aural::ui
