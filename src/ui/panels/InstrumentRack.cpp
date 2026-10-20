#include "ui/panels/InstrumentRack.h"

#include "plugins/BuiltIns.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/InstrumentOutputs.h"
#include "ui/PluginMenu.h"

namespace aural::ui
{

namespace
{
juce::Colour trackColour(const Theme& theme, const core::Track& track, int index)
{
    return theme.track.forTrack(track.colour, index);
}
} // namespace

// ----- a rack unit --------------------------------------------------------------------------------

// One instrument: a 64 px unit in the rack, a recessed panel with the track's
// hue down its left edge. Left to right: power, the instrument (name, format
// and the track it plays on), its outputs as chips, then Edit and the menu.
class InstrumentRack::Unit final : public juce::Component
{
public:
    Unit(InstrumentRack& owner, const Theme& theme)
        : owner_(owner), theme_(theme), power_(theme, Icon::power, "Switch the instrument on or off"),
          menu_(theme, Icon::menu, "Replace, remove")
    {
        power_.setClickingTogglesState(true);
        power_.onClick = [this]
        {
            if (owner_.onCommand && hasInstrument_)
                owner_.onCommand(std::make_unique<core::SetInsertBypass>(
                    trackId_, instrumentId_, !power_.getToggleState()));
        };
        addAndMakeVisible(power_);
        edit_.setButtonText("Edit");
        edit_.setTooltip("Open the instrument's editor");
        edit_.onClick = [this]
        {
            if (owner_.onOpenEditor && hasInstrument_)
                owner_.onOpenEditor(trackId_, instrumentId_);
        };
        addAndMakeVisible(edit_);
        menu_.onClick = [this] { showMenu(); };
        addAndMakeVisible(menu_);
        choose_.onClick = [this] { showReplaceMenu(choose_); };
        addChildComponent(choose_);
    }

    void set(const core::Project& project, const core::Track& track, int index)
    {
        trackId_ = track.id;
        hasInstrument_ = track.hasInstrument();
        instrumentId_ = track.instrument.id;
        hue_ = trackColour(theme_, track, index);
        trackName_ = juce::String::fromUTF8(track.name.c_str());
        // Who plays it: the MIDI tracks aimed at this channel.
        playedBy_.clear();
        for (const auto* feed : project.midiFeeds(track.id))
        {
            if (playedBy_.isNotEmpty())
                playedBy_ << ", ";
            playedBy_ << juce::String::fromUTF8(feed->name.c_str());
            if (feed->midiChannel != 1)
                playedBy_ << " (ch " << feed->midiChannel << ")";
        }
        bypassed_ = track.instrument.bypass;
        power_.setToggleState(hasInstrument_ && !bypassed_, juce::dontSendNotification);
        power_.setOnColour(hue_);
        power_.setVisible(hasInstrument_);
        edit_.setVisible(hasInstrument_);
        menu_.setVisible(hasInstrument_);
        choose_.setVisible(!hasInstrument_);
        if (hasInstrument_)
        {
            const auto& type = track.instrument.type;
            name_ = owner_.displayName ? juce::String::fromUTF8(owner_.displayName(type).c_str())
                                       : juce::String(type);
            format_ = !hosting::PluginRegistry::isHostedType(type) ? "BUILT-IN"
                      : type.rfind("clap:", 0) == 0                ? "CLAP"
                                                                   : "VST3";
        }
        else
            name_ = {}, format_ = {};
        // The outputs: the main pair is this track; the rest, output tracks.
        outputs_.clear();
        const auto live = owner_.liveInsert && hasInstrument_ ? owner_.liveInsert(instrumentId_) : nullptr;
        const int extra = live != nullptr ? live->auxOutputCount() : 0;
        outputs_.push_back({0, trackName_, track.id});
        for (int output = 1; output <= extra; ++output)
        {
            const auto* out = outputTrack(project, track, output);
            outputs_.push_back({output,
                                out != nullptr ? juce::String::fromUTF8(out->name.c_str()) : juce::String(),
                                out != nullptr ? out->id : core::invalidId,
                                live->auxOutputName(output - 1)});
        }
        resized();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        // The unit: a surface1 panel with a rim and the track's hue on the left.
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(bounds, theme_.radius.md, 1.0f);
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addRoundedRectangle(bounds, theme_.radius.md);
            g.reduceClipRegion(clip);
            g.setColour(hasInstrument_ && !bypassed_ ? hue_ : hue_.withAlpha(0.35f));
            g.fillRect(bounds.withWidth(3.0f));
        }
        // The instrument and its track.
        auto id = identity_;
        g.setFont(theme_.label());
        g.setColour(!hasInstrument_ ? c.textTertiary : bypassed_ ? c.textSecondary : c.textPrimary);
        auto top = id.removeFromTop(id.getHeight() / 2);
        if (format_.isNotEmpty())
        {
            g.setFont(theme_.captionCaps());
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), format_) +
                          2 * theme_.spacing.sm;
            const auto badge = top.removeFromRight(w).withSizeKeepingCentre(w, 16).toFloat();
            g.setColour(c.accent.withAlpha(0.18f));
            g.fillRoundedRectangle(badge, badge.getHeight() * 0.5f);
            g.setColour(c.accent);
            g.drawText(format_, badge, juce::Justification::centred);
            top.removeFromRight(theme_.spacing.xs);
        }
        g.setFont(theme_.label());
        g.setColour(!hasInstrument_ ? c.textTertiary : bypassed_ ? c.textSecondary : c.textPrimary);
        g.drawText(hasInstrument_ ? name_ : juce::String("No instrument"),
                   top,
                   juce::Justification::bottomLeft,
                   true);
        g.setFont(theme_.small());
        g.setColour(c.textTertiary);
        auto bottom = id.withTrimmedTop(2);
        g.setColour(hue_);
        g.fillEllipse(bottom.removeFromLeft(8)
                          .toFloat()
                          .withSizeKeepingCentre(6.0f, 6.0f)
                          .withY(static_cast<float>(bottom.getY()) + 4.0f));
        bottom.removeFromLeft(theme_.spacing.xs);
        g.setColour(c.textSecondary);
        g.drawText(trackName_ + juce::String::fromUTF8("  \xe2\x86\x90  ") +
                       (playedBy_.isNotEmpty() ? playedBy_ : juce::String("no MIDI track plays it")),
                   bottom,
                   juce::Justification::topLeft,
                   true);

        // The outputs: a caption and a chip per pair.
        g.setFont(theme_.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText("OUTPUTS", outputsCaption_, juce::Justification::centredLeft);
        for (size_t i = 0; i < outputs_.size(); ++i)
            paintChip(g, outputs_[i], chip(static_cast<int>(i)), static_cast<int>(i) == hoverChip_);
    }

    void mouseMove(const juce::MouseEvent& e) override { setHover(chipAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        const int i = chipAt(e.getPosition());
        if (i < 0 || static_cast<size_t>(i) >= outputs_.size())
            return;
        const auto& out = outputs_[static_cast<size_t>(i)];
        if (out.track != core::invalidId)
        {
            if (owner_.onSelectTrack)
                owner_.onSelectTrack(out.track);
        }
        else if (owner_.onCommand && owner_.project_ != nullptr)
            if (const auto* track = owner_.project_->findTrack(trackId_))
                owner_.onCommand(
                    std::make_unique<core::AddTrack>(makeOutputTrack(*track, out.output, out.carries)));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(theme_.spacing.md, theme_.spacing.sm).withTrimmedLeft(3);
        const int control = theme_.sizes.controlCompact;
        auto right = area.removeFromRight(control);
        menu_.setBounds(right.withSizeKeepingCentre(control, control));
        area.removeFromRight(theme_.spacing.xs);
        edit_.setBounds(area.removeFromRight(64).withSizeKeepingCentre(64, control));
        area.removeFromRight(theme_.spacing.md);
        power_.setBounds(area.removeFromLeft(control).withSizeKeepingCentre(control, control));
        area.removeFromLeft(theme_.spacing.sm);
        identity_ = area.removeFromLeft(std::min(240, area.getWidth() / 3));
        choose_.setBounds(identity_.withSizeKeepingCentre(std::min(identity_.getWidth(), 180), control));
        area.removeFromLeft(theme_.spacing.lg);
        outputsCaption_ = area.removeFromLeft(64);
        outputsArea_ = area;
    }

private:
    struct Output
    {
        int output = 0;    // 0 = the main pair
        juce::String name; // the track carrying it
        core::Id track = core::invalidId;
        std::string carries; // what the instrument says the pair carries ("Kick")
    };

    juce::Rectangle<int> chip(int i) const
    {
        // As wide as 148 px, narrower when there are many outputs (down to 72).
        const int gap = theme_.spacing.xs;
        const int n = std::max<int>(1, static_cast<int>(outputs_.size()));
        const int w = std::clamp((outputsArea_.getWidth() - (n - 1) * gap) / n, 72, 148);
        return juce::Rectangle<int>(
                   outputsArea_.getX() + i * (w + gap), outputsArea_.getY(), w, outputsArea_.getHeight())
            .withSizeKeepingCentre(w, theme_.sizes.controlCompact)
            .getIntersection(outputsArea_);
    }
    int chipAt(juce::Point<int> p) const
    {
        for (size_t i = 0; i < outputs_.size(); ++i)
            if (chip(static_cast<int>(i)).contains(p))
                return static_cast<int>(i);
        return -1;
    }
    void setHover(int i)
    {
        if (i == hoverChip_)
            return;
        hoverChip_ = i;
        setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint(outputsArea_);
    }

    // A chip: "1/2  Keys" on a surface2 pill for a pair with a track (click
    // selects it), a dashed "+ 3/4" for one without (click makes its track).
    void paintChip(juce::Graphics& g, const Output& out, juce::Rectangle<int> r, bool hover) const
    {
        if (r.isEmpty())
            return;
        const auto& c = theme_.colours;
        const auto f = r.toFloat().reduced(0.5f);
        const auto pair = juce::String(outputPairName(out.output));
        if (out.track == core::invalidId)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle(f, theme_.radius.sm);
            const float dashes[] = {3.0f, 3.0f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            if (hover)
            {
                g.setColour(c.surface2);
                g.fillRoundedRectangle(f, theme_.radius.sm);
            }
            g.setColour(c.lineStrong);
            g.fillPath(dashed);
            g.setFont(theme_.caption());
            g.setColour(hover ? c.textSecondary : c.textTertiary);
            g.drawText(out.carries.empty() ? "+ Track for " + pair
                                           : "+ " + juce::String::fromUTF8(out.carries.c_str()) + "  " + pair,
                       r,
                       juce::Justification::centred);
            return;
        }
        g.setColour(hover ? c.surface3 : c.surface2);
        g.fillRoundedRectangle(f, theme_.radius.sm);
        auto text = r.reduced(theme_.spacing.sm, 0);
        g.setFont(theme_.font(Theme::Face::mono, theme_.fonts.size.caption, theme_.fonts.weight.medium));
        g.setColour(hue_);
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), pair) + theme_.spacing.sm;
        g.drawText(pair, text.removeFromLeft(w), juce::Justification::centredLeft);
        g.setFont(theme_.small());
        g.setColour(c.textPrimary);
        g.drawText(out.name, text, juce::Justification::centredLeft, true);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Open editor");
        menu.addItem(2, "Select track");
        menu.addSeparator();
        auto replace = buildPluginMenu(PluginKind::instrument, owner_.registry_, {}, 10);
        menu.addSubMenu("Replace instrument", replace.menu);
        menu.addItem(3, "Remove instrument");
        juce::Component::SafePointer<Unit> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_),
                           [safe, replace](int choice)
                           {
                               if (safe == nullptr || choice == 0)
                                   return;
                               auto& owner = safe->owner_;
                               if (choice == 1 && owner.onOpenEditor)
                                   owner.onOpenEditor(safe->trackId_, safe->instrumentId_);
                               else if (choice == 2 && owner.onSelectTrack)
                                   owner.onSelectTrack(safe->trackId_);
                               else if (choice == 3 && owner.onCommand)
                                   owner.onCommand(
                                       std::make_unique<core::SetInstrument>(safe->trackId_, core::Insert{}));
                               else if (const auto type = replace.typeFor(choice); !type.empty())
                                   safe->setInstrument(type);
                           });
    }

    void showReplaceMenu(juce::Component& target)
    {
        auto built = buildPluginMenu(PluginKind::instrument, owner_.registry_);
        juce::Component::SafePointer<Unit> safe(this);
        built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                                 [safe, built](int choice)
                                 {
                                     if (safe != nullptr)
                                         if (const auto type = built.typeFor(choice); !type.empty())
                                             safe->setInstrument(type);
                                 });
    }

    void setInstrument(const std::string& type)
    {
        if (!owner_.onCommand)
            return;
        core::Insert instrument;
        instrument.type = type;
        instrument.params = plugins::defaultParameters(type);
        owner_.onCommand(std::make_unique<core::SetInstrument>(trackId_, instrument));
        if (owner_.onInserted)
            owner_.onInserted(type);
    }

    InstrumentRack& owner_;
    const Theme& theme_;
    IconButton power_, menu_;
    juce::TextButton edit_;
    AddSlot choose_{theme_, "Instrument"};
    core::Id trackId_ = core::invalidId, instrumentId_ = core::invalidId;
    bool hasInstrument_ = false, bypassed_ = false;
    juce::Colour hue_;
    juce::String name_, format_, trackName_, playedBy_;
    std::vector<Output> outputs_;
    juce::Rectangle<int> identity_, outputsCaption_, outputsArea_;
    int hoverChip_ = -1;
};

// ----- the rack -----------------------------------------------------------------------------------

InstrumentRack::InstrumentRack(const Theme& theme) : theme_(theme)
{
    setOpaque(true);
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(theme_.sizes.scrollbar);
    addAndMakeVisible(viewport_);
    add_.setTooltip("Add an instrument channel with the instrument you choose, and a MIDI track to play it");
    add_.onClick = [this] { showAddMenu(add_); };
    list_.addAndMakeVisible(add_);
}

InstrumentRack::~InstrumentRack() = default;

void InstrumentRack::setProject(const core::Project* project)
{
    project_ = project;
    rebuild();
}

void InstrumentRack::rebuild()
{
    // A unit per MIDI track (and any track with an instrument), in track order.
    std::vector<core::Id> ids;
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
            if (t.isInstrument())
                ids.push_back(t.id);
    if (ids != builtFor_)
    {
        while (units_.size() > ids.size())
            units_.pop_back();
        while (units_.size() < ids.size())
        {
            units_.push_back(std::make_unique<Unit>(*this, theme_));
            list_.addAndMakeVisible(*units_.back());
        }
        builtFor_ = ids;
        resized();
    }
    if (project_ != nullptr)
        for (size_t i = 0; i < ids.size(); ++i)
            if (const auto* t = project_->findTrack(ids[i]))
                units_[i]->set(*project_, *t, project_->trackIndex(t->id));
    repaint();
}

void InstrumentRack::addInstrumentTrack(const std::string& type)
{
    if (!onCommand || project_ == nullptr)
        return;
    // The instrument channel and a MIDI track aimed at it, as one step.
    int count = 0;
    for (const auto& t : project_->tracks)
        count += t.isMidi() ? 1 : 0;
    core::Track channel;
    channel.name = displayName ? displayName(type) : type;
    channel.instrument.type = type;
    channel.instrument.params = plugins::defaultParameters(type);
    core::Track midi;
    midi.name = "MIDI " + std::to_string(count + 1);
    midi.armed = true;
    onCommand(std::make_unique<core::AddInstrumentTrack>(channel, midi));
    if (onInserted)
        onInserted(type);
}

void InstrumentRack::showAddMenu(juce::Component& target)
{
    auto built = buildPluginMenu(PluginKind::instrument, registry_);
    juce::Component::SafePointer<InstrumentRack> safe(this);
    built.menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                             [safe, built](int choice)
                             {
                                 if (safe != nullptr)
                                     if (const auto type = built.typeFor(choice); !type.empty())
                                         safe->addInstrumentTrack(type);
                             });
}

void InstrumentRack::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.surface0);
    if (units_.empty())
    {
        g.setFont(theme_.small());
        g.setColour(theme_.colours.textTertiary);
        g.drawText("No instruments yet: add one here, or choose one for a MIDI track",
                   getLocalBounds().withTrimmedTop(unitHeight + 2 * theme_.spacing.md),
                   juce::Justification::centredTop);
    }
}

void InstrumentRack::resized()
{
    viewport_.setBounds(getLocalBounds());
    const int width = std::max(1, viewport_.getWidth() - theme_.sizes.scrollbar);
    int y = theme_.spacing.md;
    for (auto& unit : units_)
    {
        unit->setBounds(theme_.spacing.md, y, width - 2 * theme_.spacing.md, unitHeight);
        y += unitHeight + theme_.spacing.sm;
    }
    add_.setBounds(
        theme_.spacing.md, y, std::min(260, width - 2 * theme_.spacing.md), theme_.sizes.controlLarge);
    y += theme_.sizes.controlLarge + theme_.spacing.md;
    list_.setSize(width, y);
}

} // namespace aural::ui
