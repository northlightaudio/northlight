#include "ui/shell/HeaderBar.h"

#include "ui/NorthlightLookAndFeel.h"

#include <algorithm>

namespace northlight::ui
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

// ----- WindowButton -------------------------------------------------------------------

WindowButton::WindowButton(const Theme& theme, Icon icon, const juce::String& tooltip, bool closes)
    : juce::Button(tooltip), theme_(theme), icon_(icon), closes_(closes)
{
    setTooltip(tooltip);
    setWantsKeyboardFocus(false);
}

void WindowButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto& c = theme_.colours;
    auto glyph = c.textSecondary;
    if (isMouseOver || isButtonDown)
    {
        g.setColour(closes_ ? (isButtonDown ? c.recordHover : c.record)
                            : (isButtonDown ? c.surfacePressed : c.surface2));
        g.fillRect(getLocalBounds());
        glyph = closes_ ? c.textOnColour : c.textPrimary;
    }
    const float size = static_cast<float>(theme_.sizes.icon);
    Icons::draw(g, icon_, getLocalBounds().toFloat().withSizeKeepingCentre(size, size), glyph);
}

// ----- TimeDisplay ----------------------------------------------------------------------

void TimeDisplay::setSecondary(bool secondary)
{
    secondary_ = secondary;
    repaint();
}

void TimeDisplay::setFramed(bool framed)
{
    framed_ = framed;
    repaint();
}

void TimeDisplay::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto& m = theme_.material;
    const auto bounds = getLocalBounds().toFloat();
    auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel());
    if (framed_)
    {
        g.setColour(m.enabled ? m.readoutWell : c.surface2);
        g.fillRoundedRectangle(bounds, theme_.radius.sm);
        if (m.enabled && lnf != nullptr)
            lnf->drawInset(g, bounds, theme_.radius.sm, m.grooveInset);
    }
    if (editor_ != nullptr)
        return;
    const juce::Colour text = tone_ == Tone::recording ? (m.enabled ? m.readoutRecord : c.record)
                              : tone_ == Tone::countIn ? c.warning
                              : secondary_             ? c.textSecondary
                              : m.enabled              ? m.readoutText
                                                       : c.textPrimary;
    g.setFont(primary_ ? theme_.timeLarge() : theme_.timeSmall());
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
    : theme_(theme), app_(theme, Icon::mark, "Northlight: About, Preferences, Audio Setup, Plugins"),
      undo_(theme, Icon::undo, "Undo  Ctrl+Z"), redo_(theme, Icon::redo, "Redo  Ctrl+Shift+Z"),
      goToStart_(theme, Icon::goToStart, "Go to start  Enter"), play_(theme, Icon::play, "Play  Space"),
      stop_(theme, Icon::stop, "Stop  Space"), record_(theme, Icon::record, "Record  R"),
      loop_(theme, Icon::loop, "Loop  L"), punch_(theme, Icon::punch, "Punch  P"),
      click_(theme, Icon::metronome, "Click  C"), countIn_(theme, Icon::countIn, "Count-in: 1 bar"),
      bars_(theme, true), time_(theme, false), tempo_(theme), snap_(theme, Icon::snap, "Snap: Adaptive  N"),
      ripple_(theme, Icon::ripple, "Ripple  Shift+N"), zoomFit_(theme, Icon::zoomFit, "Zoom to fit  Z"),
      minimise_(theme, Icon::windowMinimise, "Minimise", false),
      maximise_(theme, Icon::windowMaximise, "Maximise", false),
      closeWindow_(theme, Icon::close, "Close", true), mixer_(theme, Icon::mixer, "Mixer  F2"),
      editor_(theme, Icon::inspector, "Editor  F3"), browser_(theme, Icon::browser, "Browser  F4")
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
    // Windows normally takes these clicks as caption controls; the handlers are
    // here too for when it does not.
    minimise_.onClick = [this]
    {
        if (onMinimise)
            onMinimise();
    };
    maximise_.onClick = [this]
    {
        if (onMaximise)
            onMaximise();
    };
    closeWindow_.onClick = [this]
    {
        if (onCloseWindow)
            onCloseWindow();
    };
    for (auto* b : {&goToStart_, &play_, &stop_, &record_})
    {
        b->setSize(theme_.sizes.controlLarge, theme_.sizes.controlLarge);
        b->setIconSize(theme_.sizes.icon); // half the key, as in the brand mockup
        b->setKey(true);
    }
    // Play fills with the accent while playing, as Record fills while recording.
    play_.setSolidWhenOn(true);
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

    project_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    project_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
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
    bars_.setFramed(false);
    time_.setFramed(false);
    time_.setSecondary(true);
    tempo_.setReadout(true);
    tempo_.setUnit("BPM");
    signature_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "readout");
    signature_.setColour(juce::ComboBox::textColourId, theme_.colours.textPrimary);
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
    for (auto* c : {&minimise_, &maximise_, &closeWindow_})
        addChildComponent(c);
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
    if (project_.getButtonText() != s.projectName)
    {
        project_.setButtonText(s.projectName);
        resized(); // the button's width follows the name
    }
    project_.setTooltip(s.projectPath.isNotEmpty() ? s.projectPath : juce::String("Unsaved project"));
    undo_.setEnabled(s.canUndo);
    undo_.setTooltip(s.canUndo ? "Undo " + s.undoName + "  Ctrl+Z" : "Undo  Ctrl+Z");
    redo_.setEnabled(s.canRedo);
    redo_.setTooltip(s.canRedo ? "Redo " + s.redoName + "  Ctrl+Shift+Z" : "Redo  Ctrl+Shift+Z");
    play_.setIcon(s.playing ? Icon::pause : Icon::play);
    play_.setTooltip(s.playing ? "Stop  Space" : "Play  Space");
    play_.setToggleState(s.playing, juce::dontSendNotification);
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
    maximise_.setIcon(s.maximised ? Icon::windowRestore : Icon::windowMaximise);
    maximise_.setTooltip(s.maximised ? "Restore down" : "Maximise");
    if (closeWindow_.isVisible() != s.windowControls)
    {
        for (auto* b : {&minimise_, &maximise_, &closeWindow_})
            b->setVisible(s.windowControls);
        resized();
    }
    if (s.masterClipped != clipLit_)
    {
        clipLit_ = s.masterClipped;
        repaint(clipIndicatorBounds());
    }
    repaint(getLocalBounds().removeFromLeft(app_.getRight()));
}

juce::Component::WindowControlKind HeaderBar::windowControlAt(juce::Point<float> point) const
{
    using Kind = juce::Component::WindowControlKind;
    if (state_.windowControls)
    {
        if (minimise_.getBounds().toFloat().contains(point))
            return Kind::minimise;
        if (maximise_.getBounds().toFloat().contains(point))
            return Kind::maximise;
        if (closeWindow_.getBounds().toFloat().contains(point))
            return Kind::close;
    }
    for (const auto* child : getChildren())
        if (child->isVisible() && child->getBounds().toFloat().contains(point))
            return Kind::client;
    if (clipLit_ && clipIndicatorBounds().toFloat().contains(point))
        return Kind::client;
    return Kind::caption;
}

juce::Rectangle<int> HeaderBar::clipIndicatorBounds() const
{
    const int size = theme_.sizes.meterWidth;
    return {readout_.getRight() + theme_.spacing.sm, (getHeight() - size) / 2, size, size};
}

void HeaderBar::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto& m = theme_.material;
    g.fillAll(c.surface1);
    // The bar's lower edge.
    g.setColour(c.lineSubtle);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    // The trays and the readout well: a darker bed recessed into the bar. The
    // recess is a soft shade fading down from the top edge over a few pixels, not
    // the groove's hard inset (which reads as a black line on a shallow tray), and
    // a faint rim.
    const float radius = theme_.radius.md;
    const auto recess = [&](juce::Rectangle<float> r, juce::Colour fill)
    {
        g.setColour(fill);
        g.fillRoundedRectangle(r, radius);
        if (m.enabled)
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path shape;
            shape.addRoundedRectangle(r, radius);
            g.reduceClipRegion(shape);
            g.setGradientFill(juce::ColourGradient::vertical(
                c.canvas.withAlpha(0.55f), r.getY(), c.canvas.withAlpha(0.0f), r.getY() + 5.0f));
            g.fillRect(r.withHeight(5.0f));
        }
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    };
    for (const auto& tray : trays_)
        recess(tray.toFloat(), c.surface0);
    // The readout well: one dark field, its values told apart by size and colour.
    if (!readout_.isEmpty())
        recess(readout_.toFloat(), m.enabled ? m.readoutWell : c.surface2);
    // The unsaved dot, between the project name and the menu chevron. It sits a
    // fixed distance before the chevron's zone (the look and feel's icon + xs at
    // the button's right), in the room the button's width reserves for it, so it
    // keeps clear of both and the bar does not shift when it appears.
    if (state_.modified)
    {
        const int d = unsavedDot;
        const int chevronZone = project_.getRight() - (theme_.sizes.icon + theme_.spacing.xs);
        const int x = chevronZone - unsavedDotGap / 2 - d;
        g.setColour(c.accent);
        g.fillEllipse(static_cast<float>(x),
                      static_cast<float>(project_.getBounds().getCentreY() - d / 2),
                      static_cast<float>(d),
                      static_cast<float>(d));
    }
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
    // Every control is controlLarge tall; each cluster sits in a tray (or the
    // readout well) that adds xs all round, with xxs between the controls in
    // it (xs between the transport keys, sm between the readout's fields) and
    // md between trays (screens/transport.md).
    const int key = theme_.sizes.controlLarge;
    const int pad = theme_.spacing.xs, inner = theme_.spacing.xxs, between = theme_.spacing.md;
    const int trayH = key + 2 * pad;
    const int trayY = (getHeight() - trayH) / 2;
    trays_.clear();

    struct Item
    {
        juce::Component* c;
        int w;
    };
    using Items = std::vector<Item>;
    const auto widthOf = [&](const Items& items, int gap)
    {
        int w = 2 * pad - gap;
        for (const auto& i : items)
            w += i.w + gap;
        return w;
    };
    // Places a group at x in a tray (or the readout well) and returns its width.
    const auto group = [&](int x, const Items& items, bool readout, int gap)
    {
        const int w = widthOf(items, gap);
        const juce::Rectangle<int> box(x, trayY, w, trayH);
        if (readout)
            readout_ = box;
        else
            trays_.push_back(box);
        int cx = x + pad;
        for (const auto& i : items)
        {
            i.c->setBounds(cx, trayY + pad, i.w, key);
            cx += i.w + gap;
        }
        return w;
    };
    const int keyGap = theme_.spacing.xs, fieldGap = theme_.spacing.sm;

    // The name, its padding, the chevron, and room for the unsaved dot on both
    // sides (so the centred name does not move when the dot shows).
    const int fullProjectW = std::min(
        224,
        juce::GlyphArrangement::getStringWidthInt(theme_.label(), project_.getButtonText()) +
            2 * theme_.spacing.md + theme_.sizes.icon + theme_.spacing.sm + 2 * (unsavedDot + unsavedDotGap));
    const Items transport{{&goToStart_, key}, {&play_, key}, {&stop_, key}, {&record_, key}};
    const Items modes{{&loop_, key}, {&punch_, key}, {&click_, key}, {&countIn_, key}};
    const Items edit{{&snap_, key + theme_.spacing.sm}, {&ripple_, key}, {&zoomFit_, key}};
    const Items panels{{&mixer_, key}, {&editor_, key}, {&browser_, key}};
    const auto leftItems = [&](int projectW)
    { return Items{{&project_, projectW}, {&undo_, key}, {&redo_, key}}; };
    const auto readoutItems = [&](bool withTime)
    {
        return withTime ? Items{{&bars_, 136}, {&time_, 104}, {&tempo_, 92}, {&signature_, 60}}
                        : Items{{&bars_, 136}, {&tempo_, 92}, {&signature_, 60}};
    };

    // Right, from the edge: the window buttons (custom chrome), the panel toggles,
    // then snap, ripple and zoom.
    int right = getWidth() - theme_.spacing.md;
    if (state_.windowControls)
    {
        // Full height, as on Windows; the bar's bottom rule stays visible.
        const int w = windowButtonWidth, h = getHeight() - 1;
        int bx = getWidth();
        for (auto* b : {&closeWindow_, &maximise_, &minimise_})
        {
            bx -= w;
            b->setBounds(bx, 0, w, h);
        }
        right = bx - theme_.spacing.sm;
    }
    right -= widthOf(panels, inner);
    group(right, panels, false, inner);
    right -= between + widthOf(edit, inner);
    group(right, edit, false, inner);
    right -= between;

    // Centre: the readout, centred in the window, flanked by the transport keys on
    // its left and the mode toggles on its right. The flanking gap holds the
    // master clip indicator on the right, and matches it on the left. When the
    // window is too narrow the readout moves off centre just enough for both
    // flanks to clear the side clusters; when even that does not fit, the bar
    // gives way in steps: the clock time leaves the readout (bars stay), then the
    // project name shortens (screens/transport.md). Nothing ever overlaps at the
    // window's minimum width.
    const int transportW = widthOf(transport, keyGap), modesW = widthOf(modes, inner);
    const int flank = 2 * theme_.spacing.sm + theme_.sizes.meterWidth;
    const int leftStart = theme_.spacing.md + key + theme_.spacing.sm;
    const int compactProjectW = std::min(fullProjectW, 96);
    struct Fit
    {
        int projectW;
        bool withTime;
    };
    const std::array<Fit, 3> fits{{{fullProjectW, true}, {fullProjectW, false}, {compactProjectW, false}}};
    Fit chosen = fits.back();
    for (const auto& fit : fits)
    {
        const int leftEnd = leftStart + widthOf(leftItems(fit.projectW), inner) + between;
        const int readoutW = widthOf(readoutItems(fit.withTime), fieldGap);
        if (leftEnd + transportW + flank + readoutW + flank + modesW <= right)
        {
            chosen = fit;
            break;
        }
    }

    // Left: the app mark on its own, then project, undo and redo.
    int x = theme_.spacing.md;
    app_.setBounds(x, trayY + pad, key, key);
    x += key + theme_.spacing.sm;
    x += group(x, leftItems(chosen.projectW), false, inner) + between;
    const int leftEnd = x;

    time_.setVisible(chosen.withTime);
    const auto readout = readoutItems(chosen.withTime);
    const int readoutW = widthOf(readout, fieldGap);
    const int minX = leftEnd + transportW + flank, maxX = right - modesW - flank - readoutW;
    x = std::max(minX, std::min((getWidth() - readoutW) / 2, maxX));
    group(x - flank - transportW, transport, false, keyGap);
    group(x, readout, true, fieldGap);
    group(x + readoutW + flank, modes, false, inner);
}

} // namespace northlight::ui
