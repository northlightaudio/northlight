#include "ui/ArrangeView.h"

#include "core/Command.h"
#include "core/Comping.h"
#include "plugins/FxTracks.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/MediaFiles.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"
#include "ui/PluginMenu.h"
#include "ui/timeline/TimeGrid.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr double minSamplesPerPixel = 1.0;
constexpr double maxSamplesPerPixel = 48000.0 * 16.0; // eight bars per pixel at 120 bpm
} // namespace

ArrangeView::ArrangeView(audio::PeakStore& peaks)
    : peaks_(peaks), addTrack_(theme_, Icon::add, "Add track"),
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
    addTrack_.onClick = [this] { addTrackMenu(); };
    rowsSmaller_.onClick = [this]
    { setRowHeight(rowHeight > theme_.sizes.trackRow ? theme_.sizes.trackRow : theme_.sizes.trackRowMin); };
    rowsLarger_.onClick = [this]
    { setRowHeight(rowHeight < theme_.sizes.trackRow ? theme_.sizes.trackRow : theme_.sizes.trackRowLarge); };
    const auto tools = toolButtons();
    for (size_t i = 0; i < tools.size(); ++i)
        tools[i]->onClick = [this, i] { setTool(static_cast<Tool>(i)); };
    toolMenu_.setShowsMenuChevron(true);
    toolMenu_.onClick = [this] { toolsMenu(); };
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
        const auto n = static_cast<int>(headers_.size());
        if (row >= 0 && row < n)
            return headers_[static_cast<size_t>(row)]->colour();
        if (row >= n && row - n < static_cast<int>(fxHeaders_.size()))
            return fxHeaders_[static_cast<size_t>(row - n)]->colour();
        return theme_.colours.textTertiary;
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
        if (rows().tops != valuesTops_)
        {
            valuesTops_ = rows().tops;
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
    if (project_ == nullptr || project_->findMarker(selectedMarker_) == nullptr)
        selectedMarker_ = core::invalidId;
    if (project_ == nullptr || project_->findChannel(selectedTrack_) == nullptr) // a track or an FX track
        selectedTrack_ = core::invalidId;
    refreshMissingMedia();
    animateRowChanges();
    rebuildHeaders();
    valuesTops_ = rows().tops;
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

void ArrangeView::setSelectedTrack(core::Id trackId)
{
    if (selectedTrack_ == trackId)
        return;
    selectedTrack_ = trackId;
    for (auto* list : {&headers_, &fxHeaders_})
        for (auto& header : *list)
            header->setSelected(header->trackId() == trackId);
    repaint();
}

void ArrangeView::selectTrack(core::Id trackId)
{
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
        header->onContextMenu = [this](core::Id id, juce::Point<int> screen) { trackMenu(id, screen); };
        header->onTargetMenu = [this](core::Id id, juce::Rectangle<int> area) { targetMenu(id, area); };
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
                masterMenu(screen);
            else
                fxTrackMenu(id, screen);
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
        const auto* bus = project_->findBus(fx[i]->id);
        // Unless coloured: FX tracks Lichen, groups Overcast, the master Slate.
        const char* paletteName = master ? "Slate" : bus != nullptr && bus->fx ? "Lichen" : "Overcast";
        if (shown.colour == 0 || master)
            for (const auto& entry : theme_.track.palette)
                if (entry.name == paletteName)
                    shown.colour = entry.value.getARGB();
        fxHeaders_[i]->setMaster(master);
        fxHeaders_[i]->setTrack(
            shown, static_cast<int>(count + i), *project_, inputChoices_, midiInputNames_);
        fxHeaders_[i]->setSelected(fx[i]->id == selectedTrack_);
    }
    for (size_t i = 0; i < count; ++i)
    {
        const auto& track = project_->tracks[i];
        headers_[i]->setTrack(track, static_cast<int>(i), *project_, inputChoices_, midiInputNames_);
        headers_[i]->setSelected(track.id == selectedTrack_);
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
    const auto& r = rows();
    for (size_t i = 0; i + 1 < r.tops.size() && i < ids.size(); ++i)
    {
        // The row's own height (not its growth or the gap): what a removal closes.
        const auto* channel = r.channels[i];
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
    rows_.channels.clear();
    rows_.tops.assign(1, 0);
    if (project_ != nullptr)
    {
        for (const auto& t : project_->tracks)
            rows_.channels.push_back(&t);
        for (const auto* channel : busRows())
            rows_.channels.push_back(channel);
        for (const auto* channel : rows_.channels)
        {
            int h = rowHeight;
            if (channel->id == resizeChannel_)
                h = resizeHeight_;
            else if (channel->height > 0)
                h = std::clamp(channel->height, theme_.sizes.trackRowMin, theme_.sizes.trackRowMax);
            if (const auto growing = rowGrowth_.find(channel->id); growing != rowGrowth_.end())
                h = std::max(1, juce::roundToInt(static_cast<float>(h) * growing->second));
            // A track's expanded take lanes and its automation lanes sit under its
            // row (rowHeightOf leaves them out).
            const auto index = rows_.tops.size() - 1;
            if (index >= project_->tracks.size() || project_->tracks[index].hasAudio())
                h += automation_.blockHeight(*channel);
            if (index < project_->tracks.size())
            {
                h += takes_.lanesHeight(project_->tracks[index]);
                // A track in a closed folder keeps its row, at no height.
                if (project_->hiddenByFolder(project_->tracks[index]))
                    h = 0;
            }
            rows_.tops.push_back(rows_.tops.back() + h);
        }
        // A removed row's space, closing: everything from its place down sits lower.
        if (gapRow_ >= 0 && gapHeight_ > 0.0f)
            for (size_t k = static_cast<size_t>(std::min(gapRow_, static_cast<int>(rows_.tops.size()) - 1));
                 k < rows_.tops.size();
                 ++k)
                rows_.tops[k] += juce::roundToInt(gapHeight_);
    }
    rowsValid_ = true;
    return rows_;
}

int ArrangeView::rowCount() const noexcept
{
    return static_cast<int>(rows().channels.size());
}

const core::Channel* ArrangeView::channelAtRow(int row) const
{
    const auto& r = rows();
    return row >= 0 && static_cast<size_t>(row) < r.channels.size() ? r.channels[static_cast<size_t>(row)]
                                                                    : nullptr;
}

int ArrangeView::contentHeight() const noexcept
{
    return rows().tops.back();
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
    if (markerEditor_ != nullptr)
        markerEditor_.reset();
    // The boundary between the headers and the lanes resizes the headers.
    if (e.y >= rulerHeight && std::abs(e.x - headerWidth()) <= 2)
    {
        drag_ = Drag::headerResize;
        return;
    }
    // The middle button pans.
    if (e.mods.isMiddleButtonDown())
    {
        drag_ = Drag::pan;
        panScroll_ = view_.first;
        panVertical_ = verticalScroll_;
        return;
    }
    // A row's bottom edge (under its header or across its lane) resizes that track.
    if (const int edge = resizeEdgeAt(e.getPosition()); edge >= 0 && !e.mods.isPopupMenu())
    {
        resizeStartHeight_ = rowHeightOf(edge);
        resizeHeight_ = resizeStartHeight_;
        resizeChannel_ = channelAtRow(edge)->id;
        rowsValid_ = false;
        drag_ = Drag::rowResize;
        return;
    }
    // Take lanes (their headers too) and the comp's chip, boundaries and menu on the main lane.
    if (e.y >= rulerHeight && e.y < getHeight() - scrollbarHeight && e.x < getWidth() - scrollbarWidth)
        if (const int row = rowAtY(e.y); row >= 0 && takes_.mouseDown(e, takeRow(row)))
        {
            selectTrack(project_->tracks[static_cast<size_t>(row)].id);
            if (takes_.dragging())
                drag_ = Drag::takes;
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
        rulerMenu(e.getScreenPosition());
        return;
    }
    // The loop band's handles and body, when the loop is on.
    const auto band = loopBand();
    if (loop_ && !band.isEmpty() && e.y >= band.getY())
    {
        loopOriginalStart_ = regionStart_;
        loopOriginalEnd_ = regionEnd_;
        dragAnchor_ = sampleForX(e.x);
        if (std::abs(e.x - band.getX()) <= loopHandle)
            drag_ = Drag::loopStart;
        else if (std::abs(e.x - band.getRight()) <= loopHandle)
            drag_ = Drag::loopEnd;
        else if (band.contains(e.getPosition()))
            drag_ = Drag::loopMove;
        if (drag_ != Drag::none)
            return;
    }
    // Shift+drag in the bars row: a time selection across the selected tracks.
    if (e.mods.isShiftDown() && e.y >= markersRowHeight)
    {
        drag_ = Drag::timeSelect;
        dragAnchor_ = snapped(sampleForX(e.x));
        timeSelStart_ = timeSelEnd_ = dragAnchor_;
        return;
    }
    // A marker flag: select it, locate to it, and a drag moves it. Elsewhere
    // a drag sets the loop/punch region and a plain click locates.
    if (const auto* marker = markerAtRulerPoint(e.getPosition()))
    {
        selectedMarker_ = marker->id;
        selection_.clear();
        dragMarker_ = *marker;
        markerPreview_ = marker->position;
        dragStartSample_ = sampleForX(e.x);
        drag_ = Drag::marker;
        if (onLocate)
            onLocate(marker->position);
        repaint();
        return;
    }
    selectedMarker_ = core::invalidId;
    drag_ = Drag::region;
    zoomStartSpp_ = view_.samplesPerPixel;
    dragAnchor_ = sampleForX(e.x);
    return;
}

// The lanes: select the track, then pick up a clip (move, trim, fade or gain by
// where it was grabbed), start a range or a lasso.
void ArrangeView::mouseDownLanes(const juce::MouseEvent& e)
{
    selectedMarker_ = core::invalidId;

    dragStartSample_ = sampleForX(e.x);
    const auto hit = hitTest(e.getPosition());
    dragStartRow_ = hit.row;
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
            clipMenu(ref, e.getScreenPosition());
        }
        else if (hit.track != nullptr)
            trackMenu(hit.track->id, e.getScreenPosition());
        return;
    }
    if (usesClipTool() && mouseDownWithTool(e, hit))
        return;
    if (hit.midiClip != nullptr)
    {
        const ClipRef ref{hit.track->id, hit.midiClip->id, true};
        drag_ = hit.zone;
        if (e.mods.isShiftDown() || e.mods.isCtrlDown())
            select(ref, true);
        else if (!isSelected(ref))
        {
            selection_.clear();
            select(ref, true);
        }
        dragClip_ = ref;
        dragOriginal_ = core::AudioClip{};
        dragOriginal_.start = hit.midiClip->start;
        dragOriginal_.length = hit.midiClip->length;
        moveDelta_ = 0;
        rowDelta_ = 0;
        trimStart_ = hit.midiClip->start;
        trimLength_ = hit.midiClip->length;
        trimOffset_ = 0;
        repaint();
        return;
    }
    if (tool_ == Tool::pencil && hit.clip == nullptr && hit.track != nullptr && hit.track->isMidi())
    {
        // The pencil: a new clip from the grid line at or before the press.
        drag_ = Drag::draw;
        drawTrack_ = hit.track->id;
        const auto from = std::max<core::SamplePosition>(0, dragStartSample_);
        drawAnchor_ = snapEnabled() ? lineAtOrBefore(from) : from;
        drawStart_ = drawAnchor_;
        drawEnd_ = tempoMap_.move(drawAnchor_, {1, 0.0}, 1);
        repaint();
        return;
    }
    if (hit.clip == nullptr && tool_ == Tool::range)
    {
        // The range tool: a drag in a lane selects a time range.
        drag_ = Drag::timeSelect;
        dragAnchor_ = snapped(dragStartSample_);
        timeSelStart_ = timeSelEnd_ = dragAnchor_;
        return;
    }
    if (hit.clip == nullptr)
    {
        // Empty lane: a drag lassos, a click locates (and clears the selection).
        drag_ = Drag::lasso;
        lasso_ = juce::Rectangle<int>(e.x, e.y, 0, 0);
        dragAnchor_ = dragStartSample_;
        return;
    }

    const ClipRef ref{hit.track->id, hit.clip->id};
    if (e.mods.isCtrlDown() && hit.zone == Drag::move)
    {
        // Ctrl+drag on the body: gain. (Ctrl+click alone toggles the selection.)
        drag_ = Drag::gain;
    }
    else if (hit.zone == Drag::trimRight && e.mods.isAltDown())
    {
        // Alt+drag of the right edge: stretch the clip to the new length.
        drag_ = Drag::stretch;
    }
    else
    {
        drag_ = hit.zone;
    }
    if (e.mods.isShiftDown())
        select(ref, true);
    else if (e.mods.isCtrlDown() && drag_ != Drag::gain)
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
    dragClip_ = ref;
    dragOriginal_ = *hit.clip;
    moveDelta_ = 0;
    rowDelta_ = 0;
    trimStart_ = hit.clip->start;
    trimLength_ = hit.clip->length;
    trimOffset_ = hit.clip->sourceOffset;
    fadePreview_ = drag_ == Drag::fadeOut ? hit.clip->fadeOut : hit.clip->fadeIn;
    fadeCurveStart_ = drag_ == Drag::fadeOut ? hit.clip->fadeOutCurve : hit.clip->fadeInCurve;
    fadeCurvePreview_ = fadeCurveStart_;
    gainPreview_ = hit.clip->gain;
    stretchLength_ = hit.clip->length;
    repaint();
}

void ArrangeView::dragRowResize(const juce::MouseEvent& e)
{
    const int h = std::clamp(
        resizeStartHeight_ + e.y - dragStart_.y, theme_.sizes.trackRowMin, theme_.sizes.trackRowMax);
    if (h != resizeHeight_)
    {
        resizeHeight_ = h;
        rowsValid_ = false;
        layoutHeaders();
        updateScrollbar();
        automation_.repaint();
        repaint();
    }
}

// A drag in the bars row: the loop/punch region, unless the first movement is
// mostly vertical, which turns it into a zoom.
void ArrangeView::dragRegion(const juce::MouseEvent& e, core::SamplePosition at)
{
    if (!draggedFar_)
        return;
    // The first movement decides: mostly vertical zooms, otherwise the region.
    if (std::abs(e.y - dragStart_.y) > std::abs(e.x - dragStart_.x) && std::abs(e.y - dragStart_.y) > 6)
    {
        drag_ = Drag::zoom;
        return;
    }
    regionStart_ = snapped(std::min(dragAnchor_, at));
    regionEnd_ = snapped(std::max(dragAnchor_, at));
}

// The dragged clip's new start snaps; the rest of the selection follows by the
// same delta, and rows by the same count, never before zero, outside the
// tracks or onto a track of the other kind.
void ArrangeView::dragMoveClips(const juce::MouseEvent& e, core::SamplePosition at)
{
    const auto wanted = dragOriginal_.start + (at - dragStartSample_);
    moveDelta_ =
        snapWithTargets(wanted, dragStartRow_ + rowDelta_, true, dragOriginal_.start) - dragOriginal_.start;
    core::SamplePosition earliest = dragOriginal_.start;
    int lowestRow = dragStartRow_, highestRow = dragStartRow_;
    if (project_ != nullptr)
        for (const auto& ref : selection_)
            if (const auto span = spanOf(ref))
            {
                const int t = project_->trackIndex(ref.track);
                earliest = std::min(earliest, span->start);
                lowestRow = std::min(lowestRow, t);
                highestRow = std::max(highestRow, t);
            }
    moveDelta_ = std::max(moveDelta_, -earliest);
    const int row = rowAtY(e.y);
    const int trackCount = project_ != nullptr ? static_cast<int>(project_->tracks.size()) : 0;
    int wantedRows = row >= 0 ? row - dragStartRow_ : rowDelta_;
    wantedRows = std::clamp(wantedRows, -lowestRow, std::max(0, trackCount - 1 - highestRow));
    // A clip only lands on a track of its own kind (MIDI on MIDI, audio on audio).
    bool kindsMatch = project_ != nullptr;
    if (kindsMatch)
        for (const auto& ref : selection_)
        {
            const int from = project_->trackIndex(ref.track);
            if (from < 0)
                continue;
            const auto& target = project_->tracks[static_cast<size_t>(from + wantedRows)];
            if (target.isMidi() != ref.midi)
                kindsMatch = false;
        }
    if (kindsMatch)
        rowDelta_ = wantedRows;
}

void ArrangeView::dragTrimLeft(core::SamplePosition at)
{
    const auto sourceStart = dragClip_.midi
                                 ? core::SamplePosition{0}
                                 : dragOriginal_.start - static_cast<core::SamplePosition>(std::llround(
                                                             static_cast<double>(dragOriginal_.sourceOffset) *
                                                             dragOriginal_.stretch)); // source sample 0
    const auto newStart = std::clamp(snapWithTargets(at, dragStartRow_),
                                     std::max<core::SamplePosition>(0, sourceStart),
                                     dragOriginal_.end() - 1);
    trimStart_ = newStart;
    trimLength_ = dragOriginal_.end() - newStart;
    trimOffset_ =
        dragClip_.midi
            ? dragOriginal_.sourceOffset + (newStart - dragOriginal_.start)
            : std::max<core::SamplePosition>(0, dragOriginal_.sourceAt(newStart - dragOriginal_.start));
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == Drag::none)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    dragPoint_ = e.getPosition();
    const int x = std::clamp(e.x, headerWidth(), getWidth() - scrollbarWidth);
    const auto at = sampleForX(x);
    switch (drag_)
    {
        case Drag::headerResize:
            view_.originX =
                std::clamp(e.x, theme_.sizes.trackHeaderMinWidth, theme_.sizes.trackHeaderMaxWidth);
            resized();
            layoutHeaders();
            break;
        case Drag::rowResize:
            dragRowResize(e);
            break;
        case Drag::takes:
            takes_.mouseDrag(e);
            break;
        case Drag::draw:
            if (draggedFar_)
            {
                const bool grid = snapEnabled();
                const auto cell = [this, grid](core::SamplePosition s, bool up)
                {
                    s = std::max<core::SamplePosition>(0, s);
                    return !grid ? s : up ? lineAtOrAfter(s) : lineAtOrBefore(s);
                };
                // At least one grid step long.
                const auto oneStep =
                    grid ? std::max(drawAnchor_ + 1, tempoMap_.move(drawAnchor_, gridAt(drawAnchor_), 1))
                         : drawAnchor_ + 1;
                if (at >= drawAnchor_)
                {
                    drawStart_ = drawAnchor_;
                    drawEnd_ = std::max(oneStep, cell(at, true));
                }
                else
                {
                    drawStart_ = cell(at, false);
                    drawEnd_ = oneStep;
                }
            }
            break;
        case Drag::pan:
            setScroll(panScroll_ -
                      static_cast<core::SamplePosition>((e.x - dragStart_.x) * view_.samplesPerPixel));
            setVerticalScroll(panVertical_ - (e.y - dragStart_.y));
            break;
        case Drag::zoom:
        {
            // A vertical drag zooms around where it began (Logic style): 80 px per octave.
            const double factor = std::pow(2.0, static_cast<double>(e.y - dragStart_.y) / 80.0);
            setZoom(zoomStartSpp_ * factor, dragStart_.x);
            break;
        }
        case Drag::region:
            dragRegion(e, at);
            break;
        case Drag::loopMove:
        {
            const auto delta = snapped(loopOriginalStart_ + (at - dragAnchor_)) - loopOriginalStart_;
            const auto start = std::max<core::SamplePosition>(0, loopOriginalStart_ + delta);
            regionEnd_ = start + (loopOriginalEnd_ - loopOriginalStart_);
            regionStart_ = start;
            break;
        }
        case Drag::loopStart:
            regionStart_ = std::min(snapped(at), loopOriginalEnd_ - 1);
            break;
        case Drag::loopEnd:
            regionEnd_ = std::max(snapped(at), loopOriginalStart_ + 1);
            break;
        case Drag::timeSelect:
            timeSelStart_ = std::min(dragAnchor_, snapped(at));
            timeSelEnd_ = std::max(dragAnchor_, snapped(at));
            break;
        case Drag::marker:
            markerPreview_ = snapped(dragMarker_.position + (at - dragStartSample_));
            break;
        case Drag::lasso:
            lasso_ = juce::Rectangle<int>::leftTopRightBottom(std::min(dragStart_.x, x),
                                                              std::min(dragStart_.y, e.y),
                                                              std::max(dragStart_.x, x),
                                                              std::max(dragStart_.y, e.y));
            break;
        case Drag::move:
            dragMoveClips(e, at);
            break;
        case Drag::trimLeft:
            dragTrimLeft(at);
            break;
        case Drag::trimRight:
        {
            const auto newEnd = std::max(snapWithTargets(at, dragStartRow_), dragOriginal_.start + 1);
            trimLength_ = newEnd - dragOriginal_.start;
            break;
        }
        case Drag::stretch:
        {
            // Alt is held for the gesture, so the grid snaps here unless it is off.
            auto end = std::max<core::SamplePosition>(at, dragOriginal_.start + 1);
            if (snap_)
                end = tempoMap_.snap(end, gridAt(end));
            // From a twentieth to twenty times the source's own length.
            const double source = static_cast<double>(dragOriginal_.sourceLength());
            stretchLength_ = std::clamp<core::SamplePosition>(
                end - dragOriginal_.start,
                std::max<core::SamplePosition>(1, std::llround(source * 0.05)),
                std::llround(source * 20.0));
            break;
        }
        case Drag::fadeIn:
            fadePreview_ =
                std::clamp<core::SamplePosition>(at - dragOriginal_.start, 0, dragOriginal_.length);
            fadeCurvePreview_ = fadeCurveFor(e.y - dragStart_.y);
            break;
        case Drag::fadeOut:
            fadePreview_ =
                std::clamp<core::SamplePosition>(dragOriginal_.end() - at, 0, dragOriginal_.length);
            fadeCurvePreview_ = fadeCurveFor(e.y - dragStart_.y);
            break;
        case Drag::erase:
        case Drag::mute:
            if (const auto ref = clipAt(hitTest(e.getPosition())); ref.clip != core::invalidId)
                sweepOver(ref);
            break;
        case Drag::gain:
        {
            // A quarter of a dB per pixel, up 24 dB at most.
            const float db = static_cast<float>(dragStart_.y - e.y) * 0.25f;
            const float base = static_cast<float>(db::fromGain(dragOriginal_.gain));
            gainPreview_ = db::toGain(std::clamp(base + db, -60.0f, 24.0f));
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void ArrangeView::mouseUp(const juce::MouseEvent& e)
{
    const auto drag = drag_;
    drag_ = Drag::none;
    const auto before = std::exchange(pressSelection_, {});
    switch (drag)
    {
        case Drag::takes:
            takes_.mouseUp(e);
            break;
        case Drag::draw:
        {
            const auto track = std::exchange(drawTrack_, core::invalidId);
            addMidiClip(track, drawStart_, drawEnd_);
            break;
        }
        case Drag::rowResize:
        {
            const auto channel = resizeChannel_;
            resizeChannel_ = core::invalidId;
            rowsValid_ = false;
            if (resizeHeight_ != resizeStartHeight_ && project_ != nullptr &&
                project_->findChannel(channel) != nullptr)
            {
                std::vector<std::unique_ptr<core::Command>> commands;
                commands.push_back(std::make_unique<core::SetChannelHeight>(channel, resizeHeight_));
                emit(std::move(commands), "Resize track");
            }
            layoutHeaders();
            updateScrollbar();
            break;
        }
        case Drag::headerResize:
        case Drag::zoom:
        case Drag::pan:
            break;
        case Drag::region:
            if (draggedFar_)
            {
                if (onRegionChanged)
                    onRegionChanged(regionStart_, regionEnd_);
            }
            else if (onLocate)
            {
                onLocate(dragAnchor_);
            }
            break;
        case Drag::loopMove:
        case Drag::loopStart:
        case Drag::loopEnd:
            if (onRegionChanged)
                onRegionChanged(regionStart_, regionEnd_);
            break;
        case Drag::timeSelect:
            finishTimeSelection();
            break;
        case Drag::lasso:
            if (draggedFar_)
            {
                finishLasso();
            }
            else
            {
                if (!e.mods.isShiftDown())
                    selection_.clear();
                if (onLocate)
                    onLocate(dragAnchor_);
            }
            break;
        case Drag::move:
            if (draggedFar_)
                finishMove();
            else if (e.mods.isCtrlDown())
            {
                // Ctrl+click: toggle membership.
                std::erase(selection_, dragClip_);
            }
            else if (!e.mods.isShiftDown())
            {
                selection_.clear();
                select(dragClip_, true);
            }
            break;
        case Drag::trimLeft:
        case Drag::trimRight:
            if (draggedFar_)
                finishTrim();
            break;
        case Drag::stretch:
            if (draggedFar_)
                finishStretch();
            break;
        case Drag::fadeIn:
        case Drag::fadeOut:
            if (draggedFar_)
                finishFade(drag);
            break;
        case Drag::gain:
            if (draggedFar_)
                finishGain();
            else
                std::erase(selection_, dragClip_); // Ctrl+click without a drag toggles
            break;
        case Drag::marker:
            if (draggedFar_)
                finishMarkerDrag();
            break;
        case Drag::erase:
        case Drag::mute:
            finishSweep(drag == Drag::erase);
            break;
        case Drag::none:
            break;
    }
    if (selection_ != before)
    {
        // A clip picked on another track moves the track selection with it.
        if (!selection_.empty())
            setSelectedTrack(selection_.back().track);
        notifySelection();
    }
    repaint();
}

void ArrangeView::mouseMove(const juce::MouseEvent& e)
{
    if (drag_ != Drag::none)
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
    if (tool_ == Tool::split && drag_ == Drag::none && pointer_.x >= 0 &&
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
        const auto* marker = markerAtRulerPoint(p);
        const auto hoverId = marker != nullptr ? marker->id : core::invalidId;
        if (hoverId != hoverMarker_)
        {
            hoverMarker_ = hoverId;
            repaint(0, 0, getWidth(), rulerHeight);
        }
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
    if (hoverRow_ == -1 && hoverClip_ == ClipRef{} && hoverMarker_ == core::invalidId)
        return;
    hoverRow_ = -1;
    hoverClip_ = ClipRef{};
    hoverMarker_ = core::invalidId;
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

void ArrangeView::drawGrid(juce::Graphics& g, juce::Rectangle<int> area, bool ruler)
{
    // Bar lines, beats and sub-beats (ui::forEachGridLine's thresholds); in the
    // ruler the ticks are short and the bars are numbered.
    if (project_ == nullptr)
        return;
    const auto columns = area.withLeft(std::max(headerWidth(), area.getX()))
                             .withRight(std::min(getWidth() - scrollbarWidth, area.getRight()));
    if (ruler)
        paintBarsRuler(g, theme_, tempoMap_, view_, columns);
    else
        paintGridLines(g, theme_, tempoMap_, view_, columns);
}

void ArrangeView::drawRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& c = theme_.colours;
    g.setColour(c.surface0);
    g.fillRect(area);
    const auto markersRow = area.removeFromTop(markersRowHeight);
    const auto barsRow = area.withTrimmedBottom(1);
    g.saveState();
    g.reduceClipRegion(headerWidth(), 0, getWidth() - headerWidth() - scrollbarWidth, rulerHeight);
    drawGrid(g, barsRow, true);

    // The loop band in the bars row (or a faint band for a region that is neither
    // loop nor punch); the punch band in the markers row's lower half.
    if (const auto band = loopBand(); !band.isEmpty())
    {
        if (loop_ || !punch_)
        {
            g.setColour(loop_ ? c.loopFill : c.textSecondary.withAlpha(0.12f));
            g.fillRect(band);
            g.setColour(loop_ ? c.loopEdge : c.textSecondary.withAlpha(0.5f));
            g.fillRect(band.getX(), band.getY(), 2, band.getHeight());
            g.fillRect(band.getRight() - 2, band.getY(), 2, band.getHeight());
        }
        if (punch_)
        {
            const auto punch = band.withY(markersRow.getCentreY()).withHeight(markersRow.getHeight() / 2);
            g.setColour(c.punchFill);
            g.fillRect(punch);
            g.setColour(c.punchEdge);
            g.fillRect(punch.getX(), punch.getY(), 2, punch.getHeight());
            g.fillRect(punch.getRight() - 2, punch.getY(), 2, punch.getHeight());
        }
    }
    // The time selection in the bars row.
    if (timeSelEnd_ > timeSelStart_)
    {
        const int x0 = xForSample(timeSelStart_), x1 = xForSample(timeSelEnd_);
        g.setColour(c.selectionFill);
        g.fillRect(x0, barsRow.getY(), std::max(1, x1 - x0), barsRow.getHeight());
        g.setColour(c.selectionOutline);
        g.fillRect(x0, barsRow.getY(), 1, barsRow.getHeight());
        g.fillRect(x1, barsRow.getY(), 1, barsRow.getHeight());
    }
    drawMarkers(g, markersRow);
    g.restoreState();
    g.setColour(c.lineSubtle);
    g.fillRect(0, rulerOnly - 1, getWidth(), 1);
    g.fillRect(headerWidth() - 1, 0, 1, rulerOnly);
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

void ArrangeView::drawClip(juce::Graphics& g,
                           const core::Track& track,
                           const core::AudioClip& model,
                           juce::Rectangle<int> lane)
{
    const auto& c = theme_.colours;
    // What is drawn: the model clip, or its preview while it is being edited (a
    // copy only then).
    const bool editing = drag_ != Drag::none && dragClip_.track == track.id && dragClip_.clip == model.id;
    if (!editing && drag_ == Drag::none)
    {
        const int left = xForSample(model.start), right = std::max(left + 2, xForSample(model.end()));
        if (right < paintArea_.getX() || left > paintArea_.getRight())
            return; // not in the area being painted
    }
    core::AudioClip preview;
    if (editing)
    {
        preview = model;
        auto& clip = preview;
        if (drag_ == Drag::trimLeft || drag_ == Drag::trimRight)
        {
            clip.start = trimStart_;
            clip.length = trimLength_;
            clip.sourceOffset = trimOffset_;
        }
        else if (drag_ == Drag::fadeIn)
        {
            clip.fadeIn = fadePreview_;
            clip.fadeInCurve = fadeCurvePreview_;
        }
        else if (drag_ == Drag::fadeOut)
        {
            clip.fadeOut = fadePreview_;
            clip.fadeOutCurve = fadeCurvePreview_;
        }
        else if (drag_ == Drag::gain)
            clip.gain = gainPreview_;
        else if (drag_ == Drag::stretch)
        {
            clip.stretch = model.stretch * static_cast<double>(stretchLength_) /
                           static_cast<double>(std::max<core::SamplePosition>(1, model.length));
            clip.length = stretchLength_;
        }
    }
    const core::AudioClip& clip = editing ? preview : model;
    paintstats::counters().clipsDrawn++;
    const ClipRef ref{track.id, model.id};
    const bool selected = isSelected(track.id, model.id);
    const bool hovered = hoverClip_ == ref;
    const bool moving = drag_ == Drag::move && selected && draggedFar_;
    const bool missing = mediaMissing(clip.mediaPath);

    const int x0 = xForSample(clip.start);
    const int x1 = std::max(x0 + 2, xForSample(clip.end()));
    if (x1 < headerWidth() || x0 > getWidth())
        return;

    const auto block = spanBounds(clip.start, clip.length, project_->trackIndex(track.id));
    const auto visible =
        block.getIntersection(lane.withLeft(headerWidth()).withRight(getWidth() - scrollbarWidth));
    if (visible.isEmpty())
        return;

    const auto colour = trackColour(track, project_->trackIndex(track.id));
    const float dim = moving || (drag_ == Drag::erase && swept(ref)) ? 0.4f : 1.0f;
    const bool muted = track.mute || shownMuted(ref, model.muted);
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? theme_.sizes.clipHeader : 0;
    const auto body = block.withTrimmedTop(headerH);

    // Body and header.
    g.setColour(
        missing ? c.surface2
                : colour.withAlpha(
                      (selected ? theme_.track.clipBodySelectedAlpha : theme_.track.clipBodyAlpha) * dim));
    g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);
    if (showHeader)
        fillClipHeader(
            g, block, headerH, (missing ? c.warning : colour).withAlpha(theme_.track.clipHeaderAlpha * dim));
    if (muted && !missing)
        hatch(g, body, visible, colour, dim);

    // Waveform: one min/max column per pixel from the peak cache (decoded samples
    // when zoomed past the finest peak level), scaled by the
    // clip gain so a gain change is visible; a darker centre line under it.
    if (missing)
    {
        g.setColour(c.warning);
        g.setFont(theme_.caption());
        g.drawText("Missing: " + juce::File(clip.mediaPath).getFileName(),
                   body.getIntersection(visible).reduced(theme_.spacing.xs + 2, 0),
                   juce::Justification::centredLeft,
                   true);
    }
    else if (const auto wave = body.getIntersection(visible).getIntersection(paintArea_); wave.isEmpty())
    {
        // No waveform column in the area being painted.
    }
    else if (const auto view = waveformIn(clip, wave); !view.empty())
    {
        // One look into the peak store per clip (the view holds the peaks).
        const int channels = std::min(2, view.numChannels());
        const float laneHeight = static_cast<float>(wave.getHeight()) / static_cast<float>(channels);
        const float scale = std::min(1.0f, clip.gain);
        const auto waveColour =
            colour.withAlpha((muted ? theme_.track.waveformMutedAlpha : theme_.track.waveformAlpha) * dim);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float midY = static_cast<float>(wave.getY()) + laneHeight * (static_cast<float>(ch) + 0.5f);
            g.setColour(colour.withAlpha(0.3f * dim));
            g.fillRect(static_cast<float>(wave.getX()), midY, static_cast<float>(wave.getWidth()), 1.0f);
            g.setColour(waveColour);
            const float half = laneHeight * 0.48f * scale;
            // The fades scale the waveform as they scale the sound (core::fadeShape),
            // so a faded edge reads as quiet.
            const float inK = core::fadeShapeK(clip.fadeInCurve), outK = core::fadeShapeK(clip.fadeOutCurve);
            const auto fadeGain = [&](core::SamplePosition pos)
            {
                float f = 1.0f;
                if (clip.fadeIn > 0 && pos < clip.fadeIn)
                    f = core::fadeShape(static_cast<float>(pos) / static_cast<float>(clip.fadeIn), inK);
                if (clip.fadeOut > 0 && pos > clip.length - clip.fadeOut)
                    f *= core::fadeShape(
                        static_cast<float>(clip.length - pos) / static_cast<float>(clip.fadeOut), outK);
                return f;
            };
            for (int x = wave.getX(); x < wave.getRight(); ++x)
            {
                paintstats::counters().waveColumns++;
                const auto from = clip.sourceAt(sampleForX(x) - clip.start);
                const auto to = clip.sourceAt(sampleForX(x + 1) - clip.start);
                const auto r = view.lookup(ch, from, std::max(to, from + 1));
                const float faded = half * fadeGain(sampleForX(x) - clip.start);
                const float top = midY - r.maximum * faded;
                const float bottom = midY - r.minimum * faded;
                g.fillRect(static_cast<float>(x),
                           std::min(top, bottom),
                           1.0f,
                           std::max(1.0f, std::abs(bottom - top)));
            }
        }
    }

    // Fades: the body dimmed under a textPrimary curve; handles on hover or selection.
    // The curve runs from the clip's edge (silent) to full level, shaped as it plays
    // (core::fadeShape); the part of the body above it is dimmed.
    const auto fadeWedge = [&](core::SamplePosition fade, float curve, bool in)
    {
        if (fade <= 0)
            return;
        const float w = static_cast<float>(fade) / static_cast<float>(view_.samplesPerPixel);
        const float top = static_cast<float>(body.getY());
        const float bottom = static_cast<float>(body.getBottom());
        const float edge = in ? static_cast<float>(x0) : static_cast<float>(x1);
        const float inner = in ? edge + w : edge - w;
        const float k = core::fadeShapeK(curve);
        const int steps = std::clamp(static_cast<int>(w / 3.0f), 1, 32); // straight when linear or tiny
        juce::Path line;
        for (int i = 0; i <= (k == 1.0f ? 1 : steps); ++i)
        {
            const float t =
                k == 1.0f ? static_cast<float>(i) : static_cast<float>(i) / static_cast<float>(steps);
            const juce::Point<float> point(edge + (inner - edge) * t,
                                           bottom - core::fadeShape(t, k) * (bottom - top));
            if (i == 0)
                line.startNewSubPath(point);
            else
                line.lineTo(point);
        }
        juce::Path wedge(line);
        wedge.lineTo(inner, top);
        wedge.lineTo(edge, top);
        wedge.closeSubPath();
        g.setColour(c.canvas.withAlpha(0.45f));
        g.fillPath(wedge);
        g.setColour(c.textPrimary.withAlpha(0.85f * dim));
        g.strokePath(line, juce::PathStrokeType(1.0f));
    };
    fadeWedge(clip.fadeIn, clip.fadeInCurve, true);
    fadeWedge(clip.fadeOut, clip.fadeOutCurve, false);
    if ((hovered || selected) && block.getWidth() > 2 * fadeHandle + 4)
    {
        g.setColour(c.textPrimary.withAlpha(0.9f));
        g.fillRect(x0 + 1, block.getY() + 1, fadeHandle, fadeHandle);
        g.fillRect(x1 - fadeHandle - 1, block.getY() + 1, fadeHandle, fadeHandle);
    }

    // Outline: 40% at rest, 70% hovered, full when selected; warning when missing.
    g.setColour(missing ? c.warning : colour.withAlpha(selected ? 1.0f : hovered ? 0.7f : 0.4f));
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);

    // The name in the header (or over the body when the header is hidden).
    const auto textArea = (showHeader ? block.withHeight(headerH) : block)
                              .getIntersection(visible)
                              .withTrimmedLeft(fadeHandle + 2 > 6 ? fadeHandle + 2 : 6)
                              .withTrimmedRight(6);
    if ((showHeader || block.getHeight() >= 14) && textArea.intersects(paintArea_))
    {
        g.setColour(c.textPrimary.withAlpha(dim));
        g.setFont(theme_.caption());
        juce::String label = juce::String::fromUTF8(clip.name.c_str());
        if (std::abs(clip.gain - 1.0f) > 1.0e-3f)
            label << "  " << db::gainText(clip.gain) << " dB";
        // Stretch as the playing speed, transposition in semitones, a note for a
        // clip that follows the tempo, and the render's progress.
        if (clip.stretch != 1.0)
            label << "  " << juce::String(100.0 / clip.stretch, 1) << "%";
        if (clip.transpose != 0.0)
            label << "  " << (clip.transpose > 0.0 ? "+" : "") << juce::String(clip.transpose, 1) << " st";
        if (clip.followTempo)
            label << "  " << juce::String::fromUTF8("\xe2\x99\xa9");
        if (clip.isStretched() && !editing && stretchProgress)
            if (const double done = stretchProgress(model); done < 1.0)
            {
                label << "  stretching " << juce::String(static_cast<int>(done * 100.0)) << "%";
                stretchPending_ = true;
            }
        g.drawText(label, textArea, juce::Justification::centredLeft, true);
    }
}
engine::WaveformView ArrangeView::waveformIn(const core::AudioClip& clip, juce::Rectangle<int> wave)
{
    const auto first = clip.sourceAt(sampleForX(wave.getX()) - clip.start);
    const auto last = clip.sourceAt(sampleForX(wave.getRight()) - clip.start);
    return peaks_.viewFor(clip.mediaPath,
                          sampleRate(),
                          first,
                          last + 1,
                          static_cast<double>(last - first) / std::max(1, wave.getWidth()));
}

void ArrangeView::drawFolderLane(juce::Graphics& g, const core::Track& folder, juce::Rectangle<int> lane)
{
    // What the folder holds, at a glance: each track's clips as thin bars in its
    // colour, one row of bars per track (at most eight rows), under a caption.
    const auto& c = theme_.colours;
    std::vector<int> members;
    for (int t = 0; t < static_cast<int>(project_->tracks.size()); ++t)
        if (const auto& track = project_->tracks[static_cast<size_t>(t)];
            &track != &folder && project_->insideFolder(track.id, folder.id))
            members.push_back(t);
    g.setColour(c.textTertiary);
    g.setFont(theme_.caption());
    g.drawText(juce::String(static_cast<int>(members.size())) + (members.size() == 1 ? " track" : " tracks") +
                   (folder.folderOpen ? juce::String() : juce::String("  (closed)")),
               lane.withTrimmedLeft(theme_.spacing.sm).withHeight(std::min(lane.getHeight(), 18)),
               juce::Justification::centredLeft);
    const auto bars = lane.withTrimmedTop(std::min(lane.getHeight() / 2, 18)).reduced(0, 3);
    const int shown = std::min(8, static_cast<int>(members.size()));
    if (shown == 0 || bars.getHeight() < 4)
        return;
    const float rowH = static_cast<float>(bars.getHeight()) / static_cast<float>(shown);
    for (int i = 0; i < shown; ++i)
    {
        const auto& track = project_->tracks[static_cast<size_t>(members[static_cast<size_t>(i)])];
        g.setColour(trackColour(track, members[static_cast<size_t>(i)]).withAlpha(track.mute ? 0.3f : 0.75f));
        const float y = static_cast<float>(bars.getY()) + rowH * static_cast<float>(i);
        const float barH = std::max(1.0f, rowH - 1.0f);
        const auto bar = [&](core::SamplePosition start, core::SamplePosition length)
        {
            const int x0 = std::max(lane.getX(), xForSample(start));
            const int x1 = std::min(lane.getRight(), xForSample(start + length));
            if (x1 > x0)
                g.fillRoundedRectangle(static_cast<float>(x0), y, static_cast<float>(x1 - x0), barH, 1.0f);
        };
        for (const auto& clip : track.clips)
            bar(clip.start, clip.length);
        for (const auto& clip : track.midiClips)
            bar(clip.start, clip.length);
    }
}

void ArrangeView::drawMidiClip(juce::Graphics& g,
                               const core::Track& track,
                               const core::MidiClip& model,
                               juce::Rectangle<int> lane,
                               bool recording)
{
    const auto& c = theme_.colours;
    const ClipRef ref{track.id, model.id, true};
    // Where it is drawn: the model's span, or the trim preview while it is edited.
    struct
    {
        core::SamplePosition start, length;
        const std::string& name;
        core::SamplePosition end() const noexcept { return start + length; }
    } clip{model.start, model.length, model.name};
    const bool editing = drag_ != Drag::none && dragClip_ == ref;
    if (editing && (drag_ == Drag::trimLeft || drag_ == Drag::trimRight))
    {
        clip.start = trimStart_;
        clip.length = trimLength_;
    }
    if (drag_ == Drag::none)
    {
        const int left = xForSample(clip.start), right = std::max(left + 2, xForSample(clip.end()));
        if (right < paintArea_.getX() || left > paintArea_.getRight())
            return; // not in the area being painted
    }
    paintstats::counters().clipsDrawn++;
    const bool selected = isSelected(ref);
    const bool hovered = hoverClip_ == ref;
    const bool moving = drag_ == Drag::move && selected && draggedFar_;
    const int x0 = xForSample(clip.start);
    const int x1 = std::max(x0 + 2, xForSample(clip.end()));
    if (x1 < headerWidth() || x0 > getWidth())
        return;
    const int row = project_->trackIndex(track.id);
    const auto block = spanBounds(clip.start, clip.length, row);
    const auto visible =
        block.getIntersection(lane.withLeft(headerWidth()).withRight(getWidth() - scrollbarWidth));
    if (visible.isEmpty())
        return;
    const auto colour = recording ? c.record : trackColour(track, row);
    const float dim = moving || (drag_ == Drag::erase && swept(ref)) ? 0.4f : 1.0f;
    const bool muted = track.mute || shownMuted(ref, model.muted);
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? theme_.sizes.clipHeader : 0;
    g.setColour(
        colour.withAlpha((selected ? theme_.track.clipBodySelectedAlpha : theme_.track.clipBodyAlpha) * dim));
    g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);
    if (showHeader)
        fillClipHeader(g, block, headerH, colour.withAlpha(theme_.track.clipHeaderAlpha * dim));
    if (muted)
        hatch(g, block.withTrimmedTop(headerH), visible, colour, dim);

    // Notes: a bar per note, pitch on a range that fits the clip's notes.
    if (!model.notes.empty())
    {
        int low = 127, high = 0;
        for (const auto& n : model.notes)
        {
            low = std::min(low, n.note);
            high = std::max(high, n.note);
        }
        low = std::max(0, low - 2);
        high = std::min(127, high + 2);
        const auto inner = block.withTrimmedTop(headerH).reduced(1, 2);
        const float rowH = static_cast<float>(inner.getHeight()) / static_cast<float>(high - low + 1);
        g.setColour((muted ? c.textDisabled : c.midiNote).withAlpha(dim));
        for (const auto& n : model.notes)
        {
            // Only the part inside the clip shows (trimmed clips keep their notes).
            const auto start = model.start + n.start;
            const auto nx0 = xForSample(std::max(start, clip.start));
            const auto nx1 = xForSample(std::min(start + n.length, clip.end()));
            if (nx1 <= nx0 || start >= clip.end() || start + n.length <= clip.start ||
                nx1 < paintArea_.getX() || nx0 > paintArea_.getRight())
                continue;
            const float y = static_cast<float>(inner.getY()) + static_cast<float>(high - n.note) * rowH;
            g.fillRect(static_cast<float>(nx0),
                       y,
                       static_cast<float>(std::max(1, nx1 - nx0)),
                       std::max(1.0f, rowH - 1.0f));
        }
    }
    g.setColour(colour.withAlpha(selected ? 1.0f : hovered || recording ? 0.7f : 0.4f));
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    const auto textArea =
        (showHeader ? block.withHeight(headerH) : block).getIntersection(visible).reduced(6, 0);
    if ((showHeader || block.getHeight() >= 14) && textArea.intersects(paintArea_))
    {
        g.setColour(c.textPrimary.withAlpha(dim));
        g.setFont(theme_.caption());
        const auto name = recording ? juce::String::fromUTF8("\xe2\x97\x8f Recording")
                                    : juce::String::fromUTF8(clip.name.c_str());
        g.drawText(name + "  " + juce::String(model.notes.size()) + " notes",
                   textArea,
                   juce::Justification::centredLeft,
                   true);
    }
}

void ArrangeView::fillClipHeader(juce::Graphics& g,
                                 juce::Rectangle<int> block,
                                 int headerH,
                                 juce::Colour colour)
{
    g.setColour(colour);
    juce::Path header;
    header.addRoundedRectangle(static_cast<float>(block.getX()),
                               static_cast<float>(block.getY()),
                               static_cast<float>(block.getWidth()),
                               static_cast<float>(headerH),
                               theme_.radius.sm,
                               theme_.radius.sm,
                               true,
                               true,
                               false,
                               false);
    g.fillPath(header);
}

// A take being recorded (screens/arrange.md "Recording clip"): a clip in `record` at
// the body alpha, growing with the file, its waveform from the live peaks and
// the header "● Recording". It sits where its clip will land (latency-compensated),
// so nothing jumps when the file closes and the clip takes its place.
void ArrangeView::drawLiveTake(juce::Graphics& g,
                               const core::Track& track,
                               const engine::LiveTake& take,
                               juce::Rectangle<int> lane)
{
    const auto& c = theme_.colours;
    if (take.samplesWritten <= 0 || take.startPosition < 0)
        return;
    const auto placement = engine::placeRecording(take.startPosition, take.samplesWritten, live_.latency);
    if (placement.length <= 0)
        return;
    const int row = project_->trackIndex(track.id);
    const auto block = spanBounds(placement.start, placement.length, row);
    const auto visible =
        block.getIntersection(lane.withLeft(headerWidth()).withRight(getWidth() - scrollbarWidth));
    if (visible.isEmpty())
        return;
    const auto colour = c.record;
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? theme_.sizes.clipHeader : 0;
    const auto body = block.withTrimmedTop(headerH);
    g.setColour(colour.withAlpha(theme_.track.clipBodyAlpha));
    g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);
    if (showHeader)
        fillClipHeader(g, block, headerH, colour.withAlpha(theme_.track.clipHeaderAlpha));

    // Waveform: one min/max column per pixel from the peaks written so far; the
    // unwritten tail (at most a bin and the writer's lag) reads as silence.
    const auto wave = body.getIntersection(visible);
    const float midY = static_cast<float>(wave.getY()) + static_cast<float>(wave.getHeight()) * 0.5f;
    g.setColour(colour.withAlpha(0.3f));
    g.fillRect(static_cast<float>(wave.getX()), midY, static_cast<float>(wave.getWidth()), 1.0f);
    g.setColour(colour.withAlpha(theme_.track.waveformAlpha));
    const float half = static_cast<float>(wave.getHeight()) * 0.48f;
    const auto painted = wave.getIntersection(paintArea_);
    for (int x = painted.getX(); x < painted.getRight(); ++x)
    {
        const auto from = placement.sourceOffset + (sampleForX(x) - placement.start);
        const auto to = placement.sourceOffset + (sampleForX(x + 1) - placement.start);
        float minimum = 0.0f, maximum = 0.0f;
        take.lookup(from, std::max(to, from + 1), minimum, maximum);
        const float top = midY - maximum * half;
        const float bottom = midY - minimum * half;
        g.fillRect(
            static_cast<float>(x), std::min(top, bottom), 1.0f, std::max(1.0f, std::abs(bottom - top)));
    }

    g.setColour(colour.withAlpha(0.7f));
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    if (showHeader || block.getHeight() >= 14)
    {
        g.setColour(c.textPrimary);
        g.setFont(theme_.caption());
        juce::String label = juce::String::fromUTF8("\xe2\x97\x8f Recording");
        if (take.takeIndex > 0)
            label << "  take " << (take.takeIndex + 1);
        g.drawText(label,
                   (showHeader ? block.withHeight(headerH) : block).getIntersection(visible).reduced(6, 0),
                   juce::Justification::centredLeft,
                   true);
    }
}
void ArrangeView::paint(juce::Graphics& g)
{
    const FrameStats::Scope timing;
    const auto& c = theme_.colours;
    g.fillAll(c.canvas);
    paintArea_ = g.getClipBounds();
    paintstats::counters().arrangePaints++;
    const SelectionIndex index(*this); // every clip drawn asks whether it is selected

    const auto lanesArea = juce::Rectangle<int>(
        headerWidth(), rulerHeight, getWidth() - headerWidth() - scrollbarWidth, lanesHeight());
    int y = rulerHeight - verticalScroll_;
    if (project_ != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(lanesArea);
        // Lane backgrounds, then the grid under the clips.
        int row = 0;
        for (const auto& track : project_->tracks)
        {
            const int h = rowHeightOf(row);
            y = rowTop(row);
            const juce::Rectangle<int> lane(headerWidth(), y, lanesArea.getWidth(), h);
            if (h > 0 && lane.intersects(paintArea_))
            {
                if (track.id == selectedTrack_)
                    g.setColour(c.laneSelected);
                else if (row == hoverRow_)
                    g.setColour(c.laneHover);
                else
                    g.setColour(row % 2 == 1 ? c.laneAlt : c.canvas);
                g.fillRect(lane);
                if (track.armed)
                {
                    g.setColour(c.record.withAlpha(recording_ ? 0.12f : 0.05f));
                    g.fillRect(lane);
                }
                g.setColour(c.lineSubtle);
                g.fillRect(lane.withTrimmedTop(h - 1));
            }
            y += h;
            ++row;
        }
        // Buses and the master: a lane with no clips, saying who feeds it.
        for (const auto* fx : busRows())
        {
            const int h = rowHeightOf(row);
            y = rowTop(row);
            const juce::Rectangle<int> lane(headerWidth(), y, lanesArea.getWidth(), h);
            const auto caption = lane.reduced(theme_.spacing.sm, 0).withHeight(std::min(h, 24));
            if (lane.intersects(paintArea_))
            {
                g.setColour(fx->id == selectedTrack_ ? c.laneSelected
                                                     : (row % 2 == 1 ? c.laneAlt : c.canvas));
                g.fillRect(lane);
                g.setColour(c.lineSubtle);
                g.fillRect(lane.withTrimmedTop(h - 1));
            }
            if (caption.intersects(paintArea_))
            {
                // Who feeds it: sends into an FX track, outputs into a group bus.
                const auto* bus = project_->findBus(fx->id);
                int feeders = 0;
                const auto counts = [&](const core::Channel& ch)
                {
                    if (bus != nullptr && bus->fx)
                        for (const auto& send : ch.sends)
                            feeders += send.bus == fx->id ? 1 : 0;
                    else if (bus != nullptr)
                        feeders += ch.outputBus == fx->id ? 1 : 0;
                };
                for (const auto& t : project_->tracks)
                    counts(t);
                for (const auto& b : project_->buses)
                    counts(b);
                const auto channels = [&](const char* verb)
                { return juce::String(feeders) + (feeders == 1 ? " channel " : " channels ") + verb; };
                const juce::String text =
                    bus == nullptr ? juce::String("Master: the whole mix, after every track and bus")
                    : bus->fx
                        ? (feeders == 0 ? juce::String("FX track: no sends yet (a track's menu, Send to)")
                                        : "FX track: " + channels(feeders == 1 ? "sends here" : "send here"))
                        : (feeders == 0 ? juce::String("Bus: nothing routes here yet (a track's output)")
                                        : "Bus: " + channels(feeders == 1 ? "routes here" : "route here"));
                g.setColour(c.textTertiary);
                g.setFont(theme_.caption());
                g.drawText(text, caption, juce::Justification::centredLeft);
            }
            y += h;
            ++row;
        }
        automation_.paintBackgrounds(g); // the lanes' rows, under the grid
        drawGrid(g, lanesArea.getIntersection(paintArea_), false);

        // Loop and punch through the lanes at half the ruler's alpha; the time selection.
        if (regionEnd_ > regionStart_ && (loop_ || punch_))
        {
            const int x0 = xForSample(regionStart_), x1 = xForSample(regionEnd_);
            g.setColour((punch_ ? c.punchFill : c.loopFill).withMultipliedAlpha(0.5f));
            g.fillRect(x0, lanesArea.getY(), std::max(1, x1 - x0), lanesArea.getHeight());
        }
        if (timeSelEnd_ > timeSelStart_)
        {
            const int x0 = xForSample(timeSelStart_), x1 = xForSample(timeSelEnd_);
            g.setColour(c.selectionFill);
            for (size_t t = 0; t < project_->tracks.size(); ++t)
                if (std::find(timeSelTracks_.begin(), timeSelTracks_.end(), project_->tracks[t].id) !=
                    timeSelTracks_.end())
                    g.fillRect(rowBounds(static_cast<int>(t)).withLeft(x0).withRight(x1));
        }

        y = rulerHeight - verticalScroll_;
        row = 0;
        for (const auto& track : project_->tracks)
        {
            y = rowTop(row);
            const int h = rowHeightOf(row++);
            const juce::Rectangle<int> lane(headerWidth(), y, lanesArea.getWidth(), h);
            const bool shown = h > 0 && lane.intersects(paintArea_);
            if (shown && track.isFolder())
                drawFolderLane(g, track, lane);
            if (shown)
            {
                for (const auto& clip : track.clips)
                    drawClip(g, track, clip, lane);
                for (const auto& clip : track.midiClips)
                    drawMidiClip(g, track, clip, lane);
                for (const auto& take : live_.takes)
                    if (take.trackId == track.id)
                        drawLiveTake(g, track, take, lane);
                for (const auto& take : live_.midi)
                    if (take.trackId == track.id)
                        drawMidiClip(g, track, take.clip, lane, true);
                if (track.isFrozen())
                {
                    // Frozen: the clips are what was rendered, shown under a veil.
                    g.setColour(c.canvas.withAlpha(0.45f));
                    g.fillRect(lane);
                    g.setColour(c.info);
                    g.setFont(theme_.captionCaps());
                    g.drawText(juce::String::fromUTF8("\xe2\x9d\x84 FROZEN"),
                               lane.withTrimmedLeft(theme_.spacing.sm).withHeight(std::min(h, 20)),
                               juce::Justification::centredLeft);
                }
                if (track.isAux() && lane.withTrimmedLeft(theme_.spacing.sm).intersects(paintArea_))
                {
                    const auto* source = project_->findTrack(track.auxTrack);
                    g.setColour(c.textTertiary);
                    g.setFont(theme_.caption());
                    g.drawText("Output " + juce::String(2 * track.auxOutput + 1) + "/" +
                                   juce::String(2 * track.auxOutput + 2) + " of " +
                                   (source != nullptr ? juce::String::fromUTF8(source->name.c_str())
                                                      : juce::String("a removed instrument")),
                               lane.withTrimmedLeft(theme_.spacing.sm),
                               juce::Justification::centredLeft);
                }
                if (track.isInstrument() && lane.withTrimmedLeft(theme_.spacing.sm).intersects(paintArea_))
                {
                    // An instrument channel: what it is and who plays it.
                    juce::String text = "Instrument channel";
                    if (track.hasInstrument())
                        text << ": "
                             << juce::String::fromUTF8(displayName_
                                                           ? displayName_(track.instrument.type).c_str()
                                                           : track.instrument.type.c_str());
                    juce::String players;
                    for (const auto* feed : project_->midiFeeds(track.id))
                        players << (players.isEmpty() ? "" : ", ")
                                << juce::String::fromUTF8(feed->name.c_str());
                    text << juce::String::fromUTF8("  \xe2\x86\x90  ")
                         << (players.isNotEmpty() ? players : juce::String("no MIDI track plays it yet"));
                    g.setColour(c.textTertiary);
                    g.setFont(theme_.caption());
                    g.drawText(
                        text, lane.withTrimmedLeft(theme_.spacing.sm), juce::Justification::centredLeft);
                }
            }
            // The comp's badges, chip and boundaries over the row; its take lanes under it.
            if (!track.takeGroups.empty())
            {
                const auto takeArea = takeRow(row - 1);
                takes_.paintMain(g, takeArea);
                takes_.paintLanes(g, takeArea);
            }
            y += h;
        }
        g.restoreState();

        // The take lanes' headers, in the header column under each expanded track's header.
        g.saveState();
        g.reduceClipRegion(0, rulerHeight, headerWidth(), lanesHeight());
        for (size_t t = 0; t < project_->tracks.size(); ++t)
            if (takes_.laneCount(project_->tracks[t]) > 0)
                takes_.paintLaneHeaders(g, takeRow(static_cast<int>(t)));
        g.restoreState();
    }

    if (project_ == nullptr || project_->tracks.empty())
    {
        // The empty state: the mark, "Start a project", the buttons (components) and a hint.
        const auto area = getLocalBounds().withTrimmedTop(rulerHeight);
        const int markSize = theme_.spacing.xxxl;
        const auto mark = juce::Rectangle<float>(static_cast<float>(markSize), static_cast<float>(markSize))
                              .withCentre({static_cast<float>(area.getCentreX()),
                                           static_cast<float>(area.getCentreY() - theme_.spacing.xxxl -
                                                              theme_.spacing.xl)});
        Icons::draw(g, Icon::mark, mark, c.textTertiary);
        g.setColour(c.textPrimary);
        g.setFont(theme_.heading());
        g.drawText(
            "Start a project",
            area.withSizeKeepingCentre(area.getWidth(), static_cast<int>(theme_.fonts.size.heading * 2))
                .translated(0, -theme_.spacing.lg),
            juce::Justification::centred);
        g.setColour(c.textTertiary);
        g.setFont(theme_.small());
        g.drawText("or drop audio files here",
                   area.withSizeKeepingCentre(area.getWidth(), static_cast<int>(theme_.fonts.size.small * 2))
                       .translated(0, theme_.spacing.xl + theme_.sizes.control + theme_.spacing.lg),
                   juce::Justification::centred);
    }

    // Marker lines through the lanes (the flags sit in the ruler).
    if (project_ != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(lanesArea);
        for (const auto& m : project_->markers)
        {
            const auto position =
                drag_ == Drag::marker && m.id == dragMarker_.id ? markerPreview_ : m.position;
            const int x = xForSample(position);
            const bool selected = m.id == selectedMarker_;
            g.setColour((selected ? c.accent : c.markerFlag).withAlpha(0.5f));
            g.fillRect(x, rulerHeight, 1, lanesHeight());
            if (m.isRegion())
            {
                const int x1 = xForSample(position + m.length);
                g.setColour(c.regionFill.withMultipliedAlpha(0.5f));
                g.fillRect(x, rulerHeight, std::max(1, x1 - x), lanesHeight());
                g.setColour(c.markerFlag.withAlpha(0.35f));
                g.fillRect(x1, rulerHeight, 1, lanesHeight());
            }
        }
        g.restoreState();
    }

    drawRuler(g, getLocalBounds().removeFromTop(rulerOnly));
    drawEditOverlay(g);
    drawToolOverlay(g);

    // The playhead: a 1 px line with a 7 px head in the ruler.
    const int px = xForSample(playhead_);
    if (px >= headerWidth() && px < getWidth() - scrollbarWidth)
    {
        g.setColour(recording_ ? c.playheadRecording : c.playhead);
        g.fillRect(px, 0, 1, getHeight() - scrollbarHeight);
        juce::Path head;
        head.addTriangle(static_cast<float>(px) - 3.5f,
                         static_cast<float>(rulerOnly - 8),
                         static_cast<float>(px) + 4.5f,
                         static_cast<float>(rulerOnly - 8),
                         static_cast<float>(px) + 0.5f,
                         static_cast<float>(rulerOnly - 1));
        g.fillPath(head);
    }

    // The reorder insertion line over the headers.
    if (reorderFrom_ != core::invalidId && reorderTo_ >= 0)
    {
        const int ly = rowTop(reorderTo_);
        g.setColour(c.accent);
        g.fillRect(0, ly - 1, headerWidth(), 2);
    }

    // The pencil's clip while it draws: the clip it will make, in the track's colour.
    if (drag_ == Drag::draw && project_ != nullptr)
        if (const int row = project_->trackIndex(drawTrack_); row >= 0)
        {
            g.saveState();
            g.reduceClipRegion(lanesArea);
            const auto r = spanBounds(drawStart_, drawEnd_ - drawStart_, row).toFloat().reduced(0.5f, 1.5f);
            const auto colour = trackColour(project_->tracks[static_cast<size_t>(row)], row);
            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(r, theme_.radius.sm);
            g.setColour(colour);
            g.drawRoundedRectangle(r, theme_.radius.sm, 1.0f);
            g.restoreState();
        }

    // Files dragged over: the lane lights up and a ghost clip shows where they land.
    if (dragOver_)
    {
        const int row = rowAtY(dropPoint_.y);
        g.saveState();
        g.reduceClipRegion(lanesArea);
        if (row >= 0)
        {
            g.setColour(c.dropTarget);
            g.fillRect(rowBounds(row).withLeft(headerWidth()));
        }
        const int x = xForSample(snapped(sampleForX(std::max(headerWidth(), dropPoint_.x))));
        const int ghostY =
            row >= 0 ? rowBounds(row).getY() + 2 : std::max(rulerHeight, dropPoint_.y - rowHeight / 2);
        const auto ghost = juce::Rectangle<int>(x, ghostY, 160, rowHeight - 4);
        g.setColour(c.accent);
        g.drawRoundedRectangle(ghost.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
        g.setFont(theme_.caption());
        g.setColour(c.textPrimary);
        g.drawText(
            dropFiles_.isEmpty()
                ? juce::String()
                : juce::File(dropFiles_[0]).getFileName() +
                      (dropFiles_.size() > 1 ? " +" + juce::String(dropFiles_.size() - 1) : juce::String()),
            ghost.reduced(6, 0),
            juce::Justification::centredLeft,
            true);
        g.restoreState();
        if (row < 0 && project_ != nullptr && !project_->tracks.empty())
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.caption());
            g.drawText(
                dropFiles_.size() == 1 ? "New track" : juce::String(dropFiles_.size()) + " new tracks",
                juce::Rectangle<int>(0, ghostY, headerWidth(), rowHeight - 4).reduced(theme_.spacing.sm, 0),
                juce::Justification::centredLeft);
        }
    }

    // A plugin dragged over a track lights its row.
    if (pluginDropRow_ >= 0)
    {
        g.setColour(c.dropTarget);
        g.fillRect(rowBounds(pluginDropRow_).withRight(getWidth() - scrollbarWidth));
    }

    // The corner over the headers.
    g.setColour(c.surface0);
    g.fillRect(0, 0, headerWidth(), rulerOnly);
    g.setColour(c.lineSubtle);
    g.fillRect(0, rulerOnly - 1, headerWidth(), 1);
    g.fillRect(headerWidth() - 1, 0, 1, rulerOnly);
    // (The selection count is in the status bar: the corner is the tools'.)
}
// ----- editing -------------------------------------------------------------------------------

int ArrangeView::channelRowAtY(int y) const noexcept
{
    if (project_ == nullptr || y < rulerHeight || y >= getHeight() - scrollbarHeight)
        return -1;
    const auto& tops = rows().tops;
    const int content = y - rulerHeight + verticalScroll_;
    // The last top at or above the point (tops are ascending; tops.back() is the end).
    const auto it = std::upper_bound(tops.begin(), tops.end(), content);
    const int row = static_cast<int>(it - tops.begin()) - 1;
    return row >= 0 && row < rowCount() ? row : -1;
}

int ArrangeView::rowAtY(int y) const noexcept
{
    // Tracks only (FX tracks have no clips).
    const int row = channelRowAtY(y);
    return project_ != nullptr && row >= 0 && static_cast<size_t>(row) < project_->tracks.size() ? row : -1;
}

int ArrangeView::rowHeightOf(int row) const noexcept
{
    const auto& tops = rows().tops;
    if (row < 0 || static_cast<size_t>(row) + 1 >= tops.size())
        return rowHeight;
    const int lanes = project_ != nullptr && static_cast<size_t>(row) < project_->tracks.size()
                          ? takes_.lanesHeight(project_->tracks[static_cast<size_t>(row)])
                          : 0;
    return std::max(0,
                    tops[static_cast<size_t>(row) + 1] - tops[static_cast<size_t>(row)] - lanes -
                        automationHeight(row));
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
    const auto& tops = rows().tops;
    const int bottom = rulerHeight - verticalScroll_ + tops[static_cast<size_t>(row) + 1];
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
    const auto& tops = rows().tops;
    const auto at = static_cast<size_t>(std::clamp(row, 0, static_cast<int>(tops.size()) - 1));
    return rulerHeight - verticalScroll_ + tops[at];
}

int ArrangeView::resizeEdgeAt(juce::Point<int> p) const noexcept
{
    if (project_ == nullptr || p.y < rulerHeight || p.y >= getHeight() - scrollbarHeight ||
        p.x >= getWidth() - scrollbarWidth)
        return -1;
    // The row whose bottom edge is nearest the point, within the edge's reach
    // (the bottom of the row's own height: take lanes under it do not resize it).
    const auto& tops = rows().tops;
    const int content = p.y - rulerHeight + verticalScroll_;
    const auto it = std::upper_bound(tops.begin(), tops.end(), content);
    const int at = static_cast<int>(it - tops.begin()) - 1; // the row the point is in
    for (const int row : {at - 1, at})
    {
        if (row < 0 || static_cast<size_t>(row) + 1 >= tops.size())
            continue;
        const int bottom = tops[static_cast<size_t>(row)] + rowHeightOf(row);
        if (content >= bottom - TrackHeader::resizeEdge && content <= bottom + 1)
            return row;
    }
    return -1;
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
std::optional<ArrangeView::Span> ArrangeView::spanOf(const ClipRef& ref) const noexcept
{
    if (ref.midi)
    {
        if (const auto* m = findMidiClip(ref))
            return Span{m->start, m->length};
        return std::nullopt;
    }
    if (const auto* a = findClip(ref))
        return Span{a->start, a->length};
    return std::nullopt;
}

const core::MidiClip* ArrangeView::findMidiClip(const ClipRef& ref) const noexcept
{
    if (project_ == nullptr || !ref.midi)
        return nullptr;
    const auto* t = project_->findTrack(ref.track);
    return t != nullptr ? t->findMidiClip(ref.clip) : nullptr;
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
        hit.zone = p.x < bounds.getX() + edgeZone        ? Drag::trimLeft
                   : p.x >= bounds.getRight() - edgeZone ? Drag::trimRight
                                                         : Drag::move;
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
            hit.zone = Drag::fadeIn;
        else if (topRow && p.x >= bounds.getRight() - fadeHandle && bounds.getWidth() > 2 * fadeHandle + 4)
            hit.zone = Drag::fadeOut;
        else if (p.x < bounds.getX() + edgeZone)
            hit.zone = Drag::trimLeft;
        else if (p.x >= bounds.getRight() - edgeZone)
            hit.zone = Drag::trimRight;
        else
            hit.zone = Drag::move;
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
    snap_ = on;
    repaint();
}

void ArrangeView::setRippleEnabled(bool on)
{
    ripple_ = on;
    repaint();
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
    snapGrid_ = std::max(0, division);
    repaint();
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
            drag_ = tool_ == Tool::erase ? Drag::erase : Drag::mute;
            sweep_.clear();
            sweepMute_.reset();
            if (onClip)
            {
                // The clip pressed decides what the sweep does: a playing clip mutes the rest.
                if (tool_ == Tool::mute)
                    sweepMute_ = !edit::isMuted(*project_, ref);
                for (const auto& r : targets())
                    sweepOver(r);
            }
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

void ArrangeView::sweepOver(const ClipRef& ref)
{
    if (project_ == nullptr || swept(ref))
        return;
    if (drag_ == Drag::mute && !sweepMute_)
        sweepMute_ = !edit::isMuted(*project_, ref);
    sweep_.push_back(ref);
    // A comp's clips erase together, as they select together.
    if (drag_ == Drag::erase && !ref.midi)
        if (const auto* track = project_->findTrack(ref.track))
            if (const auto* group = track->takeGroupOwning(ref.clip))
                for (const auto id : group->clips)
                    if (track->findClip(id) != nullptr && !swept({ref.track, id}))
                        sweep_.push_back({ref.track, id});
    repaint();
}

bool ArrangeView::swept(const ClipRef& ref) const noexcept
{
    return std::find(sweep_.begin(), sweep_.end(), ref) != sweep_.end();
}

bool ArrangeView::shownMuted(const ClipRef& ref, bool muted) const noexcept
{
    return drag_ == Drag::mute && sweepMute_ && swept(ref) ? *sweepMute_ : muted;
}

void ArrangeView::finishSweep(bool erase)
{
    const auto clips = std::exchange(sweep_, {});
    const auto muted = std::exchange(sweepMute_, std::nullopt);
    if (project_ == nullptr || clips.empty())
        return;
    if (erase)
    {
        removeClips(clips);
        return;
    }
    auto e = edit::setMuted(*project_, clips, muted.value_or(true));
    emit(std::move(e.commands), e.name);
}

void ArrangeView::hatch(juce::Graphics& g,
                        juce::Rectangle<int> body,
                        juce::Rectangle<int> visible,
                        juce::Colour colour,
                        float dim)
{
    // A hatched body for muted clips: 45 degrees, 6 px apart.
    g.saveState();
    g.reduceClipRegion(body.getIntersection(visible));
    g.setColour(colour.withAlpha(0.10f * dim));
    for (int x = visible.getX() - body.getHeight(); x < visible.getRight(); x += 6)
        g.drawLine(static_cast<float>(x),
                   static_cast<float>(body.getBottom()),
                   static_cast<float>(x + body.getHeight()),
                   static_cast<float>(body.getY()),
                   1.0f);
    g.restoreState();
}

void ArrangeView::drawToolOverlay(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    if (project_ == nullptr)
        return;
    const auto lanesArea = juce::Rectangle<int>(
        headerWidth(), rulerHeight, getWidth() - headerWidth() - scrollbarWidth, lanesHeight());
    // The split line: through the clip under the pointer, or every lane with Alt.
    if (tool_ == Tool::split && splitAt_ >= 0 && drag_ == Drag::none)
    {
        const int x = xForSample(splitAt_);
        const auto line = splitRow_ >= 0 ? spanBounds(splitClip_.start, splitClip_.length, splitRow_)
                                         : juce::Rectangle<int>(x, rulerHeight, 1, lanesHeight());
        g.saveState();
        g.reduceClipRegion(lanesArea);
        g.setColour(c.accent);
        g.fillRect(x, line.getY(), 1, line.getHeight());
        g.restoreState();
        drawTooltip(
            g, (splitAll_ ? "Split all at " : "Split at ") + positionText(splitAt_), {x, splitPoint_.y});
        return;
    }
    if (drag_ != Drag::none || hoverClip_.clip == core::invalidId)
        return;
    const int row = project_->trackIndex(hoverClip_.track);
    const auto span = spanOf(hoverClip_);
    if (row < 0 || !span)
        return;
    const auto block = spanBounds(span->start, span->length, row);
    g.saveState();
    g.reduceClipRegion(lanesArea);
    if (tool_ == Tool::erase)
    {
        // The clip the eraser would take, outlined in danger.
        g.setColour(c.danger);
        g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    }
    else if (tool_ == Tool::glue)
    {
        // What the click would make: one outline round both clips, the seam marked.
        if (edit::canJoinWithNext(*project_, hoverClip_))
        {
            const auto next = spanOf(*edit::nextClip(*project_, hoverClip_));
            const auto joined =
                spanBounds(span->start, std::max(span->end(), next->end()) - span->start, row);
            g.setColour(c.accent);
            g.drawRoundedRectangle(joined.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
            for (const auto seam : {span->end(), next->start})
                g.fillRect(xForSample(seam) - 1, joined.getY(), 2, joined.getHeight());
        }
    }
    g.restoreState();
    // A clip the glue cannot join says why, beside it.
    if (tool_ == Tool::glue && !edit::canJoinWithNext(*project_, hoverClip_))
        drawTooltip(g,
                    edit::nextClip(*project_, hoverClip_) ? "Joins only a clip that continues this one"
                                                          : "No clip after this one to join",
                    {std::min(block.getRight(), lanesArea.getRight()) - 10, block.getY() - 14});
}

void ArrangeView::toolsMenu()
{
    static constexpr const char* labels[toolCount] = {
        "Select", "Range", "Pencil", "Erase", "Split", "Glue", "Mute"};
    juce::PopupMenu menu;
    const auto tools = toolButtons();
    for (int i = 0; i < toolCount; ++i)
    {
        juce::PopupMenu::Item item(labels[i]);
        item.itemID = i + 1;
        item.isTicked = static_cast<Tool>(i) == tool_;
        item.shortcutKeyDescription = juce::String(i + 1);
        bool filled = false;
        auto icon = std::make_unique<juce::DrawablePath>();
        icon->setPath(Icons::path(tools[static_cast<size_t>(i)]->icon(), filled));
        icon->setStrokeFill(juce::FillType(theme_.colours.textSecondary));
        icon->setStrokeType(
            juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        icon->setFill(filled ? juce::FillType(theme_.colours.textSecondary) : juce::FillType());
        item.image = std::move(icon);
        menu.addItem(std::move(item));
    }
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&toolMenu_),
                       [safe](int choice)
                       {
                           if (safe != nullptr && choice > 0)
                               safe->setTool(static_cast<Tool>(choice - 1));
                       });
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
        clipMenu(ref, localPointToGlobal(at));
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
                              project_->trackIndex(selectedTrack_),
                              playheadForEdits ? playheadForEdits() : playhead_);
    emit(std::move(e.commands), e.name);
}

void ArrangeView::renameSelectedTrack()
{
    for (auto& header : headers_)
        if (header->trackId() == selectedTrack_)
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

void ArrangeView::finishLasso()
{
    if (project_ == nullptr)
        return;
    const auto from = sampleForX(lasso_.getX());
    const auto to = sampleForX(lasso_.getRight());
    const SelectionIndex index(*this); // each clip in the lasso checks the selection
    for (size_t t = 0; t < project_->tracks.size(); ++t)
    {
        const auto lane = rowBounds(static_cast<int>(t));
        if (lane.getBottom() <= lasso_.getY() || lane.getY() >= lasso_.getBottom())
            continue;
        for (const auto& c : project_->tracks[t].clips)
            if (c.end() > from && c.start < to)
                select({project_->tracks[t].id, c.id}, true);
        for (const auto& m : project_->tracks[t].midiClips)
            if (m.end() > from && m.start < to)
                select({project_->tracks[t].id, m.id, true}, true);
    }
}

void ArrangeView::finishMove()
{
    if (project_ == nullptr || (moveDelta_ == 0 && rowDelta_ == 0))
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    std::vector<ClipRef> moved;
    std::vector<core::Id> movedGroups;
    for (const auto& ref : selection_)
    {
        const int from = project_->trackIndex(ref.track);
        const auto span = spanOf(ref);
        if (from < 0 || !span.has_value())
            continue;
        const int to = std::clamp(from + rowDelta_, 0, static_cast<int>(project_->tracks.size()) - 1);
        const auto& target = project_->tracks[static_cast<size_t>(to)];
        if (target.isMidi() != ref.midi)
            continue; // a clip stays on its own kind of track
        // A comp's clip moves its whole take group (takes, comp and clips) once.
        if (const auto* group =
                ref.midi ? nullptr : project_->tracks[static_cast<size_t>(from)].takeGroupOwning(ref.clip))
        {
            if (std::find(movedGroups.begin(), movedGroups.end(), group->id) == movedGroups.end())
            {
                movedGroups.push_back(group->id);
                commands.push_back(
                    std::make_unique<core::MoveTakeGroup>(ref.track, group->id, target.id, moveDelta_));
            }
            moved.push_back({target.id, ref.clip, false});
            continue;
        }
        const auto start = std::max<core::SamplePosition>(0, span->start + moveDelta_);
        if (ref.midi)
            commands.push_back(std::make_unique<core::MoveMidiClip>(ref.track, ref.clip, target.id, start));
        else
            commands.push_back(std::make_unique<core::MoveClip>(ref.track, ref.clip, target.id, start));
        moved.push_back({target.id, ref.clip, ref.midi});
    }
    selection_ = moved; // the selection follows the clips to their new tracks
    const std::string name = commands.size() > 1 ? "Move clips" : "Move clip";
    emit(std::move(commands), name);
    notifySelection();
}

void ArrangeView::finishTrim()
{
    if (!spanOf(dragClip_).has_value())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    if (dragClip_.midi)
        commands.push_back(
            std::make_unique<core::TrimMidiClip>(dragClip_.track, dragClip_.clip, trimStart_, trimLength_));
    else
        commands.push_back(std::make_unique<core::TrimClip>(
            dragClip_.track, dragClip_.clip, trimStart_, trimLength_, trimOffset_));
    emit(std::move(commands), "Trim clip");
}

void ArrangeView::finishStretch()
{
    const auto* clip = findClip(dragClip_);
    if (clip == nullptr || stretchLength_ == clip->length)
        return;
    const double stretch = clip->stretch * static_cast<double>(stretchLength_) /
                           static_cast<double>(std::max<core::SamplePosition>(1, clip->length));
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetClipStretch>(
        dragClip_.track, dragClip_.clip, stretch, clip->transpose, stretchLength_, clip->followTempo));
    emit(std::move(commands), "Stretch clip");
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

void ArrangeView::clipMenu(const ClipRef& ref, juce::Point<int> screen)
{
    const auto* track = project_ != nullptr ? project_->findTrack(ref.track) : nullptr;
    const auto* clip = track != nullptr && !ref.midi ? track->findClip(ref.clip) : nullptr;
    const auto* midiClip = track != nullptr && ref.midi ? track->findMidiClip(ref.clip) : nullptr;
    const auto span = spanOf(ref);
    if (track == nullptr || !span || (clip == nullptr && midiClip == nullptr))
        return;
    // Where the menu opened, for "Split here".
    const auto local = getLocalPoint(nullptr, screen);
    const auto splitHere = splitPosition(local.x, *span);
    const auto selection = selection_; // the menu acts on the selection (the clip is in it)
    const bool muted = edit::isMuted(*project_, ref);
    const bool anyPlaying = std::any_of(
        selection.begin(), selection.end(), [&](const ClipRef& r) { return !edit::isMuted(*project_, r); });
    const bool many = selection.size() > 1;

    enum Choice
    {
        stretchToTempo = 1,
        followTempo,
        resetStretch,
        trackItem,
        openInSampler,
        openEditor,
        splitHereItem = 20,
        joinItem,
        crossfadeItem,
        muteItem,
        duplicateItem,
        normaliseItem,
        cutItem,
        copyItem,
        deleteItem,
        transposeBase = 150,
    };
    juce::PopupMenu menu;
    const auto add =
        [&menu](int id, const juce::String& text, const char* keys, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item item(text);
        item.itemID = id;
        item.isEnabled = enabled;
        item.isTicked = ticked;
        item.shortcutKeyDescription = keys;
        menu.addItem(std::move(item));
    };
    menu.addSectionHeader(
        juce::String::fromUTF8((clip != nullptr ? clip->name : midiClip->name).c_str()) +
        (many ? "  +" + juce::String(static_cast<int>(selection.size()) - 1) : juce::String()));
    add(splitHereItem, "Split here", "", splitHere > span->start && splitHere < span->end());
    add(joinItem,
        many ? "Join" : "Join with next",
        many ? "J" : "",
        many ? !edit::joinClips(*project_, selection).empty() : edit::canJoinWithNext(*project_, ref));
    if (!ref.midi)
        add(crossfadeItem, "Crossfade", "X", !edit::crossfadeClips(*project_, selection).empty());
    add(muteItem,
        many ? (anyPlaying ? "Mute clips" : "Unmute clips") : (muted ? "Unmute clip" : "Mute clip"),
        "Ctrl+M");
    add(duplicateItem, "Duplicate", "Ctrl+D");
    if (!ref.midi)
        add(normaliseItem, "Normalise", "Ctrl+Shift+N", static_cast<bool>(onNormalise));
    menu.addSeparator();
    add(cutItem, "Cut", "Ctrl+X");
    add(copyItem, "Copy", "Ctrl+C");
    add(deleteItem, "Delete", "Delete");
    menu.addSeparator();
    if (clip != nullptr)
    {
        menu.addItem(stretchToTempo, "Stretch to project tempo...", static_cast<bool>(onConformClip));
        menu.addItem(followTempo, "Follow tempo changes", true, clip->followTempo);
        juce::PopupMenu transpose;
        for (const int semis : {12, 7, 5, 2, 1, 0, -1, -2, -5, -7, -12})
            transpose.addItem(transposeBase + semis,
                              semis == 0 ? juce::String("Original pitch")
                                         : (semis > 0 ? "+" : "") + juce::String(semis) + " semitones",
                              true,
                              std::abs(clip->transpose - semis) < 1.0e-6);
        menu.addSubMenu("Transpose", transpose);
        menu.addItem(resetStretch, "Reset stretch and pitch", clip->isStretched());
        menu.addSeparator();
        menu.addItem(openInSampler, "Open in Sampler", static_cast<bool>(onOpenInSampler));
    }
    else
    {
        menu.addItem(openEditor, "Open in piano roll", static_cast<bool>(onOpenMidiClip));
    }
    menu.addItem(trackItem, "Track...");
    juce::Component::SafePointer<ArrangeView> safe(this);
    const auto original = clip != nullptr ? *clip : core::AudioClip{};
    const auto trackId = ref.track;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
        [safe, ref, trackId, original, screen, splitHere, selection](int choice)
        {
            if (safe == nullptr || choice == 0 || safe->project_ == nullptr)
                return;
            auto& view = *safe;
            const auto& project = *view.project_;
            const auto run = [&](edit::Edit e) { view.emit(std::move(e.commands), e.name); };
            const auto set = [&](double stretch, double transpose, core::SamplePosition length, bool follow)
            {
                std::vector<std::unique_ptr<core::Command>> commands;
                commands.push_back(std::make_unique<core::SetClipStretch>(
                    trackId, original.id, stretch, transpose, length, follow));
                view.emit(std::move(commands), "Stretch clip");
            };
            switch (choice)
            {
                case splitHereItem:
                    run(edit::splitAt(project, selection, splitHere));
                    return;
                case joinItem:
                    run(selection.size() > 1 ? edit::joinClips(project, selection)
                                             : edit::joinWithNext(project, ref));
                    return;
                case crossfadeItem:
                    run(edit::crossfadeClips(project, selection));
                    return;
                case muteItem:
                    run(edit::toggleMuted(project, selection));
                    return;
                case duplicateItem:
                    run(edit::duplicateClips(project, selection));
                    return;
                case normaliseItem:
                    view.normaliseSelection();
                    return;
                case cutItem:
                    view.cutSelection();
                    return;
                case copyItem:
                    view.copySelection();
                    return;
                case deleteItem:
                    view.removeClips(selection);
                    return;
                case stretchToTempo:
                    if (view.onConformClip)
                        view.onConformClip(trackId, original.id);
                    return;
                case followTempo:
                    set(original.stretch, original.transpose, original.length, !original.followTempo);
                    return;
                case resetStretch:
                    set(1.0, 0.0, original.sourceLength(), false);
                    return;
                case trackItem:
                    view.trackMenu(trackId, screen);
                    return;
                case openInSampler:
                    if (view.onOpenInSampler)
                        view.onOpenInSampler(trackId, original.id);
                    return;
                case openEditor:
                    if (view.onOpenMidiClip)
                        view.onOpenMidiClip(trackId, ref.clip);
                    return;
                default:
                    if (choice >= transposeBase - 12 && choice <= transposeBase + 12)
                        set(original.stretch, choice - transposeBase, original.length, original.followTempo);
                    return;
            }
        });
}

void ArrangeView::finishFade(Drag which)
{
    const auto* clip = findClip(dragClip_);
    if (clip == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetClipFades>(
        dragClip_.track,
        dragClip_.clip,
        which == Drag::fadeIn ? fadePreview_ : clip->fadeIn,
        which == Drag::fadeOut ? fadePreview_ : clip->fadeOut,
        which == Drag::fadeIn ? fadeCurvePreview_ : clip->fadeInCurve,
        which == Drag::fadeOut ? fadeCurvePreview_ : clip->fadeOutCurve));
    emit(std::move(commands), "Set fade");
}

float ArrangeView::fadeCurveFor(int dy) const noexcept
{
    // 80 px from linear to either end; within 4 px of where linear is, linear.
    constexpr float pixelsPerUnit = 80.0f;
    const float curve = std::clamp(fadeCurveStart_ - static_cast<float>(dy) / pixelsPerUnit, -1.0f, 1.0f);
    return std::abs(curve) * pixelsPerUnit < 4.0f ? 0.0f : curve;
}

juce::String ArrangeView::fadeCurveText(float curve)
{
    if (curve == 0.0f)
        return "linear";
    return juce::String(curve > 0.0f ? "fast " : "slow ") +
           juce::String(juce::roundToInt(std::abs(curve) * 100.0f)) + "%";
}

void ArrangeView::finishGain()
{
    if (findClip(dragClip_) == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetClipGain>(dragClip_.track, dragClip_.clip, gainPreview_));
    emit(std::move(commands), "Set clip gain");
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
    if (timeSelTracks_.empty() && selectedTrack_ != core::invalidId)
        timeSelTracks_.push_back(selectedTrack_);
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

void ArrangeView::trackMenu(core::Id trackId, juce::Point<int> screen, core::Id clipId)
{
    if (project_ == nullptr || project_->findTrack(trackId) == nullptr)
        return;
    juce::PopupMenu menu;
    if (clipId != core::invalidId && project_->findTrack(trackId)->findClip(clipId) != nullptr &&
        onOpenInSampler)
    {
        menu.addSectionHeader("Clip");
        menu.addItem(7, "Open in Sampler");
        menu.addSeparator();
        menu.addSectionHeader("Track");
    }
    menu.addItem(1, "Rename");
    juce::PopupMenu colours;
    const auto& palette = theme_.track.palette;
    for (size_t i = 0; i < palette.size(); ++i)
        colours.addItem(100 + static_cast<int>(i), palette[i].name);
    colours.addSeparator();
    colours.addItem(199, "Default");
    menu.addSubMenu("Colour", colours);
    const auto* track = project_->findTrack(trackId);
    if (track->isMidi())
    {
        // MIDI output: the device chosen in Audio Setup, on a channel.
        const auto device = midiOutputName ? midiOutputName() : std::string{};
        juce::PopupMenu out;
        if (device.empty())
            out.addItem(300, "Choose a MIDI output in Audio Setup", false);
        else
        {
            out.addSectionHeader(juce::String::fromUTF8(device.c_str()));
            out.addItem(300, "Off", true, !track->midiOut);
            for (int ch = 1; ch <= 16; ++ch)
                out.addItem(300 + ch,
                            "Channel " + juce::String(ch),
                            true,
                            track->midiOut && track->midiOutChannel == ch);
        }
        menu.addSubMenu("MIDI output", out);
    }
    if (track->isMidi())
        menu.addItem(5, "Plays...");
    // Sends: tick a bus or FX track to send to it (post-fader, -6 dB), untick to stop.
    if (track->hasAudio())
    {
        juce::PopupMenu sends;
        for (size_t b = 0; b < project_->buses.size(); ++b)
        {
            const auto& bus = project_->buses[b];
            const bool sending = track->findSend(bus.id) != nullptr;
            sends.addItem(500 + static_cast<int>(b),
                          juce::String::fromUTF8(bus.name.c_str()) + (bus.fx ? "" : "  (bus)"),
                          sending || !project_->wouldCycle(trackId, bus.id),
                          sending);
        }
        sends.addSeparator();
        juce::PopupMenu newFx;
        addFxMenu(newFx, 450);
        sends.addSubMenu("New FX track", newFx);
        menu.addSubMenu("Send to", sends);
    }
    if (track->hasAudio())
        menu.addItem(4, track->automationOpen ? "Hide automation lanes" : "Show automation lanes");
    if (track->hasAudio())
        menu.addItem(800, track->isFrozen() ? "Unfreeze track" : "Freeze track");
    if (track->isFolder())
        menu.addItem(802, track->folderOpen ? "Close folder" : "Open folder");
    {
        juce::PopupMenu folders;
        folders.addItem(801, "New folder containing this track");
        folders.addSeparator();
        folders.addItem(
            810, "Not in a folder", track->folder != core::invalidId, track->folder == core::invalidId);
        const auto choices = folderChoices(trackId);
        for (size_t i = 0; i < choices.size(); ++i)
            folders.addItem(811 + static_cast<int>(i),
                            juce::String::fromUTF8(choices[i]->name.c_str()),
                            true,
                            track->folder == choices[i]->id);
        menu.addSubMenu("Folder", folders);
    }
    if (!track->takeGroups.empty())
        menu.addItem(6, takes_.laneCount(*track) > 0 ? "Hide take lanes" : "Show take lanes");
    menu.addSeparator();
    menu.addItem(2, "Delete track");
    menu.addItem(3, "Add bus");
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
        [safe, trackId, clipId](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            if (choice == 7)
            {
                if (safe->onOpenInSampler)
                    safe->onOpenInSampler(trackId, clipId);
                return;
            }
            if (choice == 800)
            {
                if (const auto* t = safe->project_ != nullptr ? safe->project_->findTrack(trackId) : nullptr;
                    t != nullptr && safe->onFreezeTrack)
                    safe->onFreezeTrack(trackId, !t->isFrozen());
                return;
            }
            if (choice == 801)
            {
                if (safe->onCommand)
                    safe->onCommand(
                        std::make_unique<core::AddFolderTrack>("Folder", std::vector<core::Id>{trackId}));
                return;
            }
            if (choice >= 810 && choice < 900 && safe->project_ != nullptr && safe->onCommand)
            {
                const auto folders = safe->folderChoices(trackId);
                const auto pick = static_cast<size_t>(choice - 811);
                safe->onCommand(std::make_unique<core::SetTrackFolder>(
                    trackId, choice == 810 || pick >= folders.size() ? core::invalidId : folders[pick]->id));
                return;
            }
            if (choice == 802)
            {
                if (const auto* t = safe->project_ != nullptr ? safe->project_->findTrack(trackId) : nullptr;
                    t != nullptr && safe->onCommand)
                    safe->onCommand(std::make_unique<core::SetFolderOpen>(trackId, !t->folderOpen));
                return;
            }
            if (choice >= 450 && choice < 500)
            {
                safe->addFxTrack(choice, 450, {trackId});
                return;
            }
            if (choice >= 500 && safe->project_ != nullptr &&
                static_cast<size_t>(choice - 500) < safe->project_->buses.size())
            {
                const auto busId = safe->project_->buses[static_cast<size_t>(choice - 500)].id;
                const auto* t = safe->project_->findTrack(trackId);
                if (t != nullptr && safe->onCommand)
                {
                    if (t->findSend(busId) != nullptr)
                        safe->onCommand(std::make_unique<core::RemoveSend>(trackId, busId));
                    else
                        safe->onCommand(std::make_unique<core::SetSend>(trackId, busId, 0.5f, false));
                }
                return;
            }
            if (choice >= 300 && choice <= 316)
            {
                if (safe->onCommand)
                    safe->onCommand(std::make_unique<core::SetTrackMidiOutput>(
                        trackId, choice > 300, std::max(1, choice - 300)));
                return;
            }
            if (choice == 4)
            {
                if (const auto* t =
                        safe->project_ != nullptr ? safe->project_->findChannel(trackId) : nullptr)
                    safe->showAutomation(trackId, !t->automationOpen);
                return;
            }
            if (choice == 6)
            {
                if (const auto* t = safe->project_ != nullptr ? safe->project_->findTrack(trackId) : nullptr)
                    safe->takes_.toggleLanes(*t);
                return;
            }
            if (choice == 5)
            {
                const auto mouse = juce::Desktop::getMousePosition();
                safe->targetMenu(trackId, {mouse.x, mouse.y, 1, 1});
                return;
            }
            const auto& palette = safe->theme_.track.palette;
            if (choice >= 100 && choice < 199 && static_cast<size_t>(choice - 100) < palette.size())
            {
                if (safe->onCommand)
                    safe->onCommand(std::make_unique<core::SetColour>(
                        trackId, palette[static_cast<size_t>(choice - 100)].value.getARGB()));
                return;
            }
            if (choice == 199 && safe->onCommand)
                safe->onCommand(std::make_unique<core::SetColour>(trackId, 0u));
            if (choice == 1)
                for (auto& header : safe->headers_)
                    if (header->trackId() == trackId)
                        header->startRename();
            if (choice == 2 && safe->onCommand)
                safe->onCommand(std::make_unique<core::RemoveTrack>(trackId));
            if (choice == 3 && safe->onAddBus)
                safe->onAddBus();
        });
}

// What a MIDI track plays: an instrument channel (or none), the MIDI channel it
// sends on, or a new instrument (a channel for it, just under the track).
void ArrangeView::targetMenu(core::Id trackId, juce::Rectangle<int> screenArea)
{
    const auto* track = project_ != nullptr ? project_->findTrack(trackId) : nullptr;
    if (track == nullptr || !track->isMidi())
        return;
    juce::PopupMenu menu;
    std::vector<core::Id> channels;
    menu.addSectionHeader("Plays");
    for (const auto& t : project_->tracks)
        if (t.isInstrument())
        {
            juce::String text = juce::String::fromUTF8(t.name.c_str());
            if (t.hasInstrument())
                text << "  ("
                     << juce::String::fromUTF8(displayName_ ? displayName_(t.instrument.type).c_str()
                                                            : t.instrument.type.c_str())
                     << ")";
            channels.push_back(t.id);
            menu.addItem(1000 + static_cast<int>(channels.size()) - 1, text, true, track->midiTarget == t.id);
        }
    menu.addItem(999, "Nothing", true, track->midiTarget == core::invalidId);
    menu.addSeparator();
    juce::PopupMenu midiChannels;
    for (int ch = 1; ch <= 16; ++ch)
        midiChannels.addItem(1100 + ch, "Channel " + juce::String(ch), true, track->midiChannel == ch);
    menu.addSubMenu("MIDI channel", midiChannels);
    auto instruments = buildPluginMenu(PluginKind::instrument, pluginSources_, 1200);
    menu.addSubMenu("New instrument", instruments.menu);
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(screenArea),
        [safe, trackId, channels, instruments](int choice)
        {
            if (safe == nullptr || choice == 0 || safe->project_ == nullptr)
                return;
            const auto* t = safe->project_->findTrack(trackId);
            if (t == nullptr)
                return;
            if (const auto type = instruments.typeFor(choice); !type.empty())
            {
                if (safe->onPlayNewInstrument)
                    safe->onPlayNewInstrument(trackId, type);
                return;
            }
            if (!safe->onCommand)
                return;
            if (choice == 999)
                safe->onCommand(
                    std::make_unique<core::SetMidiTarget>(trackId, core::invalidId, t->midiChannel));
            else if (choice >= 1000 && static_cast<size_t>(choice - 1000) < channels.size())
                safe->onCommand(std::make_unique<core::SetMidiTarget>(
                    trackId, channels[static_cast<size_t>(choice - 1000)], t->midiChannel));
            else if (choice > 1100 && choice <= 1116)
                safe->onCommand(std::make_unique<core::SetMidiTarget>(trackId, t->midiTarget, choice - 1100));
        });
}

void ArrangeView::rulerMenu(juce::Point<int> screen)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Follow playhead", true, followPlayhead_);
    menu.addItem(2, "Zoom to fit");
    menu.addSeparator();
    menu.addItem(3, "Add marker at playhead");
    menu.addItem(4, "Add region from loop", regionEnd_ > regionStart_);
    menu.addSeparator();
    menu.addItem(5, "Show tempo track", true, tempoShown_);
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
                       [safe](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           switch (choice)
                           {
                               case 1:
                                   safe->followPlayhead_ = !safe->followPlayhead_;
                                   break;
                               case 2:
                                   safe->zoomToFit();
                                   break;
                               case 3:
                                   safe->addMarkerAtPlayhead(false);
                                   break;
                               case 4:
                                   safe->addMarkerAtPlayhead(true);
                                   break;
                               case 5:
                                   safe->setTempoTrackShown(!safe->tempoShown_);
                                   break;
                               default:
                                   break;
                           }
                       });
}

void ArrangeView::addFxMenu(juce::PopupMenu& menu, int firstId) const
{
    const auto choices = plugins::fxTrackEffects();
    for (size_t i = 0; i < choices.size(); ++i)
        menu.addItem(firstId + static_cast<int>(i), juce::String(choices[i].name));
}

void ArrangeView::addFxTrack(int choice, int firstId, std::vector<core::Id> sendFrom)
{
    const auto choices = plugins::fxTrackEffects();
    const auto i = static_cast<size_t>(choice - firstId);
    if (project_ == nullptr || i >= choices.size())
        return;
    emit(plugins::addFxTrackCommands(*project_, choices[i].type, choices[i].name, sendFrom),
         sendFrom.empty() ? "Add FX track" : "Add FX track and send");
}

void ArrangeView::fxTrackMenu(core::Id busId, juce::Point<int> screen)
{
    if (project_ == nullptr || project_->findBus(busId) == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Rename");
    menu.addItem(
        2, project_->findBus(busId)->automationOpen ? "Hide automation lanes" : "Show automation lanes");
    menu.addSeparator();
    menu.addItem(3, project_->findBus(busId)->fx ? "Remove FX track" : "Remove bus");
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
                       [safe, busId](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           if (choice == 1)
                               for (auto& header : safe->fxHeaders_)
                                   if (header->trackId() == busId)
                                       header->startRename();
                           if (choice == 2)
                               if (const auto* bus =
                                       safe->project_ != nullptr ? safe->project_->findBus(busId) : nullptr)
                                   safe->showAutomation(busId, !bus->automationOpen);
                           if (choice == 3 && safe->onCommand)
                               safe->onCommand(std::make_unique<core::RemoveBus>(busId));
                       });
}

void ArrangeView::masterMenu(juce::Point<int> screen)
{
    if (project_ == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader("Master");
    menu.addItem(1, project_->master.automationOpen ? "Hide automation lanes" : "Show automation lanes");
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1}),
                       [safe](int choice)
                       {
                           if (safe != nullptr && choice == 1 && safe->project_ != nullptr)
                               safe->showAutomation(core::masterId, !safe->project_->master.automationOpen);
                       });
}

void ArrangeView::addTrackMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Audio track");
    // A MIDI track plays an instrument channel: a new one, one already here, or
    // nothing yet.
    juce::PopupMenu midi;
    auto instruments = buildPluginMenu(PluginKind::instrument, pluginSources_, 1200);
    midi.addSubMenu("With a new instrument", instruments.menu);
    std::vector<core::Id> channels;
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
            if (t.isInstrument())
            {
                if (channels.empty())
                    midi.addSectionHeader("Playing");
                juce::String text = juce::String::fromUTF8(t.name.c_str());
                if (t.hasInstrument())
                    text << "  ("
                         << juce::String::fromUTF8(displayName_ ? displayName_(t.instrument.type).c_str()
                                                                : t.instrument.type.c_str())
                         << ")";
                channels.push_back(t.id);
                midi.addItem(1000 + static_cast<int>(channels.size()) - 1, text);
            }
    midi.addSeparator();
    midi.addItem(999, "Playing nothing yet");
    menu.addSubMenu("MIDI track", midi);
    juce::PopupMenu fx;
    addFxMenu(fx, 400);
    menu.addSubMenu("FX track", fx);
    menu.addItem(3, "Bus");
    menu.addItem(
        4, selectedTrack_ != core::invalidId ? "Folder track (holding the selected track)" : "Folder track");
    juce::Component::SafePointer<ArrangeView> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&addTrack_),
        [safe, instruments, channels](int choice)
        {
            if (safe == nullptr)
                return;
            if (choice == 4 && safe->onCommand)
            {
                std::vector<core::Id> members;
                if (safe->project_ != nullptr && safe->project_->findTrack(safe->selectedTrack_) != nullptr)
                    members.push_back(safe->selectedTrack_);
                safe->onCommand(std::make_unique<core::AddFolderTrack>("Folder", members));
                return;
            }
            if (choice == 1 && safe->onAddAudioTrack)
                safe->onAddAudioTrack();
            if (const auto type = instruments.typeFor(choice); !type.empty() && safe->onAddMidiTrack)
                safe->onAddMidiTrack(type);
            if (choice == 999 && safe->onAddMidiTrackPlaying)
                safe->onAddMidiTrackPlaying(core::invalidId);
            if (choice >= 1000 && static_cast<size_t>(choice - 1000) < channels.size() &&
                safe->onAddMidiTrackPlaying)
                safe->onAddMidiTrackPlaying(channels[static_cast<size_t>(choice - 1000)]);
            if (choice == 3 && safe->onAddBus)
                safe->onAddBus();
            if (choice >= 400 && choice < 450)
                safe->addFxTrack(choice, 400, {});
        });
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
    const auto plain = [&](juce::juce_wchar ch)
    { return key == juce::KeyPress(ch, juce::ModifierKeys(), 0); };
    const auto ctrl = [&](juce::juce_wchar ch)
    { return key == juce::KeyPress(ch, juce::ModifierKeys::commandModifier, 0); };
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (selectedMarker_ != core::invalidId && project_ != nullptr &&
            project_->findMarker(selectedMarker_))
        {
            std::vector<std::unique_ptr<core::Command>> commands;
            commands.push_back(std::make_unique<core::RemoveMarker>(selectedMarker_));
            selectedMarker_ = core::invalidId;
            emit(std::move(commands), "Remove marker");
            return true;
        }
        deleteSelection();
        return true;
    }
    if (plain('m'))
    {
        addMarkerAtPlayhead(false);
        return true;
    }
    if (key == juce::KeyPress('m', juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress('M'))
    {
        addMarkerAtPlayhead(true);
        return true;
    }
    if (plain(',') || plain('.'))
    {
        jumpToMarker(plain(',') ? -1 : 1);
        return true;
    }
    if (ctrl('d'))
    {
        duplicateSelection();
        return true;
    }
    if (ctrl('c'))
    {
        copySelection();
        return true;
    }
    if (ctrl('x'))
    {
        cutSelection();
        return true;
    }
    if (ctrl('v'))
    {
        paste();
        return true;
    }
    for (int i = 0; i < toolCount; ++i)
        if (plain(static_cast<juce::juce_wchar>('1' + i)))
        {
            setTool(static_cast<Tool>(i));
            return true;
        }
    if (ctrl('m'))
    {
        toggleMuteSelection();
        return true;
    }
    // Take lanes: Alt+Up / Alt+Down cycle the take at the playhead, Alt+T shows or hides the lanes.
    if (const auto* track = project_ != nullptr ? project_->findTrack(selectedTrack_) : nullptr;
        track != nullptr && key.getModifiers() == juce::ModifierKeys::altModifier)
    {
        if (key.getKeyCode() == juce::KeyPress::upKey || key.getKeyCode() == juce::KeyPress::downKey)
            return takes_.cycleTake(*track,
                                    playheadForEdits ? playheadForEdits() : playhead_,
                                    key.getKeyCode() == juce::KeyPress::upKey ? 1 : -1);
        if (key.getKeyCode() == 'T' || key.getKeyCode() == 't')
            return takes_.toggleLanes(*track);
    }
    if (key.getModifiers().isAltDown() &&
        (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey))
    {
        nudgeSelection(key.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1,
                       key.getModifiers().isShiftDown());
        return true;
    }
    if (ctrl('a'))
    {
        selectAll();
        return true;
    }
    // A: the selected track's automation lanes open or close.
    if (plain('a'))
        if (const auto* channel = project_ != nullptr ? project_->findChannel(selectedTrack_) : nullptr)
        {
            const auto* track = project_->findTrack(selectedTrack_);
            if (track == nullptr || track->hasAudio())
            {
                showAutomation(selectedTrack_, !channel->automationOpen);
                return true;
            }
        }
    if (key ==
        juce::KeyPress('n', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        normaliseSelection();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    if (plain('s'))
    {
        splitAtPlayhead();
        return true;
    }
    if (key == juce::KeyPress('s', juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress('S'))
    {
        splitAtSelection();
        return true;
    }
    if (plain('j'))
    {
        joinSelection();
        return true;
    }
    if (plain('x'))
    {
        crossfadeSelection();
        return true;
    }
    if (plain('g'))
    {
        setSnapEnabled(!snap_);
        return true;
    }
    if (plain('e'))
    {
        setRippleEnabled(!ripple_);
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        if (!selection_.empty())
        {
            nudgeSelection(key == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }
    }
    return false;
}

juce::MouseCursor ArrangeView::cursorFor(Drag zone) const noexcept
{
    switch (zone)
    {
        case Drag::trimLeft:
        case Drag::trimRight:
        case Drag::stretch:
            return juce::MouseCursor::LeftRightResizeCursor;
        case Drag::fadeIn:
        case Drag::fadeOut:
            return juce::MouseCursor::PointingHandCursor;
        case Drag::move:
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
    const auto& c = theme_.colours;
    g.setFont(theme_.caption());
    const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + 2 * theme_.spacing.sm;
    const int h = theme_.sizes.controlCompact;
    auto box = juce::Rectangle<int>(at.x + 14, at.y + 18, w, h);
    if (box.getRight() > getWidth() - scrollbarWidth)
        box.setX(at.x - w - 6);
    if (box.getBottom() > getHeight() - scrollbarHeight)
        box.setY(at.y - h - 6);
    g.setColour(c.surface2);
    g.fillRoundedRectangle(box.toFloat(), theme_.radius.sm);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    g.setColour(c.textPrimary);
    g.drawText(text, box, juce::Justification::centred);
}

void ArrangeView::drawEditOverlay(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    if (project_ == nullptr)
        return;
    g.saveState();
    g.reduceClipRegion(
        headerWidth(), rulerHeight, getWidth() - headerWidth() - scrollbarWidth, lanesHeight());
    if (drag_ == Drag::lasso && draggedFar_)
    {
        g.setColour(c.selectionFill);
        g.fillRect(lasso_);
        g.setColour(c.selectionOutline);
        g.drawRect(lasso_, 1);
    }
    if (drag_ == Drag::move && draggedFar_)
    {
        // Ghosts of the selection where it would land, and the snap guide at the
        // dragged clip's new start.
        for (const auto& ref : selection_)
        {
            const int from = project_->trackIndex(ref.track);
            const auto span = spanOf(ref);
            if (from < 0 || !span.has_value())
                continue;
            const int to = std::clamp(from + rowDelta_, 0, static_cast<int>(project_->tracks.size()) - 1);
            const auto bounds =
                spanBounds(std::max<core::SamplePosition>(0, span->start + moveDelta_), span->length, to);
            const auto colour = trackColour(project_->tracks[static_cast<size_t>(from)], from);
            g.setColour(colour.withAlpha(theme_.track.clipBodySelectedAlpha));
            g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
            g.setColour(colour);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
        }
        const auto newStart = std::max<core::SamplePosition>(0, dragOriginal_.start + moveDelta_);
        const int gx = xForSample(newStart);
        g.setColour(c.snapGuide);
        g.fillRect(gx, rulerHeight, 1, lanesHeight());
        // The move in beats of the signature where the clip starts.
        const double beatQuarters =
            core::TempoMap::quartersPerBeat(tempoMap_.signatureAt(dragOriginal_.start));
        const double deltaBeats = (tempoMap_.quartersAt(static_cast<double>(newStart)) -
                                   tempoMap_.quartersAt(static_cast<double>(dragOriginal_.start))) /
                                  beatQuarters;
        drawTooltip(g,
                    positionText(newStart) + "  " + (deltaBeats >= 0 ? "+" : "") +
                        juce::String(deltaBeats, 2) + " beats" +
                        (snapTargetName_.isNotEmpty() ? "  to " + snapTargetName_ : juce::String()),
                    dragPoint_);
    }
    if ((drag_ == Drag::trimLeft || drag_ == Drag::trimRight) && draggedFar_)
    {
        const double beatQuarters = core::TempoMap::quartersPerBeat(tempoMap_.signatureAt(trimStart_));
        const double beats = (tempoMap_.quartersAt(static_cast<double>(trimStart_ + trimLength_)) -
                              tempoMap_.quartersAt(static_cast<double>(trimStart_))) /
                             beatQuarters;
        drawTooltip(g, "Length " + juce::String(beats, 2) + " beats", dragPoint_);
    }
    if (drag_ == Drag::stretch && draggedFar_)
        drawTooltip(
            g,
            "Speed " +
                juce::String(100.0 * static_cast<double>(dragOriginal_.length) /
                                 (dragOriginal_.stretch *
                                  static_cast<double>(std::max<core::SamplePosition>(1, stretchLength_))),
                             1) +
                "%  " + positionText(dragOriginal_.start + stretchLength_),
            dragPoint_);
    if ((drag_ == Drag::fadeIn || drag_ == Drag::fadeOut) && draggedFar_)
        drawTooltip(g,
                    "Fade " + juce::String(static_cast<double>(fadePreview_) / sampleRate(), 2) + " s  " +
                        fadeCurveText(fadeCurvePreview_),
                    dragPoint_);
    if (drag_ == Drag::gain && draggedFar_)
        drawTooltip(g, db::gainText(gainPreview_) + " dB", dragPoint_);
    g.restoreState();
    if ((drag_ == Drag::loopMove || drag_ == Drag::loopStart || drag_ == Drag::loopEnd ||
         drag_ == Drag::region) &&
        draggedFar_)
        drawTooltip(g, positionText(regionStart_) + " - " + positionText(regionEnd_), dragPoint_);
    if (drag_ == Drag::timeSelect && draggedFar_)
        drawTooltip(g, positionText(timeSelStart_) + " - " + positionText(timeSelEnd_), dragPoint_);
}
// ----- markers -------------------------------------------------------------------------------

juce::Rectangle<int> ArrangeView::markerFlagBounds(const core::Marker& marker) const noexcept
{
    const auto position =
        drag_ == Drag::marker && marker.id == dragMarker_.id ? markerPreview_ : marker.position;
    const int x = xForSample(position);
    const int nameWidth = 10 +
                          juce::GlyphArrangement::getStringWidthInt(
                              theme_.caption(), juce::String::fromUTF8(marker.name.c_str())) +
                          6;
    const int width =
        marker.isRegion() ? std::max(nameWidth, xForSample(position + marker.length) - x) : nameWidth;
    return {x, 0, std::max(12, std::min(width, 240)), markersRowHeight};
}
const core::Marker* ArrangeView::markerAtRulerPoint(juce::Point<int> p) const noexcept
{
    if (project_ == nullptr || p.y >= markersRowHeight)
        return nullptr;
    // Later markers draw on top: test them first.
    for (auto it = project_->markers.rbegin(); it != project_->markers.rend(); ++it)
        if (markerFlagBounds(*it).contains(p))
            return &*it;
    return nullptr;
}
void ArrangeView::drawMarkers(juce::Graphics& g, juce::Rectangle<int> row)
{
    if (project_ == nullptr)
        return;
    const auto& c = theme_.colours;
    g.saveState();
    g.reduceClipRegion(
        headerWidth(), row.getY(), getWidth() - headerWidth() - scrollbarWidth, row.getHeight());
    g.setFont(theme_.caption());
    for (size_t i = 0; i < project_->markers.size(); ++i)
    {
        const auto& m = project_->markers[i];
        const auto flag = markerFlagBounds(m);
        if (flag.getRight() < headerWidth() || flag.getX() > getWidth())
            continue;
        const bool selected = m.id == selectedMarker_;
        const bool hovered = m.id == hoverMarker_;
        const auto fill = selected ? c.accent : c.markerFlag;
        if (m.isRegion())
        {
            g.setColour(c.regionFill);
            g.fillRect(flag);
        }
        // The flag: a triangle on a stem, 10 px.
        juce::Path shape;
        const float x = static_cast<float>(flag.getX()) + 0.5f;
        const float top = static_cast<float>(row.getY()) + 2.0f;
        shape.addTriangle(x, top, x + 7.0f, top + 4.0f, x, top + 8.0f);
        shape.addRectangle(x - 0.5f, top, 1.0f, static_cast<float>(row.getHeight()) - 2.0f);
        g.setColour(fill);
        g.fillPath(shape);
        // The name, clipped at the next marker.
        int clipRight = flag.getRight();
        if (!m.isRegion() && i + 1 < project_->markers.size())
            clipRight = std::min(clipRight, markerFlagBounds(project_->markers[i + 1]).getX() - 2);
        g.setColour(selected ? c.accent : hovered ? c.textPrimary : c.textSecondary);
        g.drawText(
            juce::String::fromUTF8(m.name.c_str()),
            juce::Rectangle<int>(
                flag.getX() + 10, row.getY(), std::max(0, clipRight - flag.getX() - 10), row.getHeight()),
            juce::Justification::centredLeft,
            true);
    }
    g.restoreState();
}
void ArrangeView::addMarkerAtPlayhead(bool region)
{
    if (project_ == nullptr)
        return;
    const auto at = playheadForEdits ? playheadForEdits() : playhead_;
    core::Marker marker;
    if (region && regionEnd_ > regionStart_)
    {
        marker.position = regionStart_;
        marker.length = regionEnd_ - regionStart_;
        marker.name = "Region " + std::to_string(project_->markers.size() + 1);
    }
    else
    {
        marker.position = at;
        marker.name = "Marker " + std::to_string(project_->markers.size() + 1);
    }
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::AddMarker>(marker));
    emit(std::move(commands), region ? "Add region" : "Add marker");
}

void ArrangeView::jumpToMarker(int direction)
{
    if (project_ == nullptr || !onLocate)
        return;
    const auto at = playheadForEdits ? playheadForEdits() : playhead_;
    const auto* target = direction < 0 ? project_->markerBefore(at) : project_->markerAfter(at);
    if (target == nullptr)
    {
        if (direction < 0)
            onLocate(0);
        return;
    }
    selectedMarker_ = target->id;
    onLocate(target->position);
    repaint();
}

void ArrangeView::finishMarkerDrag()
{
    if (project_ == nullptr || project_->findMarker(dragMarker_.id) == nullptr ||
        markerPreview_ == dragMarker_.position)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetMarker>(
        dragMarker_.id, markerPreview_, dragMarker_.length, dragMarker_.name));
    emit(std::move(commands), "Move marker");
}

void ArrangeView::renameMarker(core::Id markerId)
{
    const auto* marker = project_ != nullptr ? project_->findMarker(markerId) : nullptr;
    if (marker == nullptr)
        return;
    // An inline field over the name: Enter commits, Escape or focus loss cancels.
    editingMarker_ = markerId;
    markerEditor_ = std::make_unique<juce::TextEditor>();
    markerEditor_->setFont(theme_.caption());
    markerEditor_->setText(juce::String::fromUTF8(marker->name.c_str()), false);
    markerEditor_->setSelectAllWhenFocused(true);
    const auto flag = markerFlagBounds(*marker);
    markerEditor_->setBounds(flag.getX() + 8, 0, std::max(80, flag.getWidth()), markersRowHeight);
    const core::Marker copy = *marker;
    markerEditor_->onReturnKey = [this, copy]
    {
        const auto name = markerEditor_->getText().trim().toStdString();
        markerEditor_.reset();
        editingMarker_ = core::invalidId;
        if (name.empty() || name == copy.name)
            return;
        std::vector<std::unique_ptr<core::Command>> commands;
        commands.push_back(std::make_unique<core::SetMarker>(copy.id, copy.position, copy.length, name));
        emit(std::move(commands), "Rename marker");
    };
    markerEditor_->onEscapeKey = [this]
    {
        markerEditor_.reset();
        editingMarker_ = core::invalidId;
    };
    markerEditor_->onFocusLost = markerEditor_->onEscapeKey;
    addAndMakeVisible(*markerEditor_);
    markerEditor_->grabKeyboardFocus();
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
        if (const auto* marker = markerAtRulerPoint(e.getPosition()))
        {
            renameMarker(marker->id);
            return;
        }
        // The bars row: the loop becomes the clip under the pointer in the selected
        // track (or any track), else the region marker there.
        if (e.y >= markersRowHeight && project_ != nullptr && onRegionChanged)
        {
            const auto at = sampleForX(e.x);
            for (const auto& t : project_->tracks)
            {
                if (selectedTrack_ != core::invalidId && t.id != selectedTrack_)
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
