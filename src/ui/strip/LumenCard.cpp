// Lumen's card (screens/plugins.md 7.4i): the front of a 1970s suitcase electric
// piano, a hardware face (design-system.md "Hardware face") built from rendered
// layers (assets/lumen; "Rendered parts"). The face is drawn at 1160 x 600 and
// scaled to fit the card's body, the card narrowing with it.
//
// On the black tolex case: the harp window (four tines behind glass, vibrating
// while Lumen sounds), the nameplate, the service trims (TUNE, AGE, VOICES) and the
// pilot lamp. On the brushed preamp plate: HARP, TOUCH, PREAMP, VIBRATO (its
// three-way lever and the two jewel lamps the pan moves between), PHASE and
// CHORUS. Every word is printed by the face, never rendered.

#include "ui/strip/LumenCard.h"

#include "plugins/BeatSync.h"
#include "plugins/Lumen.h"
#include "plugins/dsp/Modulation.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/LumenFace.h"
#include "ui/strip/RenderedCard.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace northlight::ui::strip
{

namespace lu = plugins::lumen;
namespace lf = lumenface;

namespace
{
constexpr float renderScale = 2.0f; // the still renders are twice the face's size

const juce::Image& part(const juce::String& file)
{
    return hardware::image("lumen", file);
}

// A patch of a render covering `area` (face pixels). Shares the render's pixels;
// empty without the render.
juce::Image patchOf(const juce::Image& source, juce::Rectangle<float> area, float scale = renderScale)
{
    if (!source.isValid())
        return {};
    const auto r = (area * scale).toNearestInt().getIntersection(source.getBounds());
    return r.isEmpty() ? juce::Image() : source.getClippedImage(r);
}

using hardware::legendFont;

// ----- the tines -------------------------------------------------------------------------------

// The tines behind the harp window, bent by the nearest two rendered frames, the
// second faded over the first. At rest the still face shows them, so nothing is drawn.
class Tines final : public juce::Component
{
public:
    explicit Tines(const Theme& theme) : theme_(theme)
    {
        setInterceptsMouseClicks(false, false);
        setBounds(lf::windowBezel.toNearestInt());
    }

    float swing() const noexcept { return swing_; }
    void setSwing(float swing)
    {
        if (std::abs(swing - swing_) < 0.01f)
            return;
        swing_ = swing;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        if (!frame(0).isValid())
        {
            paintFlat(g);
            return;
        }
        if (lf::atRest(swing_))
            return;
        const auto f = lf::framesFor(swing_);
        hardware::drawScaled(g, frame(f.first), area);
        if (f.blend > 0.01f)
            hardware::drawScaled(g, frame(f.second), area, f.blend);
    }

private:
    const juce::Image& frame(int i) const
    {
        static const auto names = []
        {
            std::array<juce::String, lf::tineFrames> out;
            for (int k = 0; k < lf::tineFrames; ++k)
                out[static_cast<size_t>(k)] = juce::String::formatted("tine-%d.jpg", k);
            return out;
        }();
        return part(names[static_cast<size_t>(std::clamp(i, 0, lf::tineFrames - 1))]);
    }

    // Without the renders: each tine a line from its block to its pickup.
    void paintFlat(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        const auto origin = lf::windowBezel.getPosition();
        g.setColour(hw.lumenWindow);
        g.fillRect(lf::windowGlass - origin);
        constexpr std::array<std::pair<float, float>, 4> rows{
            {{200.0f, 520.0f}, {158.0f, 482.0f}, {116.0f, 444.0f}, {74.0f, 406.0f}}};
        for (const auto& [y, tip] : rows)
        {
            const float tipY = y + swing_ * 7.0f;
            g.setColour(hw.lumenToneBar);
            g.fillRect(juce::Rectangle<float>(84.0f, y - 15.0f, (tip - 86.0f) * 0.62f, 6.0f) - origin);
            g.setColour(hw.lumenTine);
            g.drawLine(juce::Line<float>(juce::Point<float>{86.0f, y} - origin,
                                         juce::Point<float>{tip, tipY} - origin),
                       2.0f);
            g.setColour(hw.lumenPickup);
            g.fillRoundedRectangle(juce::Rectangle<float>(tip + 14.0f, y - 11.0f, 26.0f, 22.0f) - origin,
                                   3.0f);
        }
    }

    const Theme& theme_;
    float swing_ = 0.0f;
};

// ----- the lamps -------------------------------------------------------------------------------

// A jewel lamp drawn from its patch of the lit render, as bright as it is lit
// (0..1): its glow and its light on the plate round it come with the patch.
class Lamp final : public juce::Component
{
public:
    Lamp(const Theme& theme, juce::Point<float> centre, float patch, juce::Colour flat)
        : theme_(theme), flat_(flat)
    {
        setInterceptsMouseClicks(false, false);
        area_ = juce::Rectangle<float>(patch, patch).withCentre(centre);
        setBounds(area_.toNearestInt());
    }

    void setBrightness(float brightness)
    {
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        if (std::abs(brightness - brightness_) < 0.02f)
            return;
        brightness_ = brightness;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        juce::ignoreUnused(theme_);
        if (brightness_ <= 0.01f)
            return;
        const auto lit = patchOf(part("lit.jpg"), getBounds().toFloat());
        if (lit.isValid())
            hardware::drawScaled(g, lit, getLocalBounds().toFloat(), brightness_);
        else
        {
            g.setColour(flat_.withMultipliedAlpha(brightness_));
            g.fillEllipse(getLocalBounds().toFloat().reduced(getWidth() * 0.3f));
        }
    }

private:
    const Theme& theme_;
    juce::Colour flat_;
    juce::Rectangle<float> area_;
    float brightness_ = 0.0f;
};

// ----- the face --------------------------------------------------------------------------------

// The piano at its drawn size; the card scales it to fit. Everything that does
// not move is drawn once per scale into an image.
class Face final : public RenderedFace
{
public:
    explicit Face(const Theme& theme) : RenderedFace(lf::width, lf::height), theme_(theme) {}

    // The parameter each knob shows (lumenface::knobs, then serviceKnobs).
    std::array<int, lf::knobs.size()> knobParam{};
    std::array<int, lf::serviceKnobs.size()> serviceParam{};

private:
    // ----- the static layer --------------------------------------------------------------------
    void paintStatic(juce::Graphics& g) const override
    {
        if (artPass())
        {
            if (const auto deck = part("deck.jpg"); deck.isValid())
                hardware::drawScaled(g, deck, {0.0f, 0.0f, lf::width, lf::height});
            else
                paintFlat(g);
            paintTicks(g);
            paintRules(g);
        }
        paintCaseWords(g);
        paintPlateWords(g);
    }

    // Without the renders: the piano in flat token colours.
    void paintFlat(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        g.setColour(hw.lumenCase);
        g.fillRect(0.0f, 0.0f, lf::width, lf::trimTop);
        g.setColour(hw.aluminium);
        g.fillRect(0.0f, lf::trimTop, lf::width, lf::plateTop - lf::trimTop);
        g.setGradientFill(juce::ColourGradient(
            hw.lumenPlate, 0.0f, lf::plateTop, hw.lumenPlateDark, 0.0f, lf::height, false));
        g.fillRect(0.0f, lf::plateTop, lf::width, lf::height - lf::plateTop);
        g.setColour(hw.aluminium);
        g.fillRoundedRectangle(lf::windowBezel, 4.0f);
        g.setColour(hw.lumenWindow);
        g.fillRect(lf::windowGlass);
        g.setColour(hw.lumenNameplate);
        g.fillRoundedRectangle(lf::nameplate, 3.0f);
        const auto knob = [&](juce::Point<float> c, float skirt)
        {
            g.setColour(hw.knobBody);
            g.fillEllipse(juce::Rectangle<float>(skirt, skirt).withCentre(c));
            g.setColour(hw.aluminium);
            g.fillEllipse(juce::Rectangle<float>(skirt * 0.56f, skirt * 0.56f).withCentre(c));
        };
        for (const auto& k : lf::knobs)
            knob(k.centre, lf::skirt);
        for (const auto& k : lf::serviceKnobs)
            knob(k.centre, lf::serviceSkirt);
        for (const auto& c : lf::vibratoLamps)
        {
            g.setColour(hw.aluminium);
            g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(c));
        }
        g.setColour(hw.aluminium);
        g.fillEllipse(juce::Rectangle<float>(26.0f, 26.0f).withCentre(lf::pilotLamp));
    }

    // Each knob's printed scale: eleven ticks over its turn, every fifth longer.
    void paintTicks(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        g.setColour(hw.lumenPlateInk.withAlpha(0.8f));
        for (const auto& k : lf::knobs)
            hardware::drawTicks(
                g, k.centre, lf::skirt * 0.5f + 5.0f, lf::skirt * 0.5f + 10.0f, 11, 1.4f, 5, 3.0f);
        g.setColour(hw.lumenCaseInkSoft);
        for (const auto& k : lf::serviceKnobs)
            hardware::drawTicks(g,
                                k.centre,
                                lf::serviceSkirt * 0.5f + 4.0f,
                                lf::serviceSkirt * 0.5f + 8.0f,
                                11,
                                1.2f,
                                5,
                                2.5f);
    }

    // A rule either side of each section's caption, out to the section's width.
    void paintRules(juce::Graphics& g) const
    {
        g.setColour(theme_.hardware.lumenRule);
        const auto font = legendFont(theme_, nameSize(14.0f), 700, 0.2f);
        for (const auto& c : lf::captions)
        {
            const float text = juce::GlyphArrangement::getStringWidth(font, c.text) + 16.0f;
            const float left = c.centreX - c.columnWidth * 0.5f, right = c.centreX + c.columnWidth * 0.5f;
            if (c.centreX - text * 0.5f - left > 6.0f)
                g.fillRect(juce::Rectangle<float>(left, lf::captionY, c.centreX - text * 0.5f - left, 1.0f));
            if (right - (c.centreX + text * 0.5f) > 6.0f)
                g.fillRect(juce::Rectangle<float>(
                    c.centreX + text * 0.5f, lf::captionY, right - c.centreX - text * 0.5f, 1.0f));
        }
    }

    void paintCaseWords(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The nameplate: the name, the kind and the model line.
        const auto plate = lf::nameplate;
        g.setColour(hw.lumenNameplateInk);
        g.setFont(legendFont(theme_, 48.0f, 700, 0.32f));
        print(g,
              "LUMEN",
              juce::Rectangle<float>(plate.getWidth() - 40.0f, 50.0f).withCentre({plate.getCentreX(), 58.0f}),
              juce::Justification::centred,
              true);
        if (!isCompact())
        {
            g.setColour(hw.lumenNameplateInkSoft);
            g.setFont(legendFont(theme_, 15.0f, 600, 0.16f));
            print(g,
                  juce::String::fromUTF8("ELECTRIC PIANO  \xc2\xb7  SUITCASE SEVENTY-THREE"),
                  juce::Rectangle<float>(plate.getWidth() - 40.0f, 18.0f)
                      .withCentre({plate.getCentreX(), 92.0f}));
            g.setFont(legendFont(theme_, 12.0f, 700, 0.3f));
            print(g,
                  juce::String::fromUTF8("NORTHLIGHT  \xc2\xb7  MODEL LM-73"),
                  juce::Rectangle<float>(plate.getWidth() - 40.0f, 14.0f)
                      .withCentre({plate.getCentreX(), 110.0f}));
        }
        // The service trims and the pilot lamp.
        g.setColour(hw.lumenCaseInk);
        g.setFont(legendFont(theme_, nameSize(13.0f), 600, nameTracking(0.12f)));
        for (size_t i = 0; i < lf::serviceKnobs.size(); ++i)
        {
            const auto& k = lf::serviceKnobs[i];
            print(g,
                  nameFor(serviceParam[i], k.name),
                  juce::Rectangle<float>(isCompact() ? 110.0f : 90.0f, isCompact() ? 22.0f : 16.0f)
                      .withCentre({k.centre.x, k.centre.y + lf::serviceNameBelow}));
        }
        print(g,
              "POWER",
              juce::Rectangle<float>(90.0f, 16.0f)
                  .withCentre({lf::pilotLamp.x, lf::pilotLamp.y + lf::serviceNameBelow}));
    }

    void paintPlateWords(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        g.setColour(hw.lumenPlateInk);
        g.setFont(legendFont(theme_, nameSize(14.0f), 700, nameTracking(0.2f)));
        for (const auto& c : lf::captions)
            print(g,
                  c.text,
                  juce::Rectangle<float>(c.columnWidth + 40.0f, 20.0f).withCentre({c.centreX, lf::captionY}));
        g.setFont(legendFont(theme_, nameSize(13.0f), 600, nameTracking(0.08f)));
        for (size_t i = 0; i < lf::knobs.size(); ++i)
        {
            const auto& k = lf::knobs[i];
            print(g,
                  nameFor(knobParam[i], k.name),
                  juce::Rectangle<float>(isCompact() ? 92.0f : 76.0f, isCompact() ? 24.0f : 16.0f)
                      .withCentre({k.centre.x, k.centre.y + lf::nameBelow + (isCompact() ? 4.0f : 0.0f)}));
        }
        // The lever's positions beside it, the lamps' sides under them.
        g.setColour(hw.lumenPlateInkSoft);
        g.setFont(legendFont(theme_, nameSize(11.0f), 700, nameTracking(0.12f)));
        const auto n = lf::leverNut;
        print(g, "PAN", {n.x + 18.0f, n.y - 34.0f, 40.0f, 14.0f}, juce::Justification::centredLeft);
        print(g, "OFF", {n.x + 18.0f, n.y - 7.0f, 40.0f, 14.0f}, juce::Justification::centredLeft);
        print(g, "AMP", {n.x + 18.0f, n.y + 20.0f, 40.0f, 14.0f}, juce::Justification::centredLeft);
        print(g,
              "L",
              juce::Rectangle<float>(16.0f, 14.0f).withCentre(lf::vibratoLamps[0].translated(0.0f, 22.0f)));
        print(g,
              "R",
              juce::Rectangle<float>(16.0f, 14.0f).withCentre(lf::vibratoLamps[1].translated(0.0f, 22.0f)));
    }

    // ----- what changes with the values -------------------------------------------------------
    void paintLive(juce::Graphics& g) const override
    {
        if (isCompact() || !value)
            return; // the knob in hand shows its value where its name is
        const auto& hw = theme_.hardware;
        g.setColour(hw.lumenPlateInkSoft);
        g.setFont(legendFont(theme_, 12.5f, 600, 0.04f));
        for (size_t i = 0; i < lf::knobs.size(); ++i)
        {
            const auto& k = lf::knobs[i];
            print(g,
                  value(knobParam[i]),
                  juce::Rectangle<float>(78.0f, 15.0f).withCentre({k.centre.x, k.centre.y + lf::valueBelow}));
        }
        g.setColour(hw.lumenCaseInkSoft);
        g.setFont(legendFont(theme_, 12.0f, 600, 0.04f));
        for (size_t i = 0; i < lf::serviceKnobs.size(); ++i)
        {
            const auto& k = lf::serviceKnobs[i];
            print(g,
                  value(serviceParam[i]),
                  juce::Rectangle<float>(84.0f, 14.0f)
                      .withCentre({k.centre.x, k.centre.y + lf::serviceValueBelow}));
        }
    }

    const Theme& theme_;
};

// ----- the card ----------------------------------------------------------------------------

class LumenCard final : public RenderedCard
{
public:
    LumenCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme), tines_(theme),
          pilot_(theme, lf::pilotLamp, lf::lampPatch, theme.hardware.lumenLamp),
          left_(theme, lf::vibratoLamps[0], 28.0f, theme.hardware.lumenAmber),
          right_(theme, lf::vibratoLamps[1], 28.0f, theme.hardware.lumenAmber)
    {
        addAndMakeVisible(face_);
        face_.addAndMakeVisible(tines_);
        face_.addAndMakeVisible(pilot_);
        face_.addAndMakeVisible(left_);
        face_.addAndMakeVisible(right_);

        // The knobs, each a rendered pointer turning over the body the deck shows.
        constexpr std::array<int, lf::knobs.size()> params{
            lu::bark,         lu::symmetry,    lu::bell,        lu::hardness,       lu::decay,
            lu::damper,       lu::velocity,    lu::noise,       lu::bass,           lu::treble,
            lu::drive,        lu::level,       lu::tremoloRate, lu::tremoloSync,    lu::tremoloDepth,
            lu::tremoloShape, lu::phaserRate,  lu::phaserDepth, lu::phaserFeedback, lu::phaserMix,
            lu::chorusRate,   lu::chorusDepth, lu::chorusMix};
        for (size_t i = 0; i < params.size(); ++i)
        {
            knobs_[i] =
                &bindRenderedKnob(params[i],
                                  lf::onCap(lf::knobs[i]),
                                  lf::skirt,
                                  lf::pointerImage,
                                  part(juce::String::formatted("pointer-%02d.png", static_cast<int>(i))));
            face_.knobParam[i] = params[i];
        }
        constexpr std::array<int, lf::serviceKnobs.size()> service{lu::tune, lu::age, lu::voices};
        for (size_t i = 0; i < service.size(); ++i)
        {
            serviceKnobs_[i] = &bindRenderedKnob(
                service[i],
                lf::onCap(lf::serviceKnobs[i]),
                lf::serviceSkirt,
                lf::servicePointerImage,
                part(juce::String::formatted("pointer-%02d.png", static_cast<int>(lf::knobs.size() + i))));
            face_.serviceParam[i] = service[i];
        }
        const auto hz = [](double v) { return juce::String(v, v < 1.0 ? 2 : 1) + " Hz"; };
        rateKnob_ = knobFor(lu::tremoloRate);
        rateKnob_->format = [this, hz](double v)
        {
            // Synced, the rate is a note value.
            if (const int c = plugins::sync::choiceOf(param(lu::tremoloSync)); c > 0)
                return juce::String(plugins::sync::noteValues[static_cast<size_t>(c)].name);
            return hz(v);
        };
        knobFor(lu::phaserRate)->format = hz;
        knobFor(lu::chorusRate)->format = hz;
        auto* sync = knobFor(lu::tremoloSync);
        sync->setInterval(1.0);
        sync->format = [](double v)
        {
            const int c = plugins::sync::choiceOf(static_cast<float>(v));
            return c == 0 ? juce::String("Free")
                          : juce::String(plugins::sync::noteValues[static_cast<size_t>(c)].name);
        };
        knobFor(lu::tune)->format = [](double v)
        {
            const bool whole = std::abs(v - std::round(v)) < 0.05;
            return juce::String(v, whole ? 0 : 1) + " Hz";
        };
        knobFor(lu::voices)->setInterval(1.0);
        knobFor(lu::voices)->format = [](double v) { return juce::String(static_cast<int>(std::lround(v))); };
        face_.value = [this](int index)
        {
            if (auto* k = knobFor(index))
                return k->accessibleValueText();
            return juce::String();
        };

        // The vibrato's lever: PAN up, OFF in the middle, AMP down.
        const std::array<juce::String, 3> leverFiles{"lever-up.png", "lever-centre.png", "lever-down.png"};
        lever_ = &addLever("Vibrato",
                           {"Pan", "Off", "Amplitude"},
                           "tapeecho",
                           leverFiles,
                           lf::leverNut,
                           lf::leverScale,
                           52.0f * lf::leverScale);
        lever_->onChoose = [this](int position)
        {
            beginGesture("Vibrato");
            setParam(lu::tremoloMode,
                     static_cast<float>(lf::leverModes[static_cast<size_t>(std::clamp(position, 0, 2))]));
            endGesture();
            paramsChanged();
        };
        paramsChanged();
    }

private:
    int mode() const { return juce::jlimit(0, 2, static_cast<int>(std::lround(param(lu::tremoloMode)))); }

    void paramsChanged() override
    {
        lever_->setPosition(lf::leverPositionFor(mode()));
        rateKnob_->setAlpha(plugins::sync::choiceOf(param(lu::tremoloSync)) > 0 ? 0.5f : 1.0f);
        updateLamps();
        repaintChanged();
    }

    // The lever and the lamps show the mode themselves; the sync changes the
    // rate's printed value.
    bool printsWidely(int index) const override
    {
        juce::ignoreUnused(index);
        return false;
    }
    std::vector<int> alsoChanges(int index) const override
    {
        if (index == lu::tremoloSync)
            return {lu::tremoloRate};
        return {};
    }

    // The vibrato's lamps: each side's level as the processor applies it, dark when off.
    void updateLamps()
    {
        const auto m = static_cast<plugins::dsp::Tremolo::Mode>(mode());
        if (!isOn() || m == plugins::dsp::Tremolo::Mode::off)
        {
            left_.setBrightness(0.0f);
            right_.setBrightness(0.0f);
            return;
        }
        plugins::dsp::Tremolo t;
        t.value = theme_.motion.reduceMotion ? 0.0f : wave_ * 2.0f - 1.0f;
        float l = 1.0f, r = 1.0f;
        t.apply(l, r, m, param(lu::tremoloDepth) * 0.01f);
        const float full = m == plugins::dsp::Tremolo::Mode::pan ? 1.0f / std::sqrt(2.0f) : 1.0f;
        left_.setBrightness(l * full * 0.7f);
        right_.setBrightness(r * full * 0.7f);
    }

    void framesArrived(double elapsedMs) override
    {
        const double seconds = elapsedMs * 0.001;
        pilot_.setBrightness(isOn() ? 1.0f : 0.0f);
        // The newest frame carries the voices sounding and the tremolo's wave (0..1).
        int voices = 0;
        if (historySize() > 0)
        {
            const auto& last = historyAt(historySize() - 1);
            voices = static_cast<int>(std::lround(last.reduction));
            wave_ = std::clamp(last.extra, 0.0f, 1.0f);
        }
        updateLamps();
        // The tines swing, slowed, as wide as the output is loud; still with reduced
        // motion, when off or silent.
        const float level = std::clamp((outputDb() + 48.0f) / 48.0f, 0.0f, 1.0f);
        const bool still = theme_.motion.reduceMotion || !isOn() || (voices == 0 && level < 0.02f);
        swingLevel_ += ((still ? 0.0f : level) - swingLevel_) * 0.25f;
        phase_ = std::fmod(phase_ + static_cast<float>(seconds) * juce::MathConstants<float>::twoPi *
                                        lf::swingsPerSecond,
                           juce::MathConstants<float>::twoPi);
        tines_.setSwing(swingLevel_ < 0.01f ? 0.0f : lf::swingFor(swingLevel_, phase_));
    }

    Face face_;
    Tines tines_;
    Lamp pilot_, left_, right_;
    std::array<Knob*, lf::knobs.size()> knobs_{};
    std::array<Knob*, lf::serviceKnobs.size()> serviceKnobs_{};
    Knob* rateKnob_ = nullptr;
    HardwareLever* lever_ = nullptr;
    float wave_ = 0.5f, swingLevel_ = 0.0f, phase_ = 0.0f;
};
} // namespace

std::unique_ptr<ModuleCard>
createLumenCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<LumenCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
