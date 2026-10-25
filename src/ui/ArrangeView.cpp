#include "ui/ArrangeView.h"

#include "core/Command.h"
#include "core/Comping.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/MediaFiles.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"
#include "ui/PluginMenu.h"
#include "ui/arrange/ArrangeGestures.h"
#include "ui/arrange/ArrangePainter.h"
#include "ui/components/DragTip.h"
#include "ui/timeline/ClipPainter.h"
#include "ui/timeline/TimeGrid.h"
#include "ui/timeline/WaveformPainter.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr double minSamplesPerPixel = 1.0;
constexpr double maxSamplesPerPixel = 48000.0 * 16.0; // eight bars per pixel at 120 bpm
} // namespace

ArrangeView::ArrangeView(audio::PeakStore& peaks, const Theme& theme)
    : theme_(theme), peaks_(peaks), addTrack_(theme_, Icon::add, "Add track"),
      rowsSmaller_(theme_, Icon::zoomOut, "Smaller rows"), rowsLarger_(theme_, Icon::zoomIn, "Larger rows"),
      selectTool_(theme_, Icon::pointer, "Select tool: move, trim and select clips  1"),
      rangeTool_(theme_, Icon::rangeTool, "Range tool: select a time range across tracks  2"),
      pencilTool_(theme_, Icon::pencil, "Pencil: draw a MIDI clip on a MIDI track  3"),
      eraseTool_(theme_, Icon::eraser, "Erase: click or sweep over clips to delete them  4"),
      splitTool_(theme_, Icon::scissors, "Split: click a clip to cut it there, Shift+click every track  5"),
      glueTool_(theme_, Icon::glue, "Glue: click a clip to join it with the next  6"),
      muteTool_(theme_, Icon::muteClip, "Mute: click or sweep over clips to mute or unmute them  7"),
      toolMenu_(theme_, Icon::pointer, "Tools: 1 to 7")
{
    setOpaque(true);
    rulerOnly = theme_.sizes.ruler;
    rulerHeight = rulerOnly;
    view_.originX = theme_.sizes.trackHeaderWidth;
    rowHeight = theme_.sizes.trackRow;
    scrollbarHeight = theme_.sizes.scrollbar;
    scrollbarWidth = theme_.sizes.scrollbar;
    addTrack_.onClick = [this] { menus_.addTrackMenu(); };
    rowsSmaller_.onClick = [this]
    { setRowHeight(rowHeight > theme_.sizes.trackRow ? theme_.sizes.trackRow : theme_.sizes.trackRowMin); };
    rowsLarger_.onClick = [this]
    { setRowHeight(rowHeight < theme_.sizes.trackRow ? theme_.sizes.trackRow : theme_.sizes.trackRowLarge); };
    const auto tools = toolButtons();
    for (size_t i = 0; i < tools.size(); ++i)
        tools[i]->onClick = [this, i] { setTool(static_cast<Tool>(i)); };
    toolMenu_.setShowsMenuChevron(true);
    toolMenu_.onClick = [this] { menus_.toolsMenu(); };
    for (auto* b : {&addTrack_, &rowsSmaller_, &rowsLarger_, &toolMenu_})
    {
        b->setIconSize(theme_.sizes.iconSmall);
        addAndMakeVisible(*b);
    }
    for (auto* b : tools)
    {
        b->setIconSize(theme_.sizes.iconSmall);
        addAndMakeVisible(*b);
    }
    refreshToolButtons();
    automation_.geometry.snap = [this](core::SamplePosition s) { return snapped(s); };
    automation_.geometry.lanesArea = [this]
    {
        return juce::Rectangle<int>(
            headerWidth(), rulerHeight, getWidth() - headerWidth() - scrollbarWidth, lanesHeight());
    };
    automation_.geometry.rowCount = [this] { return rowCount(); };
    automation_.geometry.channelAt = [this](int row) { return channelAtRow(row); };
    automation_.geometry.block = [this](int row) { return automationBlock(row); };
    automation_.geometry.colour = [this](int row)
    {
        // The channel's colour, as its header and clips show it.
        const auto* channel = channelAtRow(row);
        return channel != nullptr && project_ != nullptr ? theme_.track.forChannel(*project_, channel->id)
                                                         : theme_.colours.textTertiary;
    };
    automation_.geometry.playhead = [this] { return playhead_; };
    automation_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    { emit(std::move(commands), name); };
    automation_.onLayoutChanged = [this]
    {
        rowsValid_ = false;
        layoutHeaders();
        updateScrollbar();
        repaint();
    };
    addAndMakeVisible(automation_);
    // The tempo track sits over the automation overlay so its handles take the mouse.
    tempoLane_.playhead = [this] { return playhead_; };
    tempoLane_.snap = [this](const core::TempoMap& map, core::SamplePosition s) { return snappedOn(map, s); };
    tempoLane_.onEdit = [this](core::TempoState state, const std::string& name)
    {
        if (onTempoEdit)
            onTempoEdit(std::move(state), name);
    };
    tempoLane_.onHide = [this] { setTempoTrackShown(false); };
    markers_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    { emit(std::move(commands), name); };
    markers_.onLocate = [this](core::SamplePosition at)
    {
        if (onLocate)
            onLocate(at);
    };
    markers_.snap = [this](core::SamplePosition at) { return snapped(at); };
    addChildComponent(tempoLane_);
    takes_.snap = [this](core::SamplePosition at) { return snapped(at); };
    takes_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    { emit(std::move(commands), name); };
    takes_.onRepaint = [this] { repaint(); };
    import_.setButtonText(juce::String::fromUTF8("Import audio\xe2\x80\xa6"));
    for (auto* b : {&addAudio_, &addInstrument_, &import_})
    {
        b->getProperties().set("variant", "ghost");
        addChildComponent(*b);
    }
    addAudio_.onClick = [this]
    {
        if (onAddAudioTrack)
            onAddAudioTrack();
    };
    addInstrument_.onClick = [this]
    {
        if (onAddMidiTrack)
            onAddMidiTrack({});
    };
    import_.onClick = [this]
    {
        if (onImportRequested)
            onImportRequested();
    };
    scrollbar_.setAutoHide(false);
    scrollbar_.addListener(this);
    addAndMakeVisible(scrollbar_);
    verticalScrollbar_.setAutoHide(false);
    verticalScrollbar_.addListener(this);
    addAndMakeVisible(verticalScrollbar_);
    peaks_.addChangeListener(this);
}

ArrangeView::~ArrangeView()
{
    peaks_.removeChangeListener(this);
    scrollbar_.removeListener(this);
    verticalScrollbar_.removeListener(this);
}

void ArrangeView::setProject(const core::Project* project, bool valuesOnly)
{
    rowsValid_ = false; // heights may have changed (a values refresh carries a resize)
    if (valuesOnly && project == project_ && project_ != nullptr)
    {
        // Values only (a fader drag): the headers take their channels' values
        // without refilling their menus.
        for (auto* list : {&headers_, &fxHeaders_})
            for (auto& header : *list)
                if (const auto* channel = project_->findChannel(header->trackId()))
                    header->setValues(*channel);
        // A cosmetic edit (a row or lane height, lanes opened, closed, collapsed
        // or hidden) moves the rows: lay them out again only then.
        if (rows().layout.tops() != valuesTops_)
        {
            valuesTops_ = rows().layout.tops();
            layoutHeaders();
            updateScrollbar();
            repaint();
        }
        else
            automation_.repaint();
        return;
    }
    automation_.setProject(project);
    takes_.setProject(project);
    project_ = project;
    tempoMap_ = project_ != nullptr ? core::tempoMapOf(*project_) : core::TempoMap();
    tempoLane_.setProject(project);
    std::erase_if(selection_, [this](const ClipRef& ref) { return !spanOf(ref).has_value(); });
    markers_.setProject(project);
    if (sharedSelection_ == nullptr &&
        (project_ == nullptr || project_->findChannel(selectedTrack()) == nullptr))
        ownSelection_.select(core::invalidId); // a track or an FX track that went
    refreshMissingMedia();
    animateRowChanges();
    rebuildHeaders();
    valuesTops_ = rows().layout.tops();
    updateScrollbar();
    layoutEmptyState();
    repaint();
    notifySelection(); // clips that are gone left the selection
}

void ArrangeView::refreshMissingMedia()
{
    missingMedia_.clear();
    if (project_ == nullptr)
        return;
    // Each file is looked for once, and again only after a while: every structural
    // edit comes here, and a disk lookup per clip per edit stalls the message
    // thread on a slow or network drive.
    const auto now = juce::Time::getMillisecondCounter();
    for (const auto& t : project_->tracks)
        for (const auto& clip : t.clips)
        {
            if (clip.mediaPath.empty())
                continue;
            auto& check = mediaChecks_[clip.mediaPath];
            if (check.checkedMs == 0 || now - check.checkedMs >= mediaRecheckMs)
            {
                check.missing = !juce::File(clip.mediaPath).existsAsFile();
                check.checkedMs = std::max<juce::uint32>(1, now);
            }
            if (check.missing)
                missingMedia_.insert(clip.mediaPath);
        }
}

bool ArrangeView::mediaMissing(const std::string& path)
{
    return missingMedia_.count(path) > 0;
}

void ArrangeView::setRowHeight(int height)
{
    height = std::clamp(height, theme_.sizes.trackRowMin, theme_.sizes.trackRowLarge);
    if (height == rowHeight)
        return;
    rowHeight = height;
    rowsValid_ = false;
    rowsSmaller_.setEnabled(rowHeight > theme_.sizes.trackRowMin);
    rowsLarger_.setEnabled(rowHeight < theme_.sizes.trackRowLarge);
    setVerticalScroll(verticalScroll_);
}

void ArrangeView::pushMeters()
{
    // A clip's stretch is rendering: show its progress (each frame until done).
    if (stretchPending_)
    {
        stretchPending_ = false;
        repaint();
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double elapsed = lastMeterMs_ > 0.0 ? now - lastMeterMs_ : 33.0;
    lastMeterMs_ = now;
    if (!trackMeter)
        return;
    for (size_t i = 0; i < headers_.size(); ++i)
        if (headers_[i]->isVisible())
            headers_[i]->pushMeter(trackMeter(static_cast<int>(i)), elapsed);
    if (busMeter && project_ != nullptr)
        for (auto& header : fxHeaders_)
            if (header->isVisible())
            {
                if (const int bus = project_->busIndex(header->trackId()); bus >= 0)
                    header->pushMeter(busMeter(bus), elapsed);
                else if (header->trackId() == core::masterId && masterMeter)
                    header->pushMeter(masterMeter(), elapsed);
            }
}

void ArrangeView::layoutEmptyState()
{
    const bool empty = project_ == nullptr || project_->tracks.empty();
    for (auto* b : {&addAudio_, &addInstrument_, &import_})
        b->setVisible(empty);
    if (!empty)
        return;
    // The mark, a heading, three ghost buttons in a row and a hint (screens/shell.md).
    const int buttonW = 150, gap = theme_.spacing.md;
    const int total = 3 * buttonW + 2 * gap;
    auto row =
        getLocalBounds().withTrimmedTop(rulerHeight).withSizeKeepingCentre(total, theme_.sizes.control);
    row.translate(0, theme_.spacing.xl);
    addAudio_.setBounds(row.removeFromLeft(buttonW));
    row.removeFromLeft(gap);
    addInstrument_.setBounds(row.removeFromLeft(buttonW));
    row.removeFromLeft(gap);
    import_.setBounds(row.removeFromLeft(buttonW));
}

void ArrangeView::setSelection(const Selection* selection)
{
    sharedSelection_ = selection;
    showSelectedTrack();
}

void ArrangeView::setSelectedTrack(core::Id trackId)
{
    if (sharedSelection_ == nullptr)
        ownSelection_.select(trackId);
    showSelectedTrack();
}

void ArrangeView::showSelectedTrack()
{
    const auto selected = selectedTrack();
    for (auto* list : {&headers_, &fxHeaders_})
        for (auto& header : *list)
            header->setSelected(header->trackId() == selected);
    repaint();
}

void ArrangeView::selectTrack(core::Id trackId)
{
    // The owner selects (and has this view show it); on its own, the view does.
    if (sharedSelection_ == nullptr)
        setSelectedTrack(trackId);
    if (onTrackSelected)
        onTrackSelected(trackId);
}

void ArrangeView::setInputChoices(std::vector<InputChoice> choices)
{
    inputChoices_ = std::move(choices);
    rebuildHeaders();
}

void ArrangeView::setMidiInputNames(std::vector<std::string> names)
{
    if (names == midiInputNames_)
        return;
    midiInputNames_ = std::move(names);
    rebuildHeaders();
}

void ArrangeView::rebuildHeaders()
{
    const size_t count = project_ != nullptr ? project_->tracks.size() : 0;
    while (headers_.size() > count)
        headers_.pop_back();
    while (headers_.size() < count)
    {
        auto header = std::make_unique<TrackHeader>(theme_);
        header->onInputChanged = [this](core::Id id, int channel, const std::string& bus)
        {
            if (onTrackInputChanged)
                onTrackInputChanged(id, channel, bus);
        };
        header->onMidiInputChanged = [this](core::Id id, const std::string& name)
        {
            if (onTrackMidiInputChanged)
                onTrackMidiInputChanged(id, name);
        };
        header->onArmChanged = [this](core::Id id, bool armed)
        {
            if (onTrackArmChanged)
                onTrackArmChanged(id, armed);
        };
        header->onSelected = [this](core::Id id) { selectTrack(id); };
        header->onMonitorChanged = [this](core::Id id, core::MonitorMode mode)
        {
            if (onTrackMonitorChanged)
                onTrackMonitorChanged(id, mode);
        };
        header->onCommand = [this](std::unique_ptr<core::Command> command)
        {
            if (onCommand)
                onCommand(std::move(command));
        };
        header->onGestureBegin = [this](const std::string& name)
        {
            if (onGestureBegin)
                onGestureBegin(name);
        };
        header->onGestureEnd = [this]
        {
            if (onGestureEnd)
                onGestureEnd();
        };
        header->onReorderDrag = [this](core::Id id, juce::Point<int> screen, bool released)
        { reorderDrag(id, screen, released); };
        header->onContextMenu = [this](core::Id id, juce::Point<int> screen)
        { menus_.trackMenu(id, screen); };
        header->onTargetMenu = [this](core::Id id, juce::Rectangle<int> area)
        { menus_.targetMenu(id, area); };
        header->onAutomationToggled = [this](core::Id id, bool shown) { showAutomation(id, shown); };
        header->onAutomationMenu = [this](core::Id id, juce::Rectangle<int> area)
        { automation_.showChannelMenu(id, area); };
        addAndMakeVisible(*header);
        headers_.push_back(std::move(header));
    }
    // Buses and the master: the same header, shown as a channel without an input
    // or arm (the master without mute, solo or pan either).
    const auto fx = busRows();
    while (fxHeaders_.size() > fx.size())
        fxHeaders_.pop_back();
    while (fxHeaders_.size() < fx.size())
    {
        auto header = std::make_unique<TrackHeader>(theme_);
        header->onSelected = [this](core::Id id) { selectTrack(id); };
        header->onCommand = [this](std::unique_ptr<core::Command> command)
        {
            if (onCommand)
                onCommand(std::move(command));
        };
        header->onGestureBegin = [this](const std::string& name)
        {
            if (onGestureBegin)
                onGestureBegin(name);
        };
        header->onGestureEnd = [this]
        {
            if (onGestureEnd)
                onGestureEnd();
        };
        header->onContextMenu = [this](core::Id id, juce::Point<int> screen)
        {
            if (id == core::masterId)
                menus_.masterMenu(screen);
            else
                menus_.busMenu(id, screen);
        };
        header->onAutomationToggled = [this](core::Id id, bool shown) { showAutomation(id, shown); };
        header->onAutomationMenu = [this](core::Id id, juce::Rectangle<int> area)
        { automation_.showChannelMenu(id, area); };
        addAndMakeVisible(*header);
        fxHeaders_.push_back(std::move(header));
    }
    for (size_t i = 0; i < fx.size(); ++i)
    {
        core::Track shown;
        static_cast<core::Channel&>(shown) = *fx[i];
        shown.kind = core::TrackKind::aux; // no input, no arm
        const bool master = fx[i]->id == core::masterId;
        // Unless coloured: FX tracks Lichen, groups Overcast, the master Slate (forChannel).
        shown.colour = theme_.track.forChannel(*project_, fx[i]->id).getARGB();
        fxHeaders_[i]->setMaster(master);
        fxHeaders_[i]->setTrack(
            shown, static_cast<int>(count + i), *project_, inputChoices_, midiInputNames_);
        fxHeaders_[i]->setSelected(fx[i]->id == selectedTrack());
    }
    for (size_t i = 0; i < count; ++i)
    {
        const auto& track = project_->tracks[i];
        headers_[i]->setTrack(track, static_cast<int>(i), *project_, inputChoices_, midiInputNames_);
        headers_[i]->setSelected(track.id == selectedTrack());
        bool missing = false;
        for (const auto& clip : track.clips)
            missing = missing || mediaMissing(clip.mediaPath);
        headers_[i]->setMediaMissing(missing);
    }
    layoutHeaders();
}
void ArrangeView::setMonitoringStates(const std::vector<bool>& monitoring, bool recording)
{
    for (size_t i = 0; i < headers_.size(); ++i)
        headers_[i]->setLiveState(i < monitoring.size() && monitoring[i], recording);
}

void ArrangeView::layoutHeaders()
{
    int y = rulerHeight - verticalScroll_;
    int row = 0;
    for (auto* list : {&headers_, &fxHeaders_})
        for (auto& header : *list)
        {
            y = rowTop(row);
            const int h = rowHeightOf(row++);
            const bool visible = h > 0 && y + h > rulerHeight && y < getHeight() - scrollbarHeight;
            header->setVisible(visible);
            header->setBounds(0, y, headerWidth(), h);
            y += h;
        }
}

void ArrangeView::liveRecordingChanged()
{
    const bool has = !live_.empty();
    if (has || hadLive_)
        repaint();
    hadLive_ = has;
}

void ArrangeView::setPlayhead(core::SamplePosition position, bool playing, bool recording)
{
    if (position == playhead_ && playing == playing_ && recording == recording_)
        return;
    const bool stateChanged = playing != playing_ || recording != recording_;
    playhead_ = position;
    playing_ = playing;
    recording_ = recording;

    // Follow playback: page forward when the playhead passes 90% of the width.
    if (playing_ && followPlayhead_)
    {
        const auto visible = visibleSamples();
        if (playhead_ >= view_.first + visible * 9 / 10 || playhead_ < view_.first)
        {
            setScroll(playhead_ - visible / 10);
            return; // setScroll repainted everything
        }
    }
    // Only the columns the playhead left and takes repaint (11.7).
    const int x = xForSample(playhead_);
    if (stateChanged)
        repaint();
    else
    {
        if (lastPlayheadX_ >= 0)
            repaint(lastPlayheadX_ - 5, 0, 11, getHeight());
        repaint(x - 5, 0, 11, getHeight());
    }
    lastPlayheadX_ = x;
} // ----- geometry -------------------------------------------------------------------------

double ArrangeView::sampleRate() const noexcept
{
    return project_ != nullptr && project_->sampleRate > 0.0 ? project_->sampleRate : 48000.0;
}

int ArrangeView::xForSample(core::SamplePosition s) const noexcept
{
    return view_.xOf(s);
}

core::SamplePosition ArrangeView::sampleForX(int x) const noexcept
{
    // Over the headers: the first visible sample.
    return view_.sampleAt(std::max(x, headerWidth()));
}

core::SamplePosition ArrangeView::visibleSamples() const noexcept
{
    return static_cast<core::SamplePosition>(std::max(1, getWidth() - headerWidth() - scrollbarWidth) *
                                             view_.samplesPerPixel);
}

std::vector<const core::Channel*> ArrangeView::busRows() const
{
    std::vector<const core::Channel*> list;
    if (project_ == nullptr)
        return list;
    for (const auto& b : project_->buses)
        list.push_back(&b);
    list.push_back(&project_->master);
    return list;
}

void ArrangeView::animateRowChanges()
{
    // What came and went since the last structural refresh. A few at a time
    // animate (an add, a remove, an undo); a new project or a large change
    // just appears.
    std::vector<core::Id> ids;
    if (project_ != nullptr)
    {
        for (const auto& t : project_->tracks)
            ids.push_back(t.id);
        for (const auto& b : project_->buses)
            if (b.fx)
                ids.push_back(b.id);
    }
    std::vector<core::Id> added, removedIds;
    std::vector<int> removedAt;
    for (const auto id : ids)
        if (std::find(shownRowIds_.begin(), shownRowIds_.end(), id) == shownRowIds_.end())
            added.push_back(id);
    for (size_t i = 0; i < shownRowIds_.size(); ++i)
        if (std::find(ids.begin(), ids.end(), shownRowIds_[i]) == ids.end())
        {
            removedIds.push_back(shownRowIds_[i]);
            removedAt.push_back(static_cast<int>(i));
        }
    const int ms = theme_.motion.ms(theme_.motion.panelMs);
    const bool animate = ms > 0 && !shownRowIds_.empty() && added.size() + removedIds.size() <= 2;
    if (animate)
    {
        juce::Component::SafePointer<ArrangeView> safe(this);
        for (const auto id : added)
        {
            rowGrowth_[id] = 0.0f;
            motion::Tween::run(
                *this,
                ms,
                [safe, id](float t)
                {
                    if (safe == nullptr)
                        return;
                    safe->rowGrowth_[id] = t;
                    safe->rowsValid_ = false;
                    safe->layoutHeaders();
                    safe->updateScrollbar();
                    safe->repaint();
                },
                [safe, id]
                {
                    if (safe == nullptr)
                        return;
                    safe->rowGrowth_.erase(id);
                    safe->rowsValid_ = false;
                    safe->layoutHeaders();
                    safe->repaint();
                });
        }
        if (removedIds.size() == 1)
        {
            // Rows above the removed one keep their places; from its place down the
            // rows start where they were and glide up.
            int above = 0;
            for (int i = 0; i < removedAt[0]; ++i)
                above += std::find(ids.begin(), ids.end(), shownRowIds_[static_cast<size_t>(i)]) != ids.end()
                             ? 1
                             : 0;
            gapRow_ = above;
            const float height = static_cast<float>(shownRowHeights_[static_cast<size_t>(removedAt[0])]);
            gapHeight_ = height;
            motion::Tween::run(
                *this,
                ms,
                [safe, height](float t)
                {
                    if (safe == nullptr)
                        return;
                    safe->gapHeight_ = height * (1.0f - t);
                    safe->rowsValid_ = false;
                    safe->layoutHeaders();
                    safe->updateScrollbar();
                    safe->repaint();
                },
                [safe]
                {
                    if (safe != nullptr)
                        safe->gapRow_ = -1;
                });
        }
    }
    rowsValid_ = false;
    shownRowIds_ = ids;
    shownRowHeights_.clear();
    const auto& r = rows().layout;
    for (int i = 0; i < r.count() && static_cast<size_t>(i) < ids.size(); ++i)
    {
        // The row's own height (not its growth or the gap): what a removal closes.
        const auto* channel = r.channel(i);
        int h = rowHeight;
        if (channel->height > 0)
            h = std::clamp(channel->height, theme_.sizes.trackRowMin, theme_.sizes.trackRowMax);
        shownRowHeights_.push_back(h);
    }
}

const ArrangeView::RowLayout& ArrangeView::rows() const
{
    // Built once per change (the project, a height, the default height, a resize
    // drag) and read by every row lookup, paint and hit test.
    if (rowsValid_ &&
        (project_ == nullptr ||
         (rows_.tracksData == project_->tracks.data() && rows_.tracksSize == project_->tracks.size() &&
          rows_.busesData == project_->buses.data() && rows_.busesSize == project_->buses.size())))
        return rows_;
    rows_.tracksData = project_ != nullptr ? project_->tracks.data() : nullptr;
    rows_.tracksSize = project_ != nullptr ? project_->tracks.size() : 0;
    rows_.busesData = project_ != nullptr ? project_->buses.data() : nullptr;
    rows_.busesSize = project_ != nullptr ? project_->buses.size() : 0;
    std::vector<ArrangeRow> specs;
    if (project_ != nullptr)
    {
        std::vector<const core::Channel*> channels;
        for (const auto& t : project_->tracks)
            channels.push_back(&t);
        for (const auto* channel : busRows())
            channels.push_back(channel);
        for (size_t index = 0; index < channels.size(); ++index)
        {
            const auto* channel = channels[index];
            ArrangeRow row{channel, rowHeight, 0};
            if (channel->id == resizeChannel_)
                row.height = resizeHeight_;
            else if (channel->height > 0)
                row.height = std::clamp(channel->height, theme_.sizes.trackRowMin, theme_.sizes.trackRowMax);
            if (const auto growing = rowGrowth_.find(channel->id); growing != rowGrowth_.end())
                row.height = std::max(1, juce::roundToInt(static_cast<float>(row.height) * growing->second));
            // A track's expanded take lanes and its automation lanes sit under its row.
            if (index >= project_->tracks.size() || project_->tracks[index].hasAudio())
                row.below += automation_.blockHeight(*channel);
            if (index < project_->tracks.size())
            {
                row.below += takes_.lanesHeight(project_->tracks[index]);
                // A track in a closed folder keeps its row, at no height.
                if (project_->hiddenByFolder(project_->tracks[index]))
                    row.height = row.below = 0;
            }
            specs.push_back(row);
        }
    }
    // A removed row's space, closing: everything from its place down sits lower.
    rows_.layout.build(std::move(specs), gapRow_, gapHeight_ > 0.0f ? juce::roundToInt(gapHeight_) : 0);
    rowsValid_ = true;
    return rows_;
}

int ArrangeView::rowCount() const noexcept
{
    return rows().layout.count();
}

const core::Channel* ArrangeView::channelAtRow(int row) const
{
    return rows().layout.channel(row);
}

int ArrangeView::contentHeight() const noexcept
{
    return rows().layout.end();
}

void ArrangeView::setVerticalScroll(int firstRowPixel)
{
    const int maxScroll = std::max(0, contentHeight() - lanesHeight());
    verticalScroll_ = std::clamp(firstRowPixel, 0, maxScroll);
    updateScrollbar();
    layoutHeaders();
    repaint();
}

core::SamplePosition ArrangeView::contentLength() const noexcept
{
    auto length = project_ != nullptr ? project_->length() : 0;
    // A recording in progress counts too, so the view can follow it past the end.
    for (const auto& take : live_.takes)
        length = std::max(length, take.startPosition + take.samplesWritten);
    for (const auto& take : live_.midi)
        length = std::max(length, take.clip.end());
    // Always leave room to scroll a screen past the end, and at least a minute.
    return std::max<core::SamplePosition>(length, static_cast<core::SamplePosition>(60.0 * sampleRate())) +
           visibleSamples();
}

void ArrangeView::setZoom(double samplesPerPixel, int anchorX)
{
    // Keep the sample under the pointer where it is.
    view_ = view_.zoomedAround(std::clamp(samplesPerPixel, minSamplesPerPixel, maxSamplesPerPixel),
                               std::max(anchorX, headerWidth()));
    setScroll(view_.first);
}

void ArrangeView::setTimeView(double samplesPerPixel, core::SamplePosition firstVisible)
{
    view_.samplesPerPixel = std::clamp(samplesPerPixel, minSamplesPerPixel, maxSamplesPerPixel);
    setScroll(firstVisible);
}

void ArrangeView::setScroll(core::SamplePosition firstVisible)
{
    const auto maxScroll = std::max<core::SamplePosition>(0, contentLength() - visibleSamples());
    view_.first = std::clamp<core::SamplePosition>(firstVisible, 0, maxScroll);
    updateScrollbar();
    repaint();
}

void ArrangeView::updateScrollbar()
{
    const juce::ScopedValueSetter<bool> guard(updatingScrollbar_, true);
    scrollbar_.setRangeLimits(0.0, static_cast<double>(contentLength()));
    scrollbar_.setCurrentRange(static_cast<double>(view_.first), static_cast<double>(visibleSamples()));
    verticalScrollbar_.setRangeLimits(0.0, static_cast<double>(std::max(contentHeight(), lanesHeight())));
    verticalScrollbar_.setCurrentRange(static_cast<double>(verticalScroll_),
                                       static_cast<double>(lanesHeight()));
}

void ArrangeView::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollbar_)
        return;
    if (bar == &verticalScrollbar_)
    {
        verticalScroll_ = static_cast<int>(newRangeStart);
        layoutHeaders();
    }
    else
    {
        view_.first = static_cast<core::SamplePosition>(newRangeStart);
    }
    repaint();
}

void ArrangeView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint(); // peaks arrived
}

void ArrangeView::showAutomation(core::Id trackId, bool shown)
{
    const auto* channel = project_ != nullptr ? project_->findChannel(trackId) : nullptr;
    if (channel == nullptr || channel->automationOpen == shown || !onCommand)
        return;
    onCommand(std::make_unique<core::SetAutomationOpen>(trackId, shown));
}

void ArrangeView::setTempoTrackShown(bool shown)
{
    if (shown == tempoShown_)
        return;
    tempoShown_ = shown;
    rulerHeight = rulerOnly + (shown ? tempoLaneHeight : 0);
    tempoLane_.setVisible(shown);
    rowsValid_ = false;
    resized();
    layoutHeaders();
    updateScrollbar();
    repaint();
    if (onTempoTrackShownChanged)
        onTempoTrackShownChanged(shown);
}

void ArrangeView::resized()
{
    layoutEmptyState();
    automation_.setBounds(getLocalBounds());
    tempoLane_.setBounds(0, rulerOnly, getWidth() - scrollbarWidth, tempoLaneHeight);
    scrollbar_.setBounds(headerWidth(),
                         getHeight() - scrollbarHeight,
                         getWidth() - headerWidth() - scrollbarWidth,
                         scrollbarHeight);
    verticalScrollbar_.setBounds(getWidth() - scrollbarWidth, rulerHeight, scrollbarWidth, lanesHeight());
    // The corner: "+" at the left, the row-height stepper at the right.
    auto corner = juce::Rectangle<int>(0, 0, headerWidth(), rulerOnly).reduced(theme_.spacing.xs, 0);
    const int button = theme_.sizes.controlCompact;
    addTrack_.setBounds(corner.removeFromLeft(button).withSizeKeepingCentre(button, button));
    rowsLarger_.setBounds(corner.removeFromRight(button).withSizeKeepingCentre(button, button));
    rowsSmaller_.setBounds(corner.removeFromRight(button).withSizeKeepingCentre(button, button));
    // The tools after "+", a group of their own; when they do not all fit, one
    // button for the current tool opens a menu of them.
    corner.removeFromLeft(theme_.spacing.sm);
    const bool allFit = corner.getWidth() >= toolCount * button;
    for (auto* b : toolButtons())
    {
        b->setVisible(allFit);
        if (allFit)
            b->setBounds(corner.removeFromLeft(button).withSizeKeepingCentre(button, button));
    }
    const int menuWidth = button + theme_.spacing.sm;
    toolMenu_.setVisible(!allFit && corner.getWidth() >= menuWidth);
    if (toolMenu_.isVisible())
        toolMenu_.setBounds(corner.removeFromLeft(menuWidth).withSizeKeepingCentre(menuWidth, button));
    setVerticalScroll(verticalScroll_);
}
// ----- input -------------------------------------------------------------------------------

void ArrangeView::setRegion(core::SamplePosition start, core::SamplePosition end, bool loop, bool punch)
{
    regionStart_ = start;
    regionEnd_ = end;
    loop_ = loop;
    punch_ = punch;
    repaint();
}

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    // A press changes the selection itself; the release reports any change since.
    pressSelection_ = selection_;
    draggedFar_ = false;
    dragStart_ = e.getPosition();
    dragPoint_ = e.getPosition();
    markers_.closeEditor();
    // The boundary between the headers and the lanes resizes the headers.
    if (e.y >= rulerHeight && std::abs(e.x - headerWidth()) <= 2)
    {
        gesture_ = std::make_unique<HeaderResizeGesture>(*this);
        return;
    }
    // The middle button pans.
    if (e.mods.isMiddleButtonDown())
    {
        gesture_ = std::make_unique<PanGesture>(*this);
        return;
    }
    // A row's bottom edge (under its header or across its lane) resizes that track.
    if (const int edge = resizeEdgeAt(e.getPosition()); edge >= 0 && !e.mods.isPopupMenu())
    {
        gesture_ = std::make_unique<RowResizeGesture>(*this, edge);
        return;
    }
    // Take lanes (their headers too) and the comp's chip, boundaries and menu on the main lane.
    if (e.y >= rulerHeight && e.y < getHeight() - scrollbarHeight && e.x < getWidth() - scrollbarWidth)
        if (const int row = rowAtY(e.y); row >= 0 && takes_.mouseDown(e, takeRow(row)))
        {
            selectTrack(project_->tracks[static_cast<size_t>(row)].id);
            if (takes_.dragging())
                gesture_ = std::make_unique<TakesGesture>(*this);
            return;
        }
    if (e.x < headerWidth() || e.x >= getWidth() - scrollbarWidth || e.y >= getHeight() - scrollbarHeight)
        return;
    if (e.y < rulerHeight)
    {
        mouseDownRuler(e);
        return;
    }
    mouseDownLanes(e);
}

// The ruler: its menu, the loop band's handles, a time selection, a marker, or
// the loop/punch region (a click locates).
void ArrangeView::mouseDownRuler(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        menus_.rulerMenu(e.getScreenPosition());
        return;
    }
    // The loop band's handles and body, when the loop is on.
    const auto band = loopBand();
    if (loop_ && !band.isEmpty() && e.y >= band.getY())
    {
        const auto at = sampleForX(e.x);
        if (std::abs(e.x - band.getX()) <= loopHandle)
            gesture_ = std::make_unique<LoopGesture>(*this, LoopGesture::Part::start, at);
        else if (std::abs(e.x - band.getRight()) <= loopHandle)
            gesture_ = std::make_unique<LoopGesture>(*this, LoopGesture::Part::end, at);
        else if (band.contains(e.getPosition()))
            gesture_ = std::make_unique<LoopGesture>(*this, LoopGesture::Part::band, at);
        if (gesture_ != nullptr)
            return;
    }
    // Shift+drag in the bars row: a time selection across the selected tracks.
    if (e.mods.isShiftDown() && e.y >= markersRowHeight)
    {
        gesture_ = std::make_unique<TimeSelectGesture>(*this, sampleForX(e.x));
        return;
    }
    // A marker flag: select it, locate to it, and a drag moves it. Elsewhere
    // a drag sets the loop/punch region and a plain click locates.
    if (const auto* marker = markers_.markerAt(e.getPosition()))
    {
        selection_.clear();
        markers_.press(*marker, sampleForX(e.x));
        gesture_ = std::make_unique<MarkerGesture>(*this);
        repaint();
        return;
    }
    markers_.clearSelection();
    gesture_ = std::make_unique<RegionGesture>(*this, sampleForX(e.x));
}

// The lanes: select the track, then pick up a clip (move, trim, fade or gain by
// where it was grabbed), start a range or a lasso.
void ArrangeView::mouseDownLanes(const juce::MouseEvent& e)
{
    markers_.clearSelection();

    const auto at = sampleForX(e.x);
    const auto hit = hitTest(e.getPosition());
    if (hit.track != nullptr)
        selectTrack(hit.track->id);
    else if (const int row = channelRowAtY(e.y); row >= 0)
        selectTrack(channelAtRow(row)->id); // an FX track's lane
    if (e.mods.isPopupMenu())
    {
        if (const auto ref = clipAt(hit); ref.clip != core::invalidId)
        {
            if (!isSelected(ref))
            {
                selection_.clear();
                select(ref, true);
                notifySelection();
            }
            menus_.clipMenu(ref, e.getScreenPosition());
        }
        else if (hit.track != nullptr)
            menus_.trackMenu(hit.track->id, e.getScreenPosition());
        return;
    }
    if (usesClipTool() && mouseDownWithTool(e, hit))
        return;
    if (hit.midiClip != nullptr)
    {
        // MIDI clips move and trim.
        const ClipRef ref{hit.track->id, hit.midiClip->id, true};
        if (e.mods.isShiftDown() || e.mods.isCtrlDown())
            select(ref, true);
        else if (!isSelected(ref))
        {
            selection_.clear();
            select(ref, true);
        }
        ArrangeClipPress press{ref, {hit.midiClip->start, hit.midiClip->length}, {}, at, hit.row};
        if (hit.zone == Zone::move)
            gesture_ = std::make_unique<MoveGesture>(*this, std::move(press));
        else
            gesture_ = std::make_unique<TrimGesture>(*this, std::move(press), hit.zone == Zone::trimLeft);
        repaint();
        return;
    }
    if (tool_ == Tool::pencil && hit.clip == nullptr && hit.track != nullptr && hit.track->isMidi())
    {
        // The pencil: a new clip from the grid line at or before the press.
        gesture_ = std::make_unique<DrawGesture>(*this, hit.track->id, at);
        repaint();
        return;
    }
    if (hit.clip == nullptr && tool_ == Tool::range)
    {
        // The range tool: a drag in a lane selects a time range.
        gesture_ = std::make_unique<TimeSelectGesture>(*this, at);
        return;
    }
    if (hit.clip == nullptr)
    {
        // Empty lane: a drag lassos, a click locates (and clears the selection).
        gesture_ = std::make_unique<LassoGesture>(*this, e.getPosition(), at);
        return;
    }

    const ClipRef ref{hit.track->id, hit.clip->id};
    // Ctrl+drag on the body: gain (Ctrl+click alone toggles the selection); Alt+drag
    // of the right edge: stretch.
    const bool gain = e.mods.isCtrlDown() && hit.zone == Zone::move;
    const bool stretch = hit.zone == Zone::trimRight && e.mods.isAltDown();
    if (e.mods.isShiftDown())
        select(ref, true);
    else if (e.mods.isCtrlDown() && !gain)
        select(ref, true); // toggle happens on mouse up if nothing was dragged
    else if (!isSelected(ref.track, ref.clip))
    {
        selection_.clear();
        select(ref, true);
    }
    // A comp's clips select together: the comp moves and deletes as one, with its takes.
    if (const auto* group = hit.track->takeGroupOwning(hit.clip->id); group != nullptr && isSelected(ref))
        for (const auto id : group->clips)
            if (hit.track->findClip(id) != nullptr)
                select({hit.track->id, id}, true);
    ArrangeClipPress press{ref, {hit.clip->start, hit.clip->length}, *hit.clip, at, hit.row};
    if (gain)
        gesture_ = std::make_unique<GainGesture>(*this, std::move(press));
    else if (stretch)
        gesture_ = std::make_unique<StretchGesture>(*this, std::move(press));
    else if (hit.zone == Zone::fadeIn || hit.zone == Zone::fadeOut)
        gesture_ = std::make_unique<FadeGesture>(*this, std::move(press), hit.zone == Zone::fadeIn);
    else if (hit.zone == Zone::trimLeft || hit.zone == Zone::trimRight)
        gesture_ = std::make_unique<TrimGesture>(*this, std::move(press), hit.zone == Zone::trimLeft);
    else
        gesture_ = std::make_unique<MoveGesture>(*this, std::move(press));
    repaint();
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e)
{
    if (gesture_ == nullptr)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    dragPoint_ = e.getPosition();
    const int x = std::clamp(e.x, headerWidth(), getWidth() - scrollbarWidth);
    gesture_->drag(e, sampleForX(x));
    repaint();
}

void ArrangeView::mouseUp(const juce::MouseEvent& e)
{
    // The drag is over (nothing reads it while it finishes).
    const auto gesture = std::move(gesture_);
    const auto before = std::exchange(pressSelection_, {});
    if (gesture != nullptr)
        gesture->release(e);
    if (selection_ != before)
    {
        // A clip picked on another track moves the track selection with it.
        if (!selection_.empty())
            selectTrack(selection_.back().track);
        notifySelection();
    }
    repaint();
}

void ArrangeView::mouseMove(const juce::MouseEvent& e)
{
    if (gesture_ != nullptr)
        return;
    hoverAt(e.getPosition(), e.mods);
}

void ArrangeView::pointerAt(juce::Point<int> p, juce::ModifierKeys mods)
{
    hoverAt(p, mods);
}

juce::Point<int> ArrangeView::pointOnRow(int row, double bar) const
{
    bar -= 1.0; // the tempo map counts bars from 0
    const int whole = static_cast<int>(std::floor(bar));
    const auto from = tempoMap_.barStart(whole);
    const auto to = tempoMap_.barStart(whole + 1);
    const auto at = from + static_cast<core::SamplePosition>((bar - static_cast<double>(whole)) *
                                                             static_cast<double>(to - from));
    return {xForSample(at), rowBounds(row).getCentreY()};
}

void ArrangeView::modifierKeysChanged(const juce::ModifierKeys& mods)
{
    // Shift turns the split line into one across every track; Alt takes it off the grid.
    if (tool_ == Tool::split && gesture_ == nullptr && pointer_.x >= 0 &&
        (mods.isShiftDown() != splitAll_ || mods.isAltDown() != splitFree_))
        updateSplitHover(pointer_, mods);
}

void ArrangeView::hoverAt(juce::Point<int> p, juce::ModifierKeys mods)
{
    pointer_ = p;
    if (tool_ == Tool::split)
        updateSplitHover(p, mods);
    if (p.y >= rulerHeight && std::abs(p.x - headerWidth()) <= 2)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    if (resizeEdgeAt(p) >= 0)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (p.y < rulerHeight)
    {
        const auto band = loopBand();
        const bool handle =
            loop_ && !band.isEmpty() && p.y >= band.getY() &&
            (std::abs(p.x - band.getX()) <= loopHandle || std::abs(p.x - band.getRight()) <= loopHandle);
        setMouseCursor(handle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        const auto* marker = markers_.markerAt(p);
        if (markers_.setHover(marker != nullptr ? marker->id : core::invalidId))
            repaint(0, 0, getWidth(), rulerHeight);
        return;
    }
    // Take lanes, a comp boundary or the comp's chip set their own cursor.
    const auto takeArea = takeRow(rowAtY(p.y));
    const auto takeCursor = takes_.hover(p, takeArea.track != nullptr ? &takeArea : nullptr);
    const auto hit = hitTest(p);
    // On a clip the select, range and pencil tools trim, fade and move; the
    // other tools act on the whole clip, so their cursor stays.
    const bool onClip = hit.clip != nullptr || hit.midiClip != nullptr;
    setMouseCursor(takeCursor ? *takeCursor : usesClipTool() || !onClip ? toolCursor() : cursorFor(hit.zone));
    const ClipRef clip = hit.clip != nullptr       ? ClipRef{hit.track->id, hit.clip->id}
                         : hit.midiClip != nullptr ? ClipRef{hit.track->id, hit.midiClip->id, true}
                                                   : ClipRef{};
    if (hit.row != hoverRow_ || !(clip == hoverClip_))
    {
        hoverRow_ = hit.row;
        hoverClip_ = clip;
        for (size_t i = 0; i < headers_.size(); ++i)
            headers_[i]->setHovered(static_cast<int>(i) == hoverRow_);
        repaint();
    }
}

void ArrangeView::mouseExit(const juce::MouseEvent&)
{
    pointer_ = {-1, -1};
    if (splitAt_ >= 0)
    {
        repaint(splitArea());
        splitAt_ = -1;
    }
    if (hoverRow_ == -1 && hoverClip_ == ClipRef{} && markers_.hovered() == core::invalidId)
        return;
    hoverRow_ = -1;
    hoverClip_ = ClipRef{};
    markers_.setHover(core::invalidId);
    for (auto& header : headers_)
        header->setHovered(false);
    repaint();
}
void ArrangeView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        setZoom(view_.samplesPerPixel * TimelineViewport::wheelZoomFactor(wheel.deltaY),
                std::max(headerWidth(), e.x));
        return;
    }
    // The wheel scrolls time; Shift+wheel (or a vertical wheel over the headers)
    // scrolls the tracks.
    const bool vertical = e.mods.isShiftDown() || (e.x < headerWidth() && wheel.deltaX == 0.0f);
    const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    if (delta == 0.0f)
        return;
    if (vertical)
        setVerticalScroll(verticalScroll_ - static_cast<int>(delta * static_cast<float>(rowHeight) * 3.0f));
    else
        setScroll(view_.first -
                  static_cast<core::SamplePosition>(delta * static_cast<float>(visibleSamples()) * 0.5f));
}
// ----- painting ------------------------------------------------------------------------------

juce::Rectangle<int> ArrangeView::loopBand() const noexcept
{
    if (regionEnd_ <= regionStart_)
        return {};
    const int x0 = xForSample(regionStart_);
    const int x1 = xForSample(regionEnd_);
    return {x0, markersRowHeight, std::max(2, x1 - x0), rulerOnly - markersRowHeight - 1};
}

// ----- drag and drop ------------------------------------------------------------------------

bool ArrangeView::isInterestedInDragSource(const SourceDetails& details)
{
    return project_ != nullptr && (details.description.toString().startsWith("plugin:") ||
                                   isAudioFile(fileFromDragDescription(details.description)));
}

void ArrangeView::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void ArrangeView::itemDragMove(const SourceDetails& details)
{
    if (const auto file = fileFromDragDescription(details.description); file != juce::File())
    {
        // A file from the browser shows where it will land, as one from Explorer does.
        dragOver_ = true;
        dropFiles_ = juce::StringArray(file.getFullPathName());
        dropPoint_ = details.localPosition;
        repaint();
        return;
    }
    const int row = rowAtY(details.localPosition.y);
    if (row != pluginDropRow_)
    {
        pluginDropRow_ = row;
        repaint();
    }
}

void ArrangeView::itemDragExit(const SourceDetails&)
{
    pluginDropRow_ = -1;
    dragOver_ = false;
    repaint();
}

void ArrangeView::itemDropped(const SourceDetails& details)
{
    if (const auto file = fileFromDragDescription(details.description); file != juce::File())
    {
        dragOver_ = false;
        repaint();
        if (onImportFiles && file.existsAsFile())
            onImportFiles({file}, sampleForX(std::max(headerWidth(), details.localPosition.x)));
        return;
    }
    const int row = rowAtY(details.localPosition.y);
    pluginDropRow_ = -1;
    repaint();
    if (row < 0 || project_ == nullptr || !onInsertPlugin)
        return;
    const auto type =
        details.description.toString().fromFirstOccurrenceOf("plugin:", false, false).toStdString();
    if (!type.empty())
        onInsertPlugin(project_->tracks[static_cast<size_t>(row)].id, type);
}

void ArrangeView::setAcceptedExtensions(const juce::String& wildcards)
{
    acceptedExtensions_.clear();
    for (const auto& pattern : juce::StringArray::fromTokens(wildcards, ";", ""))
        acceptedExtensions_.add(pattern.trim().fromLastOccurrenceOf("*", false, false).toLowerCase());
}

bool ArrangeView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto extension = juce::File(f).getFileExtension().toLowerCase();
        if (acceptedExtensions_.isEmpty() || acceptedExtensions_.contains(extension))
            return true;
    }
    return false;
}

void ArrangeView::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    dragOver_ = true;
    dropFiles_ = files;
    dropPoint_ = {x, y};
    repaint();
}

void ArrangeView::fileDragMove(const juce::StringArray&, int x, int y)
{
    dropPoint_ = {x, y};
    repaint();
}
void ArrangeView::fileDragExit(const juce::StringArray&)
{
    dragOver_ = false;
    repaint();
}

void ArrangeView::filesDropped(const juce::StringArray& files, int x, int)
{
    dragOver_ = false;
    repaint();
    if (!onImportFiles)
        return;
    std::vector<juce::File> accepted;
    for (const auto& f : files)
    {
        const juce::File file(f);
        if (acceptedExtensions_.isEmpty() ||
            acceptedExtensions_.contains(file.getFileExtension().toLowerCase()))
            accepted.push_back(file);
    }
    if (!accepted.empty())
        onImportFiles(accepted, sampleForX(std::max(headerWidth(), x)));
}

juce::Colour ArrangeView::trackColour(const core::Track& track, int row) const
{
    return theme_.track.forTrack(track.colour, row);
}

void ArrangeView::paint(juce::Graphics& g)
{
    Painter(*this).paint(g);
}

// ----- editing -------------------------------------------------------------------------------

int ArrangeView::channelRowAtY(int y) const noexcept
{
    if (project_ == nullptr || y < rulerHeight || y >= getHeight() - scrollbarHeight)
        return -1;
    return rows().layout.rowAt(y - rulerHeight + verticalScroll_);
}

int ArrangeView::rowAtY(int y) const noexcept
{
    // Tracks only (FX tracks have no clips).
    const int row = channelRowAtY(y);
    return project_ != nullptr && row >= 0 && static_cast<size_t>(row) < project_->tracks.size() ? row : -1;
}

int ArrangeView::rowHeightOf(int row) const noexcept
{
    const auto& layout = rows().layout;
    return row >= 0 && row < layout.count() ? layout.height(row) : rowHeight;
}

int ArrangeView::automationHeight(int row) const noexcept
{
    const auto* channel = channelAtRow(row);
    if (channel == nullptr || project_ == nullptr)
        return 0;
    if (static_cast<size_t>(row) < project_->tracks.size())
    {
        const auto& track = project_->tracks[static_cast<size_t>(row)];
        if (!track.hasAudio() || project_->hiddenByFolder(track))
            return 0;
    }
    return automation_.blockHeight(*channel);
}

juce::Rectangle<int> ArrangeView::automationBlock(int row) const noexcept
{
    const int h = automationHeight(row);
    if (h <= 0)
        return {};
    // The lanes end the row's block: under the row and its take lanes.
    const int bottom = rulerHeight - verticalScroll_ + rows().layout.top(row + 1);
    return {0, bottom - h, getWidth() - scrollbarWidth, h};
}

TakeLanes::Row ArrangeView::takeRow(int row) const
{
    TakeLanes::Row r;
    if (project_ == nullptr || row < 0 || static_cast<size_t>(row) >= project_->tracks.size())
        return r;
    r.track = &project_->tracks[static_cast<size_t>(row)];
    r.main = rowBounds(row).withLeft(headerWidth());
    r.headerWidth = headerWidth();
    r.colour = trackColour(*r.track, row);
    return r;
}

int ArrangeView::rowTop(int row) const noexcept
{
    return rulerHeight - verticalScroll_ + rows().layout.top(row);
}

int ArrangeView::resizeEdgeAt(juce::Point<int> p) const noexcept
{
    if (project_ == nullptr || p.y < rulerHeight || p.y >= getHeight() - scrollbarHeight ||
        p.x >= getWidth() - scrollbarWidth)
        return -1;
    // The row whose bottom edge is nearest the point, within the edge's reach
    // (the bottom of the row's own height: take lanes under it do not resize it).
    return rows().layout.resizeEdgeAt(p.y - rulerHeight + verticalScroll_, TrackHeader::resizeEdge);
}

juce::Rectangle<int> ArrangeView::rowBounds(int row) const noexcept
{
    return {0, rowTop(row), getWidth() - scrollbarWidth, rowHeightOf(row)};
}

juce::Rectangle<int> ArrangeView::clipBounds(const core::AudioClip& clip, int row) const noexcept
{
    return spanBounds(clip.start, clip.length, row);
}

juce::Rectangle<int>
ArrangeView::spanBounds(core::SamplePosition start, core::SamplePosition length, int row) const noexcept
{
    const auto lane = rowBounds(row);
    const int x0 = xForSample(start);
    const int x1 = std::max(x0 + 2, xForSample(start + length));
    return {x0, lane.getY() + 2, x1 - x0, lane.getHeight() - 5};
}
std::optional<ArrangeView::Span> ArrangeView::spanOf(const ClipRef& ref) const
{
    return project_ != nullptr ? edit::spanOf(*project_, ref) : std::nullopt;
}

bool ArrangeView::isSelected(const ClipRef& ref) const noexcept
{
    if (selectionIndexDepth_ > 0)
        return selectionIndex_.contains(ref);
    return std::find(selection_.begin(), selection_.end(), ref) != selection_.end();
}

ArrangeView::Hit ArrangeView::hitTest(juce::Point<int> p) const noexcept
{
    Hit hit;
    if (p.x < headerWidth() || p.x >= getWidth() - scrollbarWidth)
        return hit;
    hit.row = rowAtY(p.y);
    if (hit.row < 0)
        return hit;
    hit.track = &project_->tracks[static_cast<size_t>(hit.row)];
    // MIDI clips: move and trim only.
    for (auto it = hit.track->midiClips.rbegin(); it != hit.track->midiClips.rend(); ++it)
    {
        const auto bounds = spanBounds(it->start, it->length, hit.row);
        if (!bounds.contains(p))
            continue;
        hit.midiClip = &*it;
        hit.zone = p.x < bounds.getX() + edgeZone        ? Zone::trimLeft
                   : p.x >= bounds.getRight() - edgeZone ? Zone::trimRight
                                                         : Zone::move;
        return hit;
    }
    // Later clips draw on top, so they win the hit.
    for (auto it = hit.track->clips.rbegin(); it != hit.track->clips.rend(); ++it)
    {
        const auto bounds = clipBounds(*it, hit.row);
        if (!bounds.contains(p))
            continue;
        hit.clip = &*it;
        const bool topRow = p.y < bounds.getY() + fadeHandle;
        if (topRow && p.x < bounds.getX() + fadeHandle && bounds.getWidth() > 2 * fadeHandle + 4)
            hit.zone = Zone::fadeIn;
        else if (topRow && p.x >= bounds.getRight() - fadeHandle && bounds.getWidth() > 2 * fadeHandle + 4)
            hit.zone = Zone::fadeOut;
        else if (p.x < bounds.getX() + edgeZone)
            hit.zone = Zone::trimLeft;
        else if (p.x >= bounds.getRight() - edgeZone)
            hit.zone = Zone::trimRight;
        else
            hit.zone = Zone::move;
        return hit;
    }
    return hit;
}

bool ArrangeView::isSelected(core::Id track, core::Id clip) const noexcept
{
    return isSelected(ClipRef{track, clip});
}

void ArrangeView::select(const ClipRef& ref, bool add)
{
    if (!add)
        selection_.clear();
    if (!isSelected(ref))
    {
        selection_.push_back(ref);
        if (selectionIndexDepth_ > 0)
            selectionIndex_.insert(ref);
    }
}

void ArrangeView::notifySelection()
{
    if (selection_ == notifiedSelection_)
        return;
    notifiedSelection_ = selection_;
    if (onSelectionChanged)
        onSelectionChanged();
}

void ArrangeView::selectAll()
{
    selection_.clear();
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
        {
            for (const auto& c : t.clips)
                selection_.push_back({t.id, c.id});
            for (const auto& m : t.midiClips)
                selection_.push_back({t.id, m.id, true});
        }
    notifySelection();
    repaint();
}

void ArrangeView::clearSelection()
{
    selection_.clear();
    notifySelection();
    repaint();
}

void ArrangeView::setSnapEnabled(bool on)
{
    if (on == snap_)
        return;
    snap_ = on;
    repaint();
    if (onEditModesChanged)
        onEditModesChanged();
}

void ArrangeView::setRippleEnabled(bool on)
{
    if (on == ripple_)
        return;
    ripple_ = on;
    repaint();
    if (onEditModesChanged)
        onEditModesChanged();
}

core::GridStep ArrangeView::gridAt(core::SamplePosition at) const noexcept
{
    return snapStepAt(tempoMap_, at, snapGrid_, view_.samplesPerPixel);
}

core::SamplePosition ArrangeView::lineAtOrBefore(core::SamplePosition s) const noexcept
{
    const auto step = gridAt(s);
    auto line = tempoMap_.snap(s, step);
    if (line > s)
        line = tempoMap_.snap(tempoMap_.move(line, step, -1), step);
    return std::max<core::SamplePosition>(0, line);
}

core::SamplePosition ArrangeView::lineAtOrAfter(core::SamplePosition s) const noexcept
{
    return tempoMap_.nextLine(s, gridAt(s));
}

void ArrangeView::setSnapGrid(int division)
{
    if (std::max(0, division) == snapGrid_)
        return;
    snapGrid_ = std::max(0, division);
    repaint();
    if (onEditModesChanged)
        onEditModesChanged();
}

juce::String ArrangeView::snapGridName() const
{
    switch (snapGrid_)
    {
        case 0:
            return "Adaptive";
        case 1:
            return "Bar";
        case 4:
            return "Beat";
        default:
            return "1/" + juce::String(snapGrid_);
    }
}

void ArrangeView::zoomBy(double factor)
{
    setZoom(view_.samplesPerPixel * factor,
            headerWidth() + (getWidth() - headerWidth() - scrollbarWidth) / 2);
    updateScrollbar();
    repaint();
}

void ArrangeView::zoomToFit()
{
    const int width = std::max(1, getWidth() - headerWidth() - scrollbarWidth - theme_.spacing.xl);
    const auto length = std::max<core::SamplePosition>(project_ != nullptr ? project_->length() : 0,
                                                       static_cast<core::SamplePosition>(sampleRate() * 4.0));
    view_.samplesPerPixel =
        std::clamp(static_cast<double>(length) / width, minSamplesPerPixel, maxSamplesPerPixel);
    setScroll(0);
    updateScrollbar();
    repaint();
}

void ArrangeView::zoomToSelection()
{
    if (selection_.empty())
    {
        zoomToFit();
        return;
    }
    core::SamplePosition from = -1, to = 0;
    for (const auto& ref : selection_)
        if (const auto span = spanOf(ref))
        {
            from = from < 0 ? span->start : std::min(from, span->start);
            to = std::max(to, span->end());
        }
    if (from < 0 || to <= from)
        return;
    const int width = std::max(1, getWidth() - headerWidth() - scrollbarWidth - theme_.spacing.xl);
    view_.samplesPerPixel =
        std::clamp(static_cast<double>(to - from) / width, minSamplesPerPixel, maxSamplesPerPixel);
    setScroll(std::max<core::SamplePosition>(
        0, from - static_cast<core::SamplePosition>(theme_.spacing.md * view_.samplesPerPixel)));
    updateScrollbar();
    repaint();
}

core::SamplePosition ArrangeView::snapped(core::SamplePosition s) const noexcept
{
    return snappedOn(tempoMap_, s);
}

core::SamplePosition ArrangeView::snappedOn(const core::TempoMap& map, core::SamplePosition s) const noexcept
{
    const bool alt = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    const auto at = std::max<core::SamplePosition>(0, s);
    return snap_ && !alt ? snappedToGrid(map, at, snapGrid_, view_.samplesPerPixel) : at;
}

core::SamplePosition ArrangeView::snapWithTargets(core::SamplePosition wanted,
                                                  int row,
                                                  bool relativeTo,
                                                  core::SamplePosition original)
{
    snapTargetName_.clear();
    const bool alt = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    wanted = std::max<core::SamplePosition>(0, wanted);
    if (!snap_ || alt || project_ == nullptr)
        return wanted;
    const auto tolerance = static_cast<core::SamplePosition>(6.0 * view_.samplesPerPixel);
    core::SamplePosition best = -1;
    // The target's name is put together for the winner only (this runs every
    // drag event, over every clip on the row).
    const std::string* bestSource = nullptr;
    const char* bestSuffix = "";
    const auto consider = [&](core::SamplePosition target, const std::string& source, const char* suffix)
    {
        const auto distance = std::abs(target - wanted);
        if (distance <= tolerance && (best < 0 || distance < std::abs(best - wanted)))
        {
            best = target;
            bestSource = &source;
            bestSuffix = suffix;
        }
    };
    const SelectionIndex index(*this);
    if (row >= 0 && static_cast<size_t>(row) < project_->tracks.size())
    {
        const auto& track = project_->tracks[static_cast<size_t>(row)];
        for (const auto& clip : track.clips)
            if (!isSelected(track.id, clip.id))
            {
                consider(clip.start, clip.name, " start");
                consider(clip.end(), clip.name, " end");
            }
        for (const auto& clip : track.midiClips)
            if (!isSelected({track.id, clip.id, true}))
            {
                consider(clip.start, clip.name, " start");
                consider(clip.end(), clip.name, " end");
            }
    }
    for (const auto& m : project_->markers)
    {
        consider(m.position, m.name, "");
        if (m.isRegion())
            consider(m.position + m.length, m.name, " end");
    }
    static const std::string loopName = "loop", punchInName = "punch in", punchOutName = "punch out",
                             regionName = "region", playheadName = "playhead";
    if (regionEnd_ > regionStart_)
    {
        if (punch_ && !loop_)
        {
            consider(regionStart_, punchInName, "");
            consider(regionEnd_, punchOutName, "");
        }
        else
        {
            consider(regionStart_, loop_ ? loopName : regionName, " start");
            consider(regionEnd_, loop_ ? loopName : regionName, " end");
        }
    }
    consider(playhead_, playheadName, "");
    if (best >= 0)
    {
        snapTargetName_ = juce::String::fromUTF8(bestSource->c_str()) + bestSuffix;
        return best;
    }
    // Relative snap: a clip that did not start on the grid keeps its offset.
    const auto step = gridAt(wanted);
    if (relativeTo)
        if (const auto offset = original - tempoMap_.snap(original, step); offset != 0)
            return tempoMap_.snap(wanted - offset, step) + offset;
    return tempoMap_.snap(wanted, step);
}

void ArrangeView::setTool(Tool tool)
{
    if (tool == tool_)
        return;
    tool_ = tool;
    setMouseCursor(toolCursor());
    if (splitAt_ >= 0)
    {
        repaint(splitArea());
        splitAt_ = -1;
    }
    if (tool_ == Tool::split && pointer_.x >= 0)
        updateSplitHover(pointer_, juce::ModifierKeys::currentModifiers);
    refreshToolButtons();
    repaint(); // the glue and erase tools mark the hovered clip
    if (onToolChanged)
        onToolChanged(tool);
}

std::array<IconButton*, ArrangeView::toolCount> ArrangeView::toolButtons() noexcept
{
    return {&selectTool_, &rangeTool_, &pencilTool_, &eraseTool_, &splitTool_, &glueTool_, &muteTool_};
}

void ArrangeView::refreshToolButtons()
{
    const auto tools = toolButtons();
    for (size_t i = 0; i < tools.size(); ++i)
        tools[i]->setToggleState(static_cast<Tool>(i) == tool_, juce::dontSendNotification);
    toolMenu_.setIcon(tools[static_cast<size_t>(tool_)]->icon());
    toolMenu_.setToggleState(tool_ != Tool::select, juce::dontSendNotification);
}

const char* ArrangeView::toolName(Tool tool) noexcept
{
    switch (tool)
    {
        case Tool::select:
            return "select";
        case Tool::range:
            return "range";
        case Tool::pencil:
            return "pencil";
        case Tool::erase:
            return "erase";
        case Tool::split:
            return "split";
        case Tool::glue:
            return "glue";
        case Tool::mute:
            return "mute";
    }
    return "select";
}

bool ArrangeView::usesClipTool() const noexcept
{
    return tool_ == Tool::erase || tool_ == Tool::split || tool_ == Tool::glue || tool_ == Tool::mute;
}

ArrangeView::ClipRef ArrangeView::clipAt(const Hit& hit) const noexcept
{
    if (hit.clip != nullptr)
        return {hit.track->id, hit.clip->id};
    if (hit.midiClip != nullptr)
        return {hit.track->id, hit.midiClip->id, true};
    return {};
}

juce::MouseCursor ArrangeView::toolCursor() const
{
    switch (tool_)
    {
        case Tool::select:
            return juce::MouseCursor::NormalCursor;
        case Tool::range:
            return juce::MouseCursor::IBeamCursor;
        case Tool::pencil:
            return Icons::toolCursor(theme_, Icon::pencil);
        case Tool::erase:
            return Icons::toolCursor(theme_, Icon::eraser);
        case Tool::split:
            return Icons::toolCursor(theme_, Icon::scissors);
        case Tool::glue:
            return Icons::toolCursor(theme_, Icon::glue);
        case Tool::mute:
            return Icons::toolCursor(theme_, Icon::muteClip);
    }
    return juce::MouseCursor::NormalCursor;
}

core::SamplePosition ArrangeView::splitPosition(int x, const Span& clip, bool free) const noexcept
{
    // On the grid when the grid line falls inside the clip, else at the pointer.
    auto at = free ? sampleForX(x) : snapped(sampleForX(x));
    if (at <= clip.start || at >= clip.end())
        at = sampleForX(x);
    return std::clamp(at, clip.start + 1, std::max(clip.start + 1, clip.end() - 1));
}

juce::Rectangle<int> ArrangeView::splitArea() const
{
    if (splitAt_ < 0)
        return {};
    const int x = xForSample(splitAt_);
    const auto line =
        splitRow_ >= 0 ? spanBounds(splitClip_.start, splitClip_.length, splitRow_).withX(x - 1).withWidth(3)
                       : juce::Rectangle<int>(x - 1, rulerHeight, 3, lanesHeight());
    // The position tag beside it (drawTooltip may put it on either side).
    const int tag = 160;
    return line.getUnion({x - tag - 8,
                          splitPoint_.y - 2 * theme_.sizes.controlCompact - 8,
                          2 * tag + 24,
                          4 * theme_.sizes.controlCompact + 16});
}

void ArrangeView::updateSplitHover(juce::Point<int> p, juce::ModifierKeys mods)
{
    const auto old = splitArea();
    const bool all = mods.isShiftDown();
    splitAt_ = -1;
    splitRow_ = -1;
    splitAll_ = all;
    splitFree_ = mods.isAltDown();
    splitPoint_ = p;
    const bool inLanes = p.x >= headerWidth() && p.x < getWidth() - scrollbarWidth && p.y >= rulerHeight &&
                         p.y < getHeight() - scrollbarHeight;
    if (inLanes && project_ != nullptr)
    {
        const auto hit = hitTest(p);
        if (const auto ref = clipAt(hit); ref.clip != core::invalidId)
            if (const auto span = spanOf(ref))
            {
                splitClip_ = *span;
                splitRow_ = all ? -1 : hit.row;
                splitAt_ = splitPosition(p.x, *span, splitFree_);
            }
        if (splitAt_ < 0 && all)
            splitAt_ =
                std::max<core::SamplePosition>(0, splitFree_ ? sampleForX(p.x) : snapped(sampleForX(p.x)));
    }
    if (!old.isEmpty())
        repaint(old);
    if (splitAt_ >= 0)
        repaint(splitArea());
}

bool ArrangeView::mouseDownWithTool(const juce::MouseEvent& e, const Hit& hit)
{
    if (project_ == nullptr)
        return false;
    const auto ref = clipAt(hit);
    const bool onClip = ref.clip != core::invalidId;
    // A press on a clip of a larger selection acts on the whole selection.
    const auto targets = [&]
    { return onClip && isSelected(ref) && selection_.size() > 1 ? selection_ : std::vector<ClipRef>{ref}; };
    switch (tool_)
    {
        case Tool::split:
        {
            const bool all = e.mods.isShiftDown(), free = e.mods.isAltDown();
            if (!onClip && !all)
                return false; // empty space: a click locates, a drag lassos
            const auto span = onClip ? spanOf(ref) : std::nullopt;
            const auto at =
                span ? splitPosition(e.x, *span, free)
                     : std::max<core::SamplePosition>(0, free ? sampleForX(e.x) : snapped(sampleForX(e.x)));
            // Shift cuts every clip there, on every track.
            auto edit = edit::splitAt(*project_, all ? edit::Selection{} : targets(), at);
            emit(std::move(edit.commands), edit.name);
            updateSplitHover(e.getPosition(), e.mods);
            return true;
        }
        case Tool::glue:
        {
            if (!onClip)
                return false;
            const auto clips = targets();
            auto edit =
                clips.size() > 1 ? edit::joinClips(*project_, clips) : edit::joinWithNext(*project_, ref);
            emit(std::move(edit.commands), edit.name);
            return true;
        }
        case Tool::erase:
        case Tool::mute:
        {
            auto sweep = std::make_unique<SweepGesture>(*this, tool_ == Tool::erase);
            if (onClip)
            {
                // The clip pressed decides what the sweep does: a playing clip mutes the rest.
                sweep->over(ref);
                for (const auto& r : targets())
                    sweep->over(r);
            }
            gesture_ = std::move(sweep);
            repaint();
            return true;
        }
        case Tool::select:
        case Tool::range:
        case Tool::pencil:
            break;
    }
    return false;
}

void ArrangeView::openFirstClipMenu()
{
    if (project_ == nullptr)
        return;
    for (size_t row = 0; row < project_->tracks.size(); ++row)
    {
        const auto& track = project_->tracks[row];
        ClipRef ref;
        if (!track.clips.empty())
            ref = {track.id, track.clips.front().id};
        else if (!track.midiClips.empty())
            ref = {track.id, track.midiClips.front().id, true};
        else
            continue;
        const auto span = spanOf(ref);
        selection_.assign(1, ref);
        notifySelection();
        repaint();
        const auto at = spanBounds(span->start, span->length, static_cast<int>(row)).getCentre();
        menus_.clipMenu(ref, localPointToGlobal(at));
        return;
    }
}

void ArrangeView::addMidiClip(core::Id trackId, core::SamplePosition start, core::SamplePosition end)
{
    const auto* track = project_ != nullptr ? project_->findTrack(trackId) : nullptr;
    if (track == nullptr || !track->isMidi() || end <= start)
        return;
    core::MidiClip clip;
    clip.name = "MIDI clip";
    clip.start = std::max<core::SamplePosition>(0, start);
    clip.length = end - clip.start;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::AddMidiClip>(trackId, clip));
    emit(std::move(commands), "Draw MIDI clip");
    // The new clip, selected: the one at that start (ids are given on apply).
    if (const auto* now = project_->findTrack(trackId))
        for (const auto& c : now->midiClips)
            if (c.start == clip.start && c.length == clip.length && c.notes.empty())
            {
                selection_.clear();
                select({trackId, c.id, true}, true);
                notifySelection();
            }
    repaint();
}

void ArrangeView::copySelection()
{
    if (project_ != nullptr)
        clipboard_ = edit::copyClips(*project_, selection_);
}

void ArrangeView::cutSelection()
{
    copySelection();
    if (!clipboard_.empty())
        deleteSelection();
}

void ArrangeView::paste()
{
    if (project_ == nullptr || clipboard_.empty())
        return;
    auto e = edit::pasteClips(*project_,
                              clipboard_,
                              project_->trackIndex(selectedTrack()),
                              playheadForEdits ? playheadForEdits() : playhead_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::renameSelectedTrack()
{
    for (auto& header : headers_)
        if (header->trackId() == selectedTrack())
            header->startRename();
}
const core::AudioClip* ArrangeView::findClip(const ClipRef& ref, const core::Track** track) const noexcept
{
    if (project_ == nullptr || ref.midi)
        return nullptr;
    const auto* t = project_->findTrack(ref.track);
    if (t == nullptr)
        return nullptr;
    if (track != nullptr)
        *track = t;
    return t->findClip(ref.clip);
}

void ArrangeView::emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
{
    if (!commands.empty() && onEdit)
        onEdit(std::move(commands), name);
}

void ArrangeView::selectInLasso(juce::Rectangle<int> lasso)
{
    if (project_ == nullptr)
        return;
    const auto from = sampleForX(lasso.getX());
    const auto to = sampleForX(lasso.getRight());
    const SelectionIndex index(*this); // each clip in the lasso checks the selection
    for (size_t t = 0; t < project_->tracks.size(); ++t)
    {
        const auto lane = rowBounds(static_cast<int>(t));
        if (lane.getBottom() <= lasso.getY() || lane.getY() >= lasso.getBottom())
            continue;
        for (const auto& c : project_->tracks[t].clips)
            if (c.end() > from && c.start < to)
                select({project_->tracks[t].id, c.id}, true);
        for (const auto& m : project_->tracks[t].midiClips)
            if (m.end() > from && m.start < to)
                select({project_->tracks[t].id, m.id, true}, true);
    }
}

std::vector<const core::Track*> ArrangeView::folderChoices(core::Id trackId) const
{
    // Folders the track can go into: not itself, nor one inside it.
    std::vector<const core::Track*> out;
    if (project_ == nullptr)
        return out;
    for (const auto& t : project_->tracks)
        if (t.isFolder() && !project_->insideFolder(t.id, trackId))
            out.push_back(&t);
    return out;
}

void ArrangeView::deleteSelection()
{
    removeClips(std::vector<ClipRef>(selection_));
}

void ArrangeView::removeClips(const std::vector<ClipRef>& clips)
{
    if (project_ == nullptr || clips.empty())
        return;
    auto e = edit::deleteClips(*project_, clips, ripple_);
    // A take group whose comp clips are all deleted goes too, takes and all.
    for (const auto& track : project_->tracks)
        for (const auto& group : track.takeGroups)
            if (!group.clips.empty() && std::all_of(group.clips.begin(),
                                                    group.clips.end(),
                                                    [&](core::Id id)
                                                    {
                                                        return track.findClip(id) == nullptr ||
                                                               edit::contains(clips, {track.id, id});
                                                    }))
                e.commands.push_back(std::make_unique<core::RemoveTakeGroup>(track.id, group.id));
    std::erase_if(selection_, [&](const ClipRef& ref) { return edit::contains(clips, ref); });
    emit(std::move(e.commands), e.name);
    notifySelection();
}

void ArrangeView::toggleMuteSelection()
{
    if (project_ == nullptr || selection_.empty())
        return;
    auto e = edit::toggleMuted(*project_, selection_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::duplicateSelection()
{
    if (project_ == nullptr)
        return;
    auto e = edit::duplicateClips(*project_, selection_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::splitAtPlayhead()
{
    // Selected clips under the playhead; with nothing selected, every clip there.
    if (project_ == nullptr)
        return;
    auto e = edit::splitAt(*project_, selection_, playheadForEdits ? playheadForEdits() : playhead_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::splitAtSelection()
{
    // Splits the selected clips (or every clip) at both ends of the time selection,
    // or at the loop region when there is no time selection.
    if (project_ == nullptr)
        return;
    const auto start = timeSelEnd_ > timeSelStart_ ? timeSelStart_ : regionStart_;
    const auto end = timeSelEnd_ > timeSelStart_ ? timeSelEnd_ : regionEnd_;
    auto e = edit::splitAtRange(*project_, selection_, start, end);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::finishTimeSelection()
{
    if (timeSelEnd_ <= timeSelStart_)
    {
        timeSelTracks_.clear();
        return;
    }
    // The range applies to the selected tracks: the clip selection's tracks, else
    // the selected track, else every track.
    timeSelTracks_.clear();
    for (const auto& ref : selection_)
        if (std::find(timeSelTracks_.begin(), timeSelTracks_.end(), ref.track) == timeSelTracks_.end())
            timeSelTracks_.push_back(ref.track);
    if (timeSelTracks_.empty() && selectedTrack() != core::invalidId)
        timeSelTracks_.push_back(selectedTrack());
    if (timeSelTracks_.empty() && project_ != nullptr)
        for (const auto& t : project_->tracks)
            timeSelTracks_.push_back(t.id);
}

void ArrangeView::reorderDrag(core::Id trackId, juce::Point<int> screen, bool released)
{
    if (project_ == nullptr)
        return;
    const auto local = getLocalPoint(nullptr, screen);
    const int count = static_cast<int>(project_->tracks.size());
    // The insertion index: the row boundary nearest the pointer.
    int index = 0;
    for (int i = 1; i <= count; ++i)
        if (std::abs(rowTop(i) - local.y) < std::abs(rowTop(index) - local.y))
            index = i;
    if (!released)
    {
        reorderFrom_ = trackId;
        reorderTo_ = index;
        repaint();
        return;
    }
    const int from = project_->trackIndex(trackId);
    reorderFrom_ = core::invalidId;
    reorderTo_ = -1;
    repaint();
    const int to = index > from ? index - 1 : index;
    if (from >= 0 && to != from && onCommand)
        onCommand(std::make_unique<core::MoveTrack>(trackId, to));
}

void ArrangeView::joinSelection()
{
    if (project_ == nullptr)
        return;
    auto e = edit::joinClips(*project_, selection_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::crossfadeSelection()
{
    if (project_ == nullptr)
        return;
    auto e = edit::crossfadeClips(*project_, selection_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::nudgeSelection(int direction, bool fine)
{
    if (project_ == nullptr)
        return;
    // A grid step (a quarter of one when fine) on the tempo track, from where
    // the first selected clip starts.
    core::SamplePosition from = -1;
    for (const auto& ref : selection_)
        if (const auto span = spanOf(ref))
            from = from < 0 ? span->start : std::min(from, span->start);
    if (from < 0)
        return;
    auto step = gridAt(from);
    if (fine)
        step = step.bars > 0 ? core::GridStep{0,
                                              tempoMap_.stepSamples(step, from) /
                                                  tempoMap_.samplesPerQuarterAt(from) / 4.0}
                             : core::GridStep{0, step.quarters / 4.0};
    const auto delta =
        std::max<core::SamplePosition>(1, std::abs(tempoMap_.move(from, step, direction) - from));
    auto e = edit::nudgeClips(*project_, selection_, delta * direction);
    emit(std::move(e.commands), e.name);
}
void ArrangeView::normaliseSelection()
{
    if (!onNormalise)
        return;
    for (const auto ref : std::vector<ClipRef>(selection_))
        onNormalise(ref.track, ref.clip);
}

bool ArrangeView::handleEditKey(const juce::KeyPress& key)
{
    return ArrangeKeymap::handle(*this, key);
}

juce::MouseCursor ArrangeView::cursorFor(Zone zone) const noexcept
{
    switch (zone)
    {
        case Zone::trimLeft:
        case Zone::trimRight:
            return juce::MouseCursor::LeftRightResizeCursor;
        case Zone::fadeIn:
        case Zone::fadeOut:
            return juce::MouseCursor::PointingHandCursor;
        case Zone::move:
            return juce::MouseCursor::DraggingHandCursor;
        default:
            return juce::MouseCursor::NormalCursor;
    }
}

juce::String ArrangeView::positionText(core::SamplePosition position) const
{
    if (project_ == nullptr)
        return {};
    const auto bb = tempoMap_.barsBeatsAt(std::max<core::SamplePosition>(0, position));
    return juce::String(bb.bar) + "." + juce::String(bb.beat) + "." +
           juce::String(bb.tick).paddedLeft('0', 3);
}

void ArrangeView::drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at)
{
    paintDragTip(g,
                 theme_,
                 text,
                 at,
                 getLocalBounds().withTrimmedRight(scrollbarWidth).withTrimmedBottom(scrollbarHeight));
}

juce::Rectangle<int> ArrangeView::lanesBounds() const noexcept
{
    return {headerWidth(), rulerHeight, getWidth() - headerWidth() - scrollbarWidth, lanesHeight()};
}
// ----- markers -------------------------------------------------------------------------------

void ArrangeView::addMarkerAtPlayhead(bool region)
{
    markers_.addMarker(playheadForEdits ? playheadForEdits() : playhead_, region, regionStart_, regionEnd_);
}

void ArrangeView::jumpToMarker(int direction)
{
    markers_.jump(direction, playheadForEdits ? playheadForEdits() : playhead_);
    repaint();
}

void ArrangeView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (const int edge = resizeEdgeAt(e.getPosition()); edge >= 0)
    {
        const auto* channel = channelAtRow(edge);
        if (channel->height != 0)
        {
            std::vector<std::unique_ptr<core::Command>> commands;
            commands.push_back(std::make_unique<core::SetChannelHeight>(channel->id, 0));
            emit(std::move(commands), "Reset track height");
            layoutHeaders();
            updateScrollbar();
            repaint();
        }
        return;
    }
    if (e.y < rulerHeight)
    {
        if (const auto* marker = markers_.markerAt(e.getPosition()))
        {
            markers_.rename(marker->id);
            return;
        }
        // The bars row: the loop becomes the clip under the pointer in the selected
        // track (or any track), else the region marker there.
        if (e.y >= markersRowHeight && project_ != nullptr && onRegionChanged)
        {
            const auto at = sampleForX(e.x);
            for (const auto& t : project_->tracks)
            {
                if (selectedTrack() != core::invalidId && t.id != selectedTrack())
                    continue;
                for (const auto& clip : t.clips)
                    if (at >= clip.start && at < clip.end())
                    {
                        onRegionChanged(clip.start, clip.end());
                        return;
                    }
                for (const auto& clip : t.midiClips)
                    if (at >= clip.start && at < clip.end())
                    {
                        onRegionChanged(clip.start, clip.end());
                        return;
                    }
            }
            for (const auto& m : project_->markers)
                if (m.isRegion() && at >= m.position && at < m.position + m.length)
                {
                    onRegionChanged(m.position, m.position + m.length);
                    return;
                }
        }
        return;
    }
    const auto hit = hitTest(e.getPosition());
    if (hit.midiClip != nullptr && onOpenMidiClip)
        onOpenMidiClip(hit.track->id, hit.midiClip->id);
    else if (hit.clip != nullptr && onOpenAudioClip)
        onOpenAudioClip(hit.track->id, hit.clip->id);
    else if (hit.track != nullptr && hit.track->isMidi() && hit.clip == nullptr && hit.midiClip == nullptr)
    {
        // Empty space on a MIDI track: a new bar-long clip from the bar there.
        const auto at = std::max<core::SamplePosition>(0, sampleForX(e.x));
        const int bar = tempoMap_.barAtQuarter(tempoMap_.quartersAt(static_cast<double>(at)));
        addMidiClip(hit.track->id, tempoMap_.barStart(bar), tempoMap_.barStart(bar + 1));
    }
}

} // namespace northlight::ui
