// Aurora's editor (screens/plugins.md 7.4b). Aurora reads as its own product, so it
// has its own face: a night sky with the aurora drifting through it (brighter
// as it plays) behind a wordmark, the presets and twelve voice lights, and six
// glass panels: oscillators, unison, filter, envelopes, LFO and output. Every
// panel has a display to read and drag and its knobs under it, in the panel's
// hue. Colours are the theme's palette, so the face follows the theme.

#include "ui/strip/AuroraEditor.h"

#include "engine/Gain.h"
#include "plugins/Aurora.h"
#include "plugins/Biquad.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <array>
#include <cmath>

namespace northlight::ui::strip
{

namespace
{
namespace au = plugins::aurora;

// The panels' hues, from the track palette.
struct Hues
{
    juce::Colour osc, unison, filter, envelope, lfo, output;
    explicit Hues(const Theme& t)
        : osc(paletteColour(t, "Sea grass")), unison(paletteColour(t, "Lichen")),
          filter(paletteColour(t, "Overcast")), envelope(paletteColour(t, "Heather")),
          lfo(paletteColour(t, "Thrift")), output(paletteColour(t, "Gorse"))
    {
    }
};

// ----- displays -------------------------------------------------------------------------------------

// The two oscillators' cycles, side by side; drag across either to morph it.
class OscillatorDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    OscillatorDisplay(ModuleCard& card, juce::Colour one, juce::Colour two)
        : card_(card), one_(one), two_(two)
    {
        setTooltip("Drag across a wave to morph it: sine, triangle, saw, square");
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        auto halves = b.reduced(6.0f, 8.0f);
        const auto left = halves.removeFromLeft(halves.getWidth() * 0.5f).reduced(3.0f, 0.0f);
        const auto right = halves.reduced(3.0f, 0.0f);
        wave(g, left, card_.param(au::osc1Shape), one_, card_.param(au::osc1Level), "1");
        wave(g, right, card_.param(au::osc2Shape), two_, card_.param(au::osc2Level), "2");
        drawWellFrame(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        param_ = e.position.x < getWidth() * 0.5f ? au::osc1Shape : au::osc2Shape;
        start_ = card_.param(param_);
        card_.beginGesture("Shape");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const float span = getWidth() * 0.5f;
        card_.setParam(param_,
                       juce::jlimit(0.0f, 3.0f, start_ + e.getDistanceFromDragStartX() / span * 3.0f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    void wave(juce::Graphics& g,
              juce::Rectangle<float> r,
              float shape,
              juce::Colour hue,
              float level,
              const char* label) const
    {
        const auto& theme = card_.theme();
        g.setColour(theme.colours.lineSubtle);
        g.drawHorizontalLine(static_cast<int>(r.getCentreY()), r.getX(), r.getRight());
        juce::Path path;
        const int n = std::max(2, static_cast<int>(r.getWidth()));
        const float amp = r.getHeight() * 0.45f * (0.35f + 0.65f * level);
        for (int i = 0; i <= n; ++i)
        {
            const float phase = static_cast<float>(i) / static_cast<float>(n);
            const float y = r.getCentreY() - au::shapeAt(shape, phase) * amp;
            const float x = r.getX() + phase * r.getWidth();
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        // A glow under the line, then the line.
        g.setColour(hue.withAlpha(0.25f));
        g.strokePath(path,
                     juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(hue);
        g.strokePath(path,
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        static const char* names[] = {"SINE", "TRI", "SAW", "SQUARE"};
        const int i = juce::jlimit(0, 3, static_cast<int>(std::lround(shape)));
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.caption - 2.0f, theme.fonts.weight.semibold));
        g.setColour(theme.colours.textTertiary);
        g.drawText(juce::String(label) + "  " + names[i],
                   r.withHeight(10.0f).translated(0.0f, -6.0f),
                   juce::Justification::centredLeft);
    }

    ModuleCard& card_;
    juce::Colour one_, two_;
    int param_ = au::osc1Shape;
    float start_ = 0.0f;
};

// The unison stack: a fan of the voices from a common point, as far apart as
// the spread detunes them and coloured by where the width pans them. Drag up
// and down for the spread.
class UnisonDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    UnisonDisplay(ModuleCard& card, juce::Colour hue, juce::Colour left, juce::Colour right)
        : card_(card), hue_(hue), left_(left), right_(right)
    {
        setTooltip("Drag up and down for the spread");
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(10.0f, 10.0f);
        const int voices =
            juce::jlimit(1, au::maxUnison, static_cast<int>(std::lround(card_.param(au::unison))));
        const float spread = card_.param(au::spread) / 50.0f;
        const float width = card_.param(au::width) * 0.01f;
        const juce::Point<float> origin{p.getX(), p.getCentreY()};
        for (int u = 0; u < voices; ++u)
        {
            const float pos =
                voices == 1 ? 0.0f : static_cast<float>(u) / static_cast<float>(voices - 1) * 2.0f - 1.0f;
            const juce::Point<float> end{p.getRight(), p.getCentreY() + pos * spread * p.getHeight() * 0.5f};
            const auto colour = left_.interpolatedWith(right_, 0.5f + 0.5f * pos * width);
            g.setColour(colour.withAlpha(0.25f));
            g.drawLine({origin, end}, 4.0f);
            g.setColour(colour);
            g.drawLine({origin, end}, 1.5f);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(end));
        }
        g.setColour(theme.colours.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(origin));
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText(juce::String(voices) + (voices == 1 ? " voice" : " voices"),
                   b.reduced(8.0f, 5.0f).removeFromTop(12.0f),
                   juce::Justification::centredRight);
        drawWellFrame(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        start_ = card_.param(au::spread);
        card_.beginGesture("Spread");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        card_.setParam(au::spread, juce::jlimit(0.0f, 50.0f, start_ - e.getDistanceFromDragStartY() * 0.4f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    juce::Colour hue_, left_, right_;
    float start_ = 0.0f;
};

// The filter's response at the mode, cutoff and resonance; drag across for the
// cutoff, up for the resonance. The mode is a list in the corner.
class FilterDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    FilterDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue), mode_(card.theme(), hue)
    {
        setTooltip("Drag across for the cutoff, up and down for the resonance");
        mode_.choices = {"LP12", "LP24", "BP", "HP"};
        mode_.setTooltip("Filter mode");
        mode_.onChoose = [this](int m)
        {
            card_.beginGesture("Filter mode");
            card_.setParam(au::filterMode, static_cast<float>(m));
            card_.endGesture();
        };
        addAndMakeVisible(mode_);
    }
    void resized() override
    {
        mode_.setBounds(
            getLocalBounds().reduced(5).removeFromTop(16).removeFromRight(mode_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        mode_.selected = static_cast<int>(std::lround(card_.param(au::filterMode)));
        const auto p = plot();
        const double rate = card_.sampleRate();
        const double fc = std::min<double>(card_.param(au::cutoff), rate * 0.45);
        const double q = 1.0 / (2.0 - 1.96 * card_.param(au::resonance));
        plugins::Biquad one, two;
        const int mode = mode_.selected;
        if (mode <= 1)
            one.setLowPass(rate, fc, q);
        else if (mode == 2)
            one.setBandPass(rate, fc, q);
        else
            one.setHighPass(rate, fc, q);
        two = one;
        juce::Path curve, area;
        const int n = std::max(2, static_cast<int>(p.getWidth()));
        for (int i = 0; i < n; ++i)
        {
            const float x = p.getX() + static_cast<float>(i);
            const double hz = std::min(xToHz(x, p), rate * 0.499);
            double m = one.magnitude(hz, rate);
            if (mode == 1)
                m *= two.magnitude(hz, rate);
            const float db = static_cast<float>(engine::gainToDb(m, engine::magnitudeFloorDb));
            const float y = dbToY(juce::jlimit(bottomDb - 4.0f, topDb, db), topDb, bottomDb, p);
            if (i == 0)
            {
                curve.startNewSubPath(x, y);
                area.startNewSubPath(x, p.getBottom());
            }
            else
                curve.lineTo(x, y);
            area.lineTo(x, y);
        }
        area.lineTo(p.getRight(), p.getBottom());
        area.closeSubPath();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            g.setGradientFill(juce::ColourGradient(
                hue_.withAlpha(0.35f), 0.0f, p.getY(), hue_.withAlpha(0.02f), 0.0f, p.getBottom(), false));
            g.fillPath(area);
            g.setColour(hue_.withAlpha(0.3f));
            g.strokePath(
                curve,
                juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(hue_);
            g.strokePath(
                curve,
                juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        drawWellFrame(g, theme, b);
        drawHandle(g, theme, hue_, handle(), dragging_);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = true;
        card_.beginGesture("Filter");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto p = plot();
        card_.setParam(au::cutoff, static_cast<float>(xToHz(e.position.x, p)));
        const float up = juce::jlimit(0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight());
        card_.setParam(au::resonance, juce::jlimit(0.0f, 1.0f, (up - 0.35f) / 0.65f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        dragging_ = false;
        card_.endGesture();
        repaint();
    }

private:
    static constexpr float topDb = 24.0f, bottomDb = -36.0f;
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(2.0f).withTrimmedTop(16.0f);
    }
    juce::Point<float> handle() const
    {
        const auto p = plot();
        const float up = 0.35f + 0.65f * card_.param(au::resonance);
        return {hzToX(card_.param(au::cutoff), p), p.getBottom() - up * p.getHeight()};
    }

    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice mode_;
    bool dragging_ = false;
};

// Both envelopes: the amp envelope filled, the filter envelope as a dashed
// line. The one chosen in the corner has the handles: the peak (attack), the
// sustain point (decay across, level up) and the end (release). Each stage has
// a quarter of the width, its time on a log scale in it.
class EnvelopeDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    EnvelopeDisplay(ModuleCard& card, juce::Colour amp, juce::Colour filter)
        : card_(card), ampHue_(amp), filterHue_(filter), which_(card.theme(), amp)
    {
        setTooltip("Drag the points: attack, decay and sustain, release");
        which_.choices = {"AMP", "FILTER"};
        which_.setTooltip("The envelope the handles edit");
        which_.onChoose = [this](int i)
        {
            which_.selected = i;
            repaint();
        };
        addAndMakeVisible(which_);
    }
    void resized() override
    {
        which_.setBounds(
            getLocalBounds().reduced(5).removeFromTop(16).removeFromRight(which_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = plot();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            for (int i = 1; i < 4; ++i)
            {
                g.setColour(theme.colours.gridBeat);
                g.drawVerticalLine(static_cast<int>(p.getX() + p.getWidth() * static_cast<float>(i) / 4.0f),
                                   p.getY(),
                                   p.getBottom());
            }
            const auto amp = points(au::ampAttack);
            const auto filter = points(au::filterAttack);
            juce::Path a = path(amp), f = path(filter);
            juce::Path area = a;
            area.lineTo(amp.back().x, p.getBottom());
            area.closeSubPath();
            g.setGradientFill(juce::ColourGradient(ampHue_.withAlpha(0.35f),
                                                   0.0f,
                                                   p.getY(),
                                                   ampHue_.withAlpha(0.03f),
                                                   0.0f,
                                                   p.getBottom(),
                                                   false));
            g.fillPath(area);
            g.setColour(ampHue_);
            g.strokePath(
                a, juce::PathStrokeType(1.8f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            juce::Path dashed;
            const float dashes[] = {4.0f, 3.0f};
            juce::PathStrokeType(1.5f).createDashedStroke(dashed, f, dashes, 2);
            g.setColour(filterHue_);
            g.fillPath(dashed);
        }
        drawWellFrame(g, theme, b);
        const int first = which_.selected == 0 ? au::ampAttack : au::filterAttack;
        const auto hue = which_.selected == 0 ? ampHue_ : filterHue_;
        const auto pts = points(first);
        drawHandle(g, theme, hue, pts[0], drag_ == 0);
        drawHandle(g, theme, hue, pts[1], drag_ == 1);
        drawHandle(g, theme, hue, pts[3], drag_ == 3);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int first = which_.selected == 0 ? au::ampAttack : au::filterAttack;
        const auto pts = points(first);
        drag_ = 3;
        float best = 1.0e9f;
        for (int i : {0, 1, 3})
            if (const float d = pts[static_cast<size_t>(i)].getDistanceFrom(e.position); d < best)
            {
                best = d;
                drag_ = i;
            }
        card_.beginGesture(drag_ == 0 ? "Attack" : drag_ == 1 ? "Decay" : "Release");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int first = which_.selected == 0 ? au::ampAttack : au::filterAttack;
        const auto p = plot();
        const float quarter = p.getWidth() / 4.0f;
        const auto msIn = [&](int stage, float x, float maxMs)
        {
            const float f =
                juce::jlimit(0.0f, 1.0f, (x - (p.getX() + quarter * static_cast<float>(stage))) / quarter);
            return std::pow(maxMs, f);
        };
        if (drag_ == 0)
            card_.setParam(first, msIn(0, e.position.x, maxAttack));
        else if (drag_ == 1)
        {
            card_.setParam(first + 1, msIn(1, e.position.x, maxDecay));
            card_.setParam(first + 2,
                           juce::jlimit(0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight()));
        }
        else if (drag_ == 3)
            card_.setParam(first + 3, msIn(3, e.position.x, maxRelease));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag_ >= 0)
            card_.endGesture();
        drag_ = -1;
        repaint();
    }

private:
    static constexpr float maxAttack = 5000.0f, maxDecay = 8000.0f, maxRelease = 10000.0f;
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedTop(16.0f);
    }
    // The peak, the sustain start, the sustain end and the release end of the
    // envelope whose attack is parameter `first` (decay, sustain, release follow).
    std::array<juce::Point<float>, 4> points(int first) const
    {
        const auto p = plot();
        const float quarter = p.getWidth() / 4.0f;
        const auto along = [&](int stage, float ms, float maxMs)
        {
            const float f = std::log(std::max(1.0f, ms)) / std::log(maxMs);
            return p.getX() + quarter * static_cast<float>(stage) + quarter * juce::jlimit(0.0f, 1.0f, f);
        };
        const float sustainY = p.getBottom() - card_.param(first + 2) * p.getHeight();
        return {juce::Point<float>{along(0, card_.param(first), maxAttack), p.getY()},
                juce::Point<float>{along(1, card_.param(first + 1), maxDecay), sustainY},
                juce::Point<float>{p.getX() + quarter * 3.0f, sustainY},
                juce::Point<float>{along(3, card_.param(first + 3), maxRelease), p.getBottom()}};
    }
    juce::Path path(const std::array<juce::Point<float>, 4>& pts) const
    {
        const auto p = plot();
        juce::Path env;
        env.startNewSubPath(p.getX(), p.getBottom());
        for (const auto& pt : pts)
            env.lineTo(pt);
        return env;
    }

    ModuleCard& card_;
    juce::Colour ampHue_, filterHue_;
    CornerChoice which_;
    int drag_ = -1;
};

// The LFO: one cycle of its shape with the live phase riding it as a dot. The
// shape is a list in the corner.
class LfoDisplay final : public juce::Component
{
public:
    LfoDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue), shape_(card.theme(), hue)
    {
        shape_.choices = {"Sine", "Tri", "Saw", "Square", "Random"};
        shape_.setTooltip("LFO shape");
        shape_.onChoose = [this](int s)
        {
            card_.beginGesture("LFO shape");
            card_.setParam(au::lfoShape, static_cast<float>(s));
            card_.endGesture();
        };
        addAndMakeVisible(shape_);
    }
    void setLive(float value01) { live_ = value01; }
    void resized() override
    {
        shape_.setBounds(
            getLocalBounds().reduced(5).removeFromTop(16).removeFromRight(shape_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const int kind = juce::jlimit(0, 4, static_cast<int>(std::lround(card_.param(au::lfoShape))));
        shape_.selected = kind;
        const auto p = b.reduced(8.0f, 8.0f).withTrimmedTop(14.0f);
        const auto value = [kind](float phase)
        {
            switch (kind)
            {
                case 0:
                    return std::sin(juce::MathConstants<float>::twoPi * phase);
                case 1:
                    return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
                case 2:
                    return 2.0f * phase - 1.0f;
                case 3:
                    return phase < 0.5f ? 1.0f : -1.0f;
                default:
                {
                    // A fixed staircase stands for sample and hold.
                    static const float steps[] = {0.2f, -0.6f, 0.9f, -0.1f, 0.5f, -0.9f, 0.3f, -0.4f};
                    return steps[juce::jlimit(0, 7, static_cast<int>(phase * 8.0f))];
                }
            }
        };
        g.setColour(theme.colours.lineSubtle);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());
        juce::Path path;
        const int n = std::max(2, static_cast<int>(p.getWidth()));
        for (int i = 0; i <= n; ++i)
        {
            const float phase = static_cast<float>(i) / static_cast<float>(n);
            const float y = p.getCentreY() - value(phase) * p.getHeight() * 0.45f;
            const float x = p.getX() + phase * p.getWidth();
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(hue_.withAlpha(0.25f));
        g.strokePath(path, juce::PathStrokeType(5.0f));
        g.setColour(hue_);
        g.strokePath(path, juce::PathStrokeType(1.8f));
        // The live value as a level marker on the right.
        if (live_ >= 0.0f)
        {
            const float y = p.getBottom() - live_ * p.getHeight();
            g.setColour(theme.colours.textPrimary);
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({p.getRight() - 2.0f, y}));
            g.setColour(hue_.withAlpha(0.4f));
            g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight() - 6.0f);
        }
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    CornerChoice shape_;
    float live_ = -1.0f;
};

// The output as a scope (the processor's output tap), steadied on a rising
// zero crossing.
class ScopeDisplay final : public juce::Component
{
public:
    ScopeDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue), samples_(2048) {}
    void update()
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr)
            return;
        if (!fb->tapsEnabled())
            fb->setTapsEnabled(true);
        const auto position = fb->tapPosition(1);
        if (position == lastTap_)
            return;
        lastTap_ = position;
        if (fb->readTap(1, samples_.data(), static_cast<int>(samples_.size())))
            repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(6.0f, 8.0f);
        g.setColour(theme.colours.lineSubtle);
        g.drawHorizontalLine(static_cast<int>(p.getCentreY()), p.getX(), p.getRight());
        // A rising zero crossing in the first half, then a fixed window after it.
        const int window = 600;
        int start = 0;
        for (int i = 1; i < static_cast<int>(samples_.size()) - window; ++i)
            if (samples_[static_cast<size_t>(i - 1)] < 0.0f && samples_[static_cast<size_t>(i)] >= 0.0f)
            {
                start = i;
                break;
            }
        float peak = 1.0e-3f;
        for (int i = 0; i < window; ++i)
            peak = std::max(peak, std::abs(samples_[static_cast<size_t>(start + i)]));
        const float scale = 0.45f * p.getHeight() / std::max(0.25f, peak);
        juce::Path path;
        for (int i = 0; i < window; i += 2)
        {
            const float x = p.getX() + static_cast<float>(i) / static_cast<float>(window) * p.getWidth();
            const float y = p.getCentreY() - samples_[static_cast<size_t>(start + i)] * scale;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.setColour(hue_.withAlpha(0.3f));
        g.strokePath(path, juce::PathStrokeType(4.0f));
        g.setColour(hue_);
        g.strokePath(path, juce::PathStrokeType(1.4f));
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    std::vector<float> samples_;
    std::uint64_t lastTap_ = 0;
};

// ----- the editor -----------------------------------------------------------------------------------

class AuroraCard final : public ModuleCard
{
public:
    AuroraCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), osc_(*this, hues_.osc, hues_.filter),
          unison_(*this, hues_.unison, hues_.osc, hues_.envelope), filter_(*this, hues_.filter),
          envelope_(*this, hues_.envelope, hues_.lfo), lfo_(*this, hues_.lfo), scope_(*this, hues_.output)
    {
        for (auto* c :
             std::initializer_list<juce::Component*>{&osc_, &unison_, &filter_, &envelope_, &lfo_, &scope_})
            addAndMakeVisible(c);
        const auto knob = [this](int p,
                                 const juce::String& label,
                                 juce::Colour hue,
                                 std::function<juce::String(double)> format = {}) -> Knob*
        {
            auto& k = addKnob(p, label);
            k.setArcColour(hue);
            if (format)
                k.format = std::move(format);
            return &k;
        };
        const auto integer = [](const char* suffix)
        {
            return [suffix](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                return (n > 0 ? "+" : "") + juce::String(n) + suffix;
            };
        };
        const auto percent = [](double v)
        { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; };
        const auto signedPercent = [](double v)
        {
            const int n = static_cast<int>(std::lround(v * 100.0));
            return (n > 0 ? "+" : "") + juce::String(n) + "%";
        };
        oscKnobs_ = {knob(au::osc1Octave, "Oct 1", hues_.osc, integer("")),
                     knob(au::osc1Level, "Level 1", hues_.osc, percent),
                     knob(au::sub, "Sub", hues_.osc, percent),
                     knob(au::osc2Octave, "Oct 2", hues_.filter, integer("")),
                     knob(au::osc2Semi, "Semi", hues_.filter, integer(" st")),
                     knob(au::osc2Fine, "Fine", hues_.filter, integer(" ct")),
                     knob(au::osc2Level, "Level 2", hues_.filter, percent),
                     knob(au::noise, "Noise", hues_.osc, percent)};
        unisonKnobs_ = {knob(au::unison,
                             "Voices",
                             hues_.unison,
                             [](double v) { return juce::String(static_cast<int>(std::lround(v))); }),
                        knob(au::spread, "Spread", hues_.unison, integer(" ct")),
                        knob(au::width, "Width", hues_.unison)};
        const auto hz = [](double v)
        {
            return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz"
                               : juce::String(static_cast<int>(std::lround(v))) + " Hz";
        };
        filterKnobs_ = {knob(au::cutoff, "Cutoff", hues_.filter, hz),
                        knob(au::resonance, "Reso", hues_.filter, percent),
                        knob(au::drive, "Drive", hues_.filter, percent),
                        knob(au::filterEnv, "Env", hues_.filter, signedPercent),
                        knob(au::keyTrack, "Key", hues_.filter, percent)};
        envelopeKnobs_ = {knob(au::ampAttack, "Attack", hues_.envelope),
                          knob(au::ampDecay, "Decay", hues_.envelope),
                          knob(au::ampSustain, "Sustain", hues_.envelope, percent),
                          knob(au::ampRelease, "Release", hues_.envelope),
                          knob(au::filterAttack, "F att", hues_.lfo),
                          knob(au::filterDecay, "F dec", hues_.lfo),
                          knob(au::filterSustain, "F sus", hues_.lfo, percent),
                          knob(au::filterRelease, "F rel", hues_.lfo)};
        lfoKnobs_ = {
            knob(au::lfoRate, "Rate", hues_.lfo),
            knob(au::lfoPitch, "Pitch", hues_.lfo, integer(" ct")),
            knob(au::lfoCutoff, "Cutoff", hues_.lfo, [](double v) { return juce::String(v, 1) + " oct"; }),
            knob(au::lfoAmp, "Level", hues_.lfo, percent)};
        outputKnobs_ = {knob(au::glide, "Glide", hues_.output),
                        knob(au::shimmer, "Shimmer", hues_.output, percent),
                        knob(au::velocity, "Velocity", hues_.output, percent),
                        knob(au::level, "Level", hues_.output)};
        for (auto* b : {&previous_, &next_, &presetName_})
        {
            b->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
            addAndMakeVisible(b);
        }
        previous_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        previous_.onClick = [this] { stepPreset(-1); };
        next_.onClick = [this] { stepPreset(1); };
        presetName_.setTooltip("Presets");
        presetName_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        presetName_.onClick = [this] { showPresets(); };
        addAndMakeVisible(presetName_);
        presetName_.setButtonText(currentPresetName());
    }

    // Fifteen knob columns in six panels, with their padding and gaps.
    int preferredWidth() const override { return 15 * cellW + 6 * 2 * pad + 5 * gap + 2 * theme_.spacing.sm; }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, captionH = 18, cellW = 52, gap = 8, pad = 8;

    struct Section
    {
        const char* title;
        juce::Colour hue;
        juce::Rectangle<int> bounds;
    };

    void layoutBody(juce::Rectangle<int> body) override
    {
        sky_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        wordmark_ = header.removeFromLeft(220);
        voices_ = header.removeFromRight(12 * 9 + 4).withSizeKeepingCentre(12 * 9 + 4, 12);
        header.removeFromRight(theme_.spacing.lg);
        auto preset = header.removeFromRight(260).withSizeKeepingCentre(260, theme_.sizes.controlCompact);
        presetBar_ = preset;
        previous_.setBounds(preset.removeFromLeft(theme_.sizes.controlCompact));
        next_.setBounds(preset.removeFromRight(theme_.sizes.controlCompact));
        presetName_.setBounds(preset.reduced(2, 0));
        body.removeFromTop(4);

        // The panels, left to right, each as wide as its knob columns.
        const int knobRows = 3;
        const int knobsH = knobRows * knobCellHeight();
        const int panelH = body.getHeight();
        const int displayH = std::max(36, panelH - captionH - knobsH - 3 * pad);
        sections_.clear();
        int x = body.getX();
        const auto panel = [&](const char* title,
                               juce::Colour hue,
                               int columns,
                               juce::Component& display,
                               const std::vector<Knob*>& knobs)
        {
            const int w = columns * cellW + 2 * pad;
            const juce::Rectangle<int> r(x, body.getY(), w, panelH);
            sections_.push_back({title, hue, r});
            auto inner = r.reduced(pad);
            inner.removeFromTop(captionH);
            display.setBounds(inner.removeFromTop(displayH));
            inner.removeFromTop(pad);
            layoutKnobs(inner.removeFromTop(knobsH), columns, knobs);
            x += w + gap;
        };
        panel("OSCILLATORS", hues_.osc, 3, osc_, oscKnobs_);
        panel("UNISON", hues_.unison, 2, unison_, unisonKnobs_);
        panel("FILTER", hues_.filter, 2, filter_, filterKnobs_);
        panel("ENVELOPES", hues_.envelope, 4, envelope_, envelopeKnobs_);
        panel("LFO", hues_.lfo, 2, lfo_, lfoKnobs_);
        panel("OUTPUT", hues_.output, 2, scope_, outputKnobs_);
    }

    // ----- the sky ------------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = sky_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        // Night: the canvas deepening towards the top.
        g.setGradientFill(juce::ColourGradient(
            c.canvas.darker(0.5f), 0.0f, bounds.getY(), c.surface0, 0.0f, bounds.getBottom(), false));
        g.fillRect(bounds);
        paintStars(g, bounds);
        paintAurora(g, bounds);
        paintWordmark(g);
        paintVoices(g);
        // The panels: glass over the sky.
        for (const auto& s : sections_)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1.withAlpha(0.72f));
            g.fillRoundedRectangle(r, theme_.radius.md);
            g.setColour(c.textPrimary.withAlpha(0.07f));
            g.drawHorizontalLine(
                static_cast<int>(r.getY()) + 1, r.getX() + theme_.radius.md, r.getRight() - theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.md, 1.0f);
            auto caption = s.bounds.reduced(pad).removeFromTop(captionH - 4);
            g.setColour(s.hue.withAlpha(0.35f));
            g.fillEllipse(caption.removeFromLeft(10).toFloat().withSizeKeepingCentre(9.0f, 9.0f));
            g.setColour(s.hue);
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f)
                              .withCentre({static_cast<float>(caption.getX()) - 5.0f,
                                           static_cast<float>(caption.getCentreY())}));
            caption.removeFromLeft(theme_.spacing.xs);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textSecondary);
            g.drawText(s.title, caption, juce::Justification::centredLeft);
        }
        // The preset bar's well.
        g.setColour(c.surface1.withAlpha(0.8f));
        g.fillRoundedRectangle(presetBar_.toFloat(), presetBar_.getHeight() * 0.5f);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(presetBar_.toFloat().reduced(0.5f), presetBar_.getHeight() * 0.5f, 1.0f);
    }

    void paintStars(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        const auto star = theme_.colours.textPrimary;
        for (int i = 0; i < 48; ++i)
        {
            // Fixed places from a cheap hash; each twinkles on its own beat.
            const float fx = std::fmod(static_cast<float>(i) * 0.6180339f + 0.13f, 1.0f);
            const float fy = std::fmod(static_cast<float>(i) * 0.4142135f * 1.7f + 0.07f, 1.0f);
            const float twinkle =
                0.55f +
                0.45f * std::sin(time_ * (0.7f + 0.11f * static_cast<float>(i % 7)) + static_cast<float>(i));
            const float size = i % 9 == 0 ? 1.8f : 1.1f;
            g.setColour(star.withAlpha(0.12f + 0.3f * twinkle * (i % 3 == 0 ? 1.0f : 0.6f)));
            g.fillEllipse(b.getX() + fx * b.getWidth(), b.getY() + fy * b.getHeight() * 0.7f, size, size);
        }
    }

    // Three ribbons of light, drifting; brighter the louder Aurora plays.
    void paintAurora(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        const float glow = 0.55f + 0.45f * glow_;
        const juce::Colour hues[] = {hues_.osc, hues_.unison, hues_.envelope};
        for (int r = 0; r < 3; ++r)
        {
            const float base = b.getY() + 18.0f + static_cast<float>(r) * 22.0f;
            const float height = 70.0f + static_cast<float>(r) * 16.0f;
            const float phase =
                time_ * (0.18f + 0.07f * static_cast<float>(r)) + static_cast<float>(r) * 2.1f;
            const int steps = 48;
            std::array<juce::Point<float>, steps + 1> edge{};
            for (int i = 0; i <= steps; ++i)
            {
                const float t = static_cast<float>(i) / steps;
                const float x = b.getX() + t * b.getWidth();
                const float y =
                    base + 14.0f * std::sin(t * 5.2f + phase) + 8.0f * std::sin(t * 11.0f - phase * 1.3f);
                edge[static_cast<size_t>(i)] = {x, y};
            }
            juce::Path ribbon;
            ribbon.startNewSubPath(edge[0]);
            for (int i = 1; i <= steps; ++i)
                ribbon.lineTo(edge[static_cast<size_t>(i)]);
            for (int i = steps; i >= 0; --i)
                ribbon.lineTo(edge[static_cast<size_t>(i)].translated(0.0f, height));
            ribbon.closeSubPath();
            const auto hue = hues[r];
            juce::ColourGradient fill(hue.withAlpha(0.0f),
                                      0.0f,
                                      base - 10.0f,
                                      hue.withAlpha(0.0f),
                                      0.0f,
                                      base + height + 10.0f,
                                      false);
            fill.addColour(0.25, hue.withAlpha(0.24f * glow));
            fill.addColour(0.55, hue.withAlpha(0.09f * glow));
            g.setGradientFill(fill);
            g.fillPath(ribbon);
        }
    }

    void paintWordmark(juce::Graphics& g) const
    {
        const auto& c = theme_.colours;
        const auto area = wordmark_.toFloat();
        const auto font = theme_.font(Theme::Face::ui, 26.0f, theme_.fonts.weight.semibold);
        juce::GlyphArrangement glyphs;
        float x = area.getX() + 4.0f;
        const float baseline = area.getCentreY() + 4.0f;
        for (const auto ch : juce::String("AURORA"))
        {
            juce::GlyphArrangement one;
            one.addLineOfText(font, juce::String::charToString(ch), x, baseline);
            const float w = one.getBoundingBox(0, -1, true).getWidth();
            glyphs.addGlyphArrangement(one);
            x += w + 6.0f; // tracked wide
        }
        juce::Path text;
        glyphs.createPath(text);
        const auto box = text.getBounds();
        g.setGradientFill(
            juce::ColourGradient(hues_.osc, box.getX(), 0.0f, hues_.envelope, box.getRight(), 0.0f, false));
        g.fillPath(text);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.medium));
        g.setColour(c.textTertiary);
        g.drawText(juce::String::fromUTF8("POLYSYNTH  \xc2\xb7  NORTHLIGHT"),
                   juce::Rectangle<float>(box.getRight() + 10.0f, baseline - 10.0f, 140.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    // Twelve lights: how many voices sound.
    void paintVoices(juce::Graphics& g) const
    {
        for (int i = 0; i < au::voiceCount; ++i)
        {
            const auto r =
                juce::Rectangle<float>(6.0f, 6.0f)
                    .withCentre({static_cast<float>(voices_.getX()) + 4.0f + static_cast<float>(i) * 9.0f,
                                 static_cast<float>(voices_.getCentreY())});
            const bool lit = i < activeVoices_;
            const auto hue = hues_.osc.interpolatedWith(hues_.envelope, static_cast<float>(i) / 11.0f);
            if (lit)
            {
                g.setColour(hue.withAlpha(0.35f));
                g.fillEllipse(r.expanded(3.0f));
            }
            g.setColour(lit ? hue : theme_.colours.surface3);
            g.fillEllipse(r);
        }
    }

    // ----- presets ------------------------------------------------------------------------------

    juce::String currentPresetName() const
    {
        for (const auto& p : au::presets())
        {
            bool same = p.values.size() <= params_.size();
            for (size_t i = 0; same && i < p.values.size(); ++i)
                same = std::abs(p.values[i] - params_[i]) < 1.0e-4f;
            if (same)
                return juce::String(p.name);
        }
        return "Custom";
    }

    void applyPreset(const au::Preset& preset)
    {
        beginGesture("Preset " + juce::String(preset.name));
        for (size_t i = 0; i < preset.values.size(); ++i)
            setParam(static_cast<int>(i), preset.values[i]);
        endGesture();
        presetName_.setButtonText(currentPresetName());
    }

    void stepPreset(int direction)
    {
        const auto& list = au::presets();
        int at = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (juce::String(list[i].name) == currentPresetName())
                at = static_cast<int>(i);
        const int n = static_cast<int>(list.size());
        const int next = at < 0 ? (direction > 0 ? 0 : n - 1) : (at + direction + n) % n;
        applyPreset(list[static_cast<size_t>(next)]);
    }

    void showPresets()
    {
        juce::PopupMenu menu;
        const auto current = currentPresetName();
        const auto& list = au::presets();
        for (size_t i = 0; i < list.size(); ++i)
            menu.addItem(static_cast<int>(i) + 1,
                         juce::String(list[i].name),
                         true,
                         current == juce::String(list[i].name));
        juce::Component::SafePointer<AuroraCard> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetName_),
                           [safe](int r)
                           {
                               if (safe != nullptr && r > 0 && static_cast<size_t>(r) <= au::presets().size())
                                   safe->applyPreset(au::presets()[static_cast<size_t>(r - 1)]);
                           });
    }

    // ----- live ---------------------------------------------------------------------------------

    void paramsChanged() override
    {
        presetName_.setButtonText(currentPresetName());
        for (auto* c : std::initializer_list<juce::Component*>{&osc_, &unison_, &filter_, &envelope_, &lfo_})
            c->repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        // The newest frame carries the voice count and the LFO (screens/plugins.md 7.4b).
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            const int voices = static_cast<int>(std::lround(last.reduction));
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(voices_.expanded(4));
            }
            lfo_.setLive(juce::jlimit(0.0f, 1.0f, last.extra));
            lfo_.repaint();
        }
        scope_.update();
        // The sky moves while Aurora sounds (not at all with reduced motion),
        // at about 30 frames a second.
        const float level = juce::jlimit(0.0f, 1.0f, (outputDb() + 48.0f) / 48.0f);
        glow_ += (level - glow_) * 0.15f;
        if (theme_.motion.reduceMotion)
            return;
        sinceSky_ += elapsedMs;
        if (sinceSky_ >= 33.0 && (level > 0.01f || glow_ > 0.01f))
        {
            time_ += static_cast<float>(sinceSky_ * 0.001);
            sinceSky_ = 0.0;
            repaint(sky_.withHeight(headerH + 60));
        }
    }

    Hues hues_;
    OscillatorDisplay osc_;
    UnisonDisplay unison_;
    FilterDisplay filter_;
    EnvelopeDisplay envelope_;
    LfoDisplay lfo_;
    ScopeDisplay scope_;
    std::vector<Knob*> oscKnobs_, unisonKnobs_, filterKnobs_, envelopeKnobs_, lfoKnobs_, outputKnobs_;
    juce::TextButton previous_{juce::String::fromUTF8("\xe2\x80\xb9")},
        next_{juce::String::fromUTF8("\xe2\x80\xba")};
    juce::TextButton presetName_;
    std::vector<Section> sections_;
    juce::Rectangle<int> sky_, wordmark_, voices_, presetBar_;
    int activeVoices_ = 0;
    float time_ = 0.0f, glow_ = 0.0f;
    double sinceSky_ = 0.0;
};
} // namespace

std::unique_ptr<ModuleCard>
createAuroraEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<AuroraCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
