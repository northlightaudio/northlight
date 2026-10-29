// The MIDI faces (design-system.md "MIDI face", screens/plugins.md 7.4h): the
// Arpeggiator, Chord, Chord Pads, Velocity and Scale. Each is a display in a well
// fed by the effect's note feedback, then its controls in Heather, in captioned
// groups.

#include "core/Model.h"
#include "core/NoteNames.h"
#include "engine/Midi.h"
#include "plugins/midi/Chords.h"
#include "plugins/midi/MidiTools.h"
#include "plugins/midi/TimeEffects.h"
#include "plugins/midi/Utilities.h"
#include "ui/OpticalCentre.h"
#include "ui/Wells.h"
#include "ui/components/ChoiceControl.h"
#include "ui/strip/CardParts.h"
#include "ui/strip/MidiParts.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace northlight::ui::strip
{

namespace
{
namespace pm = plugins::midi;
using engine::NoteFeedback;

// A copy of the effect's note feedback, for painting.
struct Notes
{
    std::array<bool, 128> held{}, sounding{}, last{};
    int lastInput = -1;
    std::array<int, 32> order{};
    int orderCount = 0;
    std::int64_t step = -1;
    std::array<std::uint16_t, 16> velocities{};
    std::uint32_t velocityCount = 0;
};

int rounded(float v)
{
    return static_cast<int>(std::lround(v));
}

// The base of the five faces: the display, bound pill rows and chips, the key a
// face plays in, and the note feedback read every tick.
class MidiFaceCard : public ModuleCard
{
public:
    MidiFaceCard(
        const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert, int width)
        : ModuleCard(theme, host, channelId, insert), width_(width)
    {
        display_.onPaint = [this](juce::Graphics& g, juce::Rectangle<float> r)
        {
            drawWell(g, theme_, r);
            paintDisplay(
                g, r.reduced(static_cast<float>(theme_.spacing.sm), static_cast<float>(theme_.spacing.xs)));
        };
        display_.setComponentID("midi-display");
        addAndMakeVisible(display_);
    }

    int preferredWidth() const override { return width_; }
    int preferredBodyHeight() const override
    {
        return displayHeight() + 6 + controlsHeight() + theme_.spacing.xs;
    }
    int minimumBodyHeight() const override { return 64 + 6 + controlsHeight() + theme_.spacing.xs; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        groups_.paint(g, theme_, g.getClipBounds().toFloat());
    }

protected:
    static constexpr int captionHeight = 16;
    static constexpr int pillHeight = 24;
    static constexpr int rowGap = 8;
    static constexpr int groupGap = 10;

    virtual int displayHeight() const { return 112; }
    // A taller dock gives the display its spare height, up to this.
    virtual int largestDisplay() const { return displayHeight() + 120; }
    virtual int controlsHeight() const = 0;
    virtual void layoutControls(juce::Rectangle<int> area) = 0;
    // The display's corner chips (and anything else over the display).
    virtual void layoutDisplay(juce::Rectangle<int> display) { juce::ignoreUnused(display); }
    virtual void paintDisplay(juce::Graphics& g, juce::Rectangle<float> inner) = 0;
    // Controls that do nothing in the current mode go to 50% (moot).
    virtual void updateMoot() {}

    int pillRowHeight() const { return captionHeight + theme_.spacing.xs + pillHeight; }
    int knobRowHeight() const { return captionHeight + theme_.spacing.xs + knobCellHeight(); }

    // ----- bound controls ------------------------------------------------------------
    // `offset`: the value of the first pill (1 for octaves 1..4, -2 for a register).
    PillRow& addPills(
        int index, std::vector<juce::String> labels, bool words, const juce::String& tooltip, int offset = 0)
    {
        auto row = std::make_unique<PillRow>(theme_, colour(), tooltip);
        row->labels = std::move(labels);
        row->words = words;
        row->fitLabels = true;
        row->onChange = [this, index, tooltip, offset](int pill)
        {
            beginGesture(tooltip);
            setParam(index, static_cast<float>(pill + offset));
            endGesture();
        };
        addAndMakeVisible(*row);
        auto& ref = *row;
        pills_.push_back({std::move(row), index, offset});
        return ref;
    }
    CornerChoice& addChip(int index, const juce::String& tooltip)
    {
        auto chip = std::make_unique<CornerChoice>(theme_, colour());
        chip->setChoices(info(index));
        chip->setTooltip(tooltip);
        chip->onChoose = [this, index, tooltip](int value)
        {
            beginGesture(tooltip);
            setParam(index, static_cast<float>(value));
            endGesture();
        };
        addAndMakeVisible(*chip);
        auto& ref = *chip;
        chips_.push_back({std::move(chip), index});
        return ref;
    }
    Knob& addBud(int index, const juce::String& label = {})
    {
        auto& knob = addKnob(index, label);
        knob.setStyle(Knob::Style::bud);
        return knob;
    }
    void setMoot(juce::Component& c, bool moot)
    {
        c.setAlpha(moot ? 0.5f : 1.0f);
        c.setEnabled(!moot);
        if (auto* knob = dynamic_cast<Knob*>(&c))
            if (auto* label = labelFor(*knob))
                label->setAlpha(moot ? 0.5f : 1.0f);
    }

    // ----- layout --------------------------------------------------------------------
    // A group in a row: its caption over `width` from the row's left (which it
    // takes, and the gap after it); returns the area under the caption.
    juce::Rectangle<int> group(juce::Rectangle<int>& row, int width, const juce::String& title)
    {
        auto column = row.removeFromLeft(std::min(width, row.getWidth()));
        row.removeFromLeft(groupGap);
        if (title.isNotEmpty())
            groups_.addCaption(title, column.removeFromTop(captionHeight).toFloat(), true);
        else
            column.removeFromTop(captionHeight);
        column.removeFromTop(theme_.spacing.xs);
        return column;
    }
    // A pill row (or a toggle) in a group's area: as tall as a pill, centred where
    // a knob's ring sits in a knob row, or at the top in a pill row.
    void placeInRow(juce::Component& c, juce::Rectangle<int> area, int width, bool knobRow)
    {
        auto slot = knobRow ? aboveLabelLine(area.withHeight(knobCellHeight())) : area;
        c.setBounds(
            slot.withSizeKeepingCentre(std::min(width, slot.getWidth()), pillHeight).withX(area.getX()));
    }
    void placeKnobs(juce::Rectangle<int> area, std::initializer_list<Knob*> knobs)
    {
        for (auto* k : knobs)
        {
            placeKnob(*k, area.removeFromLeft(knobCellWidth()).withHeight(knobCellHeight()));
        }
    }
    static int knobsWidth(int n) { return n * 60; }
    // A pill row's width: what its labels need.
    static int pillsWidth(const PillRow& row) { return row.labelsWidth(); }

    void layoutBody(juce::Rectangle<int> body) override
    {
        groups_.clear();
        auto display = body.removeFromTop(std::max(
            64, std::min(largestDisplay(), body.getHeight() - controlsHeight() - 6 - theme_.spacing.xs)));
        body.removeFromTop(6);
        display_.setBounds(display);
        layoutDisplay(display);
        layoutControls(body.reduced(theme_.spacing.xs, 0));
    }

    void paramsChanged() override
    {
        for (auto& p : pills_)
        {
            p.row->selected = rounded(param(p.index)) - p.offset;
            p.row->repaint();
        }
        for (auto& c : chips_)
        {
            c.chip->selected = rounded(param(c.index));
            c.chip->repaint();
        }
        updateMoot();
        display_.repaint();
    }

    void ticked(double) override
    {
        // The project's key can change without the effect's parameters moving.
        if (const auto* project = host_.project ? host_.project() : nullptr;
            project != nullptr && !(project->key == shownKey_))
        {
            shownKey_ = project->key;
            updateMoot();
            display_.repaint();
        }
        const auto* live = liveProcessor();
        const auto* n = live != nullptr ? live->noteFeedback() : nullptr;
        if (n == nullptr)
            return;
        const auto changes = n->changes.load(std::memory_order_relaxed);
        if (changes == seen_)
            return;
        seen_ = changes;
        for (int i = 0; i < 128; ++i)
        {
            notes_.held[static_cast<size_t>(i)] = NoteFeedback::test(n->held, i);
            notes_.sounding[static_cast<size_t>(i)] = NoteFeedback::test(n->sounding, i);
            notes_.last[static_cast<size_t>(i)] = NoteFeedback::test(n->last, i);
        }
        notes_.lastInput = n->lastInput.load(std::memory_order_relaxed);
        notes_.orderCount = std::clamp(n->orderCount.load(std::memory_order_relaxed), 0, 32);
        for (int i = 0; i < notes_.orderCount; ++i)
            notes_.order[static_cast<size_t>(i)] =
                n->order[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        notes_.step = n->step.load(std::memory_order_relaxed);
        notes_.velocityCount = n->velocityCount.load(std::memory_order_relaxed);
        for (size_t i = 0; i < notes_.velocities.size(); ++i)
            notes_.velocities[i] = n->velocities[i].load(std::memory_order_relaxed);
        notesChanged();
        display_.repaint();
    }
    virtual void notesChanged() {}

    // ----- the key -------------------------------------------------------------------
    // The key the effect plays in: the project's when it follows it and one is
    // set, else its own (plugins::midi::keyFor).
    pm::Key keyOf(int follow, int root, int kind) const
    {
        const auto* project = host_.project ? host_.project() : nullptr;
        engine::PlayHead head;
        if (project != nullptr && project->key.isSet())
        {
            head.keyRoot = project->key.root;
            head.keyMask = project->key.mask();
        }
        return pm::keyFor(head, rounded(param(follow)) == 1, param(root), param(kind));
    }
    bool followsProjectKey(int follow) const
    {
        const auto* project = host_.project ? host_.project() : nullptr;
        return rounded(param(follow)) == 1 && project != nullptr && project->key.isSet();
    }
    // The key chips in the display's top-right corner: follow (Project, Own), root
    // and scale, the last two at 50% while the project's key is used.
    void addKeyChips(int follow, int root, int kind)
    {
        followChip_ = &addChip(follow, "Which key");
        rootChip_ = &addChip(root, "The effect's own key");
        scaleChip_ = &addChip(kind, "The effect's own scale");
        keyParams_ = {follow, root, kind};
    }
    void updateKeyChips(bool meaningful = true)
    {
        if (followChip_ == nullptr)
            return;
        // The root and scale chips show the key in use: the project's (at 50%,
        // not to be chosen) while it is followed, else the effect's own.
        const auto* project = host_.project ? host_.project() : nullptr;
        const bool projectSet = project != nullptr && project->key.isSet();
        followChip_->choices = {"Own key", projectSet ? "Project key" : "Project key (none set)"};
        const bool own = !followsProjectKey(keyParams_[0]);
        if (!own)
        {
            rootChip_->selected = project->key.root;
            const auto it = std::find(pm::scaleKinds.begin(), pm::scaleKinds.end(), project->key.scale);
            scaleChip_->selected = static_cast<int>(it - pm::scaleKinds.begin());
        }
        setMoot(*followChip_, !meaningful);
        setMoot(*rootChip_, !meaningful || !own);
        setMoot(*scaleChip_, !meaningful || !own);
        layoutKeyChips();
    }
    void layoutKeyChips()
    {
        if (followChip_ == nullptr || keyArea_.isEmpty())
            return;
        auto row = keyArea_;
        for (auto* chip : {scaleChip_, rootChip_, followChip_})
        {
            // Each chip as wide as the choice it shows ("Project key", "C", "Major").
            const int w = shownWidth(*chip);
            chip->setBounds(row.removeFromRight(w).withSizeKeepingCentre(w, 18));
            row.removeFromRight(theme_.spacing.xs);
        }
        chipsLeft_ = followChip_->getX() - display_.getX();
    }
    int shownWidth(const CornerChoice& chip) const
    {
        const auto font = theme_.font(Theme::Face::ui, theme_.fonts.size.micro, theme_.fonts.weight.semibold);
        const auto i =
            static_cast<size_t>(std::clamp(chip.selected, 0, static_cast<int>(chip.choices.size()) - 1));
        return juce::GlyphArrangement::getStringWidthInt(font, chip.choices[i]) + 26;
    }
    void setKeyArea(juce::Rectangle<int> area)
    {
        keyArea_ = area;
        layoutKeyChips();
    }
    // The chord names and the like, top-left in the display.
    // Left of the key chips, ellipsised if it must be.
    void drawDisplayTitle(juce::Graphics& g, juce::Rectangle<float> inner, const juce::String& text, bool lit)
    {
        g.setColour(lit ? colour() : colour().withAlpha(0.6f));
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.label, theme_.fonts.weight.semibold));
        auto area = inner.removeFromTop(18.0f);
        if (followChip_ != nullptr && chipsLeft_ > 0)
            area =
                area.withRight(std::min(area.getRight(), static_cast<float>(chipsLeft_ - theme_.spacing.sm)));
        g.drawText(text, area, juce::Justification::centredLeft, true);
    }

    Notes notes_;
    FaceDisplay display_;
    KnobGroups groups_;

private:
    struct BoundPills
    {
        std::unique_ptr<PillRow> row;
        int index;
        int offset;
    };
    struct BoundChip
    {
        std::unique_ptr<CornerChoice> chip;
        int index;
    };
    int width_;
    std::vector<BoundPills> pills_;
    std::vector<BoundChip> chips_;
    std::uint32_t seen_ = 0xFFFFFFFFu;
    CornerChoice *followChip_ = nullptr, *rootChip_ = nullptr, *scaleChip_ = nullptr;
    std::array<int, 3> keyParams_{};
    juce::Rectangle<int> keyArea_;
    int chipsLeft_ = 0; // where the key chips start in the display
    core::MusicalKey shownKey_;
};

constexpr int wide = 416;
constexpr int medium = 320;

// A keyboard view around `note`: the octave below it and the two from it.
KeyboardView viewAround(int note, int octaves)
{
    KeyboardView v;
    v.octaves = octaves;
    v.lowNote = std::clamp((note / 12) * 12 - 12, 0, 127 - 12 * octaves);
    return v;
}

// ----- Arpeggiator ----------------------------------------------------------------------------

namespace ap = plugins::arpeggiator;

class ArpeggiatorFace final : public MidiFaceCard
{
public:
    ArpeggiatorFace(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : MidiFaceCard(theme, host, channelId, insert, wide)
    {
        mode_ = &addPills(ap::mode,
                          {juce::String::fromUTF8("\xe2\x86\x91"),
                           juce::String::fromUTF8("\xe2\x86\x93"),
                           juce::String::fromUTF8("\xe2\x86\x91\xe2\x86\x93"),
                           juce::String::fromUTF8("\xe2\x86\x93\xe2\x86\x91"),
                           "Played",
                           "Random",
                           "Chord"},
                          true,
                          "Mode: up, down, up and down, down and up, as played, random, chord");
        latch_ = &addToggle(ap::latch, "Latch");
        latch_->setTooltip("Keep playing after the keys are released");
        rate_ = &addChip(ap::rate, "Rate");
        octaves_ = &addPills(ap::octaves, {"1", "2", "3", "4"}, false, "Octaves", 1);
        gate_ = &addBud(ap::gate, "Gate");
        swing_ = &addBud(ap::swing, "Swing");
        fixed_ = &addToggle(ap::velocity, "Fixed");
        fixed_->setTooltip("Every step at one velocity");
        fixedVelocity_ = &addBud(ap::fixedVelocity, "Velocity");
        paramsChanged();
    }

private:
    int controlsHeight() const override { return pillRowHeight() + rowGap + knobRowHeight(); }
    void layoutDisplay(juce::Rectangle<int> display) override
    {
        const int w = rate_->preferredWidth();
        rate_->setBounds(
            display.reduced(theme_.spacing.sm, theme_.spacing.xs).removeFromTop(18).removeFromRight(w));
    }
    void layoutControls(juce::Rectangle<int> area) override
    {
        auto top = area.removeFromTop(pillRowHeight());
        const int latchW = 60;
        auto modeArea = group(top, top.getWidth() - latchW - groupGap, "MODE");
        placeInRow(*mode_, modeArea, modeArea.getWidth(), false);
        auto latchArea = group(top, latchW, {});
        placeInRow(*latch_, latchArea, latchW, false);
        area.removeFromTop(rowGap);
        auto row = area.removeFromTop(knobRowHeight());
        const int octW = std::max(pillsWidth(*octaves_), 120);
        auto oct = group(row, octW, "OCTAVES");
        placeInRow(*octaves_, oct, octW, true);
        placeKnobs(group(row, knobsWidth(2), "PLAY"), {gate_, swing_});
        auto vel = group(row, 64 + knobsWidth(1), "VELOCITY");
        placeInRow(*fixed_, vel, 56, true);
        placeKnobs(vel.withTrimmedLeft(64), {fixedVelocity_});
    }
    void updateMoot() override { setMoot(*fixedVelocity_, rounded(param(ap::velocity)) == 0); }

    // The pattern a bar of steps plays: per step, the notes (empty for a rest).
    struct Lane
    {
        int steps = 16;
        std::vector<std::vector<int>> notes; // per step
        std::vector<int> rows;               // the distinct notes, low to high
        int playing = -1;                    // the step lit
        bool preview = false;                // nothing held yet: a C major triad
        bool random = false;
    };
    Lane lane() const
    {
        Lane l;
        const double rate = pm::noteValueQuarters(param(ap::rate));
        l.steps = std::clamp(static_cast<int>(std::lround(4.0 / rate)), 1, 32);
        std::vector<int> keys;
        for (int i = 0; i < notes_.orderCount; ++i)
            keys.push_back(notes_.order[static_cast<size_t>(i)]);
        if (keys.empty())
        {
            keys = {60, 64, 67};
            l.preview = true;
        }
        const int mode = rounded(param(ap::mode));
        if (mode != ap::asPlayed)
            std::sort(keys.begin(), keys.end());
        const int n = static_cast<int>(keys.size());
        const int octaves = std::clamp(rounded(param(ap::octaves)), 1, 4);
        const int total = n * octaves;
        const std::int64_t k = notes_.step;
        const std::int64_t first = k >= 0 ? (k / l.steps) * l.steps : 0;
        l.playing = k >= 0 && !l.preview ? static_cast<int>(k - first) : -1;
        l.random = mode == ap::random;
        std::set<int> rows;
        for (int j = 0; j < l.steps; ++j)
        {
            std::vector<int> step;
            const auto s = static_cast<int>(first + j);
            if (mode == ap::chord)
            {
                for (int key : keys)
                    step.push_back(key + 12 * (s % octaves));
            }
            else if (!l.random)
            {
                const int p = ap::position(mode, s, total);
                step.push_back(keys[static_cast<size_t>(p % n)] + 12 * (p / n));
            }
            for (int note : step)
                rows.insert(note);
            l.notes.push_back(std::move(step));
        }
        if (l.random)
            for (int o = 0; o < octaves; ++o)
                for (int key : keys)
                    rows.insert(key + 12 * o);
        l.rows.assign(rows.begin(), rows.end());
        return l;
    }

    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> inner) override
    {
        const auto& c = theme_.colours;
        inner.removeFromTop(20.0f); // the rate chip's row
        const auto l = lane();
        if (l.rows.empty())
            return;
        const float colW = inner.getWidth() / static_cast<float>(l.steps);
        // Beat lines each quarter, the bar's end stronger.
        const int perBeat = std::max(1, l.steps / 4);
        for (int j = 0; j <= l.steps; j += perBeat)
        {
            g.setColour(j == 0 || j == l.steps ? c.lineStrong : c.lineSubtle);
            const float x = std::round(inner.getX() + colW * static_cast<float>(j));
            g.fillRect(x, inner.getY(), 1.0f, inner.getHeight());
        }
        const auto rowCount = static_cast<float>(l.rows.size());
        const float rowH = std::min(18.0f, inner.getHeight() / rowCount);
        const float laneTop = inner.getCentreY() - rowH * rowCount * 0.5f;
        const auto rowY = [&](int note)
        {
            const auto it = std::lower_bound(l.rows.begin(), l.rows.end(), note);
            const auto index = static_cast<float>(it - l.rows.begin());
            return laneTop + (rowCount - 1.0f - index) * rowH;
        };
        const double gate = std::clamp(param(ap::gate) / 100.0f, 0.05f, 1.0f);
        const double swing = param(ap::swing) / 100.0f;
        for (int j = 0; j < l.steps; ++j)
        {
            const bool playing = j == l.playing;
            const float late = (j & 1) != 0 ? static_cast<float>(swing * 0.5) * colW : 0.0f;
            const float x = inner.getX() + colW * static_cast<float>(j) + late + 1.0f;
            const float w = std::max(2.0f, static_cast<float>(gate) * colW - 2.0f);
            std::vector<int> shown = l.notes[static_cast<size_t>(j)];
            if (l.random)
            {
                // Random: what this step could play, faint; what it is playing, lit.
                for (int note : l.rows)
                {
                    g.setColour(colour().withAlpha(0.18f));
                    g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f)
                                      .withCentre({x + w * 0.5f, rowY(note) + rowH * 0.5f}));
                }
                shown.clear();
                if (playing)
                    for (int note : l.rows)
                        if (notes_.sounding[static_cast<size_t>(note)])
                            shown.push_back(note);
            }
            for (int note : shown)
            {
                g.setColour(colour().withAlpha(playing ? 1.0f : 0.45f));
                g.fillRoundedRectangle(x, rowY(note) + 1.0f, w, std::max(2.0f, rowH - 2.0f), 2.0f);
            }
        }
        if (l.preview)
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.caption());
            g.drawText("Hold a chord",
                       inner.withHeight(14.0f).withY(inner.getY() - 18.0f),
                       juce::Justification::centredLeft,
                       false);
        }
    }

    PillRow *mode_ = nullptr, *octaves_ = nullptr;
    PillToggle *latch_ = nullptr, *fixed_ = nullptr;
    CornerChoice* rate_ = nullptr;
    Knob *gate_ = nullptr, *swing_ = nullptr, *fixedVelocity_ = nullptr;
};

// ----- Chord ----------------------------------------------------------------------------------

namespace cp = plugins::chord;

class ChordFace final : public MidiFaceCard
{
public:
    ChordFace(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : MidiFaceCard(theme, host, channelId, insert, wide)
    {
        mode_ = &addPills(cp::mode, {"Shape", "In key"}, true, "Chord: by shape, or from the key");
        shape_ = std::make_unique<ChoiceControl>(theme_, info(cp::shape).choices, "Shape");
        shape_->setOnCard(true);
        shape_->onChange = [this](float v)
        {
            beginGesture("Shape");
            setParam(cp::shape, v);
            endGesture();
        };
        addAndMakeVisible(*shape_);
        size_ = &addPills(cp::size, {"Triad", "7th", "9th"}, true, "Size in the key");
        inversion_ = &addPills(cp::inversion, {"Root", "1st", "2nd", "3rd"}, true, "Inversion");
        voicing_ = &addPills(cp::voicing, {"Close", "Open", "Drop 2", "Spread"}, true, "Voicing");
        strum_ = &addPills(cp::strum, {"Off", "Up", "Down", "Alt"}, true, "Strum");
        strumTime_ = &addBud(cp::strumTime, "Strum time");
        addKeyChips(cp::follow, cp::root, cp::kind);
        paramsChanged();
    }

private:
    int displayHeight() const override { return 84; }
    int largestDisplay() const override { return 116; }
    int controlsHeight() const override { return 2 * pillRowHeight() + knobRowHeight() + 2 * rowGap; }
    void layoutDisplay(juce::Rectangle<int> display) override
    {
        setKeyArea(display.reduced(theme_.spacing.sm, theme_.spacing.xs).removeFromTop(18));
    }
    void layoutControls(juce::Rectangle<int> area) override
    {
        // CHORD and IN KEY; INVERSION and STRUM; VOICING and the strum's time.
        auto row = area.removeFromTop(pillRowHeight());
        const int modeW = pillsWidth(*mode_), shapeW = 116;
        auto chord = group(row, modeW + groupGap + shapeW, "CHORD");
        placeInRow(*mode_, chord, modeW, false);
        shape_->setBounds(chord.withTrimmedLeft(modeW + groupGap).withHeight(pillHeight).withWidth(shapeW));
        auto sizeArea = group(row, pillsWidth(*size_), "IN KEY");
        placeInRow(*size_, sizeArea, pillsWidth(*size_), false);
        area.removeFromTop(rowGap);
        row = area.removeFromTop(pillRowHeight());
        auto inv = group(row, pillsWidth(*inversion_), "INVERSION");
        placeInRow(*inversion_, inv, pillsWidth(*inversion_), false);
        auto str = group(row, pillsWidth(*strum_), "STRUM");
        placeInRow(*strum_, str, pillsWidth(*strum_), false);
        area.removeFromTop(rowGap);
        row = area.removeFromTop(knobRowHeight());
        auto voi = group(row, pillsWidth(*voicing_), "VOICING");
        placeInRow(*voicing_, voi, pillsWidth(*voicing_), true);
        placeKnobs(group(row, knobsWidth(1), {}), {strumTime_});
    }
    void updateMoot() override
    {
        const bool inKey = rounded(param(cp::mode)) == 1;
        shape_->setValue(param(cp::shape));
        setMoot(*shape_, inKey);
        setMoot(*size_, !inKey);
        setMoot(*strumTime_, rounded(param(cp::strum)) == 0);
        updateKeyChips(inKey);
    }

    // The chord the last key plays with the settings as they are now.
    plugins::ChordNotes chordFor(int note, plugins::ChordNotes* rootPosition) const
    {
        const bool inKey = rounded(param(cp::mode)) == 1;
        const auto key = keyOf(cp::follow, cp::root, cp::kind);
        const int inversion = rounded(param(cp::inversion)), voicing = rounded(param(cp::voicing));
        if (rootPosition != nullptr)
            *rootPosition = inKey
                                ? cp::inKey(note, rounded(param(cp::size)), key.root, key.mask, 0, cp::close)
                                : cp::byShape(note, rounded(param(cp::shape)), 0, cp::close);
        return inKey ? cp::inKey(note, rounded(param(cp::size)), key.root, key.mask, inversion, voicing)
                     : cp::byShape(note, rounded(param(cp::shape)), inversion, voicing);
    }

    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> inner) override
    {
        const bool preview = notes_.lastInput < 0;
        const int note = preview ? 60 : notes_.lastInput;
        const bool held = !preview && notes_.held[static_cast<size_t>(note)];
        plugins::ChordNotes root;
        const auto chord = chordFor(note, &root);
        auto view = viewAround(note, 3);
        for (int i = 0; i < chord.count; ++i)
            if (const int n = chord.notes[static_cast<size_t>(i)]; n >= 0 && n <= 127)
                view.lit[static_cast<size_t>(n)] = preview ? 0.45f : held ? 1.0f : 0.6f;
        if (!preview)
            view.held[static_cast<size_t>(note)] = held;
        if (rounded(param(cp::mode)) == 1)
            view.scaleMask = keyOf(cp::follow, cp::root, cp::kind).mask;
        const auto name = root.count > 0 ? plugins::chordName(root.notes[0], chord) : std::string();
        drawDisplayTitle(g,
                         inner,
                         preview ? juce::String::fromUTF8(name.c_str()) + "  (play a key)"
                                 : juce::String::fromUTF8(name.c_str()),
                         held);
        drawKeyboard(g, theme_, inner.withTrimmedTop(22.0f), colour(), view);
    }

    PillRow *mode_ = nullptr, *size_ = nullptr, *inversion_ = nullptr, *voicing_ = nullptr, *strum_ = nullptr;
    std::unique_ptr<ChoiceControl> shape_;
    Knob* strumTime_ = nullptr;
};

// ----- Chord Pads -----------------------------------------------------------------------------

namespace pp = plugins::chordpads;

class ChordPadsFace final : public MidiFaceCard
{
public:
    ChordPadsFace(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : MidiFaceCard(theme, host, channelId, insert, wide)
    {
        size_ = &addPills(pp::size, {"Triad", "7th"}, true, "Size");
        leading_ = &addToggle(pp::leading, "Lead");
        leading_->setTooltip("Voice leading: each chord takes the inversion nearest the last");
        bass_ = &addToggle(pp::bass, "Bass");
        bass_->setTooltip("The chord's root an octave below");
        octave_ = &addPills(pp::octave,
                            {juce::String::fromUTF8("\xe2\x88\x92"
                                                    "2"),
                             juce::String::fromUTF8("\xe2\x88\x92"
                                                    "1"),
                             "0",
                             "+1",
                             "+2"},
                            false,
                            "Register",
                            -2);
        addKeyChips(pp::follow, pp::root, pp::kind);
        paramsChanged();
    }

private:
    int displayHeight() const override { return 150; }
    int largestDisplay() const override { return 196; }
    int controlsHeight() const override { return pillRowHeight(); }
    void layoutDisplay(juce::Rectangle<int> display) override
    {
        setKeyArea(display.reduced(theme_.spacing.sm, theme_.spacing.xs).removeFromTop(18));
    }
    void layoutControls(juce::Rectangle<int> area) override
    {
        auto row = area.removeFromTop(pillRowHeight());
        const int sizeW = pillsWidth(*size_), toggleW = 52;
        auto chords = group(row, sizeW + 2 * (6 + toggleW), "CHORDS");
        placeInRow(*size_, chords, sizeW, false);
        placeInRow(*leading_, chords.withTrimmedLeft(sizeW + 6), toggleW, false);
        placeInRow(*bass_, chords.withTrimmedLeft(sizeW + 12 + toggleW), toggleW, false);
        auto reg = group(row, row.getWidth(), "REGISTER");
        placeInRow(*octave_, reg, reg.getWidth(), false);
    }
    void updateMoot() override { updateKeyChips(true); }

    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> inner) override
    {
        const auto& c = theme_.colours;
        const auto key = keyOf(pp::follow, pp::root, pp::kind);
        inner.removeFromTop(22.0f); // the key chips' row
        auto keys = inner.removeFromBottom(keyboardHeight);
        inner.removeFromBottom(6.0f);
        // The pads: seven white below, five black above, as a keyboard lies.
        const float gap = 4.0f;
        const float padW = (inner.getWidth() - gap * 6.0f) / 7.0f;
        const float padH = (inner.getHeight() - gap) / 2.0f;
        const int lastPc = notes_.lastInput >= 0 ? notes_.lastInput % 12 : -1;
        const bool held = notes_.lastInput >= 0 && notes_.held[static_cast<size_t>(notes_.lastInput)];
        constexpr int whites[7] = {0, 2, 4, 5, 7, 9, 11};
        constexpr int blacks[5] = {1, 3, 6, 8, 10};
        constexpr float blackAt[5] = {1.0f, 2.0f, 4.0f, 5.0f, 6.0f}; // between white pads i-1 and i
        const auto pad = [&](int pc, juce::Rectangle<float> r)
        {
            const bool playing = pc == lastPc && held;
            const bool last = pc == lastPc;
            g.setColour(playing ? colour().withAlpha(0.25f) : c.surface2);
            g.fillRoundedRectangle(r, theme_.radius.md);
            if (playing || last)
            {
                g.setColour(colour().withAlpha(playing ? 1.0f : 0.45f));
                g.drawRoundedRectangle(r.reduced(0.75f), theme_.radius.md, playing ? 1.5f : 1.0f);
            }
            const auto text = playing ? colour() : c.textSecondary;
            const int note = 60 + pc;
            g.setColour(text);
            auto inside = r.reduced(4.0f, 2.0f);
            g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.label, theme_.fonts.weight.semibold));
            g.drawText(
                juce::String::fromUTF8(plugins::chordpads::padNumeral(note, key.root, key.mask).c_str()),
                inside.removeFromTop(inside.getHeight() * 0.55f),
                juce::Justification::centredBottom,
                true);
            g.setFont(theme_.caption());
            g.drawText(
                juce::String::fromUTF8(
                    plugins::chordpads::padName(note, rounded(param(pp::size)), key.root, key.mask).c_str()),
                inside,
                juce::Justification::centredTop,
                true);
        };
        const auto bottom = inner.withTrimmedTop(padH + gap);
        for (int i = 0; i < 7; ++i)
            pad(whites[i],
                bottom.withX(bottom.getX() + (padW + gap) * static_cast<float>(i)).withWidth(padW));
        const float blackW = padW * 0.86f;
        for (int i = 0; i < 5; ++i)
        {
            const float centre = inner.getX() + (padW + gap) * blackAt[i] - gap * 0.5f;
            pad(blacks[i], juce::Rectangle<float>(centre - blackW * 0.5f, inner.getY(), blackW, padH));
        }
        // The voicing played, under the pads.
        const int centreNote = 54 + 12 * std::clamp(rounded(param(pp::octave)), -2, 2);
        KeyboardView view;
        view.octaves = 3;
        view.lowNote = std::clamp((centreNote - 12) / 12 * 12, 0, 91);
        bool any = false;
        for (int n = 0; n < 128; ++n)
        {
            if (notes_.sounding[static_cast<size_t>(n)])
                view.lit[static_cast<size_t>(n)] = 1.0f;
            else if (notes_.last[static_cast<size_t>(n)])
                view.lit[static_cast<size_t>(n)] = 0.45f;
            any = any || view.lit[static_cast<size_t>(n)] > 0.0f;
        }
        if (!any)
        {
            // Nothing played yet: the I chord where it would sound.
            const auto first =
                plugins::chordpads::padChord(60, rounded(param(pp::size)), key.root, key.mask, centreNote);
            for (int i = 0; i < first.count; ++i)
                view.lit[static_cast<size_t>(std::clamp(first.notes[static_cast<size_t>(i)], 0, 127))] =
                    0.45f;
        }
        view.scaleMask = key.mask;
        drawKeyboard(g, theme_, keys.withHeight(keyboardHeight), colour(), view);
    }

    PillRow *size_ = nullptr, *octave_ = nullptr;
    PillToggle *leading_ = nullptr, *bass_ = nullptr;
};

// ----- Velocity -------------------------------------------------------------------------------

namespace vp = plugins::velocity;

class VelocityFace final : public MidiFaceCard
{
public:
    VelocityFace(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : MidiFaceCard(theme, host, channelId, insert, wide)
    {
        dynamics_ = &addBud(vp::dynamics, "Dynamics");
        gain_ = &addBud(vp::gain, "Gain");
        offset_ = &addBud(vp::offset, "Offset");
        random_ = &addBud(vp::random, "Random");
        fixed_ = &addToggle(vp::fixed, "Fixed");
        fixed_->setTooltip("Every note at one velocity");
        fixedVelocity_ = &addBud(vp::fixedVelocity, "Velocity");
        minimum_ = &addBud(vp::minimum, "Minimum");
        maximum_ = &addBud(vp::maximum, "Maximum");
        centre_ = &addBud(vp::centre, "Centre");
        display_.onDown = [this](const juce::MouseEvent& e) { grab(e); };
        display_.onDrag = [this](const juce::MouseEvent& e) { drag(e); };
        display_.onUp = [this](const juce::MouseEvent&) { release(); };
        display_.onMove = [this](const juce::MouseEvent& e) { hover(e); };
        display_.onExit = [this]
        {
            hovered_ = -1;
            display_.repaint();
        };
        paramsChanged();
    }

private:
    int controlsHeight() const override { return 2 * knobRowHeight() + rowGap; }
    void layoutControls(juce::Rectangle<int> area) override
    {
        auto row = area.removeFromTop(knobRowHeight());
        placeKnobs(group(row, knobsWidth(4), "SHAPE"), {dynamics_, gain_, offset_, random_});
        auto fixed = group(row, 64 + knobsWidth(1), "FIXED");
        placeInRow(*fixed_, fixed, 56, true);
        placeKnobs(fixed.withTrimmedLeft(64), {fixedVelocity_});
        area.removeFromTop(rowGap);
        row = area.removeFromTop(knobRowHeight());
        placeKnobs(group(row, knobsWidth(3), "RANGE"), {minimum_, maximum_, centre_});
    }
    void updateMoot() override
    {
        const bool fixed = rounded(param(vp::fixed)) == 1;
        setMoot(*fixedVelocity_, !fixed);
        for (auto* k : {dynamics_, gain_, offset_, random_, minimum_, maximum_, centre_})
            setMoot(*k, fixed);
    }

    vp::Settings settings() const
    {
        vp::Settings s;
        s.dynamics = param(vp::dynamics);
        s.centre = param(vp::centre);
        s.gain = param(vp::gain);
        s.offset = param(vp::offset);
        s.minimum = param(vp::minimum);
        s.maximum = param(vp::maximum);
        s.fixed = rounded(param(vp::fixed)) == 1;
        s.fixedVelocity = param(vp::fixedVelocity);
        return s;
    }
    // The curve's area in the display, and velocity <-> position.
    juce::Rectangle<float> plot() const
    {
        return display_.getLocalBounds().toFloat().reduced(static_cast<float>(theme_.spacing.md), 10.0f);
    }
    float xOf(float v) const { return plot().getX() + (v - 1.0f) / 126.0f * plot().getWidth(); }
    float yOf(float v) const { return plot().getBottom() - (v - 1.0f) / 126.0f * plot().getHeight(); }
    float vOfX(float x) const
    {
        return std::clamp(1.0f + (x - plot().getX()) / plot().getWidth() * 126.0f, 1.0f, 127.0f);
    }
    float vOfY(float y) const
    {
        return std::clamp(1.0f + (plot().getBottom() - y) / plot().getHeight() * 126.0f, 1.0f, 127.0f);
    }
    // The handles: 0 minimum, 1 maximum, 2 centre.
    juce::Point<float> handle(int i) const
    {
        if (i == 0)
            return {plot().getRight(), yOf(param(vp::minimum))};
        if (i == 1)
            return {plot().getRight(), yOf(param(vp::maximum))};
        return {xOf(param(vp::centre)), plot().getBottom()};
    }
    int handleAt(juce::Point<float> p) const
    {
        if (rounded(param(vp::fixed)) == 1)
            return -1;
        int best = -1;
        float distance = 12.0f;
        for (int i = 0; i < 3; ++i)
            if (const float d = handle(i).getDistanceFrom(p); d < distance)
            {
                distance = d;
                best = i;
            }
        return best;
    }
    void hover(const juce::MouseEvent& e)
    {
        const int h = handleAt(e.position);
        if (h != hovered_)
        {
            hovered_ = h;
            display_.setMouseCursor(h == 2   ? juce::MouseCursor::LeftRightResizeCursor
                                    : h >= 0 ? juce::MouseCursor::UpDownResizeCursor
                                             : juce::MouseCursor::NormalCursor);
            display_.repaint();
        }
    }
    void grab(const juce::MouseEvent& e)
    {
        dragging_ = handleAt(e.position);
        if (dragging_ < 0)
            return;
        beginGesture(dragging_ == 0 ? "Minimum" : dragging_ == 1 ? "Maximum" : "Centre");
        drag(e);
    }
    void drag(const juce::MouseEvent& e)
    {
        if (dragging_ == 0)
            setParam(vp::minimum, std::round(vOfY(e.position.y)));
        else if (dragging_ == 1)
            setParam(vp::maximum, std::round(vOfY(e.position.y)));
        else if (dragging_ == 2)
            setParam(vp::centre, std::round(vOfX(e.position.x)));
    }
    void release()
    {
        if (dragging_ >= 0)
            endGesture();
        dragging_ = -1;
        display_.repaint();
    }

    void paintDisplay(juce::Graphics& g, juce::Rectangle<float>) override
    {
        const auto& c = theme_.colours;
        const auto r = plot();
        const auto s = settings();
        // The identity, faint, for reference; quarters of the range.
        g.setColour(c.lineSubtle);
        for (int q = 1; q < 4; ++q)
        {
            const float v = 1.0f + 126.0f * static_cast<float>(q) / 4.0f;
            g.fillRect(r.getX(), std::round(yOf(v)), r.getWidth(), 1.0f);
            g.fillRect(std::round(xOf(v)), r.getY(), 1.0f, r.getHeight());
        }
        juce::Path identity;
        identity.startNewSubPath(r.getBottomLeft());
        identity.lineTo(r.getTopRight());
        g.setColour(c.lineStrong);
        g.strokePath(identity, juce::PathStrokeType(1.0f));
        // The random spread, then the curve.
        const float spread = s.fixed ? 0.0f : param(vp::random);
        juce::Path curve, band;
        for (int v = 1; v <= 127; ++v)
        {
            const float out = static_cast<float>(vp::shape(v, s));
            const auto p = juce::Point<float>(xOf(static_cast<float>(v)), yOf(out));
            if (v == 1)
                curve.startNewSubPath(p);
            else
                curve.lineTo(p);
        }
        if (spread > 0.0f)
        {
            for (int v = 1; v <= 127; ++v)
            {
                const float hi = static_cast<float>(vp::shape(v, s, spread));
                const auto p = juce::Point<float>(xOf(static_cast<float>(v)), yOf(hi));
                if (v == 1)
                    band.startNewSubPath(p);
                else
                    band.lineTo(p);
            }
            for (int v = 127; v >= 1; --v)
                band.lineTo(xOf(static_cast<float>(v)), yOf(static_cast<float>(vp::shape(v, s, -spread))));
            band.closeSubPath();
            g.setColour(colour().withAlpha(0.15f));
            g.fillPath(band);
        }
        g.setColour(colour());
        g.strokePath(curve,
                     juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // The limits and the centre.
        if (!s.fixed)
        {
            for (int i = 0; i < 2; ++i)
            {
                const float y = handle(i).y;
                g.setColour(colour().withAlpha(0.45f));
                const float dashes[] = {3.0f, 3.0f};
                g.drawDashedLine(juce::Line<float>(r.getX(), y, r.getRight(), y), dashes, 2, 1.0f);
            }
            for (int i = 0; i < 3; ++i)
                drawHandle(g, theme_, colour(), handle(i), i == dragging_ || i == hovered_);
        }
        // The last notes, newest brightest.
        const auto count = std::min<std::uint32_t>(notes_.velocityCount, 16);
        for (std::uint32_t k = 0; k < count; ++k)
        {
            const auto index = (notes_.velocityCount - 1 - k) % 16;
            const auto pair = notes_.velocities[index];
            const float in = static_cast<float>(pair >> 8), out = static_cast<float>(pair & 0xFF);
            g.setColour(c.textPrimary.withAlpha(0.9f * (1.0f - static_cast<float>(k) / 16.0f)));
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({xOf(in), yOf(out)}));
        }
        // The axes' names, quietly.
        g.setColour(c.textTertiary);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.micro, theme_.fonts.weight.medium));
        g.drawText("IN",
                   juce::Rectangle<float>(r.getRight() - 64.0f, r.getBottom() - 14.0f, 40.0f, 12.0f),
                   juce::Justification::centredRight,
                   false);
        g.drawText("OUT",
                   r.withHeight(12.0f).withWidth(40.0f).translated(2.0f, 0.0f),
                   juce::Justification::centredLeft,
                   false);
    }

    Knob *dynamics_ = nullptr, *gain_ = nullptr, *offset_ = nullptr, *random_ = nullptr;
    Knob *fixedVelocity_ = nullptr, *minimum_ = nullptr, *maximum_ = nullptr, *centre_ = nullptr;
    PillToggle* fixed_ = nullptr;
    int dragging_ = -1, hovered_ = -1;
};

// ----- Scale ----------------------------------------------------------------------------------

namespace sp = plugins::scale;

class ScaleFace final : public MidiFaceCard
{
public:
    ScaleFace(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : MidiFaceCard(theme, host, channelId, insert, medium)
    {
        mode_ = &addPills(sp::mode,
                          {"Nearest", "Up", "Down", "Block", "White"},
                          true,
                          "Mode: to the nearest note, up, down, block, or the white keys play the scale");
        addKeyChips(sp::follow, sp::root, sp::kind);
        paramsChanged();
    }

private:
    int displayHeight() const override { return 84; }
    int largestDisplay() const override { return 116; }
    int controlsHeight() const override { return pillRowHeight(); }
    void layoutDisplay(juce::Rectangle<int> display) override
    {
        setKeyArea(display.reduced(theme_.spacing.sm, theme_.spacing.xs).removeFromTop(18));
    }
    void layoutControls(juce::Rectangle<int> area) override
    {
        auto row = area.removeFromTop(pillRowHeight());
        auto mode = group(row, row.getWidth(), "MODE");
        placeInRow(*mode_, mode, mode.getWidth(), false);
    }
    void updateMoot() override { updateKeyChips(true); }

    void paintDisplay(juce::Graphics& g, juce::Rectangle<float> inner) override
    {
        const auto key = keyOf(sp::follow, sp::root, sp::kind);
        const bool played = notes_.lastInput >= 0;
        const int in = played ? notes_.lastInput : 60;
        auto view = viewAround(in, 2);
        view.lowNote = std::clamp(in / 12 * 12, 0, 103);
        view.scaleMask = key.mask;
        juce::String title;
        if (played)
        {
            const bool held = notes_.held[static_cast<size_t>(in)];
            view.held[static_cast<size_t>(in)] = held;
            const int out = sp::place(in, rounded(param(sp::mode)), key.root, key.mask);
            if (out >= 0)
                view.lit[static_cast<size_t>(out)] = held ? 1.0f : 0.6f;
            title =
                juce::String::fromUTF8(core::noteName(in).c_str()) +
                juce::String::fromUTF8("  \xe2\x86\x92  ") +
                (out >= 0 ? juce::String::fromUTF8(core::noteName(out).c_str()) : juce::String("blocked"));
            drawDisplayTitle(g, inner, title, held);
        }
        drawKeyboard(g, theme_, inner.withTrimmedTop(22.0f), colour(), view);
    }

    PillRow* mode_ = nullptr;
};
} // namespace

std::unique_ptr<ModuleCard>
createMidiFaceCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    if (insert.type == plugins::arpeggiator::type)
        return std::make_unique<ArpeggiatorFace>(theme, host, channelId, insert);
    if (insert.type == plugins::chord::type)
        return std::make_unique<ChordFace>(theme, host, channelId, insert);
    if (insert.type == plugins::chordpads::type)
        return std::make_unique<ChordPadsFace>(theme, host, channelId, insert);
    if (insert.type == plugins::velocity::type)
        return std::make_unique<VelocityFace>(theme, host, channelId, insert);
    if (insert.type == plugins::scale::type)
        return std::make_unique<ScaleFace>(theme, host, channelId, insert);
    return nullptr;
}

} // namespace northlight::ui::strip
