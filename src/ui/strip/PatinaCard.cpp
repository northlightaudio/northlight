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
#include "ui/components/HardwareParts.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/EffectPresetLibrary.h"
#include "ui/strip/HardwareKey.h"
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
using Telemetry = pa::Telemetry;
using hardware::legendFont;
using hardware::polar;
constexpr int historyFrames = 600; // three seconds of 5 ms frames
constexpr std::array<const char*, pa::stageCount> stageNames{
    "Heat", "Chamber", "Drift", "Grain", "Wear", "Crush"};

juce::String percentText(double v)
{
    return ui::percentText(v);
}

const juce::Image& part(const juce::String& file)
{
    return hardware::image("patina", file);
}

void focusRing(juce::Graphics& g, juce::Component& c, juce::Rectangle<float> r, float radius)
{
    if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&c.getLookAndFeel()))
        laf->drawFocusRing(g, c, r, radius);
}

// ----- where things are on the face (pixels at 100%) ----------------------------------------

constexpr float faceWidth = 1160.0f, faceHeight = 680.0f;
constexpr float shadowMargin = 12.0f; // round each plate's and bezel's image

// Along the top, on the hammertone.
const juce::Rectangle<float> nameplateArea{24.0f, 14.0f, 300.0f, 56.0f};
// On the nameplate: the maker's mark etched in a square, then the name and the
// maker's line beside it.
const juce::Rectangle<float> nameplateMark{48.0f, 25.0f, 34.0f, 34.0f};
const juce::Rectangle<float> nameplateName{96.0f, 21.0f, 204.0f, 28.0f};
const juce::Rectangle<float> nameplateMaker{97.0f, 48.0f, 204.0f, 13.0f};
const juce::Rectangle<float> modelLine{344.0f, 33.0f, 340.0f, 18.0f};
const juce::Rectangle<float> previousKey{704.0f, 26.0f, 38.0f, 32.0f};
const juce::Rectangle<float> displayBezel{750.0f, 20.0f, 300.0f, 44.0f};
const juce::Rectangle<float> nextKey{1058.0f, 26.0f, 38.0f, 32.0f};
const juce::Point<float> lampCentre{1124.0f, 42.0f};

// The six stage modules and the master plate.
constexpr float moduleLeft = 20.0f, moduleTop = 92.0f, moduleWidth = 142.0f, moduleGap = 8.0f;
constexpr float moduleHeight = 568.0f;
const juce::Rectangle<float> masterArea{924.0f, moduleTop, 216.0f, moduleHeight};

float moduleX(int stage) noexcept
{
    return moduleLeft + static_cast<float>(stage) * (moduleWidth + moduleGap);
}
float moduleCentre(int stage) noexcept
{
    return moduleX(stage) + moduleWidth * 0.5f;
}

// Rows down a module.
constexpr float nameRow = 118.0f;
constexpr float buttonWidth = 36.0f, buttonHeight = 24.0f;
const juce::Rectangle<float> windowBezelAt{8.0f, 138.0f, 126.0f, 96.0f}; // x from the module's left
constexpr float openingRadius = 3.0f; // the bezels' openings' corners, which the glass follows
constexpr float bezelFrame = 7.0f;    // the glass opening is inset this far
constexpr float ledRow = 241.0f, ledHeight = 4.0f, ledWidth = 10.0f, ledGap = 3.0f;
constexpr int ledCount = 8;
constexpr float selectorRow = 300.0f, selectorCaptionRow = 337.0f, selectorSkirt = 30.0f;
constexpr float selectorStep = 52.0f; // degrees between a selector's positions
constexpr float mainRow = 394.0f, mainSkirt = 52.0f;
constexpr std::array<float, 2> smallRows{500.0f, 590.0f};
constexpr float smallSkirt = 30.0f, smallSpread = 34.0f;

// The master plate.
const juce::Point<float> ageCentre{1032.0f, 212.0f};
constexpr float ageSkirt = 96.0f;
const juce::Point<float> paceCentre{1032.0f, 380.0f};
constexpr float paceSkirt = 34.0f;
constexpr float outputCaptionRow = 452.0f;
constexpr std::array<float, 3> outputX{966.0f, 1032.0f, 1098.0f};

// A knob's image is this many times its skirt (the skirt is 56% of the render, the
// rest room for its shadow).
constexpr float knobImageRatio = 3.4f / 1.9f;
// The button's image is 60 x 45 round its 36 x 24 bezel.
constexpr float buttonImageWidth = 60.0f, buttonImageHeight = 45.0f;

juce::Rectangle<float> glassOf(int stage) noexcept
{
    return windowBezelAt.translated(moduleX(stage), 0.0f).reduced(bezelFrame);
}

class PatinaCard;

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
        const auto& image = part(lit ? "button-on.png" : "button-off.png");
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
        focusRing(g, *this, bezel().expanded(1.0f), 4.0f);
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
        focusRing(g, *this, r, openingRadius);
    }

private:
    juce::String upperName_;                             // the name as drawn
    float capitalHeight_ = -1.0f, capitalCentre_ = 0.0f; // the measured capital, per font size
    const Theme& theme_;
    juce::String number_, name_;
};

// The pilot lamp (Corona's red jewel), lit while the effect is on.
class Lamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Lamp(const Theme& theme, std::function<bool()> on) : theme_(theme), on_(std::move(on)) {}

    void paint(juce::Graphics& g) override
    {
        const bool lit = on_();
        const auto& image = hardware::image("corona", lit ? "jewel-lit.png" : "jewel-off.png");
        const auto r = getLocalBounds().toFloat();
        if (image.isValid())
        {
            hardware::drawScaled(g, image, r);
            return;
        }
        const auto& hw = theme_.hardware;
        g.setColour(lit ? hw.coronaJewel : hw.coronaJewel.darker(0.7f));
        g.fillEllipse(r.reduced(r.getWidth() * 0.25f));
    }

private:
    const Theme& theme_;
    std::function<bool()> on_;
};

// ----- a stage's window -------------------------------------------------------------------------

// The lit glass in a stage's bezel, its live readout, and the row of lamps under
// the bezel: the window's bounds run from the glass's top to the lamps' foot, and
// it draws nothing over the bezel between them.
class StageWindow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StageWindow(PatinaCard& card, int stage);

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    // The window's glass, where the readout draws (its own pixels).
    juce::Rectangle<float> well() const;

private:
    // What a readout draws with: the window's ink, lit or unlit.
    struct Inks
    {
        juce::Colour hue, textPrimary, textSecondary, textTertiary, gridBar, dotRing;
    };

    void paintLamps(juce::Graphics& g);
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
    Inks inks_;
    std::vector<float> a_, b_; // per column, reused
    std::vector<float> scopeIn_, scopeOut_;
};

// A strip along the top of each readout for its figure.
constexpr float readoutRoom = 16.0f;

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
    std::function<juce::String(int param)> value;
    std::function<float(int param)> paramValue;
    // A control's name when it depends on a setting (Grain's texture).
    std::function<juce::String(const Control&)> nameOf;

private:
    float nameSize(float size) const noexcept { return isCompact() ? size * compactNameScale : size; }
    float tracking(float t) const noexcept { return isCompact() ? t * 0.4f : t; }

    juce::String nameFor(const Control& c) const
    {
        const auto name = nameOf ? nameOf(c) : c.name;
        if (isCompact() && activeControl() == c.param && value)
            return value(c.param);
        return name;
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
        if (const auto& body = part("knob-body.png"); body.isValid())
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
            g.setFont(legendFont(theme_, isCompact() ? 21.0f : 18.0f, 700, tracking(0.14f)));
            print(g,
                  juce::String(stageNames[static_cast<size_t>(s)]).toUpperCase(),
                  {moduleX(s) + 14.0f, nameRow - 12.0f, moduleWidth - 28.0f - buttonWidth, 24.0f},
                  juce::Justification::centredLeft);
        }

        // The master plate's output caption over its knobs, between two rules.
        {
            g.setColour(hw.patinaInkSoft);
            g.setFont(legendFont(theme_, nameSize(13.0f), 700, tracking(0.24f)));
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
            g.setFont(legendFont(theme_, nameSize(11.5f), 700, tracking(0.12f)));
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
        g.setFont(
            legendFont(theme_, isCompact() && smallName ? size * 1.5f : nameSize(size), 700, tracking(0.1f)));
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

class PatinaCard final : public RenderedCard, private juce::ChangeListener
{
public:
    PatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : RenderedCard(theme, host, channelId, insert, face_), face_(theme),
          history_(static_cast<size_t>(historyFrames)),
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

    void toggleStage(int stage)
    {
        const int index = pa::onParam(stage);
        beginGesture(juce::String(stageNames[static_cast<size_t>(stage)]) +
                     (stageOn(stage) ? " off" : " on"));
        setParam(index, stageOn(stage) ? 0.0f : 1.0f);
        endGesture();
    }

    // Moves the keyboard focus to a stage's window.
    void focusWindow(int stage)
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
            b.setToggleState(stageOn(s), juce::dontSendNotification);
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
                {
                    history_[static_cast<size_t>(head_)] = frames[static_cast<size_t>(i)];
                    head_ = (head_ + 1) % historyFrames;
                    count_ = std::min(historyFrames, count_ + 1);
                    ++total_;
                }
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

    int presetIndex() const { return EffectPresetLibrary::instance().match(pa::type, params_); }

    void showPreset(bool force = false)
    {
        // Matched again only when the values moved (a stage switch or a lamp
        // repaints here too, and automation calls it every frame).
        if (!force && params_ == matchedParams_)
            return;
        matchedParams_ = params_;
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

    Face face_;
    std::array<std::unique_ptr<StageWindow>, pa::stageCount> windows_;
    std::array<std::unique_ptr<StageButton>, pa::stageCount> buttons_;

    std::vector<Telemetry::Frame> history_;
    int head_ = 0, count_ = 0;
    std::uint64_t total_ = 0, cursor_ = 0;
    const Telemetry* telemetrySource_ = nullptr;
    std::vector<float> matchedParams_; // the values the shown preset was matched against

    HardwareKey previous_, next_;
    PresetDisplay display_;
    Lamp lamp_;
};

// ----- the window, drawn --------------------------------------------------------------------------

StageWindow::StageWindow(PatinaCard& card, int stage) : card_(card), stage_(stage)
{
    setWantsKeyboardFocus(true);
    setComponentID(juce::String("stage:") + stageNames[static_cast<size_t>(stage)]);
    setTooltip(stageNames[static_cast<size_t>(stage)]);
}

juce::Rectangle<float> StageWindow::well() const
{
    const auto glass = glassOf(stage_);
    return {0.0f, 0.0f, glass.getWidth(), glass.getHeight()};
}

bool StageWindow::hitTest(int x, int y)
{
    return well().contains(static_cast<float>(x), static_cast<float>(y));
}

bool StageWindow::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
        card_.focusWindow(stage_ + (key == juce::KeyPress::leftKey ? -1 : 1));
    else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
        card_.toggleStage(stage_);
    else
        return false;
    return true;
}

void StageWindow::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& hw = theme.hardware;
    const bool on = card_.stageOn(stage_);

    // The glass, lit from behind while the stage is on, cut to the bezel's opening.
    const auto glass = well();
    g.saveState();
    {
        juce::Path opening;
        opening.addRoundedRectangle(glass, openingRadius);
        g.reduceClipRegion(opening);
    }
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
        g.fillRect(glass);
    }
    const auto ink = on ? hw.glassInk : hw.glassMid;
    inks_ = {ink,
             ink,
             ink.withAlpha(0.55f),
             ink.withAlpha(0.5f),
             ink.withAlpha(0.16f),
             on ? hw.glassCentre : hw.glassOffCentre};
    // Switched off, the window goes dark: no readout, no grid, no figure.
    if (on)
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
    }
    // The shade under the bezel's lip and the glass's reflection.
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
    g.fillRect(glass);
    g.restoreState();
    paintLamps(g);
    focusRing(g, *this, glass.expanded(3.0f), openingRadius + 3.0f);
}

void StageWindow::paintLamps(juce::Graphics& g)
{
    // The amount Age and Breath leave, live; dim up to where Age alone puts it.
    const auto& hw = card_.theme().hardware;
    const bool on = card_.stageOn(stage_);
    const float live =
        on ? std::clamp(card_.newest()[static_cast<size_t>(Telemetry::amountHeat + stage_)], 0.0f, 1.0f)
           : 0.0f;
    const float set = on ? std::clamp(card_.setAmount(stage_), 0.0f, 1.0f) : 0.0f;
    const int lit = juce::roundToInt(live * static_cast<float>(ledCount));
    const int dim = juce::roundToInt(set * static_cast<float>(ledCount));
    const float rowW = ledCount * ledWidth + (ledCount - 1) * ledGap;
    float x = static_cast<float>(getWidth()) * 0.5f - rowW * 0.5f;
    const float y = static_cast<float>(getHeight()) - ledHeight - 1.0f;
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
            if (i < dim)
            {
                g.setColour(colour.withAlpha(0.4f));
                g.fillRoundedRectangle(led.reduced(1.0f), 0.5f);
            }
        }
    }
}

void StageWindow::readout(juce::Graphics& g,
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

void StageWindow::scaleLabel(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text)
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

void StageWindow::paintHeat(juce::Graphics& g, juce::Rectangle<float> p)
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
    readout(g, p, reductionText(squash, "0.0 dB"), squash >= 0.1f ? hue : c.textTertiary);
}

void StageWindow::paintChamber(juce::Graphics& g, juce::Rectangle<float> p)
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

void StageWindow::paintDrift(juce::Graphics& g, juce::Rectangle<float> p)
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

void StageWindow::paintGrain(juce::Graphics& g, juce::Rectangle<float> p)
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
            noise > 1.0e-6f ? dbText(toDb(noise), 0) : juce::String::fromUTF8("\xe2\x80\x93"),
            noise > 1.0e-6f ? hue : c.textTertiary);
}

void StageWindow::paintWear(juce::Graphics& g, juce::Rectangle<float> p)
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
    readout(g,
            p,
            dip >= 1.0f ? reductionText(dip, "0.0 dB") : compactHzText(cutoff),
            dip >= 1.0f ? hue : c.textSecondary);
}

void StageWindow::paintCrush(juce::Graphics& g, juce::Rectangle<float> p)
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
                      juce::String::fromUTF8("\xc2\xb7 ") + compactHzText(hz);
    readout(g, p, text, live ? c.textSecondary : c.textTertiary);
}

} // namespace

std::unique_ptr<ModuleCard>
createPatinaCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<PatinaCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
