// Patina's card (screens/plugins.md 7.2a), a hardware face: a worn studio rack
// unit, its hammertone paint rubbed through to bare metal. Along the top a
// nameplate, the preset selector (two bakelite keys and an amber display) and
// the pilot lamp. Below them six lit meter windows in signal order, each a live
// readout of what its stage is doing to the sound right now (Heat's curve with
// the level riding it, Chamber's tail, Drift's pitch, Grain's noise against the
// music, Wear's surviving bandwidth, Crush's waveform), with its slide switch
// and a row of lamps for its amount as Age and Breath leave it. A window is also
// the stage's tab: the chicken-head knobs under the row are the selected
// stage's, between Age (the macro) on the left and the output section on the
// right. The colours are the hardware tokens; the textures are assets/patina.

#include "plugins/Patina.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/EffectPresetLibrary.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/PresetBar.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace northlight::ui::strip
{

namespace pa = plugins::patina;

namespace
{
using Telemetry = pa::Telemetry;
constexpr int historyFrames = 600; // three seconds of 5 ms frames
constexpr std::array<const char*, pa::stageCount> stageNames{
    "Heat", "Chamber", "Drift", "Grain", "Wear", "Crush"};

// The stage the controls show, remembered for the session.
int& selectedStage()
{
    static int stage = pa::heat;
    return stage;
}

juce::String percentText(double v)
{
    return juce::String(static_cast<int>(std::lround(v))) + "%";
}

juce::String signedPercent(double v)
{
    const int i = static_cast<int>(std::lround(v));
    if (i == 0)
        return "0%";
    return (i > 0 ? "+" : juce::String::fromUTF8("\xe2\x88\x92")) + juce::String(std::abs(i)) + "%";
}

juce::String shortHz(double hz)
{
    if (hz >= 10000.0)
        return juce::String(static_cast<int>(std::lround(hz / 1000.0))) + " kHz";
    if (hz >= 1000.0)
        return juce::String(hz / 1000.0, 1) + " kHz";
    return juce::String(static_cast<int>(std::lround(hz))) + " Hz";
}

// A level in whole dB with a true minus sign.
juce::String wholeDb(float db)
{
    const int i = static_cast<int>(std::lround(db));
    return (i < 0 ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String()) + juce::String(std::abs(i)) +
           " dB";
}

juce::String minusDb(float db)
{
    return db < 0.05f ? juce::String("0.0 dB")
                      : juce::String::fromUTF8("\xe2\x88\x92") + juce::String(db, 1) + " dB";
}

// The face's images (assets/patina); invalid when missing, and the face falls
// back to its flat token colours.
const juce::Image& faceImage(const char* file)
{
    return hardware::image("patina", file);
}

void focusRing(juce::Graphics& g, juce::Component& c, juce::Rectangle<float> r, float radius)
{
    if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&c.getLookAndFeel()))
        laf->drawFocusRing(g, c, r, radius);
}

class PatinaCard;

// ----- the face's small parts ---------------------------------------------------------------------

// A stage's slide switch: the lever to the right is on.
class StageSwitch final : public juce::Button
{
public:
    explicit StageSwitch(const Theme& theme) : juce::Button("Switch"), theme_(theme)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    static constexpr int width = 22, height = 12;

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto& hw = theme_.hardware;
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre(width, height);
        g.setColour(hw.shadow.withAlpha(0.6f));
        g.fillRoundedRectangle(r.expanded(1.0f), 3.0f);
        const bool on = getToggleState();
        // The light lever sits in the right half when on, the left when off.
        auto lever = r.reduced(1.0f);
        if (on)
            lever.removeFromLeft(lever.getWidth() * 0.5f);
        else
            lever.removeFromRight(lever.getWidth() * 0.5f);
        g.setColour(hw.switchDark);
        g.fillRoundedRectangle(r.reduced(1.0f), 2.0f);
        auto light = over || down ? hw.switchLight.brighter(0.1f) : hw.switchLight;
        g.setGradientFill(juce::ColourGradient(
            light, 0.0f, lever.getY(), light.darker(0.25f), 0.0f, lever.getBottom(), false));
        g.fillRoundedRectangle(lever, 1.5f);
        g.setColour(hw.shadow.withAlpha(0.35f));
        g.drawVerticalLine(
            static_cast<int>(on ? lever.getX() : lever.getRight() - 1.0f), lever.getY(), lever.getBottom());
        focusRing(g, *this, r.expanded(1.0f), 3.0f);
    }

private:
    const Theme& theme_;
};

// A bakelite key beside the preset display, with an arrow.
class RackKey final : public juce::Button
{
public:
    RackKey(const Theme& theme, bool forward)
        : juce::Button(forward ? "Next preset" : "Previous preset"), theme_(theme), forward_(forward)
    {
        setTooltip(forward ? "Next preset" : "Previous preset");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto& hw = theme_.hardware;
        auto r = getLocalBounds().toFloat();
        if (down)
            r = r.translated(0.0f, 1.0f);
        const auto top = over && !down ? hw.keyTop.brighter(0.12f) : hw.keyTop;
        g.setGradientFill(juce::ColourGradient(top,
                                               r.getX() + r.getWidth() * 0.4f,
                                               r.getY() + r.getHeight() * 0.25f,
                                               hw.keyBottom,
                                               r.getRight(),
                                               r.getBottom(),
                                               true));
        g.fillRoundedRectangle(r, 3.0f);
        if (!down)
        {
            g.setColour(hw.highlight.withAlpha(0.14f));
            g.drawHorizontalLine(static_cast<int>(r.getY()) + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
            g.setColour(hw.shadow.withAlpha(0.5f));
            g.drawHorizontalLine(static_cast<int>(r.getBottom()) - 1, r.getX() + 2.0f, r.getRight() - 2.0f);
        }
        const auto c = r.getCentre();
        juce::Path arrow;
        const float s = 4.0f;
        if (forward_)
            arrow.addTriangle(c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 0.8f, c.y);
        else
            arrow.addTriangle(c.x + s * 0.7f, c.y - s, c.x + s * 0.7f, c.y + s, c.x - s * 0.8f, c.y);
        g.setColour(hw.keyGlyph.withAlpha(over ? 0.95f : 0.75f));
        g.fillPath(arrow);
        focusRing(g, *this, r, 3.0f);
    }

private:
    const Theme& theme_;
    const bool forward_;
};

// The preset's amber display: its number and name; a click lists the presets.
class PresetDisplay final : public juce::Button
{
public:
    explicit PresetDisplay(const Theme& theme) : juce::Button("Presets"), theme_(theme)
    {
        setTooltip("Presets");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void show(const juce::String& number, const juce::String& name)
    {
        number_ = number;
        name_ = name;
        setButtonText(name);
        repaint();
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto& hw = theme_.hardware;
        const auto r = getLocalBounds().toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, 2.0f);
        g.reduceClipRegion(clip);
        juce::ColourGradient glass(hw.displayCentre,
                                   r.getCentreX(),
                                   r.getY() + r.getHeight() * 0.6f,
                                   hw.displayEdge,
                                   r.getX(),
                                   r.getY(),
                                   true);
        g.setGradientFill(glass);
        g.fillRect(r);

        auto line = r.reduced(10.0f, 0.0f);
        const auto mono = theme_.value();
        g.setFont(mono.withHeight(mono.getHeight() * 0.85f));
        g.setColour(hw.displayDim.withAlpha(0.75f));
        const auto numberArea = line.removeFromLeft(18.0f);
        g.drawText(number_, numberArea, juce::Justification::centredLeft, false);
        line.removeFromLeft(4.0f);
        auto big = mono.withHeight(mono.getHeight() * 1.05f).withExtraKerningFactor(0.06f);
        g.setFont(big);
        const auto name = name_.toUpperCase();
        // The glow round the lit letters, then the letters.
        g.setColour(hw.displayGlow.withAlpha(0.22f));
        for (const auto& d : {juce::Point<float>(-1.0f, 0.0f),
                              juce::Point<float>(1.0f, 0.0f),
                              juce::Point<float>(0.0f, -1.0f),
                              juce::Point<float>(0.0f, 1.0f)})
            g.drawText(name, line.translated(d.x, d.y), juce::Justification::centredLeft, true);
        g.setColour(over ? hw.displayText.brighter(0.15f) : hw.displayText);
        g.drawText(name, line, juce::Justification::centredLeft, true);

        // Scan lines, the glass's reflection and its shade under the bezel's lip.
        g.setColour(hw.shadow.withAlpha(0.12f));
        for (float y = r.getY() + 1.0f; y < r.getBottom(); y += 3.0f)
            g.fillRect(r.getX(), y, r.getWidth(), 1.0f);
        g.setGradientFill(juce::ColourGradient(hw.highlight.withAlpha(0.13f),
                                               r.getX(),
                                               r.getY(),
                                               hw.highlight.withAlpha(0.0f),
                                               r.getX() + r.getWidth() * 0.1f,
                                               r.getY() + r.getHeight() * 0.45f,
                                               false));
        g.fillRect(r);
        g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.8f),
                                               0.0f,
                                               r.getY(),
                                               hw.shadow.withAlpha(0.0f),
                                               0.0f,
                                               r.getY() + 4.0f,
                                               false));
        g.fillRect(r.withHeight(4.0f));
        focusRing(g, *this, r, 2.0f);
    }

private:
    const Theme& theme_;
    juce::String number_, name_;
};

// The pilot lamp: lit while Patina is on.
class Lamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Lamp(const Theme& theme, std::function<bool()> on) : theme_(theme), on_(std::move(on)) {}
    static constexpr float diameter = 26.0f;

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        const bool lit = on_();
        setTooltip(lit ? "On" : "Off: the insert is bypassed");
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto lens = juce::Rectangle<float>(diameter, diameter).withCentre(c);
        if (lit)
        {
            // The glow spilling onto the panel.
            juce::ColourGradient glow(hw.lamp.withAlpha(0.5f),
                                      c.x,
                                      c.y,
                                      hw.lamp.withAlpha(0.0f),
                                      c.x + getWidth() * 0.5f,
                                      c.y,
                                      true);
            g.setGradientFill(glow);
            g.fillEllipse(getLocalBounds().toFloat());
        }
        g.setColour(hw.shadow.withAlpha(0.7f));
        g.fillEllipse(lens.translated(0.0f, 2.0f).expanded(1.0f));
        const auto& image = faceImage("lamp.jpg");
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addEllipse(lens);
            g.reduceClipRegion(clip);
            if (image.isValid())
            {
                g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                g.drawImage(image, lens, juce::RectanglePlacement::fillDestination);
            }
            else
            {
                g.setGradientFill(juce::ColourGradient(
                    hw.lamp.brighter(0.4f), c.x, c.y, hw.lamp.darker(0.6f), lens.getRight(), c.y, true));
                g.fillEllipse(lens);
            }
            if (!lit)
            {
                g.setColour(hw.shadow.withAlpha(0.6f));
                g.fillEllipse(lens);
            }
        }
    }

private:
    const Theme& theme_;
    std::function<bool()> on_;
};

// ----- a stage's window ---------------------------------------------------------------------------

class StageTile final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StageTile(PatinaCard& card, int stage);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void sync();
    StageSwitch& power() noexcept { return power_; }
    // The window's glass, where the readout draws.
    juce::Rectangle<float> well() const;

private:
    // What a readout draws with: the window's ink, lit or unlit.
    struct Inks
    {
        juce::Colour hue, textPrimary, textSecondary, textTertiary, gridBar, dotRing;
    };

    juce::Rectangle<float> frame() const;
    void paintHeat(juce::Graphics& g, juce::Rectangle<float> p);
    void paintChamber(juce::Graphics& g, juce::Rectangle<float> p);
    void paintDrift(juce::Graphics& g, juce::Rectangle<float> p);
    void paintGrain(juce::Graphics& g, juce::Rectangle<float> p);
    void paintWear(juce::Graphics& g, juce::Rectangle<float> p);
    void paintCrush(juce::Graphics& g, juce::Rectangle<float> p);
    void readout(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text, juce::Colour colour);
    void scaleLabel(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text);

    PatinaCard& card_;
    const int stage_;
    StageSwitch power_;
    Inks inks_;
    std::vector<float> a_, b_; // per column, reused
    std::vector<float> scopeIn_, scopeOut_;
};

// ----- the card -----------------------------------------------------------------------------------

class PatinaCard final : public ModuleCard, private juce::ChangeListener
{
public:
    PatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), history_(static_cast<size_t>(historyFrames)),
          previous_(theme, false), next_(theme, true), display_(theme),
          lamp_(theme, [this] { return isOn(); })
    {
        for (int s = 0; s < pa::stageCount; ++s)
        {
            tiles_[static_cast<size_t>(s)] = std::make_unique<StageTile>(*this, s);
            addAndMakeVisible(*tiles_[static_cast<size_t>(s)]);
        }

        age_ = &addKnob(pa::age);
        age_->setRingSize(ageRing);
        age_->setTooltip("Age: every stage's amount at once. Half way plays them as set; down to nothing "
                         "makes the sound new again, up to full ruins it");
        age_->format = [](double v) { return percentText(v); };
        pace_ = &addKnob(pa::pace);
        pace_->format = [](double v) { return juce::String(0.05 * std::pow(30.0, v * 0.01), 2) + " Hz"; };
        pace_->setTooltip("How quickly the stages breathe: each stage's Breath lets its amount wander at "
                          "this pace, every stage on its own path");

        // Heat
        auto& heat = panel(pa::heat);
        selector(heat,
                 pa::heatCharacter,
                 {"Tube", "Tape", "Iron", "Fuzz"},
                 "Character",
                 {"Tube: a biased valve, soft, with even harmonics",
                  "Tape: the highs saturate first, with a head bump in the lows",
                  "Iron: a transformer, the lows driven hardest, a firm knee",
                  "Fuzz: a broken stage, starved so quiet notes sputter, then clipped"});
        main(heat, pa::heatDrive, "Drive", "How hard the stage is driven; the level is held as it rises");
        knob(heat, pa::heatTone, "Tone", "A tilt after the curve, dark to bright", signedPercent);
        breath(heat, pa::heatBreath);

        // Chamber
        auto& chamber = panel(pa::chamber);
        selector(chamber,
                 pa::chamberType,
                 {"Room", "Spring", "Plate"},
                 "Space",
                 {"Room: a small, boxy room",
                  "Spring: a reverb tank's spring, thin with a chirp",
                  "Plate: a dense, bright plate"});
        main(chamber, pa::chamberMix, "Mix", "How much of the space is heard over the sound");
        knob(chamber,
             pa::chamberDecay,
             "Decay",
             "How long the space rings",
             [this](double v)
             {
                 const float s = pa::chamberDecaySeconds(
                     static_cast<int>(std::lround(param(pa::chamberType))), static_cast<float>(v) * 0.01f);
                 return juce::String(s, 1) + " s";
             });
        knob(chamber, pa::chamberTone, "Tone", "The space's colour, dark to bright", signedPercent);
        breath(chamber, pa::chamberBreath);

        // Drift
        auto& drift = panel(pa::drift);
        selector(drift,
                 pa::driftMachine,
                 {"Reel", "Deck", "Disc"},
                 "Machine",
                 {"Reel: studio tape, a slow wandering wow and fine flutter",
                  "Deck: a cassette deck, quicker and rougher, with scrape",
                  "Disc: a record, a warp turning at 33 rpm"});
        main(drift, pa::driftWow, "Wow", "The slow pitch wander, never quite regular");
        knob(drift, pa::driftFlutter, "Flutter", "The fast flutter of the capstan", percentText);
        knob(drift,
             pa::driftRate,
             "Rate",
             "The machine's speed of wobble, half to double",
             [](double v)
             {
                 return juce::String(std::pow(2.0, 2.0 * v * 0.01 - 1.0), 2) +
                        juce::String::fromUTF8("\xc3\x97");
             });
        knob(drift, pa::driftStereo, "Stereo", "How far the two sides wobble apart", percentText);
        breath(drift, pa::driftBreath);

        // Grain
        auto& grain = panel(pa::grain);
        selector(grain,
                 pa::grainSource,
                 {"Vinyl", "Tape", "50Hz", "60Hz", "Air"},
                 "Source",
                 {"Vinyl: crackle and pops, surface noise and a low rumble",
                  "Tape: shaped tape hiss",
                  "Hum at 50 Hz: mains hum with its harmonics",
                  "Hum at 60 Hz: mains hum with its harmonics",
                  "Air: a room's tone, soft and wide over a low drone"});
        main(grain, pa::grainAmount, "Amount", "The noise's level");
        texture_ = &knob(grain, pa::grainTexture, "Texture", "", [this](double v) { return textureText(v); });
        knob(grain,
             pa::grainFollow,
             "Follow",
             "Left: the noise ducks under the music. Right: it rides with the music and is gone in silence",
             [](double v)
             {
                 const int i = static_cast<int>(std::lround(v));
                 return i == 0 ? juce::String("Steady")
                               : juce::String(i < 0 ? "Duck " : "Ride ") + juce::String(std::abs(i));
             });
        breath(grain, pa::grainBreath);

        // Wear
        auto& wear = panel(pa::wear);
        main(wear, pa::wearLoss, "Loss", "The highs a worn medium has lost");
        knob(wear,
             pa::wearDropouts,
             "Dropouts",
             "How often and how deep the level and highs dip",
             percentText);
        knob(wear,
             pa::wearSkew,
             "Skew",
             "Azimuth: the sides drift apart in time, tone and level",
             percentText);
        breath(wear, pa::wearBreath);

        // Crush
        auto& crush = panel(pa::crush);
        selector(crush,
                 pa::crushFlavour,
                 {"Soft", "Hard", juce::String::fromUTF8("\xce\xbc-law"), "Jitter"},
                 "Flavour",
                 {"Soft: filtered converters, smooth but for the bits",
                  "Hard: no filters, the aliasing of an early sampler",
                  "Mu-law: companded, an 8-bit telephone or drum machine",
                  "Jitter: no filters and an unsteady clock"});
        main(crush,
             pa::crushBits,
             "Bits",
             "The converter's bit depth",
             [](double v) { return juce::String(v, std::abs(v - std::round(v)) < 0.05 ? 0 : 1) + " bit"; });
        knob(
            crush, pa::crushRate, "Rate", "The converter's sample rate", [](double v) { return shortHz(v); });
        breath(crush, pa::crushBreath);

        // Output
        for (const int index : {pa::tone, pa::lowCut, pa::highCut, pa::width, pa::output})
            outputs_.push_back(&addKnob(index));
        outputs_[0]->format = signedPercent;
        outputs_[0]->setTooltip("A tilt over everything, dark to bright");
        outputs_[1]->format = [](double hz) { return hz <= 20.5 ? juce::String("Off") : shortHz(hz); };
        outputs_[1]->setTooltip("Cuts the lows, as a small speaker or a thin medium would");
        outputs_[2]->format = [](double hz) { return hz >= 19900.0 ? juce::String("Off") : shortHz(hz); };
        outputs_[2]->setTooltip("Cuts the highs: the band of a radio, a phone or an old record");
        outputs_[3]->format = percentText;
        outputs_[3]->setTooltip("Stereo width: mono at nothing, wider past 100%");
        outputs_[4]->setTooltip("The level out");

        // Every knob is a chicken head over a printed scale, its legends printed on the panel.
        std::vector<Knob*> all{age_, pace_};
        all.insert(all.end(), outputs_.begin(), outputs_.end());
        for (auto& p : panels_)
        {
            all.push_back(p.main);
            all.insert(all.end(), p.knobs.begin(), p.knobs.end());
        }
        for (auto* k : all)
        {
            k->setStyle(Knob::Style::pointer);
            if (auto* l = labelFor(*k))
                l->setColour(juce::Label::textColourId, theme_.hardware.legend);
        }
        for (auto& p : panels_)
            if (p.caption != nullptr)
                p.caption->setColour(juce::Label::textColourId, theme_.hardware.legend);

        // The preset selector and the lamp.
        previous_.onClick = [this] { stepPreset(-1); };
        next_.onClick = [this] { stepPreset(1); };
        display_.onClick = [this] { showPresets(); };
        for (auto* c : std::initializer_list<juce::Component*>{&previous_, &next_, &display_, &lamp_})
            addAndMakeVisible(c);

        select(selectedStage());
        EffectPresetLibrary::instance().addChangeListener(this);
        paramsChanged();
    }

    ~PatinaCard() override { EffectPresetLibrary::instance().removeChangeListener(this); }

    // ----- for the tiles ------------------------------------------------------------------------
    int selected() const noexcept { return selected_; }
    bool stageOn(int stage) const noexcept { return param(pa::onParam(stage)) >= 0.5f; }
    int historyCount() const noexcept { return count_; }
    std::uint64_t historyTotal() const noexcept { return total_; }
    const Telemetry::Frame& frameAt(int i) const noexcept
    {
        return history_[static_cast<size_t>((head_ - count_ + i + historyFrames) % historyFrames)];
    }
    const Telemetry::Frame& newest() const noexcept
    {
        static const Telemetry::Frame none{};
        return count_ > 0 ? frameAt(count_ - 1) : none;
    }
    const Telemetry* telemetry() const noexcept { return pa::telemetryOf(liveProcessor()); }
    // The amount a stage is set to after Age (where its lamps' dim tail ends).
    float setAmount(int stage) const noexcept
    {
        const float a = param(pa::age) * 0.01f;
        switch (stage)
        {
            case pa::heat:
                return pa::aged(param(pa::heatDrive) * 0.01f, a);
            case pa::chamber:
                return pa::aged(param(pa::chamberMix) * 0.01f, a);
            case pa::drift:
                return pa::aged(std::max(param(pa::driftWow), param(pa::driftFlutter)) * 0.01f, a);
            case pa::grain:
                return pa::aged(param(pa::grainAmount) * 0.01f, a);
            case pa::wear:
                return pa::aged(
                    std::max({param(pa::wearLoss), param(pa::wearDropouts), param(pa::wearSkew)}) * 0.01f, a);
            default:
                return pa::aged(std::max(pa::crushAmountOfBits(param(pa::crushBits)),
                                         pa::crushAmountOfRate(param(pa::crushRate))),
                                a);
        }
    }
    // The short name of a stage's choice ("Tube", "Spring").
    juce::String choiceName(int stage) const
    {
        static constexpr int choiceParams[] = {
            pa::heatCharacter, pa::chamberType, pa::driftMachine, pa::grainSource, -1, pa::crushFlavour};
        const int index = choiceParams[stage];
        if (index < 0)
            return {};
        const auto& choices = info(index).choices;
        const auto i = static_cast<size_t>(
            std::clamp(static_cast<int>(std::lround(param(index))), 0, static_cast<int>(choices.size()) - 1));
        auto name = juce::String::fromUTF8(choices[i].c_str());
        return name.replace("Tape hiss", "Hiss").replace("Room air", "Air").replace("Hum ", "");
    }

    void select(int stage)
    {
        selected_ = std::clamp(stage, 0, pa::stageCount - 1);
        selectedStage() = selected_;
        for (int s = 0; s < pa::stageCount; ++s)
            for (auto* c : panels_[static_cast<size_t>(s)].components)
                c->setVisible(s == selected_ && !isCollapsed());
        dimStage();
        for (auto& t : tiles_)
            t->repaint();
        if (!getLocalBounds().isEmpty())
            resized();
        repaint();
    }

    void toggleStage(int stage)
    {
        const int index = pa::onParam(stage);
        beginGesture(juce::String(stageNames[static_cast<size_t>(stage)]) +
                     (stageOn(stage) ? " off" : " on"));
        setParam(index, stageOn(stage) ? 0.0f : 1.0f);
        endGesture();
    }

    int preferredWidth() const override
    {
        return ageCellWidth + knobCellWidth() + 2 * ruleWidth() + stagePanelWidth() + 5 * knobCellWidth() +
               4 * theme_.spacing.sm + 2 * faceInset;
    }
    int preferredBodyHeight() const override { return 372; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        paintFace(g);
        // The selected stage's name over its controls.
        g.setFont(theme_.captionCaps());
        hardware::drawLegend(g,
                             theme_,
                             juce::String(stageNames[static_cast<size_t>(selected_)]).toUpperCase(),
                             captionArea_.toFloat(),
                             juce::Justification::centredLeft,
                             theme_.hardware.legend.withAlpha(0.85f));
    }

private:
    static constexpr int ageRing = 56;
    static constexpr int ageCellWidth = 84;
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;
    static constexpr int faceInset = 8; // the panel's margin round its parts
    static constexpr int headerH = 40;  // the nameplate's row
    static constexpr int presetW = 280; // the preset selector's bezel
    static constexpr int presetH = 32;
    static constexpr int lampBox = 40;         // the lamp with its glow
    static constexpr int nameplateBorder = 14; // the plate image's screw ends, drawn at this size

    struct Panel
    {
        std::vector<juce::Component*> components;
        RotarySelector* selector = nullptr;
        juce::Label* caption = nullptr;
        Knob* main = nullptr;
        std::vector<Knob*> knobs;
    };

    int ruleWidth() const noexcept { return 2 * theme_.spacing.sm + 1; }
    int selectorWidth() const noexcept
    {
        return selectors_.empty() ? 104 : selectors_.front().selector->preferredWidth();
    }
    int panelWidth(const Panel& p) const noexcept
    {
        return (p.selector != nullptr ? selectorWidth() : 0) + bigCellWidth +
               static_cast<int>(p.knobs.size()) * knobCellWidth();
    }
    int stagePanelWidth() const noexcept
    {
        int w = 0;
        for (const auto& p : panels_)
            w = std::max(w, panelWidth(p));
        return w;
    }

    Panel& panel(int stage) { return panels_[static_cast<size_t>(stage)]; }

    struct Selector
    {
        RotarySelector* selector = nullptr;
        int index = 0;
    };

    void selector(Panel& p,
                  int index,
                  std::vector<juce::String> labels,
                  const juce::String& caption,
                  std::vector<juce::String> full)
    {
        auto& s = ownedSelectors_.emplace_back(std::make_unique<RotarySelector>(theme_));
        s->setPositions(std::move(labels), std::move(full), caption);
        s->setHue(colour());
        s->setHardware(true);
        s->setDefault(static_cast<int>(std::lround(info(index).defaultValue)));
        s->setSelected(static_cast<int>(std::lround(param(index))), juce::dontSendNotification);
        s->onChange = [this, index](int value) { setParam(index, static_cast<float>(value)); };
        const auto name = juce::String(info(index).name);
        s->onGestureBegin = [this, name] { beginGesture(name); };
        s->onGestureEnd = [this] { endGesture(); };
        addChildComponent(*s);
        auto& label = ownedLabels_.emplace_back(std::make_unique<juce::Label>());
        label->setText(caption, juce::dontSendNotification);
        label->setFont(theme_.caption());
        label->setJustificationType(juce::Justification::centred);
        label->setInterceptsMouseClicks(false, false);
        addChildComponent(*label);
        p.selector = s.get();
        p.caption = label.get();
        p.components.push_back(s.get());
        p.components.push_back(label.get());
        selectors_.push_back({s.get(), index});
    }

    // A knob of a stage's panel (hidden until its stage is selected).
    Knob& knob(Panel& p,
               int index,
               const juce::String& label,
               const juce::String& tooltip,
               std::function<juce::String(double)> format)
    {
        auto& k = addKnob(index, label);
        if (tooltip.isNotEmpty())
            k.setTooltip(tooltip);
        k.format = std::move(format);
        k.setVisible(false);
        p.components.push_back(&k);
        if (auto* l = labelFor(k))
        {
            l->setVisible(false);
            p.components.push_back(l);
        }
        p.knobs.push_back(&k);
        return k;
    }

    void main(Panel& p,
              int index,
              const juce::String& label,
              const juce::String& tooltip,
              std::function<juce::String(double)> format = percentText)
    {
        auto& k = knob(p, index, label, tooltip, std::move(format));
        p.knobs.pop_back();
        k.setRingSize(bigRing);
        p.main = &k;
    }

    void breath(Panel& p, int index)
    {
        knob(p,
             index,
             "Breath",
             "Lets the stage's amount wander over time, down toward nothing at full and back, at the "
             "Pace beside Age",
             percentText);
    }

    juce::String textureText(double v) const
    {
        const int source = static_cast<int>(std::lround(param(pa::grainSource)));
        const double t = v * 0.01;
        if (source == pa::vinyl)
            return juce::String(static_cast<int>(std::lround(2.0 * std::pow(150.0, t)))) + " /s";
        return percentText(v);
    }

    void updateTexture()
    {
        const int source = static_cast<int>(std::lround(param(pa::grainSource)));
        const char* names[] = {"Density", "Colour", "Buzz", "Buzz", "Tone"};
        const char* tips[] = {"Vinyl: how many crackles a second",
                              "Tape: the hiss's colour, dark to bright",
                              "Hum: a pure tone at nothing, a rough buzz at full",
                              "Hum: a pure tone at nothing, a rough buzz at full",
                              "Air: the room's tone, dark to bright"};
        const auto i = static_cast<size_t>(std::clamp(source, 0, 4));
        if (auto* l = labelFor(*texture_))
            l->setText(names[i], juce::dontSendNotification);
        texture_->setTooltip(tips[i]);
        texture_->repaint();
    }

    // A stage that is off: its controls fade (and stay usable).
    void dimStage()
    {
        const float alpha = stageOn(selected_) ? 1.0f : 0.5f;
        for (auto* c : panels_[static_cast<size_t>(selected_)].components)
            c->setAlpha(alpha);
    }

    // ----- presets ------------------------------------------------------------------------------
    // The same list as the header's preset bar (factory, then the user's), so the
    // two stay in step.

    void changeListenerCallback(juce::ChangeBroadcaster*) override { showPreset(); }

    int presetIndex() const { return EffectPresetLibrary::instance().match(pa::type, params_); }

    void showPreset()
    {
        const int i = presetIndex();
        if (i < 0)
            display_.show(juce::String::fromUTF8("\xe2\x80\x93\xe2\x80\x93"), "Custom");
        else
            display_.show(juce::String(i + 1).paddedLeft('0', 2),
                          EffectPresetLibrary::instance().presets(pa::type)[static_cast<size_t>(i)].name);
        display_.setTooltip("Presets: " + display_.getButtonText());
    }

    void applyPreset(int index)
    {
        const auto& list = EffectPresetLibrary::instance().presets(pa::type);
        if (index < 0 || static_cast<size_t>(index) >= list.size())
            return;
        const auto preset = list[static_cast<size_t>(index)];
        beginGesture("Preset " + preset.name);
        for (size_t i = 0; i < preset.values.size(); ++i)
            setParam(static_cast<int>(i), preset.values[i]);
        endGesture();
        showPreset();
    }

    void stepPreset(int direction)
    {
        const int n = static_cast<int>(EffectPresetLibrary::instance().presets(pa::type).size());
        if (n == 0)
            return;
        const int at = presetIndex();
        applyPreset(at < 0 ? (direction > 0 ? 0 : n - 1) : (at + direction + n) % n);
    }

    void showPresets()
    {
        juce::Component::SafePointer<PatinaCard> safe(this);
        showPresetMenu(display_,
                       theme_,
                       pa::type,
                       params_,
                       [safe](int index)
                       {
                           if (safe != nullptr)
                               safe->applyPreset(index);
                       });
    }

    // ----- the face -----------------------------------------------------------------------------

    juce::Font plateTitleFont() const
    {
        return theme_.font(Theme::Face::ui, 21.0f, theme_.fonts.weight.semibold).withExtraKerningFactor(0.3f);
    }
    juce::Font plateSubtitleFont() const
    {
        return theme_.font(Theme::Face::ui, 9.0f, theme_.fonts.weight.semibold).withExtraKerningFactor(0.18f);
    }
    int plateWidth() const
    {
        const float title = juce::GlyphArrangement::getStringWidth(plateTitleFont(), "PATINA");
        const float subtitle = juce::GlyphArrangement::getStringWidth(plateSubtitleFont(), "VINTAGE COLOUR");
        return static_cast<int>(std::ceil(title + subtitle)) + 2 * nameplateBorder + 2 * 12 + 24;
    }

    // The panel, its worn edge, the nameplate and the preset's bezel: drawn
    // once per size and scale into an image, as they only change then.
    void paintFace(juce::Graphics& g)
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (!faceCache_.isValid() || cacheScale_ != scale)
        {
            cacheScale_ = scale;
            const auto w = std::max(1, juce::roundToInt(static_cast<float>(face_.getWidth()) * scale));
            const auto h = std::max(1, juce::roundToInt(static_cast<float>(face_.getHeight()) * scale));
            faceCache_ = juce::Image(juce::Image::ARGB, w, h, true);
            juce::Graphics cg(faceCache_);
            cg.addTransform(juce::AffineTransform::translation(static_cast<float>(-face_.getX()),
                                                               static_cast<float>(-face_.getY()))
                                .scaled(scale));
            drawFace(cg);
        }
        g.drawImage(faceCache_, face_.toFloat());
    }

    void drawFace(juce::Graphics& g)
    {
        const auto& hw = theme_.hardware;
        const auto bounds = face_.toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, theme_.radius.md);
        g.reduceClipRegion(clip);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        // The panel: worn hammertone, the same at every Age.
        g.setColour(hw.panel);
        g.fillRect(bounds);
        if (const auto& worn = faceImage("panel-aged.jpg"); worn.isValid())
            g.drawImage(worn, bounds, juce::RectanglePlacement::fillDestination);
        // The edges rubbed back toward bare metal.
        constexpr float wearWidth = 20.0f, wearAlpha = 0.7f;
        for (float d = 0.0f; d < wearWidth; d += 1.0f)
        {
            const float t = 1.0f - d / wearWidth;
            g.setColour(hw.panelWear.withAlpha(wearAlpha * t * t * 0.22f));
            g.drawRect(bounds.reduced(d), 1.0f);
        }
        g.setColour(hw.highlight.withAlpha(0.12f));
        g.drawHorizontalLine(face_.getY() + 1, bounds.getX(), bounds.getRight());

        drawNameplate(g);

        // The preset selector's recess.
        const auto bezel = bezel_.toFloat();
        g.setColour(hw.highlight.withAlpha(0.14f));
        g.fillRoundedRectangle(bezel.translated(0.0f, 1.0f), 5.0f);
        g.setColour(hw.bezel);
        g.fillRoundedRectangle(bezel, 5.0f);
        g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.9f),
                                               0.0f,
                                               bezel.getY(),
                                               hw.shadow.withAlpha(0.0f),
                                               0.0f,
                                               bezel.getY() + 4.0f,
                                               false));
        g.fillRoundedRectangle(bezel.withHeight(5.0f), 5.0f);

        // The rules between the groups of knobs, engraved.
        for (const int x : dividers_)
        {
            g.setColour(hw.shadow.withAlpha(0.55f));
            g.drawVerticalLine(x, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
            g.setColour(hw.highlight.withAlpha(0.1f));
            g.drawVerticalLine(x + 1, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
        }
    }

    // The nameplate, nine-sliced from its image so the screws at its ends keep
    // their shape and the wear its scale at any width.
    void drawNameplate(juce::Graphics& g)
    {
        const auto& hw = theme_.hardware;
        const auto plate = plate_.toFloat();
        g.setColour(hw.shadow.withAlpha(0.6f));
        g.fillRect(plate.translated(0.0f, 2.0f));
        if (const auto& image = faceImage("nameplate.jpg"); image.isValid())
        {
            // The image's border is a third of its height; drawn at nameplateBorder.
            const int src = image.getHeight() * 3 / 10;
            const int sw = image.getWidth(), sh = image.getHeight();
            const float b = static_cast<float>(nameplateBorder);
            const float scale = b / static_cast<float>(src);
            const std::array<int, 4> sx{0, src, sw - src, sw}, sy{0, src, sh - src, sh};
            const std::array<float, 4> dx{
                plate.getX(), plate.getX() + b, plate.getRight() - b, plate.getRight()};
            const std::array<float, 4> dy{
                plate.getY(), plate.getY() + b, plate.getBottom() - b, plate.getBottom()};
            for (size_t row = 0; row < 3; ++row)
                for (size_t col = 0; col < 3; ++col)
                {
                    const juce::Rectangle<int> source(
                        sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]);
                    const auto dest = juce::Rectangle<float>::leftTopRightBottom(
                        dx[col], dy[row], dx[col + 1], dy[row + 1]);
                    if (source.isEmpty() || dest.isEmpty())
                        continue;
                    // Corners as they are; edges and the middle repeated at the corners' scale.
                    const auto piece = image.getClippedImage(source);
                    const float tw = static_cast<float>(source.getWidth()) * scale;
                    const float th = static_cast<float>(source.getHeight()) * scale;
                    const int nx = col == 1 ? std::max(1, juce::roundToInt(dest.getWidth() / tw)) : 1;
                    const int ny = row == 1 ? std::max(1, juce::roundToInt(dest.getHeight() / th)) : 1;
                    const float w = dest.getWidth() / static_cast<float>(nx);
                    const float h = dest.getHeight() / static_cast<float>(ny);
                    for (int y = 0; y < ny; ++y)
                        for (int x = 0; x < nx; ++x)
                            g.drawImage(piece,
                                        juce::Rectangle<float>(dest.getX() + w * static_cast<float>(x),
                                                               dest.getY() + h * static_cast<float>(y),
                                                               w,
                                                               h),
                                        juce::RectanglePlacement::stretchToFit);
                }
        }
        else
        {
            g.setColour(hw.plate);
            g.fillRect(plate);
        }
        // The lettering, worn into the enamel.
        auto text = plate.reduced(static_cast<float>(nameplateBorder) + 6.0f, 0.0f);
        g.setFont(plateTitleFont());
        g.setColour(hw.ink.withAlpha(0.85f));
        const float titleW = juce::GlyphArrangement::getStringWidth(plateTitleFont(), "PATINA");
        g.drawText("PATINA", text.removeFromLeft(titleW + 4.0f), juce::Justification::centredLeft, false);
        text.removeFromLeft(8.0f);
        g.setColour(hw.ink.withAlpha(0.35f));
        g.fillRect(text.removeFromLeft(1.0f).withSizeKeepingCentre(1.0f, 20.0f));
        text.removeFromLeft(9.0f);
        g.setFont(plateSubtitleFont());
        g.setColour(hw.ink.withAlpha(0.8f));
        const auto lines = text.withSizeKeepingCentre(text.getWidth(), 24.0f);
        g.drawText("VINTAGE COLOUR", lines.withHeight(12.0f), juce::Justification::centredLeft, false);
        g.drawText("PROCESSOR", lines.withTrimmedTop(12.0f), juce::Justification::centredLeft, false);
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        face_ = body.expanded(theme_.spacing.sm, theme_.spacing.xs);
        faceCache_ = {};
        body = body.reduced(faceInset, faceInset / 2);

        // The top row: nameplate left, preset selector and lamp right.
        auto header = body.removeFromTop(headerH);
        body.removeFromTop(theme_.spacing.md);
        plate_ = header.removeFromLeft(plateWidth()).withSizeKeepingCentre(plateWidth(), headerH);
        lamp_.setBounds(header.removeFromRight(lampBox).withSizeKeepingCentre(lampBox, lampBox));
        header.removeFromRight(theme_.spacing.md);
        bezel_ = header.removeFromRight(presetW).withSizeKeepingCentre(presetW, presetH);
        {
            auto inner = bezel_.reduced(2);
            previous_.setBounds(inner.removeFromLeft(presetH - 4));
            inner.removeFromLeft(2);
            next_.setBounds(inner.removeFromRight(presetH - 4));
            inner.removeFromRight(2);
            display_.setBounds(inner);
        }

        const int valueH = static_cast<int>(theme_.fonts.size.value) + 4;
        const int labelH = static_cast<int>(theme_.fonts.size.caption) + 4;
        const int ageCellH = ageRing + 6 + valueH + labelH;
        const int bigCellH = bigRing + 6 + valueH + labelH;
        const int selectorH = selectorWidth() > 0 && !selectors_.empty()
                                  ? selectors_.front().selector->preferredHeight() + labelH
                                  : 0;
        const int captionH = labelH;
        const int controlsH =
            std::max({ageCellH, bigCellH + captionH, selectorH + captionH, knobCellHeight() + captionH});
        auto tiles = body.removeFromTop(std::max(96, body.getHeight() - controlsH - theme_.spacing.sm));
        body.removeFromTop(theme_.spacing.sm);
        // Six windows across.
        const int gap = theme_.spacing.md;
        const int tileW = (tiles.getWidth() - gap * (pa::stageCount - 1)) / pa::stageCount;
        for (int s = 0; s < pa::stageCount; ++s)
        {
            tiles_[static_cast<size_t>(s)]->setBounds(tiles.removeFromLeft(tileW));
            tiles.removeFromLeft(gap);
        }

        auto controls = body.removeFromTop(controlsH);
        dividers_.clear();
        dividerTop_ = controls.getY() + 6;
        dividerBottom_ = controls.getBottom() - 4;
        const auto rule = [&]
        {
            controls.removeFromLeft(theme_.spacing.sm);
            dividers_.push_back(controls.getX());
            controls.removeFromLeft(theme_.spacing.sm + 1);
        };
        // Every label sits on the row's bottom line.
        {
            auto cell = controls.removeFromLeft(ageCellWidth);
            if (auto* label = labelFor(*age_))
                label->setBounds(cell.removeFromBottom(labelH));
            age_->setBounds(cell.removeFromBottom(ageCellH - labelH - 6));
            layoutKnobs(
                controls.removeFromLeft(knobCellWidth()).removeFromBottom(knobCellHeight()), 1, {pace_});
        }
        rule();
        auto stageArea = controls.removeFromLeft(stagePanelWidth());
        captionArea_ = stageArea.withHeight(captionH).withTrimmedLeft(theme_.spacing.xs);
        for (auto& p : panels_)
        {
            auto area = stageArea.withTrimmedTop(captionH);
            if (p.selector != nullptr)
            {
                auto cell = area.removeFromLeft(selectorWidth());
                p.caption->setBounds(cell.removeFromBottom(labelH));
                p.selector->setBounds(cell.removeFromBottom(p.selector->preferredHeight()));
            }
            {
                auto cell = area.removeFromLeft(bigCellWidth);
                if (auto* label = labelFor(*p.main))
                    label->setBounds(cell.removeFromBottom(labelH));
                p.main->setBounds(cell.removeFromBottom(bigCellH - labelH - 6));
            }
            // The panel is as wide as the widest stage's controls: a stage with fewer
            // spreads its knobs across the rest, rather than leaving a gap before
            // the output section.
            layoutKnobs(area.removeFromBottom(knobCellHeight()), static_cast<int>(p.knobs.size()), p.knobs);
        }
        rule();
        layoutKnobs(
            controls.removeFromLeft(5 * knobCellWidth()).removeFromBottom(knobCellHeight()), 5, outputs_);
    }

    void paramsChanged() override
    {
        for (const auto& s : selectors_)
            s.selector->setSelected(static_cast<int>(std::lround(param(s.index))),
                                    juce::dontSendNotification);
        for (auto& t : tiles_)
            t->sync();
        updateTexture();
        dimStage();
        showPreset();
        lamp_.setTooltip(isOn() ? "On" : "Off: Patina is bypassed");
        lamp_.repaint();
        // The decay's seconds depend on the space.
        for (auto* k : panels_[pa::chamber].knobs)
            k->repaint();
        repaint();
    }

    void framesArrived(double) override
    {
        const auto* t = telemetry();
        if (t != telemetrySource_)
        {
            telemetrySource_ = t;
            cursor_ = t != nullptr ? t->framesWritten() : 0;
        }
        if (t != nullptr)
        {
            std::array<Telemetry::Frame, 64> frames;
            for (int n = 64; n == 64;)
            {
                n = t->read(cursor_, frames.data(), 64);
                for (int i = 0; i < n; ++i)
                {
                    history_[static_cast<size_t>(head_)] = frames[static_cast<size_t>(i)];
                    head_ = (head_ + 1) % historyFrames;
                    count_ = std::min(historyFrames, count_ + 1);
                    ++total_;
                }
            }
        }
        for (auto& tile : tiles_)
            tile->repaint();
    }

    std::array<std::unique_ptr<StageTile>, pa::stageCount> tiles_;
    std::array<Panel, pa::stageCount> panels_;
    std::vector<std::unique_ptr<RotarySelector>> ownedSelectors_;
    std::vector<std::unique_ptr<juce::Label>> ownedLabels_;
    std::vector<Selector> selectors_;
    std::vector<Knob*> outputs_;
    Knob* age_ = nullptr;
    Knob* pace_ = nullptr;
    Knob* texture_ = nullptr;
    int selected_ = pa::heat;
    std::vector<int> dividers_;
    int dividerTop_ = 0, dividerBottom_ = 0;
    juce::Rectangle<int> captionArea_, face_, plate_, bezel_;
    juce::Image faceCache_;
    float cacheScale_ = 0.0f;

    std::vector<Telemetry::Frame> history_;
    int head_ = 0, count_ = 0;
    std::uint64_t total_ = 0, cursor_ = 0;
    const Telemetry* telemetrySource_ = nullptr;

    RackKey previous_, next_;
    PresetDisplay display_;
    Lamp lamp_;
};

// ----- the window, drawn --------------------------------------------------------------------------

constexpr int tileHeader = 20;     // the name and switch over the window
constexpr float frameWidth = 4.0f; // the window's bevelled frame
constexpr float ledHeight = 5.0f, ledWidth = 9.0f, ledGap = 3.0f;
constexpr int ledCount = 8;
constexpr float readoutRoom = 18.0f; // a strip along the top of each readout for its figure

StageTile::StageTile(PatinaCard& card, int stage) : card_(card), stage_(stage), power_(card.theme())
{
    setWantsKeyboardFocus(true);
    power_.setClickingTogglesState(false);
    power_.onClick = [this] { card_.toggleStage(stage_); };
    addAndMakeVisible(power_);
    static const char* tips[] = {
        "Heat: the curve the sound is driven into, with its level riding it, and how much the peaks are "
        "squashed",
        "Chamber: the space's level over the last three seconds, and how long it rings",
        "Drift: the pitch over the last three seconds, the left side dark and the right faint",
        "Grain: the noise (dark) against the music (faint) over the last three seconds",
        "Wear: the bandwidth that survives over the last three seconds; dropouts dip it",
        "Crush: the waveform going in (faint) and coming out (dark), and the bits and rate in effect"};
    setTooltip(juce::String(tips[stage]) + ". Click to show its controls");
    sync();
}

void StageTile::sync()
{
    const bool on = card_.stageOn(stage_);
    power_.setToggleState(on, juce::dontSendNotification);
    power_.setTooltip(juce::String(stageNames[static_cast<size_t>(stage_)]) +
                      (on ? ": on, click to switch off" : ": off, click to switch on"));
    repaint();
}

void StageTile::resized()
{
    // The switch just after the name, the pair centred over the window.
    const auto font = card_.theme().captionCaps();
    const int nameW = juce::GlyphArrangement::getStringWidthInt(
        font, juce::String(stageNames[static_cast<size_t>(stage_)]).toUpperCase());
    const int w = nameW + 6 + StageSwitch::width + 4;
    const int x = (getWidth() - w) / 2 + nameW + 6 - 2;
    power_.setBounds(
        x, (tileHeader - (StageSwitch::height + 6)) / 2, StageSwitch::width + 4, StageSwitch::height + 6);
}

juce::Rectangle<float> StageTile::frame() const
{
    auto r = getLocalBounds().toFloat().reduced(2.0f, 0.0f);
    r.removeFromTop(static_cast<float>(tileHeader));
    r.removeFromBottom(ledHeight + 2.0f * ledGap + 2.0f);
    return r;
}

juce::Rectangle<float> StageTile::well() const
{
    return frame().reduced(frameWidth);
}

void StageTile::mouseDown(const juce::MouseEvent&)
{
    card_.select(stage_);
}

bool StageTile::keyPressed(const juce::KeyPress& key)
{
    const auto move = [&](int to)
    {
        to = std::clamp(to, 0, pa::stageCount - 1);
        card_.select(to);
        if (auto* parent = getParentComponent())
            for (auto* child : parent->getChildren())
                if (auto* tile = dynamic_cast<StageTile*>(child); tile != nullptr && tile->stage_ == to)
                    tile->grabKeyboardFocus();
    };
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        move(stage_ + (key == juce::KeyPress::leftKey ? -1 : 1));
    else if (key == juce::KeyPress::returnKey)
        card_.select(stage_);
    else if (key == juce::KeyPress::spaceKey)
        card_.toggleStage(stage_);
    else
        return false;
    return true;
}

void StageTile::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& hw = theme.hardware;
    const bool selected = card_.selected() == stage_;
    const bool on = card_.stageOn(stage_);
    const bool hover = isMouseOver(true);

    // The name, printed beside the switch.
    {
        const auto font = theme.captionCaps();
        g.setFont(font);
        const auto name = juce::String(stageNames[static_cast<size_t>(stage_)]).toUpperCase();
        const int nameW = juce::GlyphArrangement::getStringWidthInt(font, name);
        const auto header = getLocalBounds().removeFromTop(tileHeader).toFloat();
        hardware::drawLegend(g,
                             theme,
                             name,
                             juce::Rectangle<float>(static_cast<float>(power_.getX() - 6 - nameW + 2),
                                                    header.getY(),
                                                    static_cast<float>(nameW + 2),
                                                    header.getHeight()),
                             juce::Justification::centredRight,
                             hw.legend.withAlpha(on ? 1.0f : 0.7f));
    }

    // The window's frame: bevelled metal, lit amber round the selected stage.
    const auto f = frame();
    if (selected)
    {
        g.setColour(hw.led.withAlpha(0.25f));
        g.drawRoundedRectangle(f.expanded(2.5f), 6.0f, 3.0f);
        g.setColour(hw.led.withAlpha(0.9f));
        g.drawRoundedRectangle(f.expanded(1.0f), 5.0f, 1.5f);
    }
    else if (hover)
    {
        g.setColour(hw.highlight.withAlpha(0.3f));
        g.drawRoundedRectangle(f.expanded(1.0f), 5.0f, 1.0f);
    }
    g.setGradientFill(juce::ColourGradient(
        hw.glassFrameTop, 0.0f, f.getY(), hw.glassFrameBottom, 0.0f, f.getBottom(), false));
    g.fillRoundedRectangle(f, 4.0f);
    g.setColour(hw.highlight.withAlpha(0.12f));
    g.drawHorizontalLine(static_cast<int>(f.getBottom()) - 1, f.getX() + 3.0f, f.getRight() - 3.0f);

    // The glass, lit from behind while the stage is on.
    const auto glass = well();
    {
        juce::ColourGradient light(on ? hw.glassCentre : hw.glassOffCentre,
                                   glass.getCentreX(),
                                   glass.getY() + glass.getHeight() * 0.6f,
                                   on ? hw.glassEdge : hw.glassOffEdge,
                                   glass.getX(),
                                   glass.getY(),
                                   true);
        if (on)
            light.addColour(0.55, hw.glassMid);
        g.setGradientFill(light);
        g.fillRoundedRectangle(glass, 2.0f);
    }
    const auto ink = on ? hw.glassInk : hw.glassMid;
    inks_ = {ink,
             ink,
             ink.withAlpha(0.55f),
             ink.withAlpha(0.5f),
             ink.withAlpha(0.16f),
             on ? hw.glassCentre : hw.glassOffCentre};
    {
        juce::Graphics::ScopedSaveState save(g);
        const auto p = glass.reduced(1.0f);
        g.reduceClipRegion(p.toNearestInt());
        switch (stage_)
        {
            case pa::heat:
                paintHeat(g, p);
                break;
            case pa::chamber:
                paintChamber(g, p);
                break;
            case pa::drift:
                paintDrift(g, p);
                break;
            case pa::grain:
                paintGrain(g, p);
                break;
            case pa::wear:
                paintWear(g, p);
                break;
            default:
                paintCrush(g, p);
                break;
        }
        // The stage's choice, printed at the foot of the glass.
        const auto choice = card_.choiceName(stage_);
        if (choice.isNotEmpty())
        {
            g.setFont(theme.captionCaps());
            g.setColour(ink.withAlpha(0.7f));
            g.drawText(
                choice.toUpperCase(),
                juce::Rectangle<float>(p.getX() + 5.0f, p.getBottom() - 16.0f, p.getWidth() - 10.0f, 14.0f),
                juce::Justification::centredLeft,
                false);
        }
    }
    // The shade under the frame's lip and the glass's reflection.
    g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.45f),
                                           0.0f,
                                           glass.getY(),
                                           hw.shadow.withAlpha(0.0f),
                                           0.0f,
                                           glass.getY() + 6.0f,
                                           false));
    g.fillRect(glass.withHeight(6.0f));
    g.setGradientFill(juce::ColourGradient(hw.highlight.withAlpha(on ? 0.22f : 0.1f),
                                           glass.getX(),
                                           glass.getY(),
                                           hw.highlight.withAlpha(0.0f),
                                           glass.getX() + glass.getWidth() * 0.12f,
                                           glass.getY() + glass.getHeight() * 0.38f,
                                           false));
    g.fillRoundedRectangle(glass, 2.0f);

    // The lamps: the amount Age and Breath leave, live; dim up to where Age alone puts it.
    const float live =
        on ? std::clamp(card_.newest()[static_cast<size_t>(Telemetry::amountHeat + stage_)], 0.0f, 1.0f)
           : 0.0f;
    const float set = on ? std::clamp(card_.setAmount(stage_), 0.0f, 1.0f) : 0.0f;
    const int lit = juce::roundToInt(live * static_cast<float>(ledCount));
    const int dim = juce::roundToInt(set * static_cast<float>(ledCount));
    const float rowW = ledCount * ledWidth + (ledCount - 1) * ledGap;
    float x = f.getCentreX() - rowW * 0.5f;
    const float y = f.getBottom() + ledGap + 1.0f;
    for (int i = 0; i < ledCount; ++i, x += ledWidth + ledGap)
    {
        const auto led = juce::Rectangle<float>(x, y, ledWidth, ledHeight);
        const auto colour = i >= ledCount - 2 ? hw.ledHot : hw.led;
        if (i < lit)
        {
            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(led.expanded(1.5f), 2.5f);
            g.setColour(colour);
            g.fillRoundedRectangle(led, 1.0f);
        }
        else
        {
            g.setColour(hw.ledOff);
            g.fillRoundedRectangle(led, 1.0f);
            g.setColour(i < dim ? colour.withAlpha(0.4f) : hw.shadow.withAlpha(0.5f));
            if (i < dim)
                g.fillRoundedRectangle(led.reduced(1.0f), 0.5f);
            else
                g.drawHorizontalLine(static_cast<int>(led.getY()), led.getX(), led.getRight());
        }
    }
}

void StageTile::paintOverChildren(juce::Graphics& g)
{
    focusRing(g, *this, frame().expanded(1.0f), 5.0f);
}

void StageTile::readout(juce::Graphics& g,
                        juce::Rectangle<float> p,
                        const juce::String& text,
                        juce::Colour colour)
{
    g.setFont(card_.theme().value());
    g.setColour(colour);
    g.drawText(text,
               juce::Rectangle<float>(p.getX() + 4.0f, p.getY() + 3.0f, p.getWidth() - 8.0f, 14.0f),
               juce::Justification::centredRight);
}

void StageTile::scaleLabel(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text)
{
    g.setFont(card_.theme().caption());
    g.setColour(inks_.textTertiary);
    g.drawText(text,
               juce::Rectangle<float>(p.getX() + 5.0f, p.getY() + 3.0f, 60.0f, 14.0f),
               juce::Justification::centredLeft);
}

namespace
{
// Per column of `columns`, the largest (or with `mean`, the average) of one
// value over the frames that fall in it: columns counted from the start of the
// history so they keep their frames as the display scrolls.
void collect(const PatinaCard& card, int value, int columns, bool mean, std::vector<float>& out)
{
    out.assign(static_cast<size_t>(std::max(1, columns)), std::numeric_limits<float>::quiet_NaN());
    const auto total = card.historyTotal();
    if (total == 0)
        return;
    const auto perColumn = static_cast<std::uint64_t>(std::max(1, historyFrames / std::max(1, columns)));
    const int size = card.historyCount();
    const auto first = total - static_cast<std::uint64_t>(size);
    const auto newestBucket = (total - 1) / perColumn;
    for (int col = 0; col < columns; ++col)
    {
        const auto fromRight = static_cast<std::uint64_t>(columns - 1 - col);
        if (fromRight > newestBucket)
            continue;
        const auto bucket = newestBucket - fromRight;
        float acc = mean ? 0.0f : -1.0e30f;
        int n = 0;
        for (auto a = bucket * perColumn; a < (bucket + 1) * perColumn && a < total; ++a)
        {
            if (a < first)
                continue;
            const float v = card.frameAt(static_cast<int>(a - first))[static_cast<size_t>(value)];
            acc = mean ? acc + v : std::max(acc, v);
            ++n;
        }
        if (n > 0)
            out[static_cast<size_t>(col)] = mean ? acc / static_cast<float>(n) : acc;
    }
}

// A line through the columns that have values.
juce::Path
traceLine(const std::vector<float>& values, juce::Rectangle<float> p, const std::function<float(float)>& y)
{
    juce::Path path;
    bool started = false;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (std::isnan(values[i]))
        {
            started = false;
            continue;
        }
        const float x = p.getX() + static_cast<float>(i) + 0.5f;
        if (!started)
            path.startNewSubPath(x, y(values[i]));
        else
            path.lineTo(x, y(values[i]));
        started = true;
    }
    return path;
}

// The same, closed down to the bottom edge as an area.
juce::Path
traceArea(const std::vector<float>& values, juce::Rectangle<float> p, const std::function<float(float)>& y)
{
    juce::Path path;
    int start = -1;
    const auto close = [&](int end)
    {
        if (start < 0)
            return;
        path.lineTo(p.getX() + static_cast<float>(end) - 0.5f, p.getBottom());
        path.lineTo(p.getX() + static_cast<float>(start) + 0.5f, p.getBottom());
        path.closeSubPath();
        start = -1;
    };
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (std::isnan(values[i]))
        {
            close(static_cast<int>(i));
            continue;
        }
        const float x = p.getX() + static_cast<float>(i) + 0.5f;
        if (start < 0)
        {
            start = static_cast<int>(i);
            path.startNewSubPath(x, p.getBottom());
            path.lineTo(x, y(values[i]));
        }
        else
            path.lineTo(x, y(values[i]));
    }
    close(static_cast<int>(values.size()));
    return path;
}

const juce::PathStrokeType traceStroke(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
} // namespace

void StageTile::paintHeat(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    const auto& f = card_.newest();
    const int character = static_cast<int>(std::lround(card_.param(pa::heatCharacter)));
    const float amount = card_.stageOn(pa::heat) ? f[Telemetry::amountHeat] : card_.setAmount(pa::heat);
    // Input across (-1..1), output up, in a square centred in the well.
    const auto plot = p.withTrimmedTop(readoutRoom);
    const float side = std::min(plot.getWidth(), plot.getHeight()) - 6.0f;
    const auto sq = juce::Rectangle<float>(side, side).withCentre(plot.getCentre());
    const auto toX = [&](float x) { return sq.getCentreX() + x * sq.getWidth() * 0.5f; };
    const auto toY = [&](float y)
    { return sq.getCentreY() - std::clamp(y, -1.1f, 1.1f) * sq.getHeight() * 0.5f; };
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(sq.getCentreY()), p.getX(), p.getRight());
    g.drawVerticalLine(static_cast<int>(sq.getCentreX()), p.getY(), p.getBottom());
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(c.textTertiary.withAlpha(0.5f));
    g.drawDashedLine(juce::Line<float>(toX(-1.0f), toY(-1.0f), toX(1.0f), toY(1.0f)), dashes, 2, 1.0f);
    juce::Path curve;
    constexpr int steps = 96;
    for (int i = 0; i <= steps; ++i)
    {
        const float x = -1.0f + 2.0f * static_cast<float>(i) / steps;
        const float y = pa::heatTransfer(character, amount, x);
        if (i == 0)
            curve.startNewSubPath(toX(x), toY(y));
        else
            curve.lineTo(toX(x), toY(y));
    }
    g.setColour(hue);
    g.strokePath(curve, traceStroke);
    // The level riding the curve: the loudest of the last 50 ms.
    float level = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 10); i < card_.historyCount(); ++i)
        level = std::max(level, card_.frameAt(i)[Telemetry::heatLevel]);
    level = std::min(level, 1.0f);
    if (level > 0.001f && card_.stageOn(pa::heat))
    {
        const juce::Point<float> dot(toX(level), toY(pa::heatTransfer(character, amount, level)));
        g.setColour(c.dotRing);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(dot));
        g.setColour(hue);
        g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre(dot));
    }
    float squash = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 40); i < card_.historyCount(); ++i)
        squash = std::max(squash, card_.frameAt(i)[Telemetry::heatSquash]);
    readout(g, p, minusDb(squash), squash >= 0.1f ? hue : c.textTertiary);
}

void StageTile::paintChamber(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    constexpr float top = 0.0f, bottom = -60.0f;
    for (float db = -20.0f; db > bottom; db -= 20.0f)
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(
            static_cast<int>(dbToY(db, top, bottom, p.withTrimmedTop(readoutRoom))), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::chamberWet, static_cast<int>(p.getWidth()), false, a_);
    const auto y = [&](float v) { return dbToY(toDb(v), top, bottom, p.withTrimmedTop(readoutRoom)); };
    g.setColour(hue.withAlpha(0.32f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    const int space = static_cast<int>(std::lround(card_.param(pa::chamberType)));
    const float rt = pa::chamberDecaySeconds(space, card_.param(pa::chamberDecay) * 0.01f);
    readout(g, p, juce::String(rt, 1) + " s", c.textSecondary);
}

void StageTile::paintDrift(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    collect(card_, Telemetry::driftLeft, static_cast<int>(p.getWidth()), true, a_);
    collect(card_, Telemetry::driftRight, static_cast<int>(p.getWidth()), true, b_);
    float peak = 0.0f;
    for (size_t i = 0; i < a_.size(); ++i)
        for (const float v : {a_[i], b_[i]})
            if (!std::isnan(v))
                peak = std::max(peak, std::abs(v));
    // The scale grows to fit what plays: +-5, 10, 20 or 40 cents.
    float range = 5.0f;
    while (range < 40.0f && peak > range * 0.95f)
        range *= 2.0f;
    const auto mid = p.getCentreY() + 6.0f;
    const float half = std::min(mid - p.getY() - 18.0f, p.getBottom() - mid - 6.0f);
    const auto y = [&](float cents) { return mid - std::clamp(cents / range, -1.0f, 1.0f) * half; };
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(mid), p.getX(), p.getRight());
    const float dashes[] = {2.0f, 3.0f};
    for (const float edge : {mid - half, mid + half})
        g.drawDashedLine(juce::Line<float>(p.getX(), edge, p.getRight(), edge), dashes, 2, 1.0f);
    if (card_.param(pa::driftStereo) > 0.5f)
    {
        g.setColour(c.textSecondary.withAlpha(0.7f));
        g.strokePath(traceLine(b_, p, y), juce::PathStrokeType(1.0f));
    }
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    scaleLabel(g, p, juce::String::fromUTF8("\xc2\xb1") + juce::String(static_cast<int>(range)) + " c");
    // The widest swing of the last second.
    float recent = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 200); i < card_.historyCount(); ++i)
    {
        const auto& f = card_.frameAt(i);
        recent = std::max({recent, std::abs(f[Telemetry::driftLeft]), std::abs(f[Telemetry::driftRight])});
    }
    readout(g,
            p,
            juce::String::fromUTF8("\xc2\xb1") + juce::String(recent, 1) + " c",
            recent >= 0.1f ? hue : c.textTertiary);
}

void StageTile::paintGrain(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    constexpr float top = 0.0f, bottom = -84.0f;
    for (float db = -24.0f; db > bottom; db -= 24.0f)
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(
            static_cast<int>(dbToY(db, top, bottom, p.withTrimmedTop(readoutRoom))), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::grainNoise, static_cast<int>(p.getWidth()), false, a_);
    collect(card_, Telemetry::grainSignal, static_cast<int>(p.getWidth()), false, b_);
    const auto y = [&](float v) { return dbToY(toDb(v), top, bottom, p.withTrimmedTop(readoutRoom)); };
    g.setColour(hue.withAlpha(0.36f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), juce::PathStrokeType(1.0f));
    g.setColour(c.textSecondary.withAlpha(0.8f));
    g.strokePath(traceLine(b_, p, y), traceStroke);
    float noise = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 50); i < card_.historyCount(); ++i)
        noise = std::max(noise, card_.frameAt(i)[Telemetry::grainNoise]);
    readout(g,
            p,
            noise > 1.0e-6f ? wholeDb(toDb(noise)) : juce::String::fromUTF8("\xe2\x80\x93"),
            noise > 1.0e-6f ? hue : c.textTertiary);
}

void StageTile::paintWear(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    // Frequency up a log axis, 1 kHz at the bottom to 20 kHz at the top.
    const auto y = [&](float hz)
    {
        const float f = std::log(std::clamp(hz, 1000.0f, 20000.0f) / 1000.0f) / std::log(20.0f);
        return p.getBottom() - f * (p.getHeight() - 18.0f);
    };
    for (const float hz : {2000.0f, 5000.0f, 10000.0f})
    {
        g.setColour(c.gridBar);
        g.drawHorizontalLine(static_cast<int>(y(hz)), p.getX(), p.getRight());
    }
    collect(card_, Telemetry::wearCutoff, static_cast<int>(p.getWidth()), false, a_);
    // Dropouts are brief: take each column's lowest, not its highest.
    {
        std::vector<float> lowest;
        collect(card_, Telemetry::wearCutoff, static_cast<int>(p.getWidth()), true, lowest);
        for (size_t i = 0; i < a_.size() && i < lowest.size(); ++i)
            if (!std::isnan(lowest[i]))
                a_[i] = std::min(a_[i], lowest[i]);
    }
    g.setColour(hue.withAlpha(0.26f));
    g.fillPath(traceArea(a_, p, y));
    g.setColour(hue);
    g.strokePath(traceLine(a_, p, y), traceStroke);
    scaleLabel(g, p, "20k");
    const auto& f = card_.newest();
    const float cutoff =
        card_.historyCount() > 0 ? f[Telemetry::wearCutoff] : pa::wearCutoffHz(card_.setAmount(pa::wear));
    float dip = 0.0f;
    for (int i = std::max(0, card_.historyCount() - 60); i < card_.historyCount(); ++i)
        dip = std::max(dip, card_.frameAt(i)[Telemetry::wearDipDb]);
    readout(g, p, dip >= 1.0f ? minusDb(dip) : shortHz(cutoff), dip >= 1.0f ? hue : c.textSecondary);
}

void StageTile::paintCrush(juce::Graphics& g, juce::Rectangle<float> p)
{
    const auto& c = inks_;
    const auto hue = inks_.hue;
    const auto& f = card_.newest();
    const bool live = card_.historyCount() > 0 && card_.stageOn(pa::crush);
    const float bits = live ? f[Telemetry::crushBits] : pa::crushBitsFor(card_.setAmount(pa::crush));
    const float hz = live ? f[Telemetry::crushRate] : card_.param(pa::crushRate);
    const auto mid = p.getCentreY() + 7.0f;
    const float half = std::min(mid - p.getY() - 18.0f, p.getBottom() - mid - 4.0f);
    g.setColour(c.gridBar);
    g.drawHorizontalLine(static_cast<int>(mid), p.getX(), p.getRight());
    // A window of about six of the converter's samples, from a rising zero
    // crossing so the trace holds still.
    const auto* t = card_.telemetry();
    if (t != nullptr && live)
    {
        const double fs = t->sampleRate();
        const int window = std::clamp(static_cast<int>(6.0 * fs / std::max(1.0f, hz)), 96, 512);
        const int span = window * 3;
        scopeIn_.resize(static_cast<size_t>(span));
        scopeOut_.resize(static_cast<size_t>(span));
        if (t->readScope(scopeIn_.data(), scopeOut_.data(), span))
        {
            int start = span - window;
            for (int i = span - window - 1; i > 0; --i)
                if (scopeIn_[static_cast<size_t>(i - 1)] < 0.0f && scopeIn_[static_cast<size_t>(i)] >= 0.0f)
                {
                    start = i;
                    break;
                }
            float peak = 1.0e-3f;
            for (int i = start; i < start + window; ++i)
                peak = std::max(peak, std::abs(scopeIn_[static_cast<size_t>(i)]));
            const float scale = 1.0f / std::max(0.05f, peak);
            const auto trace = [&](const std::vector<float>& v, bool steps)
            {
                juce::Path path;
                for (int i = 0; i < window; ++i)
                {
                    const float x =
                        p.getX() + p.getWidth() * static_cast<float>(i) / static_cast<float>(window - 1);
                    const float yy =
                        mid - std::clamp(v[static_cast<size_t>(start + i)] * scale, -1.0f, 1.0f) * half;
                    if (i == 0)
                        path.startNewSubPath(x, yy);
                    else
                    {
                        if (steps)
                            path.lineTo(x, path.getCurrentPosition().y);
                        path.lineTo(x, yy);
                    }
                }
                return path;
            };
            g.setColour(c.textSecondary.withAlpha(0.6f));
            g.strokePath(trace(scopeIn_, false), juce::PathStrokeType(1.0f));
            g.setColour(hue);
            g.strokePath(trace(scopeOut_, true), juce::PathStrokeType(1.5f));
        }
    }
    const auto text = juce::String(static_cast<int>(std::lround(bits))) + " bit " +
                      juce::String::fromUTF8("\xc2\xb7 ") + shortHz(hz);
    readout(g, p, text, live ? c.textSecondary : c.textTertiary);
}

} // namespace

std::unique_ptr<ModuleCard>
createPatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<PatinaCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
