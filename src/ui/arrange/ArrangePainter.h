#pragma once

// How the arrange view paints (internal to the view: included by ArrangeView.cpp
// and ArrangePainter.cpp only): the lanes and their grid, the clips (through
// ui::clips and ui::paintWaveform), the folder lanes, the recording takes, the
// ruler, the tools' overlays, the playhead and the drop targets. One is made per
// paint, for the area being repainted: rows, clips, waveform columns and labels
// outside it are skipped (a playhead frame repaints a column).

#include "ui/ArrangeView.h"

namespace northlight::ui
{

class ArrangeView::Painter
{
public:
    explicit Painter(ArrangeView& view) noexcept : v(view) {}
    void paint(juce::Graphics& g);

private:
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> area, bool ruler);
    void drawClip(juce::Graphics& g,
                  const core::Track& track,
                  const core::AudioClip& clip,
                  juce::Rectangle<int> lane);
    void drawMidiClip(juce::Graphics& g,
                      const core::Track& track,
                      const core::MidiClip& model,
                      juce::Rectangle<int> lane,
                      bool recording = false);
    void drawLiveTake(juce::Graphics& g,
                      const core::Track& track,
                      const engine::LiveTake& take,
                      juce::Rectangle<int> lane);
    void drawFolderLane(juce::Graphics& g, const core::Track& folder, juce::Rectangle<int> lane);
    void drawToolOverlay(juce::Graphics& g);
    // The clip's waveform over `wave` (one look into the peak store; the view
    // holds the peaks, empty until they are built).
    engine::WaveformView waveformIn(const core::AudioClip& clip, juce::Rectangle<int> wave);

    ArrangeView& v;
    juce::Rectangle<int> paintArea_; // the area being repainted (its clip bounds)
};

} // namespace northlight::ui
