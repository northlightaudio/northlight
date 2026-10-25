#include "ui/arrange/ArrangePainter.h"

#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/PaintStats.h"
#include "ui/arrange/ArrangeGestures.h"
#include "ui/timeline/ClipPainter.h"
#include "ui/timeline/TimeGrid.h"
#include "ui/timeline/WaveformPainter.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

void ArrangeView::Painter::drawGrid(juce::Graphics& g, juce::Rectangle<int> area, bool ruler)
{
    // Bar lines, beats and sub-beats (ui::forEachGridLine's thresholds); in the
    // ruler the ticks are short and the bars are numbered.
    if (v.project_ == nullptr)
        return;
    const auto columns = area.withLeft(std::max(v.headerWidth(), area.getX()))
                             .withRight(std::min(v.getWidth() - v.scrollbarWidth, area.getRight()));
    if (ruler)
        paintBarsRuler(g, v.theme_, v.tempoMap_, v.view_, columns);
    else
        paintGridLines(g, v.theme_, v.tempoMap_, v.view_, columns);
}

void ArrangeView::Painter::drawRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& c = v.theme_.colours;
    g.setColour(c.surface0);
    g.fillRect(area);
    const auto markersRow = area.removeFromTop(markersRowHeight);
    const auto barsRow = area.withTrimmedBottom(1);
    g.saveState();
    g.reduceClipRegion(v.headerWidth(), 0, v.getWidth() - v.headerWidth() - v.scrollbarWidth, v.rulerHeight);
    drawGrid(g, barsRow, true);

    // The loop band in the bars row (or a faint band for a region that is neither
    // loop nor punch); the punch band in the markers row's lower half.
    if (const auto band = v.loopBand(); !band.isEmpty())
    {
        if (v.loop_ || !v.punch_)
        {
            g.setColour(v.loop_ ? c.loopFill : c.textSecondary.withAlpha(0.12f));
            g.fillRect(band);
            g.setColour(v.loop_ ? c.loopEdge : c.textSecondary.withAlpha(0.5f));
            g.fillRect(band.getX(), band.getY(), 2, band.getHeight());
            g.fillRect(band.getRight() - 2, band.getY(), 2, band.getHeight());
        }
        if (v.punch_)
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
    if (v.timeSelEnd_ > v.timeSelStart_)
    {
        const int x0 = v.xForSample(v.timeSelStart_), x1 = v.xForSample(v.timeSelEnd_);
        g.setColour(c.selectionFill);
        g.fillRect(x0, barsRow.getY(), std::max(1, x1 - x0), barsRow.getHeight());
        g.setColour(c.selectionOutline);
        g.fillRect(x0, barsRow.getY(), 1, barsRow.getHeight());
        g.fillRect(x1, barsRow.getY(), 1, barsRow.getHeight());
    }
    v.markers_.paintFlags(g, markersRow, v.headerWidth(), v.getWidth() - v.scrollbarWidth);
    g.restoreState();
    g.setColour(c.lineSubtle);
    g.fillRect(0, v.rulerOnly - 1, v.getWidth(), 1);
    g.fillRect(v.headerWidth() - 1, 0, 1, v.rulerOnly);
}

void ArrangeView::Painter::drawClip(juce::Graphics& g,
                                    const core::Track& track,
                                    const core::AudioClip& model,
                                    juce::Rectangle<int> lane)
{
    const auto& c = v.theme_.colours;
    // What is drawn: the model clip, or its preview while a drag edits it (a copy
    // only then). While nothing is dragged, a clip outside the area being painted
    // is skipped.
    const ClipRef ref{track.id, model.id};
    core::AudioClip preview;
    const bool editing = v.gesture_ != nullptr && v.gesture_->preview(ref, model, preview);
    if (v.gesture_ == nullptr)
    {
        const int left = v.xForSample(model.start), right = std::max(left + 2, v.xForSample(model.end()));
        if (right < paintArea_.getX() || left > paintArea_.getRight())
            return; // not in the area being painted
    }
    const core::AudioClip& clip = editing ? preview : model;
    paintstats::counters().clipsDrawn++;
    const bool selected = v.isSelected(track.id, model.id);
    const bool hovered = v.hoverClip_ == ref;
    const bool missing = v.mediaMissing(clip.mediaPath);

    const int x0 = v.xForSample(clip.start);
    const int x1 = std::max(x0 + 2, v.xForSample(clip.end()));
    if (x1 < v.headerWidth() || x0 > v.getWidth())
        return;

    const auto block = v.spanBounds(clip.start, clip.length, v.project_->trackIndex(track.id));
    const auto visible =
        block.getIntersection(lane.withLeft(v.headerWidth()).withRight(v.getWidth() - v.scrollbarWidth));
    if (visible.isEmpty())
        return;

    const auto colour = v.trackColour(track, v.project_->trackIndex(track.id));
    const float dim = v.gesture_ != nullptr ? v.gesture_->dim(ref, selected) : 1.0f;
    const bool muted =
        track.mute ||
        (v.gesture_ != nullptr ? v.gesture_->shownMuted(ref).value_or(model.muted) : model.muted);
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? v.theme_.sizes.clipHeader : 0;
    const auto body = block.withTrimmedTop(headerH);

    // Body and header.
    clips::paintBody(
        g,
        v.theme_,
        block,
        headerH,
        missing ? c.surface2
                : colour.withAlpha(
                      (selected ? v.theme_.track.clipBodySelectedAlpha : v.theme_.track.clipBodyAlpha) * dim),
        (missing ? c.warning : colour).withAlpha(v.theme_.track.clipHeaderAlpha * dim));
    if (muted && !missing)
        clips::paintHatch(g, body, visible, colour, dim);

    // Waveform: one min/max column per pixel from the peak cache (decoded samples
    // when zoomed past the finest peak level), at the level the clip plays (its
    // gain through its fades, so a faded edge reads as quiet); a darker centre line.
    if (missing)
    {
        g.setColour(c.warning);
        g.setFont(v.theme_.caption());
        g.drawText("Missing: " + juce::File(clip.mediaPath).getFileName(),
                   body.getIntersection(visible).reduced(v.theme_.spacing.xs + 2, 0),
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
        const auto waveColour = colour.withAlpha(
            (muted ? v.theme_.track.waveformMutedAlpha : v.theme_.track.waveformAlpha) * dim);
        const WaveformStyle style{waveColour, waveColour, colour.withAlpha(0.3f * dim)};
        paintWaveform(g,
                      wave,
                      std::min(2, view.numChannels()),
                      wave.getX(),
                      wave.getRight(),
                      style,
                      [&](int ch, int x)
                      {
                          const auto at = v.sampleForX(x) - clip.start;
                          const auto from = clip.sourceAt(at);
                          const auto to = clip.sourceAt(v.sampleForX(x + 1) - clip.start);
                          const auto r = view.lookup(ch, from, std::max(to, from + 1));
                          return WaveColumn{r.minimum, r.maximum, clipLevelAt(clip, at)};
                      });
    }

    // Fades: the body dimmed under a textPrimary curve; handles on hover or selection.
    // The curve runs from the clip's edge (silent) to full level, shaped as it plays
    // (core::fadeShape); the part of the body above it is dimmed.
    const auto fadeWedge = [&](core::SamplePosition fade, float curve, bool in)
    {
        if (fade <= 0)
            return;
        const float w = static_cast<float>(fade) / static_cast<float>(v.view_.samplesPerPixel);
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
    clips::paintOutline(g,
                        v.theme_,
                        block,
                        missing ? c.warning
                                : colour.withAlpha(selected  ? 1.0f
                                                   : hovered ? 0.7f
                                                             : 0.4f));

    // The name in the header (or over the body when the header is hidden).
    const auto textArea =
        clips::labelArea(block, visible, headerH, fadeHandle + 2 > 6 ? fadeHandle + 2 : 6, 6);
    if ((showHeader || block.getHeight() >= 14) && textArea.intersects(paintArea_))
    {
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
        if (clip.isStretched() && !editing && v.stretchProgress)
            if (const double done = v.stretchProgress(model); done < 1.0)
            {
                label << "  stretching " << juce::String(static_cast<int>(done * 100.0)) << "%";
                v.stretchPending_ = true;
            }
        clips::paintLabel(g, v.theme_, textArea, label, c.textPrimary.withAlpha(dim));
    }
}

engine::WaveformView ArrangeView::Painter::waveformIn(const core::AudioClip& clip, juce::Rectangle<int> wave)
{
    const auto first = clip.sourceAt(v.sampleForX(wave.getX()) - clip.start);
    const auto last = clip.sourceAt(v.sampleForX(wave.getRight()) - clip.start);
    return v.peaks_.viewFor(clip.mediaPath,
                            v.sampleRate(),
                            first,
                            last + 1,
                            static_cast<double>(last - first) / std::max(1, wave.getWidth()));
}

void ArrangeView::Painter::drawFolderLane(juce::Graphics& g,
                                          const core::Track& folder,
                                          juce::Rectangle<int> lane)
{
    // What the folder holds, at a glance: each track's clips as thin bars in its
    // colour, one row of bars per track (at most eight rows), under a caption.
    const auto& c = v.theme_.colours;
    std::vector<int> members;
    for (int t = 0; t < static_cast<int>(v.project_->tracks.size()); ++t)
        if (const auto& track = v.project_->tracks[static_cast<size_t>(t)];
            &track != &folder && v.project_->insideFolder(track.id, folder.id))
            members.push_back(t);
    g.setColour(c.textTertiary);
    g.setFont(v.theme_.caption());
    g.drawText(juce::String(static_cast<int>(members.size())) + (members.size() == 1 ? " track" : " tracks") +
                   (folder.folderOpen ? juce::String() : juce::String("  (closed)")),
               lane.withTrimmedLeft(v.theme_.spacing.sm).withHeight(std::min(lane.getHeight(), 18)),
               juce::Justification::centredLeft);
    const auto bars = lane.withTrimmedTop(std::min(lane.getHeight() / 2, 18)).reduced(0, 3);
    const int shown = std::min(8, static_cast<int>(members.size()));
    if (shown == 0 || bars.getHeight() < 4)
        return;
    const float rowH = static_cast<float>(bars.getHeight()) / static_cast<float>(shown);
    for (int i = 0; i < shown; ++i)
    {
        const auto& track = v.project_->tracks[static_cast<size_t>(members[static_cast<size_t>(i)])];
        g.setColour(
            v.trackColour(track, members[static_cast<size_t>(i)]).withAlpha(track.mute ? 0.3f : 0.75f));
        const float y = static_cast<float>(bars.getY()) + rowH * static_cast<float>(i);
        const float barH = std::max(1.0f, rowH - 1.0f);
        const auto bar = [&](core::SamplePosition start, core::SamplePosition length)
        {
            const int x0 = std::max(lane.getX(), v.xForSample(start));
            const int x1 = std::min(lane.getRight(), v.xForSample(start + length));
            if (x1 > x0)
                g.fillRoundedRectangle(static_cast<float>(x0), y, static_cast<float>(x1 - x0), barH, 1.0f);
        };
        for (const auto& clip : track.clips)
            bar(clip.start, clip.length);
        for (const auto& clip : track.midiClips)
            bar(clip.start, clip.length);
    }
}

void ArrangeView::Painter::drawMidiClip(juce::Graphics& g,
                                        const core::Track& track,
                                        const core::MidiClip& model,
                                        juce::Rectangle<int> lane,
                                        bool recording)
{
    const auto& c = v.theme_.colours;
    const ClipRef ref{track.id, model.id, true};
    // Where it is drawn: the model's span, or the trim preview while it is edited.
    struct
    {
        core::SamplePosition start, length;
        const std::string& name;
        core::SamplePosition end() const noexcept { return start + length; }
    } clip{model.start, model.length, model.name};
    if (Span trimmed; v.gesture_ != nullptr && v.gesture_->previewSpan(ref, trimmed))
    {
        clip.start = trimmed.start;
        clip.length = trimmed.length;
    }
    if (v.gesture_ == nullptr)
    {
        const int left = v.xForSample(clip.start), right = std::max(left + 2, v.xForSample(clip.end()));
        if (right < paintArea_.getX() || left > paintArea_.getRight())
            return; // not in the area being painted
    }
    paintstats::counters().clipsDrawn++;
    const bool selected = v.isSelected(ref);
    const bool hovered = v.hoverClip_ == ref;
    const int x0 = v.xForSample(clip.start);
    const int x1 = std::max(x0 + 2, v.xForSample(clip.end()));
    if (x1 < v.headerWidth() || x0 > v.getWidth())
        return;
    const int row = v.project_->trackIndex(track.id);
    const auto block = v.spanBounds(clip.start, clip.length, row);
    const auto visible =
        block.getIntersection(lane.withLeft(v.headerWidth()).withRight(v.getWidth() - v.scrollbarWidth));
    if (visible.isEmpty())
        return;
    const auto colour = recording ? c.record : v.trackColour(track, row);
    const float dim = v.gesture_ != nullptr ? v.gesture_->dim(ref, selected) : 1.0f;
    const bool muted =
        track.mute ||
        (v.gesture_ != nullptr ? v.gesture_->shownMuted(ref).value_or(model.muted) : model.muted);
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? v.theme_.sizes.clipHeader : 0;
    clips::paintBody(
        g,
        v.theme_,
        block,
        headerH,
        colour.withAlpha((selected ? v.theme_.track.clipBodySelectedAlpha : v.theme_.track.clipBodyAlpha) *
                         dim),
        colour.withAlpha(v.theme_.track.clipHeaderAlpha * dim));
    if (muted)
        clips::paintHatch(g, block.withTrimmedTop(headerH), visible, colour, dim);

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
            const auto nx0 = v.xForSample(std::max(start, clip.start));
            const auto nx1 = v.xForSample(std::min(start + n.length, clip.end()));
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
    clips::paintOutline(g,
                        v.theme_,
                        block,
                        colour.withAlpha(selected               ? 1.0f
                                         : hovered || recording ? 0.7f
                                                                : 0.4f));
    const auto textArea = clips::labelArea(block, visible, headerH, 6, 6);
    if ((showHeader || block.getHeight() >= 14) && textArea.intersects(paintArea_))
    {
        const auto name = recording ? juce::String::fromUTF8("\xe2\x97\x8f Recording")
                                    : juce::String::fromUTF8(clip.name.c_str());
        clips::paintLabel(g,
                          v.theme_,
                          textArea,
                          name + "  " + juce::String(model.notes.size()) + " notes",
                          c.textPrimary.withAlpha(dim));
    }
}

// A take being recorded (screens/arrange.md "Recording clip"): a clip in `record` at
// the body alpha, growing with the file, its waveform from the live peaks and
// the header "● Recording". It sits where its clip will land (latency-compensated),
// so nothing jumps when the file closes and the clip takes its place.
void ArrangeView::Painter::drawLiveTake(juce::Graphics& g,
                                        const core::Track& track,
                                        const engine::LiveTake& take,
                                        juce::Rectangle<int> lane)
{
    const auto& c = v.theme_.colours;
    if (take.samplesWritten <= 0 || take.startPosition < 0)
        return;
    const auto placement = engine::placeRecording(take.startPosition, take.samplesWritten, v.live_.latency);
    if (placement.length <= 0)
        return;
    const int row = v.project_->trackIndex(track.id);
    const auto block = v.spanBounds(placement.start, placement.length, row);
    const auto visible =
        block.getIntersection(lane.withLeft(v.headerWidth()).withRight(v.getWidth() - v.scrollbarWidth));
    if (visible.isEmpty())
        return;
    const auto colour = c.record;
    const bool showHeader = lane.getHeight() >= 24;
    const int headerH = showHeader ? v.theme_.sizes.clipHeader : 0;
    const auto body = block.withTrimmedTop(headerH);
    clips::paintBody(g,
                     v.theme_,
                     block,
                     headerH,
                     colour.withAlpha(v.theme_.track.clipBodyAlpha),
                     colour.withAlpha(v.theme_.track.clipHeaderAlpha));

    // Waveform: one min/max column per pixel from the peaks written so far; the
    // unwritten tail (at most a bin and the writer's lag) reads as silence.
    const auto wave = body.getIntersection(visible);
    const auto painted = wave.getIntersection(paintArea_);
    const WaveformStyle style{colour.withAlpha(v.theme_.track.waveformAlpha),
                              colour.withAlpha(v.theme_.track.waveformAlpha),
                              colour.withAlpha(0.3f)};
    paintWaveform(g,
                  wave,
                  1,
                  painted.getX(),
                  painted.getRight(),
                  style,
                  [&](int, int x)
                  {
                      const auto from = placement.sourceOffset + (v.sampleForX(x) - placement.start);
                      const auto to = placement.sourceOffset + (v.sampleForX(x + 1) - placement.start);
                      WaveColumn column;
                      take.lookup(from, std::max(to, from + 1), column.minimum, column.maximum);
                      return column;
                  });

    clips::paintOutline(g, v.theme_, block, colour.withAlpha(0.7f));
    if (showHeader || block.getHeight() >= 14)
    {
        juce::String label = juce::String::fromUTF8("\xe2\x97\x8f Recording");
        if (take.takeIndex > 0)
            label << "  take " << (take.takeIndex + 1);
        clips::paintLabel(g, v.theme_, clips::labelArea(block, visible, headerH, 6, 6), label, c.textPrimary);
    }
}

void ArrangeView::Painter::paint(juce::Graphics& g)
{
    const FrameStats::Scope timing;
    const auto& c = v.theme_.colours;
    g.fillAll(c.canvas);
    paintArea_ = g.getClipBounds();
    paintstats::counters().arrangePaints++;
    const SelectionIndex index(v); // every clip drawn asks whether it is selected

    const auto lanesArea = juce::Rectangle<int>(
        v.headerWidth(), v.rulerHeight, v.getWidth() - v.headerWidth() - v.scrollbarWidth, v.lanesHeight());
    int y = v.rulerHeight - v.verticalScroll_;
    if (v.project_ != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(lanesArea);
        // Lane backgrounds, then the grid under the clips.
        int row = 0;
        for (const auto& track : v.project_->tracks)
        {
            const int h = v.rowHeightOf(row);
            y = v.rowTop(row);
            const juce::Rectangle<int> lane(v.headerWidth(), y, lanesArea.getWidth(), h);
            if (h > 0 && lane.intersects(paintArea_))
            {
                if (track.id == v.selectedTrack())
                    g.setColour(c.laneSelected);
                else if (row == v.hoverRow_)
                    g.setColour(c.laneHover);
                else
                    g.setColour(row % 2 == 1 ? c.laneAlt : c.canvas);
                g.fillRect(lane);
                if (track.armed)
                {
                    g.setColour(c.record.withAlpha(v.recording_ ? 0.12f : 0.05f));
                    g.fillRect(lane);
                }
                g.setColour(c.lineSubtle);
                g.fillRect(lane.withTrimmedTop(h - 1));
            }
            y += h;
            ++row;
        }
        // Buses and the master: a lane with no clips, saying who feeds it.
        for (const auto* fx : v.busRows())
        {
            const int h = v.rowHeightOf(row);
            y = v.rowTop(row);
            const juce::Rectangle<int> lane(v.headerWidth(), y, lanesArea.getWidth(), h);
            const auto caption = lane.reduced(v.theme_.spacing.sm, 0).withHeight(std::min(h, 24));
            if (lane.intersects(paintArea_))
            {
                g.setColour(fx->id == v.selectedTrack() ? c.laneSelected
                                                        : (row % 2 == 1 ? c.laneAlt : c.canvas));
                g.fillRect(lane);
                g.setColour(c.lineSubtle);
                g.fillRect(lane.withTrimmedTop(h - 1));
            }
            if (caption.intersects(paintArea_))
            {
                // Who feeds it: sends into an FX track, outputs into a group bus.
                const auto* bus = v.project_->findBus(fx->id);
                int feeders = 0;
                const auto counts = [&](const core::Channel& ch)
                {
                    if (bus != nullptr && bus->fx)
                        for (const auto& send : ch.sends)
                            feeders += send.bus == fx->id ? 1 : 0;
                    else if (bus != nullptr)
                        feeders += ch.outputBus == fx->id ? 1 : 0;
                };
                for (const auto& t : v.project_->tracks)
                    counts(t);
                for (const auto& b : v.project_->buses)
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
                g.setFont(v.theme_.caption());
                g.drawText(text, caption, juce::Justification::centredLeft);
            }
            y += h;
            ++row;
        }
        v.automation_.paintBackgrounds(g); // the lanes' rows, under the grid
        drawGrid(g, lanesArea.getIntersection(paintArea_), false);

        // Loop and punch through the lanes at half the ruler's alpha; the time selection.
        if (v.regionEnd_ > v.regionStart_ && (v.loop_ || v.punch_))
        {
            const int x0 = v.xForSample(v.regionStart_), x1 = v.xForSample(v.regionEnd_);
            g.setColour((v.punch_ ? c.punchFill : c.loopFill).withMultipliedAlpha(0.5f));
            g.fillRect(x0, lanesArea.getY(), std::max(1, x1 - x0), lanesArea.getHeight());
        }
        if (v.timeSelEnd_ > v.timeSelStart_)
        {
            const int x0 = v.xForSample(v.timeSelStart_), x1 = v.xForSample(v.timeSelEnd_);
            g.setColour(c.selectionFill);
            for (size_t t = 0; t < v.project_->tracks.size(); ++t)
                if (std::find(v.timeSelTracks_.begin(), v.timeSelTracks_.end(), v.project_->tracks[t].id) !=
                    v.timeSelTracks_.end())
                    g.fillRect(v.rowBounds(static_cast<int>(t)).withLeft(x0).withRight(x1));
        }

        y = v.rulerHeight - v.verticalScroll_;
        row = 0;
        for (const auto& track : v.project_->tracks)
        {
            y = v.rowTop(row);
            const int h = v.rowHeightOf(row++);
            const juce::Rectangle<int> lane(v.headerWidth(), y, lanesArea.getWidth(), h);
            const bool shown = h > 0 && lane.intersects(paintArea_);
            if (shown && track.isFolder())
                drawFolderLane(g, track, lane);
            if (shown)
            {
                for (const auto& clip : track.clips)
                    drawClip(g, track, clip, lane);
                for (const auto& clip : track.midiClips)
                    drawMidiClip(g, track, clip, lane);
                for (const auto& take : v.live_.takes)
                    if (take.trackId == track.id)
                        drawLiveTake(g, track, take, lane);
                for (const auto& take : v.live_.midi)
                    if (take.trackId == track.id)
                        drawMidiClip(g, track, take.clip, lane, true);
                if (track.isFrozen())
                {
                    // Frozen: the clips are what was rendered, shown under a veil.
                    g.setColour(c.canvas.withAlpha(0.45f));
                    g.fillRect(lane);
                    g.setColour(c.info);
                    g.setFont(v.theme_.captionCaps());
                    g.drawText(juce::String::fromUTF8("\xe2\x9d\x84 FROZEN"),
                               lane.withTrimmedLeft(v.theme_.spacing.sm).withHeight(std::min(h, 20)),
                               juce::Justification::centredLeft);
                }
                if (track.isAux() && lane.withTrimmedLeft(v.theme_.spacing.sm).intersects(paintArea_))
                {
                    const auto* source = v.project_->findTrack(track.auxTrack);
                    g.setColour(c.textTertiary);
                    g.setFont(v.theme_.caption());
                    g.drawText("Output " + juce::String(2 * track.auxOutput + 1) + "/" +
                                   juce::String(2 * track.auxOutput + 2) + " of " +
                                   (source != nullptr ? juce::String::fromUTF8(source->name.c_str())
                                                      : juce::String("a removed instrument")),
                               lane.withTrimmedLeft(v.theme_.spacing.sm),
                               juce::Justification::centredLeft);
                }
                if (track.isInstrument() && lane.withTrimmedLeft(v.theme_.spacing.sm).intersects(paintArea_))
                {
                    // An instrument channel: what it is and who plays it.
                    juce::String text = "Instrument channel";
                    if (track.hasInstrument())
                        text << ": "
                             << juce::String::fromUTF8(v.displayName_
                                                           ? v.displayName_(track.instrument.type).c_str()
                                                           : track.instrument.type.c_str());
                    juce::String players;
                    for (const auto* feed : v.project_->midiFeeds(track.id))
                        players << (players.isEmpty() ? "" : ", ")
                                << juce::String::fromUTF8(feed->name.c_str());
                    text << juce::String::fromUTF8("  \xe2\x86\x90  ")
                         << (players.isNotEmpty() ? players : juce::String("no MIDI track plays it yet"));
                    g.setColour(c.textTertiary);
                    g.setFont(v.theme_.caption());
                    g.drawText(
                        text, lane.withTrimmedLeft(v.theme_.spacing.sm), juce::Justification::centredLeft);
                }
            }
            // The comp's badges, chip and boundaries over the row; its take lanes under it.
            if (!track.takeGroups.empty())
            {
                const auto takeArea = v.takeRow(row - 1);
                v.takes_.paintMain(g, takeArea);
                v.takes_.paintLanes(g, takeArea);
            }
            y += h;
        }
        g.restoreState();

        // The take lanes' headers, in the header column under each expanded track's header.
        g.saveState();
        g.reduceClipRegion(0, v.rulerHeight, v.headerWidth(), v.lanesHeight());
        for (size_t t = 0; t < v.project_->tracks.size(); ++t)
            if (v.takes_.laneCount(v.project_->tracks[t]) > 0)
                v.takes_.paintLaneHeaders(g, v.takeRow(static_cast<int>(t)));
        g.restoreState();
    }

    if (v.project_ == nullptr || v.project_->tracks.empty())
    {
        // The empty state: the mark, "Start a project", the buttons (components) and a hint.
        const auto area = v.getLocalBounds().withTrimmedTop(v.rulerHeight);
        const int markSize = v.theme_.spacing.xxxl;
        const auto mark = juce::Rectangle<float>(static_cast<float>(markSize), static_cast<float>(markSize))
                              .withCentre({static_cast<float>(area.getCentreX()),
                                           static_cast<float>(area.getCentreY() - v.theme_.spacing.xxxl -
                                                              v.theme_.spacing.xl)});
        Icons::draw(g, Icon::mark, mark, c.textTertiary);
        g.setColour(c.textPrimary);
        g.setFont(v.theme_.heading());
        g.drawText(
            "Start a project",
            area.withSizeKeepingCentre(area.getWidth(), static_cast<int>(v.theme_.fonts.size.heading * 2))
                .translated(0, -v.theme_.spacing.lg),
            juce::Justification::centred);
        g.setColour(c.textTertiary);
        g.setFont(v.theme_.small());
        g.drawText(
            "or drop audio files here",
            area.withSizeKeepingCentre(area.getWidth(), static_cast<int>(v.theme_.fonts.size.small * 2))
                .translated(0, v.theme_.spacing.xl + v.theme_.sizes.control + v.theme_.spacing.lg),
            juce::Justification::centred);
    }

    // Marker lines through the lanes (the flags sit in the ruler).
    v.markers_.paintLines(g, lanesArea);

    drawRuler(g, v.getLocalBounds().removeFromTop(v.rulerOnly));
    // What a drag shows: over the lanes, then over everything (its tag).
    if (v.gesture_ != nullptr && v.project_ != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(v.lanesBounds());
        v.gesture_->paintLanes(g);
        g.restoreState();
        v.gesture_->paintTop(g);
    }
    drawToolOverlay(g);

    // The playhead: a 1 px line with a 7 px head in the ruler.
    const int px = v.xForSample(v.playhead_);
    if (px >= v.headerWidth() && px < v.getWidth() - v.scrollbarWidth)
    {
        g.setColour(v.recording_ ? c.playheadRecording : c.playhead);
        g.fillRect(px, 0, 1, v.getHeight() - v.scrollbarHeight);
        juce::Path head;
        head.addTriangle(static_cast<float>(px) - 3.5f,
                         static_cast<float>(v.rulerOnly - 8),
                         static_cast<float>(px) + 4.5f,
                         static_cast<float>(v.rulerOnly - 8),
                         static_cast<float>(px) + 0.5f,
                         static_cast<float>(v.rulerOnly - 1));
        g.fillPath(head);
    }

    // The reorder insertion line over the headers.
    if (v.reorderFrom_ != core::invalidId && v.reorderTo_ >= 0)
    {
        const int ly = v.rowTop(v.reorderTo_);
        g.setColour(c.accent);
        g.fillRect(0, ly - 1, v.headerWidth(), 2);
    }

    // What a drag draws last (the pencil's clip).
    if (v.gesture_ != nullptr)
        v.gesture_->paintLate(g);

    // Files dragged over: the lane lights up and a ghost clip shows where they land.
    if (v.dragOver_)
    {
        const int row = v.rowAtY(v.dropPoint_.y);
        g.saveState();
        g.reduceClipRegion(lanesArea);
        if (row >= 0)
        {
            g.setColour(c.dropTarget);
            g.fillRect(v.rowBounds(row).withLeft(v.headerWidth()));
        }
        const int x = v.xForSample(v.snapped(v.sampleForX(std::max(v.headerWidth(), v.dropPoint_.x))));
        const int ghostY = row >= 0 ? v.rowBounds(row).getY() + 2
                                    : std::max(v.rulerHeight, v.dropPoint_.y - v.rowHeight / 2);
        const auto ghost = juce::Rectangle<int>(x, ghostY, 160, v.rowHeight - 4);
        g.setColour(c.accent);
        g.drawRoundedRectangle(ghost.toFloat().reduced(0.5f), v.theme_.radius.sm, 1.0f);
        g.setFont(v.theme_.caption());
        g.setColour(c.textPrimary);
        g.drawText(v.dropFiles_.isEmpty()
                       ? juce::String()
                       : juce::File(v.dropFiles_[0]).getFileName() +
                             (v.dropFiles_.size() > 1 ? " +" + juce::String(v.dropFiles_.size() - 1)
                                                      : juce::String()),
                   ghost.reduced(6, 0),
                   juce::Justification::centredLeft,
                   true);
        g.restoreState();
        if (row < 0 && v.project_ != nullptr && !v.project_->tracks.empty())
        {
            g.setColour(c.textTertiary);
            g.setFont(v.theme_.caption());
            g.drawText(v.dropFiles_.size() == 1 ? "New track"
                                                : juce::String(v.dropFiles_.size()) + " new tracks",
                       juce::Rectangle<int>(0, ghostY, v.headerWidth(), v.rowHeight - 4)
                           .reduced(v.theme_.spacing.sm, 0),
                       juce::Justification::centredLeft);
        }
    }

    // A plugin dragged over a track lights its row.
    if (v.pluginDropRow_ >= 0)
    {
        g.setColour(c.dropTarget);
        g.fillRect(v.rowBounds(v.pluginDropRow_).withRight(v.getWidth() - v.scrollbarWidth));
    }

    // The corner over the headers.
    g.setColour(c.surface0);
    g.fillRect(0, 0, v.headerWidth(), v.rulerOnly);
    g.setColour(c.lineSubtle);
    g.fillRect(0, v.rulerOnly - 1, v.headerWidth(), 1);
    g.fillRect(v.headerWidth() - 1, 0, 1, v.rulerOnly);
    // (The selection count is in the status bar: the corner is the tools'.)
}

void ArrangeView::Painter::drawToolOverlay(juce::Graphics& g)
{
    const auto& c = v.theme_.colours;
    if (v.project_ == nullptr)
        return;
    const auto lanesArea = juce::Rectangle<int>(
        v.headerWidth(), v.rulerHeight, v.getWidth() - v.headerWidth() - v.scrollbarWidth, v.lanesHeight());
    // The split line: through the clip under the pointer, or every lane with Alt.
    if (v.tool_ == Tool::split && v.splitAt_ >= 0 && v.gesture_ == nullptr)
    {
        const int x = v.xForSample(v.splitAt_);
        const auto line = v.splitRow_ >= 0
                              ? v.spanBounds(v.splitClip_.start, v.splitClip_.length, v.splitRow_)
                              : juce::Rectangle<int>(x, v.rulerHeight, 1, v.lanesHeight());
        g.saveState();
        g.reduceClipRegion(lanesArea);
        g.setColour(c.accent);
        g.fillRect(x, line.getY(), 1, line.getHeight());
        g.restoreState();
        v.drawTooltip(g,
                      (v.splitAll_ ? "Split all at " : "Split at ") + v.positionText(v.splitAt_),
                      {x, v.splitPoint_.y});
        return;
    }
    if (v.gesture_ != nullptr || v.hoverClip_.clip == core::invalidId)
        return;
    const int row = v.project_->trackIndex(v.hoverClip_.track);
    const auto span = v.spanOf(v.hoverClip_);
    if (row < 0 || !span)
        return;
    const auto block = v.spanBounds(span->start, span->length, row);
    g.saveState();
    g.reduceClipRegion(lanesArea);
    if (v.tool_ == Tool::erase)
    {
        // The clip the eraser would take, outlined in danger.
        g.setColour(c.danger);
        g.drawRoundedRectangle(block.toFloat().reduced(0.5f), v.theme_.radius.sm, 1.0f);
    }
    else if (v.tool_ == Tool::glue)
    {
        // What the click would make: one outline round both clips, the seam marked.
        if (edit::canJoinWithNext(*v.project_, v.hoverClip_))
        {
            const auto next = v.spanOf(*edit::nextClip(*v.project_, v.hoverClip_));
            const auto joined =
                v.spanBounds(span->start, std::max(span->end(), next->end()) - span->start, row);
            g.setColour(c.accent);
            g.drawRoundedRectangle(joined.toFloat().reduced(0.5f), v.theme_.radius.sm, 1.0f);
            for (const auto seam : {span->end(), next->start})
                g.fillRect(v.xForSample(seam) - 1, joined.getY(), 2, joined.getHeight());
        }
    }
    g.restoreState();
    // A clip the glue cannot join says why, beside it.
    if (v.tool_ == Tool::glue && !edit::canJoinWithNext(*v.project_, v.hoverClip_))
        v.drawTooltip(g,
                      edit::nextClip(*v.project_, v.hoverClip_) ? "Joins only a clip that continues this one"
                                                                : "No clip after this one to join",
                      {std::min(block.getRight(), lanesArea.getRight()) - 10, block.getY() - 14});
}

} // namespace northlight::ui
