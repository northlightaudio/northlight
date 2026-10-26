#pragma once

// The arrange view's drags, one object per kind (internal to the view: included
// by ArrangeView.cpp and ArrangeGestures.cpp only). The view starts one on a
// press, hands it every drag and the release, and asks it how the clips it
// touches look meanwhile; each keeps its own preview state.

#include "ui/ArrangeView.h"

#include <optional>

namespace northlight::ui
{

class ArrangeView::Gesture
{
public:
    explicit Gesture(ArrangeView& view) noexcept : v(view) {}
    virtual ~Gesture() = default;
    Gesture(const Gesture&) = delete;
    Gesture& operator=(const Gesture&) = delete;

    // The pointer moved (`at`: its timeline position, within the lanes).
    virtual void drag(const juce::MouseEvent& e, core::SamplePosition at) = 0;
    // The button went up: commit or cancel. The view no longer holds the gesture.
    virtual void release(const juce::MouseEvent& e) = 0;

    // Drawn over the lanes (clipped to them), then over everything, then after the
    // playhead.
    virtual void paintLanes(juce::Graphics&) {}
    virtual void paintTop(juce::Graphics&) {}
    virtual void paintLate(juce::Graphics&) {}

    // An audio clip as it shows mid-gesture: true when this gesture changes it
    // (`shown` then holds the preview).
    virtual bool preview(const ClipRef&, const core::AudioClip&, core::AudioClip&) const { return false; }
    // A clip's span as it shows (a MIDI clip's trim).
    virtual bool previewSpan(const ClipRef&, Span&) const { return false; }
    // How strongly a clip draws (moved clips and erased ones dim), and whether it
    // shows muted (the mute sweep).
    virtual float dim(const ClipRef&, bool /*selected*/) const { return 1.0f; }
    virtual std::optional<bool> shownMuted(const ClipRef&) const { return std::nullopt; }

protected:
    ArrangeView& v;
};

// ----- the view ----------------------------------------------------------------------

// The boundary between the headers and the lanes.
class ArrangeView::HeaderResizeGesture final : public Gesture
{
public:
    using Gesture::Gesture;
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent&) override {}
};

// The middle button drags the view.
class ArrangeView::PanGesture final : public Gesture
{
public:
    explicit PanGesture(ArrangeView& view) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent&) override {}

private:
    core::SamplePosition scroll_;
    int vertical_;
};

// A row's bottom edge: that row's height (the layout shows it until release).
class ArrangeView::RowResizeGesture final : public Gesture
{
public:
    RowResizeGesture(ArrangeView& view, int row);
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent&) override;
};

// A swipe or a comp boundary (TakeLanes).
class ArrangeView::TakesGesture final : public Gesture
{
public:
    using Gesture::Gesture;
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent& e) override;
};

// The pencil drawing a new MIDI clip, in whole grid cells from the press.
class ArrangeView::DrawGesture final : public Gesture
{
public:
    DrawGesture(ArrangeView& view, core::Id track, core::SamplePosition at);
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintLate(juce::Graphics& g) override;

private:
    core::Id track_;
    core::SamplePosition anchor_, start_, end_;
};

// ----- the ruler ---------------------------------------------------------------------

// A drag in the bars row: the loop/punch region, or a zoom when the first
// movement is mostly vertical (Logic style); a click locates.
class ArrangeView::RegionGesture final : public Gesture
{
public:
    RegionGesture(ArrangeView& view, core::SamplePosition at) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintTop(juce::Graphics& g) override;

private:
    core::SamplePosition anchor_;
    double startSamplesPerPixel_;
    bool zooming_ = false;
};

// The loop band dragged along, or one of its ends.
class ArrangeView::LoopGesture final : public Gesture
{
public:
    enum class Part
    {
        band,
        start,
        end,
    };
    LoopGesture(ArrangeView& view, Part part, core::SamplePosition at) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintTop(juce::Graphics& g) override;

private:
    Part part_;
    core::SamplePosition anchor_, start_, end_; // the press, and the loop then
};

// Shift+drag in the bars row, or the range tool in a lane (`row`, -1 for the
// ruler): a time range; from a lane, dragged across rows, it selects the tracks
// it spans.
class ArrangeView::TimeSelectGesture final : public Gesture
{
public:
    TimeSelectGesture(ArrangeView& view, core::SamplePosition at, int row);
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintTop(juce::Graphics& g) override;

private:
    core::SamplePosition anchor_;
    int row_;
    std::vector<core::Id> tracksBefore_; // the track selection at the press
};

// A marker's flag dragged along (MarkerController).
class ArrangeView::MarkerGesture final : public Gesture
{
public:
    using Gesture::Gesture;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
};

// ----- the lanes ---------------------------------------------------------------------

// Empty lane space: a drag lassos clips, a click locates (and clears the selection).
class ArrangeView::LassoGesture final : public Gesture
{
public:
    LassoGesture(ArrangeView& view, juce::Point<int> press, core::SamplePosition at) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent& e) override;
    void paintLanes(juce::Graphics& g) override;

private:
    juce::Rectangle<int> lasso_;
    core::SamplePosition anchor_;
};

// What a press on a clip picked up: the clip as it was (its span, and an audio
// clip's state), where the press was on the timeline and its row.
struct ArrangeClipPress
{
    edit::ClipRef clip;
    edit::Span span;
    core::AudioClip audio; // audio clips only
    core::SamplePosition at = 0;
    int row = -1;
};

// A clip's body: the selection moves (across tracks of its kind), snapped; with
// Ctrl held (at the release) a copy goes there instead and the originals stay.
// The automation under the clips goes with them when they stay on their tracks.
class ArrangeView::MoveGesture final : public Gesture
{
public:
    MoveGesture(ArrangeView& view, ArrangeClipPress press, bool copy) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent& e) override;
    void paintLanes(juce::Graphics& g) override;
    float dim(const ClipRef& ref, bool selected) const override;

private:
    ArrangeClipPress press_;
    core::SamplePosition delta_ = 0;
    int rows_ = 0;
    bool copy_;
    bool wasSelected_; // before the press (a Ctrl+click toggles it)
};

// A clip's left or right edge: a trim, snapped (the source stays put).
class ArrangeView::TrimGesture final : public Gesture
{
public:
    TrimGesture(ArrangeView& view, ArrangeClipPress press, bool left) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintLanes(juce::Graphics& g) override;
    bool preview(const ClipRef& ref, const core::AudioClip& model, core::AudioClip& shown) const override;
    bool previewSpan(const ClipRef& ref, Span& span) const override;

private:
    ArrangeClipPress press_;
    bool left_;
    core::SamplePosition start_, length_, sourceOffset_;
};

// Alt+drag of an audio clip's right edge: time-stretch to the new end.
class ArrangeView::StretchGesture final : public Gesture
{
public:
    StretchGesture(ArrangeView& view, ArrangeClipPress press) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintLanes(juce::Graphics& g) override;
    bool preview(const ClipRef& ref, const core::AudioClip& model, core::AudioClip& shown) const override;

private:
    ArrangeClipPress press_;
    core::SamplePosition length_;
};

// A fade handle: sideways its length, up and down its curve.
class ArrangeView::FadeGesture final : public Gesture
{
public:
    FadeGesture(ArrangeView& view, ArrangeClipPress press, bool fadeIn) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition at) override;
    void release(const juce::MouseEvent&) override;
    void paintLanes(juce::Graphics& g) override;
    bool preview(const ClipRef& ref, const core::AudioClip& model, core::AudioClip& shown) const override;
    // The curve for a vertical drag: up bends it fast (bowed up), down slow; a
    // detent at linear.
    static float curveFor(float startCurve, int dy) noexcept;
    static juce::String curveText(float curve);

private:
    ArrangeClipPress press_;
    bool in_;
    core::SamplePosition length_;
    float curve_, startCurve_;
};

// Ctrl+Shift+drag of an audio clip's body up or down: its gain.
class ArrangeView::GainGesture final : public Gesture
{
public:
    GainGesture(ArrangeView& view, ArrangeClipPress press) noexcept;
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent&) override;
    void paintLanes(juce::Graphics& g) override;
    bool preview(const ClipRef& ref, const core::AudioClip& model, core::AudioClip& shown) const override;

private:
    ArrangeClipPress press_;
    float gain_;
};

// The erase or mute tool sweeping over clips; the release removes them, or sets
// them to what the first one got, as one undo step.
class ArrangeView::SweepGesture final : public Gesture
{
public:
    SweepGesture(ArrangeView& view, bool erase) noexcept;
    void over(const ClipRef& ref); // the sweep passed over a clip
    void drag(const juce::MouseEvent& e, core::SamplePosition) override;
    void release(const juce::MouseEvent&) override;
    float dim(const ClipRef& ref, bool selected) const override;
    std::optional<bool> shownMuted(const ClipRef& ref) const override;

private:
    bool swept(const ClipRef& ref) const noexcept;

    bool erase_;
    std::vector<ClipRef> swept_;
    std::optional<bool> mute_; // what the mute tool sets them to (from the first clip)
};

} // namespace northlight::ui
