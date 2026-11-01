// Patina's card (screens/plugins.md 7.2a): a broadcast colour processor's face, a
// hardware face (design-system.md "Hardware face") built from rendered parts
// (assets/patina; "Rendered parts"), drawn at 1160 x 660 and scaled to fit the
// card's body, the card narrowing with it.
//
// Slate hammertone carries the nameplate, the preset display between its two keys
// and the pilot lamp along the top. Below them, six grey enamel modules in signal
// order, one per stage, each showing every one of its controls: the stage's name
// and lit push-button, its window (a live readout of what the stage is doing now:
// Heat's curve with the level riding it, Chamber's tail, Drift's pitch, Grain's
// noise against the music, Wear's surviving bandwidth, Crush's waveform) with a
// row of lamps for its amount as Age and Breath leave it, its type selector, its
// main knob and its small knobs. The master plate at the right holds Age, Pace and
// the output section.

#include "plugins/Patina.h"
#include "ui/Icons.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/Number.h"
#include "ui/components/Accessibility.h"
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/EffectPresetLibrary.h"
#include "ui/strip/FaceParts.h"
#include "ui/strip/FeedbackHistory.h"
#include "ui/strip/HardwareKey.h"
#include "ui/strip/PatinaFace.h"
#include "ui/strip/PatinaWindows.h"
#include "ui/strip/PresetBar.h"
#include "ui/strip/RenderedCard.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

namespace pa = plugins::patina;

namespace
{
using namespace patinaface;
using hardware::legendFont;
using hardware::polar;

juce::String percentText(double v)
{
    return ui::percentText(v);
}

const juce::Image& part(const juce::String& file)
{
    return hardware::image("patina", file);
}

using hardware::drawFocusRing;

// ----- the face's small parts ---------------------------------------------------------------------

// A stage's lit push-button: its rendered key, dark or lit; a click, Space or
// Enter switches the stage.
class StageButton final : public juce::Button
{
public:
    StageButton(const Theme& theme, int stage)
        : juce::Button(juce::String(stageNames[static_cast<size_t>(stage)])), theme_(theme)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    bool hitTest(int x, int y) override
    {
        return bezel().contains(static_cast<float>(x), static_cast<float>(y));
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const bool lit = getToggleState();
        const auto image = part(lit ? "button-on.png" : "button-off.png");
        const auto r = getLocalBounds().toFloat();
        if (image.isValid())
            hardware::drawScaled(g, image, down ? r.translated(0.0f, 0.5f) : r);
        else
        {
            const auto& hw = theme_.hardware;
            g.setColour(hw.aluminium);
            g.fillRoundedRectangle(bezel(), 3.0f);
            g.setColour(lit ? hw.led : hw.ledOff);
            g.fillRoundedRectangle(bezel().reduced(3.0f), 2.0f);
        }
        if (over && isEnabled())
        {
            g.setColour(theme_.hardware.highlight.withAlpha(0.1f));
            g.fillRoundedRectangle(bezel().reduced(3.0f), 2.0f);
        }
        drawFocusRing(g, *this, bezel().expanded(1.0f), 4.0f);
    }

private:
    juce::Rectangle<float> bezel() const
    {
        return getLocalBounds().toFloat().withSizeKeepingCentre(
            buttonWidth * static_cast<float>(getWidth()) / buttonImageWidth,
            buttonHeight * static_cast<float>(getHeight()) / buttonImageHeight);
    }

    const Theme& theme_;
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
        if (number == number_ && name == name_)
            return;
        number_ = number;
        name_ = name;
        upperName_ = name.toUpperCase();
        setButtonText(name);
        repaint();
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto& hw = theme_.hardware;
        const auto r = getLocalBounds().toFloat();
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(r, openingRadius);
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

        // The name is capitals, so the line is centred on their height, not the
        // font's (whose descent would leave them high); the number shares its baseline.
        const auto mono = theme_.value();
        auto big = mono.withHeight(mono.getHeight() * 1.2f).withExtraKerningFactor(0.08f);
        const auto small = mono.withHeight(mono.getHeight() * 0.95f);
        // Where a capital's middle sits below its baseline: measured once per font
        // size (a glyph's outline is laid out and flattened to find it).
        if (capitalHeight_ != big.getHeight())
        {
            juce::GlyphArrangement capital;
            capital.addLineOfText(big, "H", 0.0f, 0.0f);
            juce::Path capitalPath;
            capital.createPath(capitalPath);
            capitalCentre_ = capitalPath.getBounds().getCentreY();
            capitalHeight_ = big.getHeight();
        }
        const float baseline = std::round(r.getCentreY() - capitalCentre_);
        const auto onBaseline = [baseline](juce::Rectangle<float> area, const juce::Font& font)
        { return area.withY(baseline - font.getAscent()).withHeight(font.getHeight()); };
        auto line = r.reduced(12.0f, 0.0f);
        g.setFont(small);
        g.setColour(hw.displayDim.withAlpha(0.75f));
        g.drawText(
            number_, onBaseline(line.removeFromLeft(22.0f), small), juce::Justification::centredLeft, false);
        line.removeFromLeft(6.0f);
        line = onBaseline(line, big);
        g.setFont(big);
        const auto& name = upperName_;
        g.setColour(hw.displayGlow.withAlpha(0.22f));
        for (const auto& d : {juce::Point<float>(-1.0f, 0.0f),
                              juce::Point<float>(1.0f, 0.0f),
                              juce::Point<float>(0.0f, -1.0f),
                              juce::Point<float>(0.0f, 1.0f)})
            g.drawText(name, line.translated(d.x, d.y), juce::Justification::centredLeft, true);
        g.setColour(over ? hw.displayText.brighter(0.15f) : hw.displayText);
        g.drawText(name, line, juce::Justification::centredLeft, true);

        // Scan lines, and the shade under the bezel's lip.
        g.setColour(hw.shadow.withAlpha(0.12f));
        for (float y = r.getY() + 1.0f; y < r.getBottom(); y += 3.0f)
            g.fillRect(r.getX(), y, r.getWidth(), 1.0f);
        g.setGradientFill(juce::ColourGradient(hw.shadow.withAlpha(0.45f),
                                               0.0f,
                                               r.getY(),
                                               hw.shadow.withAlpha(0.0f),
                                               0.0f,
                                               r.getY() + 5.0f,
                                               false));
        g.fillRect(r.withHeight(5.0f));
        drawFocusRing(g, *this, r, openingRadius);
    }

private:
    juce::String upperName_;                             // the name as drawn
    float capitalHeight_ = -1.0f, capitalCentre_ = 0.0f; // the measured capital, per font size
    const Theme& theme_;
    juce::String number_, name_;
};

// ----- the face -----------------------------------------------------------------------------------

// A control printed on the face: where it is, what it is called, and its kind.
struct Control
{
    enum class Kind
    {
        main,
        small,
        selector,
        age,
        pace,
    };
    int param = 0;
    juce::Point<float> centre;
    float skirt = 0.0f;
    juce::String name;
    Kind kind = Kind::small;
    juce::StringArray legends; // a selector's positions
};

class Face final : public RenderedFace
{
public:
    explicit Face(const Theme& theme) : RenderedFace(faceWidth, faceHeight), theme_(theme) {}

    std::vector<Control> controls;
    std::function<float(int param)> paramValue;
    // A control's name when it depends on a setting (Grain's texture).
    std::function<juce::String(const Control&)> nameOf;

private:
    juce::String nameFor(const Control& c) const
    {
        return RenderedFace::nameFor(c.param, nameOf ? nameOf(c) : c.name);
    }

    // The sizes of a control's print, by kind.
    static float nameSizeOf(Control::Kind k) noexcept
    {
        switch (k)
        {
            case Control::Kind::age:
                return 22.0f;
            case Control::Kind::main:
                return 16.0f;
            default:
                return 14.5f;
        }
    }
    // Where a control's name sits: under its scale.
    static juce::Rectangle<float> nameArea(const Control& c, float width)
    {
        // A clear gap from the scale's ticks to the name's capitals.
        const float below = c.kind == Control::Kind::age    ? 52.0f
                            : c.kind == Control::Kind::main ? 38.0f
                                                            : 30.0f;
        return {c.centre.x - width * 0.5f, c.centre.y + c.skirt * 0.5f + below - 22.0f, width, 22.0f};
    }
    static float nameWidth(const Control& c) noexcept
    {
        return c.kind == Control::Kind::small || c.kind == Control::Kind::pace ? 68.0f : 128.0f;
    }

    // A selector position's angle (degrees clockwise from 12 o'clock).
    static float positionAngle(const Control& c, int i) noexcept
    {
        const int n = c.legends.size();
        return n <= 1 ? 0.0f : (static_cast<float>(i) - 0.5f * static_cast<float>(n - 1)) * selectorStep;
    }
    // A legend's box beyond its dot: its near edge a few px past the dot, whatever its width.
    juce::Rectangle<float> legendArea(const Control& c, int i, const juce::Font& font) const
    {
        const float a = positionAngle(c, i);
        const float w = juce::GlyphArrangement::getStringWidth(font, c.legends[i]);
        const float h = font.getHeight();
        const float rad = juce::degreesToRadians(a);
        const float extent = std::abs(std::sin(rad)) * w * 0.5f + std::abs(std::cos(rad)) * h * 0.5f;
        return juce::Rectangle<float>(w + 4.0f, h)
            .withCentre(polar(c.centre, c.skirt * 0.5f + 9.0f + extent, a));
    }

    void
    plate(juce::Graphics& g, const juce::String& file, juce::Rectangle<float> area, juce::Colour flat) const
    {
        hardware::drawPlate(g, part(file), area, shadowMargin, flat);
    }

    void knobBody(juce::Graphics& g, juce::Point<float> c, float skirt) const
    {
        const float image = skirt * knobImageRatio;
        if (const auto body = part("knob-body.png"); body.isValid())
        {
            hardware::drawScaled(
                g, body, juce::Rectangle<float>(c.x - image * 0.5f, c.y - image * 0.5f, image, image * 1.5f));
            return;
        }
        const auto& hw = theme_.hardware;
        g.setColour(hw.knobBody);
        g.fillEllipse(juce::Rectangle<float>(skirt, skirt).withCentre(c));
        g.setColour(hw.aluminium);
        g.fillEllipse(juce::Rectangle<float>(skirt * 0.5f, skirt * 0.5f).withCentre(c));
    }

    // ----- the static layer --------------------------------------------------------------------
    void paintStatic(juce::Graphics& g) const override
    {
        const auto& hw = theme_.hardware;
        if (artPass())
        {
            g.setColour(hw.patinaPanel);
            g.fillAll();
            hardware::drawScaled(g, part("panel.jpg"), getLocalBounds().toFloat());
            plate(g, "nameplate.png", nameplateArea, hw.patinaNameplate);
            plate(g, "display.png", displayBezel, hw.patinaBezel);
            for (int s = 0; s < pa::stageCount; ++s)
            {
                // Each stage's plate worn its own way (six renders), so no chip repeats.
                plate(g,
                      "module-" + juce::String(s + 1) + ".png",
                      {moduleX(s), moduleTop, moduleWidth, moduleHeight},
                      hw.patinaPlate);
                plate(g, "window.png", windowBezelAt.translated(moduleX(s), 0.0f), hw.patinaBezel);
            }
            plate(g, "master.png", masterArea, hw.patinaPlate);
        }

        // The nameplate, etched: the mark in its square, the name, the maker's line.
        if (artPass())
        {
            const auto etch = [&](juce::Point<float> offset, juce::Colour colour)
            {
                g.setColour(colour);
                g.drawRect(nameplateMark.translated(offset.x, offset.y), 1.6f);
                Icons::draw(
                    g, Icon::mark, nameplateMark.reduced(3.5f).translated(offset.x, offset.y), colour);
            };
            etch({0.0f, 0.8f}, hw.highlight.withAlpha(0.35f));
            etch({}, hw.patinaInk.withAlpha(0.9f));
        }
        etched(g, "PATINA", nameplateName, legendFont(theme_, 31.0f, 700, 0.34f));
        etched(g,
               juce::String::fromUTF8("NORTHLIGHT \xc2\xb7 STUDIO EQUIPMENT"),
               nameplateMaker,
               legendFont(theme_, 11.0f, 700, 0.26f));
        if (!isCompact())
        {
            g.setColour(hw.patinaPanelInkSoft);
            g.setFont(legendFont(theme_, 13.0f, 600, 0.24f));
            print(g,
                  juce::String::fromUTF8("VINTAGE COLOUR PROCESSOR \xc2\xb7 MODEL PT-6"),
                  modelLine,
                  juce::Justification::centredLeft);
        }

        // Each module's name, beside its button.
        for (int s = 0; s < pa::stageCount; ++s)
        {
            g.setColour(hw.patinaInk);
            g.setFont(legendFont(theme_, isCompact() ? 21.0f : 18.0f, 700, nameTracking(0.14f)));
            print(g,
                  juce::String(stageNames[static_cast<size_t>(s)]).toUpperCase(),
                  {moduleX(s) + 14.0f, nameRow - 12.0f, moduleWidth - 28.0f - buttonWidth, 24.0f},
                  juce::Justification::centredLeft);
        }

        // The master plate's output caption over its knobs, between two rules.
        {
            g.setColour(hw.patinaInkSoft);
            g.setFont(legendFont(theme_, nameSize(13.0f), 700, nameTracking(0.24f)));
            const auto area = juce::Rectangle<float>(
                masterArea.getX() + 16.0f, outputCaptionRow - 9.0f, masterArea.getWidth() - 32.0f, 18.0f);
            print(g, "OUTPUT", area);
            if (artPass())
            {
                const float text =
                    juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), "OUTPUT") + 16.0f;
                const float rule = (area.getWidth() - text) * 0.5f;
                g.fillRect(juce::Rectangle<float>(area.getX(), outputCaptionRow, rule, 1.0f));
                g.fillRect(juce::Rectangle<float>(area.getRight() - rule, outputCaptionRow, rule, 1.0f));
            }
        }

        for (const auto& c : controls)
            paintControl(g, c);
    }

    // Everything a control prints lies in this box round it (its legends, name
    // and value): outside the area being repainted, its words are skipped whole.
    static juce::Rectangle<int> reach(const Control& c)
    {
        const float r = c.skirt * 0.5f;
        return juce::Rectangle<float>(c.centre.x - 72.0f, c.centre.y - r - 40.0f, 144.0f, 2.0f * r + 120.0f)
            .getSmallestIntegerContainer();
    }

    // Lettering etched into the nameplate's steel: the ink, and under it a faint
    // light edge where the cut's lower wall catches the light.
    void etched(juce::Graphics& g,
                const juce::String& text,
                juce::Rectangle<float> area,
                const juce::Font& font) const
    {
        const auto& hw = theme_.hardware;
        g.setFont(font);
        g.setColour(hw.highlight.withAlpha(0.35f));
        print(g, text, area.translated(0.0f, 0.8f), juce::Justification::centredLeft);
        g.setColour(hw.patinaInk.withAlpha(0.9f));
        print(g, text, area, juce::Justification::centredLeft);
    }

    void paintControl(juce::Graphics& g, const Control& c) const
    {
        if (!artPass() && !g.clipRegionIntersects(reach(c)))
            return;
        const auto& hw = theme_.hardware;
        if (c.kind == Control::Kind::selector)
        {
            // A dot at each position (the chosen one's lit in the live layer), its legend beyond.
            if (artPass())
            {
                g.setColour(hw.patinaInkSoft);
                for (int i = 0; i < c.legends.size(); ++i)
                    g.fillEllipse(
                        juce::Rectangle<float>(3.0f, 3.0f)
                            .withCentre(polar(c.centre, c.skirt * 0.5f + 5.0f, positionAngle(c, i))));
            }
            knobBodyIfArt(g, c);
            g.setColour(hw.patinaInkSoft);
            g.setFont(legendFont(theme_, nameSize(11.5f), 700, nameTracking(0.12f)));
            if (isCompact())
            {
                // The setting in place of the legends, where the caption would be.
                const int at = paramValue ? juce::roundToInt(paramValue(c.param)) : 0;
                g.setColour(hw.patinaInk);
                print(g,
                      activeControl() == c.param || c.legends.isEmpty()
                          ? nameFor(c)
                          : c.legends[juce::jlimit(0, c.legends.size() - 1, at)],
                      {c.centre.x - 64.0f, selectorCaptionRow - 11.0f, 128.0f, 22.0f});
                return;
            }
            print(g, c.name, {c.centre.x - 64.0f, selectorCaptionRow - 9.0f, 128.0f, 18.0f});
            return;
        }

        // A knob's printed scale: eleven ticks over 270 degrees (the age dial's
        // with its three legends).
        const float r = c.skirt * 0.5f;
        g.setColour(hw.patinaInkSoft);
        if (artPass())
            hardware::drawTicks(
                g, c.centre, r + 3.0f, r + (c.kind == Control::Kind::age ? 9.0f : 6.0f), 11, 1.3f);
        if (c.kind == Control::Kind::age && !isCompact())
        {
            // The dial's two ends: untouched, and as far as it goes.
            g.setColour(hw.patinaInk);
            g.setFont(legendFont(theme_, 16.0f, 700, 0.14f));
            // On one line, level with the scale's last ticks, each reading outward
            // from just past its end of the scale.
            const auto end = polar(c.centre, r + 9.0f, 135.0f);
            const float y = end.y + 6.0f, gap = end.x - c.centre.x - 6.0f;
            print(g,
                  "NEW",
                  juce::Rectangle<float>(c.centre.x - gap - 80.0f, y, 80.0f, 20.0f),
                  juce::Justification::centredRight);
            print(g,
                  "RUINED",
                  juce::Rectangle<float>(c.centre.x + gap, y, 80.0f, 20.0f),
                  juce::Justification::centredLeft);
        }
        knobBodyIfArt(g, c);
        g.setColour(hw.patinaInk);
        // Compact, a small knob's name grows more than the rest: it is the smallest print.
        const float size = nameSizeOf(c.kind);
        const bool smallName = c.kind == Control::Kind::small || c.kind == Control::Kind::pace;
        g.setFont(legendFont(
            theme_, isCompact() && smallName ? size * 1.5f : nameSize(size), 700, nameTracking(0.1f)));
        print(g, nameFor(c), nameArea(c, nameWidth(c)));
    }

    void knobBodyIfArt(juce::Graphics& g, const Control& c) const
    {
        if (artPass())
            knobBody(g, c.centre, c.skirt);
    }

    // ----- what changes with the values -------------------------------------------------------
    void paintLive(juce::Graphics& g) const override
    {
        const auto& hw = theme_.hardware;
        for (const auto& c : controls)
        {
            if (!g.clipRegionIntersects(reach(c)))
                continue;
            if (c.kind == Control::Kind::selector)
            {
                if (isCompact() || !paramValue)
                    continue;
                // The chosen position: its dot lit, its legend in full ink.
                const int at =
                    juce::jlimit(0, std::max(0, c.legends.size() - 1), juce::roundToInt(paramValue(c.param)));
                g.setColour(hw.dialRed);
                g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f)
                                  .withCentre(polar(c.centre, c.skirt * 0.5f + 5.0f, positionAngle(c, at))));
                const auto font = legendFont(theme_, 12.5f, 700, 0.06f);
                g.setFont(font);
                for (int i = 0; i < c.legends.size(); ++i)
                {
                    g.setColour(i == at ? hw.patinaInk : hw.patinaInkSoft);
                    hardware::drawCrispText(g, c.legends[i], legendArea(c, i, font));
                }
                continue;
            }
            if (isCompact() || !value)
                continue;
            const float size = c.kind == Control::Kind::age    ? 15.0f
                               : c.kind == Control::Kind::main ? 13.0f
                                                               : 13.5f;
            g.setColour(hw.patinaInkSoft);
            g.setFont(legendFont(theme_, size, 600, 0.04f));
            hardware::drawCrispText(
                g, value(c.param), nameArea(c, nameWidth(c) + 8.0f).translated(0.0f, size + 4.0f));
        }
    }

    const Theme& theme_;
};

// ----- the card -----------------------------------------------------------------------------------

class PatinaCard final : public RenderedCard, public WindowHost, private juce::ChangeListener
{
public:
    PatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme), history_(historyFrames),
          previous_(theme, juce::String(), "Previous preset", false),
          next_(theme, juce::String(), "Next preset", false), display_(theme),
          lamp_(theme, [this] { return isOn(); })
    {
        addAndMakeVisible(face_);
        face_.value = [this](int index) { return valueText(index); };
        face_.paramValue = [this](int index) { return param(index); };
        face_.nameOf = [this](const Control& c)
        { return c.param == pa::grainTexture ? textureName() : c.name; };

        for (int s = 0; s < pa::stageCount; ++s)
        {
            auto window = std::make_unique<StageWindow>(*this, s);
            const auto glass = glassOf(s);
            window->setBounds(
                juce::Rectangle<float>(
                    glass.getX(), glass.getY(), glass.getWidth(), ledRow + ledHeight + 2.0f - glass.getY())
                    .toNearestInt());
            face_.addAndMakeVisible(*window);
            windows_[static_cast<size_t>(s)] = std::move(window);

            auto button = std::make_unique<StageButton>(theme_, s);
            button->setClickingTogglesState(false);
            button->onClick = [this, s] { toggleStage(s); };
            const juce::Point<float> at(moduleX(s) + moduleWidth - 14.0f - buttonWidth * 0.5f, nameRow);
            button->setBounds(
                juce::Rectangle<float>(buttonImageWidth, buttonImageHeight).withCentre(at).toNearestInt());
            face_.addAndMakeVisible(*button);
            buttons_[static_cast<size_t>(s)] = std::move(button);
        }

        // The stages, each a module of selector, main knob and small knobs.
        stage(pa::heat, pa::heatCharacter, "CHARACTER", {"TUBE", "TAPE", "IRON", "FUZZ"});
        main(pa::heat, pa::heatDrive, "DRIVE");
        small(pa::heat, {{pa::heatTone, "TONE"}, {pa::heatBreath, "BREATH"}});

        stage(pa::chamber, pa::chamberType, "SPACE", {"ROOM", "SPRING", "PLATE"});
        main(pa::chamber, pa::chamberMix, "MIX");
        small(pa::chamber,
              {{pa::chamberDecay, "DECAY"}, {pa::chamberTone, "TONE"}, {pa::chamberBreath, "BREATH"}});

        stage(pa::drift, pa::driftMachine, "MACHINE", {"REEL", "DECK", "DISC"});
        main(pa::drift, pa::driftWow, "WOW");
        small(pa::drift,
              {{pa::driftFlutter, "FLUTTER"},
               {pa::driftRate, "RATE"},
               {pa::driftStereo, "STEREO"},
               {pa::driftBreath, "BREATH"}});

        stage(pa::grain, pa::grainSource, "SOURCE", {"VINYL", "HISS", "50 HZ", "60 HZ", "AIR"});
        main(pa::grain, pa::grainAmount, "AMOUNT");
        small(pa::grain,
              {{pa::grainTexture, "TEXTURE"}, {pa::grainFollow, "FOLLOW"}, {pa::grainBreath, "BREATH"}});

        // Wear has no type: its dropouts and skew take the selector's place, their
        // names just above the selectors' captions.
        const float wearRow = selectorCaptionRow - smallSkirt * 0.5f - 19.0f;
        control(pa::wearDropouts,
                {moduleCentre(pa::wear) - smallSpread, wearRow},
                smallSkirt,
                "DROPOUTS",
                Control::Kind::small);
        control(pa::wearSkew,
                {moduleCentre(pa::wear) + smallSpread, wearRow},
                smallSkirt,
                "SKEW",
                Control::Kind::small);
        main(pa::wear, pa::wearLoss, "LOSS");
        small(pa::wear, {{pa::wearBreath, "BREATH"}});

        stage(pa::crush,
              pa::crushFlavour,
              "FLAVOUR",
              {"SOFT", "HARD", juce::String::fromUTF8("\xce\xbc-LAW"), "JITTER"});
        main(pa::crush, pa::crushBits, "BITS");
        small(pa::crush, {{pa::crushRate, "RATE"}, {pa::crushBreath, "BREATH"}});

        // The master plate.
        control(pa::age, ageCentre, ageSkirt, "AGE", Control::Kind::age);
        control(pa::pace, paceCentre, paceSkirt, "PACE", Control::Kind::pace);
        control(pa::tone, {outputX[0], smallRows[0]}, smallSkirt, "TONE", Control::Kind::small);
        control(pa::lowCut, {outputX[1], smallRows[0]}, smallSkirt, "LOW CUT", Control::Kind::small);
        control(pa::highCut, {outputX[2], smallRows[0]}, smallSkirt, "HIGH CUT", Control::Kind::small);
        control(pa::width,
                {(outputX[0] + outputX[1]) * 0.5f, smallRows[1]},
                smallSkirt,
                "WIDTH",
                Control::Kind::small);
        control(pa::output,
                {(outputX[1] + outputX[2]) * 0.5f, smallRows[1]},
                smallSkirt,
                "OUTPUT",
                Control::Kind::small);

        // The preset selector and the lamp.
        const auto arrow = [](bool forward)
        {
            return [forward](juce::Graphics& g, juce::Rectangle<float> r)
            {
                const auto c = r.getCentre();
                const float s = std::min(r.getWidth(), r.getHeight()) * 0.32f;
                juce::Path p;
                if (forward)
                    p.addTriangle(c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 0.8f, c.y);
                else
                    p.addTriangle(c.x + s * 0.7f, c.y - s, c.x + s * 0.7f, c.y + s, c.x - s * 0.8f, c.y);
                g.fillPath(p);
            };
        };
        previous_.icon = arrow(false);
        next_.icon = arrow(true);
        previous_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        previous_.onPress = [this] { stepPreset(-1); };
        next_.onPress = [this] { stepPreset(1); };
        display_.onClick = [this] { showPresets(); };
        previous_.setBounds(previousKey.toNearestInt());
        next_.setBounds(nextKey.toNearestInt());
        display_.setBounds(displayBezel.reduced(6.0f).toNearestInt());
        lamp_.setBounds(juce::Rectangle<float>(40.0f, 46.0f).withCentre(lampCentre).toNearestInt());
        lamp_.setTooltip("On");
        for (auto* c : std::initializer_list<juce::Component*>{&previous_, &next_, &display_, &lamp_})
            face_.addAndMakeVisible(c);

        EffectPresetLibrary::instance().addChangeListener(this);
        paramsChanged();
    }

    ~PatinaCard() override { EffectPresetLibrary::instance().removeChangeListener(this); }

    // ----- for the windows ----------------------------------------------------------------------
    const Theme& theme() const noexcept override { return theme_; }
    float param(int index) const noexcept override { return ModuleCard::param(index); }
    bool stageOn(int stage) const noexcept override { return param(pa::onParam(stage)) >= 0.5f; }
    int historyCount() const noexcept override { return history_.size(); }
    std::uint64_t historyTotal() const noexcept override { return history_.total(); }
    const Telemetry::Frame& frameAt(int i) const noexcept override { return history_.at(i); }
    const Telemetry::Frame& newest() const noexcept override
    {
        static const Telemetry::Frame none{};
        return history_.empty() ? none : history_.newest();
    }
    const Telemetry* telemetry() const noexcept override { return pa::telemetryOf(liveProcessor()); }
    // The amount a stage is set to after Age (where its lamps' dim tail ends).
    float setAmount(int stage) const noexcept override
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

    void toggleStage(int stage) override
    {
        const int index = pa::onParam(stage);
        beginGesture(juce::String(stageNames[static_cast<size_t>(stage)]) +
                     (stageOn(stage) ? " off" : " on"));
        setParam(index, stageOn(stage) ? 0.0f : 1.0f);
        endGesture();
    }

    // Moves the keyboard focus to a stage's window.
    void focusWindow(int stage) override
    {
        windows_[static_cast<size_t>(std::clamp(stage, 0, pa::stageCount - 1))]->grabKeyboardFocus();
    }

private:
    // A knob bound to its parameter on the face, its name printed under it.
    Knob&
    control(int index, juce::Point<float> centre, float skirt, const juce::String& name, Control::Kind kind)
    {
        auto& k = bindRenderedKnob(index, centre, skirt, skirt * knobImageRatio, part("knob-pointer.png"));
        k.format = formatFor(index);
        face_.controls.push_back({index, centre, skirt, name, kind, {}});
        return k;
    }

    // A stage's type selector: a knob that clicks between its positions, fanned
    // across its top, their legends printed round it.
    void stage(int stage, int index, const juce::String& caption, juce::StringArray legends)
    {
        const juce::Point<float> centre(moduleCentre(stage), selectorRow);
        auto& k = bindRenderedKnob(
            index, centre, selectorSkirt, selectorSkirt * knobImageRatio, part("knob-pointer.png"));
        k.setInterval(1.0);
        k.setSweep(juce::degreesToRadians(selectorStep * static_cast<float>(legends.size() - 1)));
        const auto& choices = info(index).choices;
        k.format = [choices](double v)
        {
            const auto i = static_cast<size_t>(
                std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(choices.size()) - 1));
            return juce::String::fromUTF8(choices[i].c_str());
        };
        face_.controls.push_back(
            {index, centre, selectorSkirt, caption, Control::Kind::selector, std::move(legends)});
    }

    void main(int stage, int index, const juce::String& name)
    {
        control(index, {moduleCentre(stage), mainRow}, mainSkirt, name, Control::Kind::main);
    }

    struct Named
    {
        int index;
        const char* name;
    };
    // A stage's small knobs: two to a row, the rows' knobs centred.
    void small(int stage, std::vector<Named> knobs)
    {
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const size_t row = i / 2;
            const bool alone = row * 2 + 1 >= knobs.size() && i % 2 == 0;
            const float x =
                alone ? moduleCentre(stage) : moduleCentre(stage) + (i % 2 == 0 ? -smallSpread : smallSpread);
            control(knobs[i].index, {x, smallRows[row]}, smallSkirt, knobs[i].name, Control::Kind::small);
        }
    }

    std::function<juce::String(double)> formatFor(int index)
    {
        switch (index)
        {
            case pa::age:
                return percentText;
            case pa::pace:
                return [](double v)
                { return juce::String(pa::paceHz(static_cast<float>(v) * 0.01f), 2) + " Hz"; };
            case pa::heatTone:
            case pa::chamberTone:
            case pa::tone:
                return signedPercentText;
            case pa::chamberDecay:
                return [this](double v)
                {
                    const float s = pa::chamberDecaySeconds(
                        static_cast<int>(std::lround(param(pa::chamberType))), static_cast<float>(v) * 0.01f);
                    return juce::String(s, 1) + " s";
                };
            case pa::driftRate:
                return [](double v)
                {
                    return juce::String(pa::driftRateScale(static_cast<float>(v) * 0.01f), 2) +
                           juce::String::fromUTF8("\xc3\x97");
                };
            case pa::grainTexture:
                return [this](double v) { return textureText(v); };
            case pa::grainFollow:
                return [](double v)
                {
                    const int i = static_cast<int>(std::lround(v));
                    return i == 0 ? juce::String("Steady")
                                  : juce::String(i < 0 ? "Duck " : "Ride ") + juce::String(std::abs(i));
                };
            case pa::crushBits:
                return [](double v)
                { return fixedText(v, std::abs(v - std::round(v)) < 0.05 ? 0 : 1) + " bit"; };
            case pa::crushRate:
                return [](double v) { return compactHzText(v); };
            case pa::lowCut:
                return [](double hz) { return hz <= 20.5 ? juce::String("Off") : compactHzText(hz); };
            case pa::highCut:
                return [](double hz) { return hz >= 19900.0 ? juce::String("Off") : compactHzText(hz); };
            case pa::output:
                return [](double db)
                {
                    const int tenths = juce::roundToInt(db * 10.0);
                    return (tenths < 0 ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String()) +
                           juce::String(std::abs(tenths) / 10) + "." + juce::String(std::abs(tenths) % 10) +
                           " dB";
                };
            default:
                return percentText;
        }
    }

    juce::String textureText(double v) const
    {
        const int source = static_cast<int>(std::lround(param(pa::grainSource)));
        if (source == pa::vinyl)
            return juce::String(
                       static_cast<int>(std::lround(pa::crackleRate(static_cast<float>(v) * 0.01f)))) +
                   " /s";
        return percentText(v);
    }
    juce::String textureName() const
    {
        const int source = static_cast<int>(std::lround(param(pa::grainSource)));
        const char* names[] = {"DENSITY", "COLOUR", "BUZZ", "BUZZ", "TONE"};
        return names[std::clamp(source, 0, 4)];
    }

    juce::String valueText(int index) const
    {
        const auto* k = knobFor(index);
        return k != nullptr && k->format ? k->format(param(index)) : juce::String();
    }

    void paramsChanged() override
    {
        for (int s = 0; s < pa::stageCount; ++s)
        {
            auto& b = *buttons_[static_cast<size_t>(s)];
            showToggle(b, stageOn(s));
            b.setTooltip(juce::String(stageNames[static_cast<size_t>(s)]) + (stageOn(s) ? ": on" : ": off"));
            windows_[static_cast<size_t>(s)]->repaint();
        }
        showPreset();
        lamp_.repaint();
        repaintChanged();
    }

    // The stage switches show on their own buttons and windows.
    bool printsWidely(int index) const override
    {
        for (int s = 0; s < pa::stageCount; ++s)
            if (index == pa::onParam(s))
                return false;
        return knobFor(index) == nullptr;
    }
    // The space sets the decay's seconds; the source names the texture knob and its unit.
    std::vector<int> alsoChanges(int index) const override
    {
        if (index == pa::chamberType)
            return {pa::chamberDecay};
        if (index == pa::grainSource)
            return {pa::grainTexture};
        return {};
    }

    void framesArrived(double) override
    {
        const auto* t = telemetry();
        if (t != telemetrySource_)
        {
            telemetrySource_ = t;
            cursor_ = t != nullptr ? t->framesWritten() : 0;
        }
        bool arrived = false;
        if (t != nullptr)
        {
            std::array<Telemetry::Frame, 64> frames;
            for (int n = 64; n == 64;)
            {
                n = t->read(cursor_, frames.data(), 64);
                arrived = arrived || n > 0;
                for (int i = 0; i < n; ++i)
                    history_.push(frames[static_cast<size_t>(i)]);
            }
        }
        // The windows of the stages that are on, and only when frames came: an off
        // stage's window is still (it repaints when the stage is switched).
        if (!arrived)
            return;
        for (int s = 0; s < pa::stageCount; ++s)
            if (stageOn(s))
                windows_[static_cast<size_t>(s)]->repaint();
    }

    // ----- presets ------------------------------------------------------------------------------
    // The same list as the header's preset bar (factory, then the user's), so the
    // two stay in step.
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        showPreset(true);
    } // the library changed

    int presetIndex() const { return EffectPresetLibrary::instance().match(pa::type, values()); }

    void showPreset(bool force = false)
    {
        // Matched again only when the values moved (a stage switch or a lamp
        // repaints here too, and automation calls it every frame).
        if (!force && values() == matchedParams_)
            return;
        matchedParams_ = values();
        const int i = presetIndex();
        if (i < 0)
            display_.show(juce::String::fromUTF8("\xe2\x80\x93\xe2\x80\x93"), "Custom");
        else
            display_.show(juce::String(i + 1).paddedLeft('0', 2),
                          EffectPresetLibrary::instance().presets(pa::type)[static_cast<size_t>(i)].name);
    }

    void applyPreset(int index)
    {
        const auto& list = EffectPresetLibrary::instance().presets(pa::type);
        if (index < 0 || static_cast<size_t>(index) >= list.size())
            return;
        const auto preset = list[static_cast<size_t>(index)];
        applyValues(preset.values, "Preset " + preset.name);
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
                       values(),
                       [safe](int index)
                       {
                           if (safe != nullptr)
                               safe->applyPreset(index);
                       });
    }

    Face face_;
    std::array<std::unique_ptr<StageWindow>, pa::stageCount> windows_;
    std::array<std::unique_ptr<StageButton>, pa::stageCount> buttons_;

    FeedbackHistory<Telemetry::Frame> history_;
    std::uint64_t cursor_ = 0;
    const Telemetry* telemetrySource_ = nullptr;
    std::vector<float> matchedParams_; // the values the shown preset was matched against

    HardwareKey previous_, next_;
    PresetDisplay display_;
    JewelLamp lamp_; // the pilot lamp, lit while the effect is on
};

} // namespace

std::unique_ptr<ModuleCard>
createPatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<PatinaCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
