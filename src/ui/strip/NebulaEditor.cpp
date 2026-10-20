// Nebula's editor (07-plugins.md 7.4d). A pad synth's face should feel slow
// and soft: gas clouds in the palette's cool hues drifting behind the panels,
// dust rising through them, everything breathing with the sound. The spectrum
// is the centrepiece: each partial a column of light under the formant's
// cloud, swaying with the motion; drag across it to move the vowel, up and
// down for the brightness. Then the cloud (where the partials sit in the
// stereo field and how they wander), the envelope, the motion and the space.

#include "ui/strip/NebulaEditor.h"

#include "plugins/Nebula.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <array>
#include <cmath>

namespace aural::ui::strip
{

namespace
{
namespace ne = plugins::nebula;

struct Hues
{
    juce::Colour spectrum, cloud, envelope, motion, space, halo;
    explicit Hues(const Theme& t)
        : spectrum(paletteColour(t, "Violet")), cloud(paletteColour(t, "Teal")),
          envelope(paletteColour(t, "Magenta")), motion(paletteColour(t, "Sky")),
          space(paletteColour(t, "Indigo")), halo(paletteColour(t, "Mint"))
    {
    }
};

constexpr float referenceHz = 220.0f; // the spectrum is drawn for A3 (and the octave)

// The spectrum's axis: 55 Hz to 12 kHz on a log scale, where a pad's partials live.
constexpr float lowHz = 55.0f, highHz = 12000.0f;
float spectrumX(float hz, juce::Rectangle<float> r)
{
    return r.getX() +
           std::log(juce::jlimit(lowHz, highHz, hz) / lowHz) / std::log(highHz / lowHz) * r.getWidth();
}
float spectrumHz(float x, juce::Rectangle<float> r)
{
    return lowHz * std::pow(highHz / lowHz, juce::jlimit(0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

// ----- the spectrum -------------------------------------------------------------------------------

class SpectrumDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumDisplay(ModuleCard& card, const Hues& hues)
        : card_(card), hues_(hues), octave_(card.theme(), hues.spectrum)
    {
        setTooltip("Drag across to move the formant (the vowel), up and down for the brightness");
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        octave_.choices = {"-2 oct", "-1 oct", "0 oct", "+1 oct", "+2 oct"};
        octave_.setTooltip("Octave");
        octave_.onChoose = [this](int i)
        {
            card_.beginGesture("Octave");
            card_.setParam(ne::octave, static_cast<float>(i - 2));
            card_.endGesture();
        };
        addAndMakeVisible(octave_);
    }
    void setMotion(float motion)
    {
        motion_ = motion;
        repaint();
    }
    void resized() override
    {
        octave_.setBounds(
            getLocalBounds().reduced(6).removeFromTop(16).removeFromRight(octave_.preferredWidth()));
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        octave_.selected = juce::jlimit(0, 4, static_cast<int>(std::lround(card_.param(ne::octave))) + 2);
        const auto p = b.reduced(8.0f, 8.0f).withTrimmedTop(16.0f).withTrimmedBottom(10.0f);
        const float noteHz = referenceHz * std::exp2(std::round(card_.param(ne::octave)));
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(b.reduced(1.0f).toNearestInt());
            // The formant: a cloud over its place in the spectrum.
            const float centre = 250.0f * std::pow(12.0f, card_.param(ne::vowel));
            const float strength = card_.param(ne::formant);
            if (strength > 0.01f)
            {
                const float cx = spectrumX(centre, p);
                const float spread = p.getWidth() * 0.12f;
                g.setGradientFill(juce::ColourGradient(hues_.envelope.withAlpha(0.32f * strength),
                                                       cx,
                                                       p.getCentreY(),
                                                       hues_.envelope.withAlpha(0.0f),
                                                       cx + spread,
                                                       p.getCentreY(),
                                                       true));
                g.fillEllipse(juce::Rectangle<float>(spread * 2.0f, p.getHeight() * 1.2f)
                                  .withCentre({cx, p.getCentreY()}));
            }
            // Octave lines for a sense of place.
            for (const float hz : {110.0f, 220.0f, 440.0f, 880.0f, 1760.0f, 3520.0f, 7040.0f})
            {
                g.setColour(theme.colours.gridBeat);
                g.drawVerticalLine(static_cast<int>(spectrumX(hz, p)), p.getY(), p.getBottom());
            }
            // The partials: columns of light, the halo's in their own hue.
            const auto partials = ne::spectrum(card_.params(), noteHz, motion_);
            for (const auto& partial : partials)
            {
                const float hz = noteHz * partial.ratio;
                if (hz > highHz || partial.level < 0.002f)
                    continue;
                const float x = spectrumX(hz, p);
                const float h = std::pow(partial.level, 0.6f) * p.getHeight();
                const auto hue = partial.halo ? hues_.halo : hues_.spectrum;
                const float top = p.getBottom() - h;
                g.setGradientFill(juce::ColourGradient(
                    hue.withAlpha(0.85f), x, top, hue.withAlpha(0.0f), x, p.getBottom() + 6.0f, false));
                g.fillRoundedRectangle(juce::Rectangle<float>(x - 2.0f, top, 4.0f, p.getBottom() - top),
                                       2.0f);
                g.setColour(hue.withAlpha(0.3f));
                g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({x, top}));
                g.setColour(theme.colours.textPrimary.withAlpha(0.9f));
                g.fillEllipse(juce::Rectangle<float>(3.5f, 3.5f).withCentre({x, top}));
            }
        }
        drawWellFrame(g, theme, b);
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText(juce::String(static_cast<int>(std::lround(card_.param(ne::partials)))) + " partials",
                   b.reduced(8.0f, 5.0f).removeFromTop(14.0f),
                   juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        startBright_ = card_.param(ne::brightness);
        card_.beginGesture("Spectrum");
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto p = getLocalBounds().toFloat().reduced(8.0f, 8.0f);
        const float hz = spectrumHz(e.position.x, p);
        card_.setParam(ne::vowel, juce::jlimit(0.0f, 1.0f, std::log(hz / 250.0f) / std::log(12.0f)));
        card_.setParam(
            ne::brightness,
            juce::jlimit(0.0f,
                         1.0f,
                         startBright_ - static_cast<float>(e.getDistanceFromDragStartY()) / p.getHeight()));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    const Hues& hues_;
    CornerChoice octave_;
    float motion_ = 0.0f, startBright_ = 0.0f;
};

// ----- the cloud ----------------------------------------------------------------------------------

// Where the partials sit: across, their place in the stereo field; up, their
// order; each a pair of dots as far apart as the spread, wandering with the drift.
class CloudDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CloudDisplay(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setTooltip("The partials in the stereo field (left to right), low to high, wandering with the drift");
    }
    float time = 0.0f;
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(10.0f, 10.0f);
        g.setColour(theme.colours.lineSubtle);
        g.drawVerticalLine(static_cast<int>(p.getCentreX()), p.getY(), p.getBottom());
        const int count =
            juce::jlimit(1, ne::maxPartials, static_cast<int>(std::lround(card_.param(ne::partials))));
        const float width = card_.param(ne::width) * 0.01f;
        const float drift = card_.param(ne::drift);
        const float spread = card_.param(ne::spread) / 30.0f;
        const auto partials = ne::spectrum(card_.params(), referenceHz);
        for (int k = 0; k < count; ++k)
        {
            const float side = k % 2 == 0 ? -1.0f : 1.0f;
            const float pos = side * width * (0.35f + 0.65f * static_cast<float>(k) / ne::maxPartials);
            const float wander =
                drift * 0.08f *
                std::sin(time * (0.4f + 0.05f * static_cast<float>(k)) + static_cast<float>(k) * 1.9f);
            const float x = p.getCentreX() + (pos + wander) * p.getWidth() * 0.5f;
            const float y =
                p.getBottom() - (static_cast<float>(k) + 0.5f) / static_cast<float>(count) * p.getHeight();
            const float level =
                static_cast<size_t>(k) < partials.size() ? partials[static_cast<size_t>(k)].level : 0.0f;
            const float size = 3.0f + 6.0f * std::sqrt(level);
            const auto hue = hues_.cloud.interpolatedWith(hues_.spectrum, 0.5f + 0.5f * pos);
            for (const float d : {-1.0f, 1.0f})
            {
                const juce::Point<float> at{x + d * spread * 6.0f, y};
                g.setColour(hue.withAlpha(0.25f));
                g.fillEllipse(juce::Rectangle<float>(size * 2.0f, size * 2.0f).withCentre(at));
                g.setColour(hue);
                g.fillEllipse(juce::Rectangle<float>(size * 0.6f, size * 0.6f).withCentre(at));
            }
        }
        drawWellFrame(g, theme, b);
        g.setFont(theme.font(Theme::Face::ui, theme.fonts.size.caption - 2.0f, theme.fonts.weight.semibold));
        g.setColour(theme.colours.textTertiary);
        g.drawText("L", b.reduced(6.0f, 4.0f), juce::Justification::bottomLeft);
        g.drawText("R", b.reduced(6.0f, 4.0f), juce::Justification::bottomRight);
    }

private:
    ModuleCard& card_;
    const Hues& hues_;
};

// ----- envelope, motion, space --------------------------------------------------------------------

class EnvelopeDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    EnvelopeDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("Drag the points: attack, decay and sustain, release");
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto pts = points();
        const auto p = plot();
        juce::Path env;
        env.startNewSubPath(p.getX(), p.getBottom());
        for (const auto& pt : pts)
            env.lineTo(pt);
        juce::Path area = env;
        area.lineTo(pts.back().x, p.getBottom());
        area.closeSubPath();
        g.setGradientFill(juce::ColourGradient(
            hue_.withAlpha(0.4f), 0.0f, p.getY(), hue_.withAlpha(0.03f), 0.0f, p.getBottom(), false));
        g.fillPath(area);
        g.setColour(hue_);
        g.strokePath(env,
                     juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        drawWellFrame(g, theme, b);
        drawHandle(g, theme, hue_, pts[0], drag_ == 0);
        drawHandle(g, theme, hue_, pts[1], drag_ == 1);
        drawHandle(g, theme, hue_, pts[3], drag_ == 3);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto pts = points();
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
        const auto p = plot();
        const float quarter = p.getWidth() / 4.0f;
        const auto msIn = [&](int stage, float x, float maxMs)
        {
            return std::pow(
                maxMs,
                juce::jlimit(0.0f, 1.0f, (x - (p.getX() + quarter * static_cast<float>(stage))) / quarter));
        };
        if (drag_ == 0)
            card_.setParam(ne::attack, msIn(0, e.position.x, 10000.0f));
        else if (drag_ == 1)
        {
            card_.setParam(ne::decay, msIn(1, e.position.x, 10000.0f));
            card_.setParam(ne::sustain,
                           juce::jlimit(0.0f, 1.0f, (p.getBottom() - e.position.y) / p.getHeight()));
        }
        else if (drag_ == 3)
            card_.setParam(ne::release, msIn(3, e.position.x, 15000.0f));
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
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(8.0f, 8.0f); }
    std::array<juce::Point<float>, 4> points() const
    {
        const auto p = plot();
        const float quarter = p.getWidth() / 4.0f;
        const auto along = [&](int stage, float ms, float maxMs)
        {
            const float f = std::log(std::max(1.0f, ms)) / std::log(maxMs);
            return p.getX() + quarter * static_cast<float>(stage) + quarter * juce::jlimit(0.0f, 1.0f, f);
        };
        const float sustainY = p.getBottom() - card_.param(ne::sustain) * p.getHeight();
        return {juce::Point<float>{along(0, card_.param(ne::attack), 10000.0f), p.getY()},
                juce::Point<float>{along(1, card_.param(ne::decay), 10000.0f), sustainY},
                juce::Point<float>{p.getX() + quarter * 3.0f, sustainY},
                juce::Point<float>{along(3, card_.param(ne::release), 15000.0f), p.getBottom()}};
    }

    ModuleCard& card_;
    juce::Colour hue_;
    int drag_ = -1;
};

// The motion: a slow wave with the live value riding it.
class MotionDisplay final : public juce::Component
{
public:
    MotionDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    float live = 0.5f; // 0..1
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 10.0f);
        const float depth = 0.15f + 0.85f * card_.param(ne::motionDepth);
        juce::Path wave;
        for (int i = 0; i <= 64; ++i)
        {
            const float t = static_cast<float>(i) / 64.0f;
            const float y = p.getCentreY() -
                            std::sin(t * juce::MathConstants<float>::twoPi) * p.getHeight() * 0.45f * depth;
            if (i == 0)
                wave.startNewSubPath(p.getX(), y);
            else
                wave.lineTo(p.getX() + t * p.getWidth(), y);
        }
        g.setColour(hue_.withAlpha(0.25f));
        g.strokePath(wave, juce::PathStrokeType(5.0f));
        g.setColour(hue_);
        g.strokePath(wave, juce::PathStrokeType(1.6f));
        const float y = p.getCentreY() - (live * 2.0f - 1.0f) * p.getHeight() * 0.45f * depth;
        g.setColour(theme.colours.textPrimary);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({p.getRight() - 2.0f, y}));
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// The space: the reverb's tail as a glowing slope, as long as the size and as
// bright as the mix, with the sound now as a light at its head.
class SpaceDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpaceDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue)
    {
        setTooltip("Drag across for the size, up and down for the mix");
    }
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawScreen(g, theme, b);
        const auto p = b.reduced(8.0f, 10.0f);
        const float size = card_.param(ne::space), mix = card_.param(ne::spaceMix);
        const float end = p.getX() + (0.25f + 0.75f * size) * p.getWidth();
        juce::Path tail;
        tail.startNewSubPath(p.getX(), p.getBottom());
        tail.lineTo(p.getX(), p.getBottom() - (0.3f + 0.7f * mix) * p.getHeight());
        tail.quadraticTo(
            p.getX() + (end - p.getX()) * 0.25f, p.getBottom() - 0.1f * p.getHeight(), end, p.getBottom());
        tail.closeSubPath();
        g.setGradientFill(juce::ColourGradient(
            hue_.withAlpha(0.6f), p.getX(), p.getY(), hue_.withAlpha(0.05f), end, p.getBottom(), false));
        g.fillPath(tail);
        const float glow = juce::jlimit(0.0f, 1.0f, (card_.outputDb() + 48.0f) / 48.0f);
        g.setColour(theme.colours.textPrimary.withAlpha(0.3f + 0.7f * glow));
        g.fillEllipse(
            juce::Rectangle<float>(6.0f, 6.0f)
                .withCentre({p.getX() + 3.0f, p.getBottom() - (0.3f + 0.7f * mix) * p.getHeight()}));
        drawWellFrame(g, theme, b);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        start_ = {card_.param(ne::space), card_.param(ne::spaceMix)};
        card_.beginGesture("Space");
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        card_.setParam(ne::space,
                       juce::jlimit(0.0f, 1.0f, start_.x + e.getDistanceFromDragStartX() / 100.0f));
        card_.setParam(ne::spaceMix,
                       juce::jlimit(0.0f, 1.0f, start_.y - e.getDistanceFromDragStartY() / 80.0f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { card_.endGesture(); }

private:
    ModuleCard& card_;
    juce::Colour hue_;
    juce::Point<float> start_;
};

// ----- the editor ---------------------------------------------------------------------------------

class NebulaCard final : public ModuleCard
{
public:
    NebulaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), spectrum_(*this, hues_),
          cloud_(*this, hues_), envelope_(*this, hues_.envelope), motion_(*this, hues_.motion),
          space_(*this, hues_.space)
    {
        for (auto* c :
             std::initializer_list<juce::Component*>{&spectrum_, &cloud_, &envelope_, &motion_, &space_})
            addAndMakeVisible(c);
        const auto percent = [](double v)
        { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; };
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
        spectrumKnobs_ = {knob(ne::partials,
                               "Partials",
                               hues_.spectrum,
                               [](double v) { return juce::String(static_cast<int>(std::lround(v))); }),
                          knob(ne::brightness, "Bright", hues_.spectrum, percent),
                          knob(ne::oddEven,
                               "Odd/even",
                               hues_.spectrum,
                               [](double v)
                               {
                                   const int n = static_cast<int>(std::lround(v * 100.0));
                                   return (n > 0 ? "+" : "") + juce::String(n);
                               }),
                          knob(ne::stretch, "Stretch", hues_.spectrum, percent),
                          knob(ne::vowel, "Vowel", hues_.envelope, percent),
                          knob(ne::formant, "Formant", hues_.envelope, percent)};
        cloudKnobs_ = {knob(ne::drift, "Drift", hues_.cloud, percent),
                       knob(ne::spread,
                            "Spread",
                            hues_.cloud,
                            [](double v) { return juce::String(static_cast<int>(std::lround(v))) + " ct"; }),
                       knob(ne::width, "Width", hues_.cloud),
                       knob(ne::halo, "Halo", hues_.halo, percent),
                       knob(ne::sub, "Sub", hues_.cloud, percent),
                       knob(ne::breath, "Breath", hues_.cloud, percent)};
        envelopeKnobs_ = {knob(ne::attack, "Attack", hues_.envelope),
                          knob(ne::decay, "Decay", hues_.envelope),
                          knob(ne::sustain, "Sustain", hues_.envelope, percent),
                          knob(ne::release, "Release", hues_.envelope)};
        motionKnobs_ = {knob(ne::motionRate,
                             "Rate",
                             hues_.motion,
                             [](double v) { return juce::String(v, v < 1.0 ? 2 : 1) + " Hz"; }),
                        knob(ne::motionDepth, "Depth", hues_.motion, percent),
                        knob(ne::velocity, "Velocity", hues_.motion, percent),
                        knob(ne::level, "Level", hues_.motion)};
        spaceKnobs_ = {knob(ne::space, "Size", hues_.space, percent),
                       knob(ne::spaceMix, "Mix", hues_.space, percent),
                       knob(ne::tone,
                            "Tone",
                            hues_.space,
                            [](double v)
                            {
                                return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz"
                                                   : juce::String(static_cast<int>(std::lround(v))) + " Hz";
                            })};
        for (auto* b : {&previous_, &next_, &presetName_})
        {
            b->getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
            addAndMakeVisible(b);
        }
        previous_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        presetName_.setTooltip("Presets");
        previous_.onClick = [this] { stepPreset(-1); };
        next_.onClick = [this] { stepPreset(1); };
        presetName_.onClick = [this] { showPresets(); };
        presetName_.setButtonText(currentPresetName());
    }

    // Fifteen knob columns (the spectrum's six a little wider) in five panels.
    int preferredWidth() const override
    {
        return 6 * wideCell + 9 * cellW + 5 * 2 * pad + 4 * gap + 2 * theme_.spacing.sm;
    }
    int preferredBodyHeight() const override { return 340; }

private:
    static constexpr int headerH = 46, captionH = 18, cellW = 52, wideCell = 54, gap = 8, pad = 8;

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
        wordmark_ = header.removeFromLeft(260);
        voices_ =
            header.removeFromRight(ne::voiceCount * 9 + 4).withSizeKeepingCentre(ne::voiceCount * 9 + 4, 12);
        header.removeFromRight(theme_.spacing.lg);
        auto preset = header.removeFromRight(260).withSizeKeepingCentre(260, theme_.sizes.controlCompact);
        presetBar_ = preset;
        previous_.setBounds(preset.removeFromLeft(theme_.sizes.controlCompact));
        next_.setBounds(preset.removeFromRight(theme_.sizes.controlCompact));
        presetName_.setBounds(preset.reduced(2, 0));
        body.removeFromTop(4);

        sections_.clear();
        const int panelH = body.getHeight();
        int x = body.getX();
        const auto panel = [&](const char* title,
                               juce::Colour hue,
                               int columns,
                               int cell,
                               int rows,
                               juce::Component& display,
                               const std::vector<Knob*>& knobs)
        {
            const int w = columns * cell + 2 * pad;
            const juce::Rectangle<int> r(x, body.getY(), w, panelH);
            sections_.push_back({title, hue, r});
            auto inner = r.reduced(pad);
            inner.removeFromTop(captionH);
            const int knobsH = rows * knobCellHeight();
            display.setBounds(inner.removeFromTop(std::max(36, inner.getHeight() - knobsH - pad)));
            inner.removeFromTop(pad);
            layoutKnobs(inner.removeFromTop(knobsH), columns, knobs);
            x += w + gap;
        };
        panel("SPECTRUM", hues_.spectrum, 6, wideCell, 1, spectrum_, spectrumKnobs_);
        panel("CLOUD", hues_.cloud, 3, cellW, 2, cloud_, cloudKnobs_);
        panel("ENVELOPE", hues_.envelope, 2, cellW, 2, envelope_, envelopeKnobs_);
        panel("MOTION", hues_.motion, 2, cellW, 2, motion_, motionKnobs_);
        panel("SPACE", hues_.space, 2, cellW, 2, space_, spaceKnobs_);
    }

    // ----- the sky ----------------------------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = sky_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setGradientFill(juce::ColourGradient(
            c.canvas.darker(0.5f), 0.0f, bounds.getY(), c.surface0, 0.0f, bounds.getBottom(), false));
        g.fillRect(bounds);
        paintClouds(g, bounds);
        paintDust(g, bounds);
        paintWordmark(g);
        paintVoices(g);
        for (const auto& s : sections_)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1.withAlpha(0.68f));
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
        g.setColour(c.surface1.withAlpha(0.8f));
        g.fillRoundedRectangle(presetBar_.toFloat(), presetBar_.getHeight() * 0.5f);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(presetBar_.toFloat().reduced(0.5f), presetBar_.getHeight() * 0.5f, 1.0f);
    }

    // Gas: soft clouds in Violet, Magenta, Teal and Indigo, drifting and
    // breathing with the sound.
    void paintClouds(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        const juce::Colour hues[] = {
            hues_.spectrum, hues_.envelope, hues_.cloud, hues_.space, hues_.spectrum};
        const float places[][3] = {{0.12f, 0.25f, 0.42f},
                                   {0.42f, 0.1f, 0.36f},
                                   {0.7f, 0.3f, 0.4f},
                                   {0.92f, 0.12f, 0.3f},
                                   {0.55f, 0.65f, 0.45f}};
        const float breath = 0.6f + 0.4f * glow_;
        for (int i = 0; i < 5; ++i)
        {
            const float drift =
                std::sin(time_ * (0.05f + 0.013f * static_cast<float>(i)) + static_cast<float>(i) * 1.3f);
            const float cx = b.getX() + (places[i][0] + 0.04f * drift) * b.getWidth();
            const float cy = b.getY() + (places[i][1] + 0.03f * drift) * b.getHeight();
            const float radius = places[i][2] * b.getHeight() * (1.0f + 0.08f * drift);
            g.setGradientFill(juce::ColourGradient(hues[i].withAlpha(0.2f * breath),
                                                   cx,
                                                   cy,
                                                   hues[i].withAlpha(0.0f),
                                                   cx + radius * 1.6f,
                                                   cy,
                                                   true));
            g.fillEllipse(juce::Rectangle<float>(radius * 3.2f, radius * 2.2f).withCentre({cx, cy}));
        }
    }

    // Dust: specks rising slowly through the clouds.
    void paintDust(juce::Graphics& g, juce::Rectangle<float> b) const
    {
        for (int i = 0; i < 60; ++i)
        {
            const float fx = std::fmod(static_cast<float>(i) * 0.6180339f + 0.05f, 1.0f);
            const float speed = 0.01f + 0.02f * std::fmod(static_cast<float>(i) * 0.37f, 1.0f);
            const float fy = 1.0f - std::fmod(static_cast<float>(i) * 0.4142135f + time_ * speed, 1.0f);
            const float twinkle = 0.5f + 0.5f * std::sin(time_ * 0.8f + static_cast<float>(i) * 2.3f);
            g.setColour(
                theme_.colours.textPrimary.withAlpha(0.08f + 0.22f * twinkle * (i % 3 == 0 ? 1.0f : 0.5f)));
            const float size = i % 13 == 0 ? 1.8f : 1.0f;
            g.fillEllipse(b.getX() + fx * b.getWidth(), b.getY() + fy * b.getHeight(), size, size);
        }
    }

    void paintWordmark(juce::Graphics& g) const
    {
        const auto area = wordmark_.toFloat();
        const auto font = theme_.font(Theme::Face::ui, 26.0f, theme_.fonts.weight.regular);
        juce::GlyphArrangement glyphs;
        float x = area.getX() + 4.0f;
        const float baseline = area.getCentreY() + 4.0f;
        for (const auto ch : juce::String("NEBULA"))
        {
            juce::GlyphArrangement one;
            one.addLineOfText(font, juce::String::charToString(ch), x, baseline);
            const float w = one.getBoundingBox(0, -1, true).getWidth();
            glyphs.addGlyphArrangement(one);
            x += w + 7.0f;
        }
        juce::Path text;
        glyphs.createPath(text);
        const auto box = text.getBounds();
        // A soft glow behind the letters, then the gradient.
        g.setColour(hues_.envelope.withAlpha(0.18f * (0.5f + 0.5f * glow_)));
        g.fillRoundedRectangle(box.expanded(10.0f, 6.0f), 12.0f);
        g.setGradientFill(
            juce::ColourGradient(hues_.envelope, box.getX(), 0.0f, hues_.cloud, box.getRight(), 0.0f, false));
        g.fillPath(text);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.medium));
        g.setColour(theme_.colours.textTertiary);
        g.drawText(juce::String::fromUTF8("PAD SYNTH  \xc2\xb7  AURAL"),
                   juce::Rectangle<float>(box.getRight() + 12.0f, baseline - 10.0f, 140.0f, 12.0f),
                   juce::Justification::centredLeft);
    }

    void paintVoices(juce::Graphics& g) const
    {
        for (int i = 0; i < ne::voiceCount; ++i)
        {
            const auto r =
                juce::Rectangle<float>(6.0f, 6.0f)
                    .withCentre({static_cast<float>(voices_.getX()) + 4.0f + static_cast<float>(i) * 9.0f,
                                 static_cast<float>(voices_.getCentreY())});
            const bool lit = i < activeVoices_;
            const auto hue =
                hues_.envelope.interpolatedWith(hues_.cloud, static_cast<float>(i) / (ne::voiceCount - 1));
            if (lit)
            {
                g.setColour(hue.withAlpha(0.35f));
                g.fillEllipse(r.expanded(3.0f));
            }
            g.setColour(lit ? hue : theme_.colours.surface3);
            g.fillEllipse(r);
        }
    }

    // ----- presets ----------------------------------------------------------------------------------

    juce::String currentPresetName() const
    {
        for (const auto& p : ne::presets())
        {
            bool same = p.values.size() <= params_.size();
            for (size_t i = 0; same && i < p.values.size(); ++i)
                same = std::abs(p.values[i] - params_[i]) < 1.0e-4f;
            if (same)
                return juce::String(p.name);
        }
        return "Custom";
    }
    void applyPreset(const ne::Preset& preset)
    {
        beginGesture("Preset " + juce::String(preset.name));
        for (size_t i = 0; i < preset.values.size(); ++i)
            setParam(static_cast<int>(i), preset.values[i]);
        endGesture();
        presetName_.setButtonText(currentPresetName());
    }
    void stepPreset(int direction)
    {
        const auto& list = ne::presets();
        int at = -1;
        const auto current = currentPresetName();
        for (size_t i = 0; i < list.size(); ++i)
            if (juce::String(list[i].name) == current)
                at = static_cast<int>(i);
        const int n = static_cast<int>(list.size());
        applyPreset(
            list[static_cast<size_t>(at < 0 ? (direction > 0 ? 0 : n - 1) : (at + direction + n) % n)]);
    }
    void showPresets()
    {
        juce::PopupMenu menu;
        const auto current = currentPresetName();
        for (size_t i = 0; i < ne::presets().size(); ++i)
            menu.addItem(static_cast<int>(i) + 1,
                         juce::String(ne::presets()[i].name),
                         true,
                         current == juce::String(ne::presets()[i].name));
        juce::Component::SafePointer<NebulaCard> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetName_),
                           [safe](int r)
                           {
                               if (safe != nullptr && r > 0 && static_cast<size_t>(r) <= ne::presets().size())
                                   safe->applyPreset(ne::presets()[static_cast<size_t>(r - 1)]);
                           });
    }

    // ----- live -------------------------------------------------------------------------------------

    void paramsChanged() override
    {
        presetName_.setButtonText(currentPresetName());
        for (auto* c :
             std::initializer_list<juce::Component*>{&spectrum_, &cloud_, &envelope_, &motion_, &space_})
            c->repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        // The newest frame carries the voice count and the motion (0..1).
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            const int voices = static_cast<int>(std::lround(last.reduction));
            if (voices != activeVoices_)
            {
                activeVoices_ = voices;
                repaint(voices_.expanded(4));
            }
            const float motion = juce::jlimit(0.0f, 1.0f, last.extra);
            if (activeVoices_ > 0 && std::abs(motion - motion_.live) > 0.004f)
            {
                motion_.live = motion;
                motion_.repaint();
                spectrum_.setMotion(motion * 2.0f - 1.0f);
            }
        }
        const float level = juce::jlimit(0.0f, 1.0f, (outputDb() + 54.0f) / 54.0f);
        glow_ += (level - glow_) * 0.08f;
        if (theme_.motion.reduceMotion || (activeVoices_ == 0 && glow_ < 0.01f))
            return;
        sinceSky_ += elapsedMs;
        if (sinceSky_ >= 40.0)
        {
            time_ += static_cast<float>(sinceSky_ * 0.001);
            cloud_.time = time_;
            sinceSky_ = 0.0;
            repaint(); // the clouds pass behind every panel
        }
    }

    Hues hues_;
    SpectrumDisplay spectrum_;
    CloudDisplay cloud_;
    EnvelopeDisplay envelope_;
    MotionDisplay motion_;
    SpaceDisplay space_;
    std::vector<Knob*> spectrumKnobs_, cloudKnobs_, envelopeKnobs_, motionKnobs_, spaceKnobs_;
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
createNebulaEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<NebulaCard>(theme, host, channelId, insert);
}

} // namespace aural::ui::strip
