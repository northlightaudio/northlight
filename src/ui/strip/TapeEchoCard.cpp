// The tape echo's card (screens/plugins.md 7.2a): a 1970s tape echo console, a
// hardware face (design-system.md "Hardware face") built from rendered parts
// (assets/tapeecho; "Rendered parts"). The face is drawn at 1160 x 600 and scaled
// to fit the card's body, the card narrowing with it.
//
// The cream panel carries the window onto the tape transport (a neon on each head,
// lit by the level passing it; the rollers turning with the tape; a smoked lid),
// the record level's VU meter and the loop gain lamp (amber, red and flickering
// once the loop sings). The sage panel carries the controls: HEADS, REPEAT RATE
// (the motor, or with SYNC on head 1's note value), INTENSITY and ECHO VOLUME; the
// stereo, sync and dry levers; and the loop's tone, the tape and the output on
// nine small knobs.

#include "plugins/BeatSync.h"
#include "plugins/TapeEcho.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/HardwareParts.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/TapeEchoFace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace northlight::ui::strip
{

namespace te = plugins::tapeecho;
namespace tf = tapeface;

namespace
{
constexpr double frameSeconds = engine::ProcessorFeedback::frameSeconds;
constexpr float pi = juce::MathConstants<float>::pi;

// ----- where things are on the face (pixels at 100%) -----------------------------------------

const juce::Rectangle<float> creamArea{0.0f, 0.0f, tf::width, 260.0f};
const juce::Rectangle<float> sageArea{0.0f, 268.0f, tf::width, 332.0f};
const juce::Rectangle<float> trimImage{0.0f, 258.0f, tf::width, 20.0f}; // the trim with its shadow
const juce::Rectangle<float> trimBand{0.0f, 260.0f, tf::width, 8.0f};
const juce::Rectangle<float> windowFrame{232.0f, 20.0f, 642.0f, 212.0f};
const juce::Rectangle<float> windowBezel{212.0f, 0.0f, 682.0f, 277.0f};
const juce::Rectangle<int> opening{241, 29, 624, 194};    // the transport, under the bezel's lip
const juce::Rectangle<int> meterArea{876, -12, 280, 230}; // the meter's images (their top 12 px is margin)
const juce::Rectangle<int> meterBounds = meterArea.withTrimmedTop(12); // the needle's layer, on the face
const juce::Rectangle<int> lampArea{950, 178, 65, 75};                 // the loop gain lamp's image
const juce::Point<float> lampCentre{982.4f, 210.0f};

// The meter's images are rendered at 3 px to the face's 1 (an 840 x 690 frame).
constexpr float meterUnit = 280.0f / 840.0f;
const juce::Point<float> meterPivot{420.0f, 468.1f}; // in the meter's 840 x 690 frame
constexpr float meterRadius = 266.0f;

// Inside the transport's window (opening pixels): where each neon and roller is.
constexpr std::array<float, 5> neonX{127.5f, 197.0f, 254.5f, 312.0f, 369.5f}; // R, 1-4
constexpr float neonY = 146.8f;
const juce::Rectangle<float> pinchHub{431.53f, 118.77f, 20.0f, 20.0f};
const juce::Rectangle<float> tensionHub{265.83f, 39.6f, 24.0f, 24.0f};

// The knobs: the big four's centres, then the nine small ones'.
constexpr float bigRow = 350.0f, smallRow = 486.0f;
constexpr std::array<float, 4> bigX{110.0f, 278.0f, 436.0f, 594.0f}; // heads, rate, intensity, volume
constexpr float bigSkirt = 72.0f, bigImage = 122.4f;
constexpr float smallSkirt = 44.0f, smallImage = 74.8f;
float smallX(int i)
{
    return 76.0f + 126.0f * static_cast<float>(i);
}
// The levers: stereo, sync, dry, over the last three small knobs.
constexpr std::array<float, 3> leverX{832.0f, 958.0f, 1084.0f};
constexpr float leverY = 350.0f;
constexpr float leverScale = 0.85f; // of the rendered lever's size

// The heads' names under the window, centred on each head.
constexpr std::array<float, 6> headLegendX{77.0f, 133.0f, 204.0f, 262.0f, 321.0f, 380.0f};

const juce::Image& part(const char* file)
{
    return hardware::image("tapeecho", file);
}

// A legend in the console's face: Barlow Condensed at a size, weight and tracking.
juce::Font legendFont(const Theme& theme, float size, int weight, float trackingEm)
{
    return theme.font(Theme::Face::legend, size, weight).withExtraKerningFactor(trackingEm);
}

void drawImageIn(juce::Graphics& g,
                 const juce::Image& image,
                 juce::Rectangle<float> area,
                 float opacity = 1.0f)
{
    if (!image.isValid())
        return;
    g.setOpacity(opacity);
    g.drawImage(image, area, juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1.0f);
}

juce::Point<float> polar(juce::Point<float> centre, float radius, float degrees)
{
    const float a = degrees * pi / 180.0f;
    return {centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)};
}

void focusRing(juce::Graphics& g, juce::Component& c, juce::Rectangle<float> r, float radius)
{
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&c.getLookAndFeel()))
        lnf->drawFocusRing(g, c, r, radius);
}

// ----- a lever ---------------------------------------------------------------------------------

// A bat-handle toggle on the panel, rendered in each position: up, (centre,) down.
// A click above its middle moves it up a position, below it down; Up and Down
// move it, Space flips a two-way lever or steps a three-way one. Only the nut
// takes the mouse; the rest of its bounds is the image's margin.
class Lever final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Lever(const Theme& theme, juce::String name, std::vector<juce::String> positions)
        : theme_(theme), name_(std::move(name)), positions_(std::move(positions))
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(name_);
        update();
    }

    std::function<void(int position)> onChoose;

    void setPosition(int position)
    {
        position_ = juce::jlimit(0, static_cast<int>(positions_.size()) - 1, position);
        update();
        repaint();
    }
    int position() const noexcept { return position_; }

    void paint(juce::Graphics& g) override
    {
        const auto nut = nutBounds();
        const char* files[] = {"lever-up.png", "lever-centre.png", "lever-down.png"};
        const int frame = positions_.size() == 2 && position_ == 1 ? 2 : position_;
        const auto& image = part(files[frame]);
        const bool enabled = isEnabled();
        if (image.isValid())
            drawImageIn(g, image, getLocalBounds().toFloat(), enabled ? 1.0f : 0.5f);
        else
        {
            // Without its image: a washer and a line for the lever.
            const auto& hw = theme_.hardware;
            g.setColour(hw.aluminium);
            g.fillEllipse(nut.reduced(2.0f));
            g.setColour(hw.knobBody);
            if (frame == 1)
                g.fillEllipse(
                    juce::Rectangle<float>(10.0f, 10.0f).withCentre(nut.getCentre())); // pointing out
            else
                g.drawLine(juce::Line<float>(nut.getCentre(),
                                             nut.getCentre().translated(0.0f, frame == 0 ? -20.0f : 20.0f)),
                           6.0f);
        }
        if (enabled && isMouseOver(true))
        {
            g.setColour(theme_.hardware.highlight.withAlpha(0.08f));
            g.fillEllipse(nut);
        }
        focusRing(g, *this, nut, nut.getWidth() * 0.5f);
    }

    bool hitTest(int x, int y) override
    {
        return nutBounds().contains(static_cast<float>(x), static_cast<float>(y));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        move(e.position.y < nutBounds().getCentreY() ? -1 : 1);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::upKey)
            move(-1);
        else if (key == juce::KeyPress::downKey)
            move(1);
        else if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey)
            press();
        else
            return false;
        return true;
    }
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler>(
            *this,
            juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                   [this] { press(); }));
    }

private:
    // The washer and nut, at the image's centre: what a hand would push.
    juce::Rectangle<float> nutBounds() const
    {
        const float w = static_cast<float>(getWidth());
        return juce::Rectangle<float>(w * 0.5f - 26.0f, w * 0.5f - 26.0f, 52.0f, 52.0f);
    }

    void press()
    {
        const int n = static_cast<int>(positions_.size());
        choose(n == 2 ? 1 - position_ : (position_ + 1) % n);
    }
    void move(int by) { choose(juce::jlimit(0, static_cast<int>(positions_.size()) - 1, position_ + by)); }
    void choose(int position)
    {
        if (position == position_)
            return;
        setPosition(position);
        if (onChoose)
            onChoose(position);
    }
    void update()
    {
        setTooltip(name_ + ": " + positions_[static_cast<size_t>(position_)]);
        setDescription(positions_[static_cast<size_t>(position_)]);
    }

    const Theme& theme_;
    juce::String name_;
    std::vector<juce::String> positions_;
    int position_ = 0;
};

// ----- the meter -------------------------------------------------------------------------------

// The VU's needle, its shadow on the dial, and the pivot's hood and the glass over
// them; the body and the printed scale are the face's.
class Meter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Meter(const Theme& theme) : theme_(theme)
    {
        setInterceptsMouseClicks(false, false);
        setTitle("Record level");
    }

    void setReading(float linear)
    {
        const float vu = tf::vuOf(linear);
        const float angle = tf::vuAngle(vu);
        if (std::abs(angle - angle_) < 0.05f)
            return;
        angle_ = angle;
        setDescription(juce::String(vu, 1) + " VU");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& hw = theme_.hardware;
        g.addTransform(juce::AffineTransform::scale(meterUnit).translated(
            0.0f, static_cast<float>(meterArea.getY() - meterBounds.getY())));
        const auto tip = polar(meterPivot, 285.0f, angle_);
        g.setColour(hw.shadow.withAlpha(0.22f));
        g.drawLine(juce::Line<float>(meterPivot.translated(0.0f, 9.0f), tip.translated(0.0f, 9.0f)), 6.0f);
        g.setColour(hw.needle);
        g.drawLine(juce::Line<float>(meterPivot, tip), 4.2f);
        const juce::Rectangle<float> frame(0.0f, 0.0f, 840.0f, 690.0f);
        if (const auto& hood = part("vu-hood.png"); hood.isValid())
            drawImageIn(g, hood, frame);
        else
        {
            // Without its image: the pivot's hood, a dark trapezoid over the needle's base.
            juce::Path p;
            p.startNewSubPath(300.0f, 495.0f);
            p.lineTo(345.0f, 405.0f);
            p.lineTo(495.0f, 405.0f);
            p.lineTo(540.0f, 495.0f);
            p.closeSubPath();
            g.setColour(hw.knobBody);
            g.fillPath(p);
        }
        drawImageIn(g, part("vu-glass.png"), frame);
    }

private:
    const Theme& theme_;
    float angle_ = -53.0f;
};

// ----- the transport ---------------------------------------------------------------------------

// What moves in the window: each head's neon (its light over the transport, as
// bright as the level passing it), the screw slots and drillings of the two
// rollers turning with the tape, and the smoked lid over everything. The
// transport itself is the face's.
class Transport final : public juce::Component
{
public:
    explicit Transport(const Theme& theme) : theme_(theme) { setInterceptsMouseClicks(false, false); }

    // 0..1 per neon: R, then heads 1-4.
    std::array<float, 5> neon{};
    float pinchAngle = 0.0f, tensionAngle = 0.0f;

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        const char* neonFiles[] = {"neon-R.png", "neon-1.png", "neon-2.png", "neon-3.png", "neon-4.png"};
        for (size_t i = 0; i < neon.size(); ++i)
        {
            if (neon[i] <= 0.004f)
                continue;
            const auto& image = part(neonFiles[i]);
            if (image.isValid())
                drawImageIn(g, image, area, neon[i]);
            else
            {
                // Without its image: the bulb's glow alone.
                const juce::Point<float> at(neonX[i], neonY);
                g.setColour(theme_.hardware.neon.withAlpha(neon[i] * 0.8f));
                g.fillEllipse(juce::Rectangle<float>(12.0f, 6.0f).withCentre(at));
            }
        }
        hub(g, part("hub-pinch.png"), pinchHub, pinchAngle);
        hub(g, part("hub-tension.png"), tensionHub, tensionAngle);
        drawImageIn(g, part("perspex.png"), area);
    }

private:
    void hub(juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> box, float angle) const
    {
        if (!image.isValid())
            return;
        const auto c = box.getCentre();
        g.drawImageTransformed(
            image,
            juce::AffineTransform::scale(box.getWidth() / static_cast<float>(image.getWidth()))
                .translated(box.getX(), box.getY())
                .rotated(angle, c.x, c.y));
    }

    const Theme& theme_;
};

// ----- the loop gain lamp ----------------------------------------------------------------------

class GainLamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GainLamp(const Theme& theme) : theme_(theme)
    {
        setInterceptsMouseClicks(true, false);
        setTitle("Loop gain");
    }

    void set(float loopGain, bool on, double elapsedMs)
    {
        const auto state = tf::lampFor(loopGain, on);
        const bool singing = state == tf::Lamp::singing;
        // A singing loop's lamp flickers, unsteady as the loop it shows.
        flicker_ += elapsedMs * 0.001;
        const float level =
            singing && !theme_.motion.reduceMotion
                ? 0.86f + 0.14f * std::abs(std::sin(static_cast<float>(flicker_) * 11.0f +
                                                    std::sin(static_cast<float>(flicker_) * 3.1f)))
                : 1.0f;
        const auto text = juce::String(loopGain, 2) + (singing ? ", singing" : "");
        if (state == state_ && std::abs(level - level_) < 0.02f && text == text_)
            return;
        state_ = state;
        level_ = level;
        text_ = text;
        setTooltip("Loop gain " + text_);
        setDescription(text_);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& image = part(state_ == tf::Lamp::dark      ? "lamp-off.png"
                                 : state_ == tf::Lamp::singing ? "lamp-red.png"
                                                               : "lamp-amber.png");
        if (image.isValid())
            drawImageIn(g, image, getLocalBounds().toFloat(), level_);
        else
        {
            const auto& hw = theme_.hardware;
            const auto lens = juce::Rectangle<float>(28.0f, 28.0f).withCentre({32.5f, 32.5f});
            g.setColour(hw.aluminium);
            g.fillEllipse(lens.expanded(4.0f));
            g.setColour(state_ == tf::Lamp::dark      ? hw.knobBody
                        : state_ == tf::Lamp::singing ? hw.ledHot
                                                      : hw.led);
            g.fillEllipse(lens);
        }
    }

private:
    const Theme& theme_;
    tf::Lamp state_ = tf::Lamp::dark;
    float level_ = 1.0f;
    double flicker_ = 0.0;
    juce::String text_;
};

// ----- the face ----------------------------------------------------------------------------

class TapeEchoCard;

// The console, at its drawn size; the card scales it to fit. Everything that
// does not move is drawn once per scale into an image.
class Face final : public juce::Component
{
public:
    explicit Face(const Theme& theme) : theme_(theme)
    {
        setSize(static_cast<int>(tf::width), static_cast<int>(tf::height));
        setOpaque(true);
    }

    // What changes the static layer: REPEAT RATE's scale reads note values when synced.
    void setSynced(bool synced)
    {
        if (synced == synced_)
            return;
        synced_ = synced;
        cache_ = {};
        repaint();
    }

    // The values printed under the knobs and the heads selector's position (its
    // chosen legend is bold), drawn over the static layer.
    std::function<juce::String(int bigIndex)> bigValue;
    std::function<juce::String(int smallIndex)> smallValue;
    int headsPosition = 0;

    void paint(juce::Graphics& g) override
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (!cache_.isValid() || cacheScale_ != scale)
        {
            cacheScale_ = scale;
            cache_ = juce::Image(juce::Image::RGB,
                                 std::max(1, juce::roundToInt(tf::width * scale)),
                                 std::max(1, juce::roundToInt(tf::height * scale)),
                                 true);
            juce::Graphics cg(cache_);
            cg.addTransform(juce::AffineTransform::scale(scale));
            cg.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            paintStatic(cg);
        }
        g.drawImage(cache_, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
        paintLive(g);
    }

private:
    // ----- the static layer --------------------------------------------------------------------
    void paintStatic(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The enamel panels and the trim between them.
        g.setColour(hw.creamPanel);
        g.fillRect(creamArea);
        g.setColour(hw.sagePanel);
        g.fillRect(sageArea);
        drawImageIn(g, part("panel-cream.jpg"), creamArea);
        drawImageIn(g, part("panel-sage.jpg"), sageArea);
        g.setColour(hw.aluminium);
        g.fillRect(trimBand);
        drawImageIn(g, part("trim.png"), trimImage);

        paintWindow(g);
        paintMeter(g);
        paintCreamLegends(g);
        paintKnobs(g);
        paintLeverLegends(g);
    }

    void paintWindow(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        g.setColour(hw.tapeWell);
        g.fillRect(opening);
        drawImageIn(g, part("transport.jpg"), opening.toFloat());
        if (!part("display-bezel.png").isValid())
        {
            g.setColour(hw.aluminium);
            g.drawRect(windowFrame, 9.0f);
        }
        drawImageIn(g, part("display-bezel.png"), windowBezel);
        // The heads, named under the window.
        g.setFont(legendFont(theme_, 16.0f, 600, 0.08f));
        g.setColour(hw.creamInkSoft);
        const char* names[] = {"ERASE", "REC", "1", "2", "3", "4"};
        for (size_t i = 0; i < headLegendX.size(); ++i)
            g.drawText(
                names[i],
                juce::Rectangle<float>(windowFrame.getX() + headLegendX[i] - 30.0f, 236.0f, 60.0f, 20.0f),
                juce::Justification::centred,
                false);
    }

    void paintMeter(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        const auto area = meterArea.toFloat();
        const auto& body = part("vu-body.png");
        if (body.isValid())
            drawImageIn(g, body, area);
        else
        {
            // Without its image: an aluminium frame round a lit dial.
            const auto frame =
                juce::Rectangle<float>(area.getX() + 37.0f, area.getY() + 32.0f, 205.0f, 135.0f);
            g.setColour(hw.aluminium);
            g.fillRoundedRectangle(frame, 2.0f);
            g.setColour(hw.dialFace);
            g.fillRect(frame.reduced(9.0f));
        }
        // The scale, printed on the dial (in the meter's 840 x 690 frame).
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::scale(meterUnit).translated(area.getX(), area.getY()));
        const auto arc = [&](float from, float to, float radius, float thickness, juce::Colour colour)
        {
            juce::Path p;
            p.addCentredArc(
                meterPivot.x, meterPivot.y, radius, radius, 0.0f, from * pi / 180.0f, to * pi / 180.0f, true);
            g.setColour(colour);
            g.strokePath(p, juce::PathStrokeType(thickness));
        };
        arc(-50.0f, 50.0f, meterRadius, 3.3f, hw.dialInk);
        arc(tf::vuAngle(0.0f), 50.0f, meterRadius + 7.5f, 13.6f, hw.dialRed);
        const auto tick = [&](float vu, float outer, float thickness)
        {
            const float a = tf::vuAngle(vu);
            g.setColour(vu > 0.0f ? hw.dialRed : hw.dialInk);
            g.drawLine(
                juce::Line<float>(polar(meterPivot, meterRadius - 3.0f, a), polar(meterPivot, outer, a)),
                thickness);
        };
        for (const float vu : {-15.0f, -8.5f, -6.0f, -4.0f, -2.5f, -1.5f, -0.5f, 0.5f, 1.5f, 2.5f})
            tick(vu, meterRadius + 14.0f, 2.4f);
        g.setFont(legendFont(theme_, 46.0f, 600, 0.0f));
        for (const float vu : {-20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f})
        {
            tick(vu, meterRadius + 24.0f, 3.8f);
            const auto at = polar(meterPivot, meterRadius + 50.0f, tf::vuAngle(vu));
            g.drawText(juce::String(static_cast<int>(std::abs(vu))),
                       juce::Rectangle<float>(100.0f, 54.0f).withCentre(at),
                       juce::Justification::centred,
                       false);
        }
        g.setColour(hw.dialInk);
        g.setFont(legendFont(theme_, 70.0f, 700, 0.03f));
        g.drawText(
            "VU", juce::Rectangle<float>(320.0f, 300.0f, 200.0f, 70.0f), juce::Justification::centred, false);
    }

    void paintCreamLegends(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The name, its kind, and the model line at the column's foot.
        g.setColour(hw.creamInk);
        g.setFont(legendFont(theme_, 46.0f, 700, 0.04f));
        g.drawText(
            "TAPE", juce::Rectangle<float>(24.0f, 20.0f, 186.0f, 41.4f), juce::Justification::topLeft, false);
        g.drawText(
            "ECHO", juce::Rectangle<float>(24.0f, 57.4f, 186.0f, 41.4f), juce::Justification::topLeft, false);
        g.setColour(hw.creamInkSoft);
        g.setFont(legendFont(theme_, 16.0f, 600, 0.12f));
        g.drawText(juce::String::fromUTF8("FOUR-HEAD \xc2\xb7 SPRING"),
                   juce::Rectangle<float>(24.0f, 104.0f, 210.0f, 20.0f),
                   juce::Justification::centredLeft,
                   false);
        g.fillRect(juce::Rectangle<float>(24.0f, 195.0f, 28.0f, 1.0f));
        g.setColour(hw.creamInk);
        g.setFont(legendFont(theme_, 18.0f, 700, 0.12f));
        g.drawText("MODEL TE-4",
                   juce::Rectangle<float>(24.0f, 198.0f, 210.0f, 22.0f),
                   juce::Justification::centredLeft,
                   false);
        g.setColour(hw.creamInkSoft);
        g.setFont(legendFont(theme_, 14.0f, 600, 0.22f));
        g.drawText("NORTHLIGHT",
                   juce::Rectangle<float>(24.0f, 218.0f, 210.0f, 19.0f),
                   juce::Justification::centredLeft,
                   false);
        g.setFont(legendFont(theme_, 15.0f, 600, 0.12f));
        g.drawText("LOOP GAIN",
                   juce::Rectangle<float>(lampCentre.x + 28.0f, 200.0f, 130.0f, 20.0f),
                   juce::Justification::centredLeft,
                   false);
    }

    // A knob's printed scale: 11 ticks over 270 degrees, every fifth longer.
    void scale(juce::Graphics& g, juce::Point<float> c, float inner, float outer, float thickness) const
    {
        g.setColour(theme_.hardware.sageInk);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = -135.0f + 27.0f * static_cast<float>(i);
            const float from = i % 5 == 0 ? inner - 3.0f : inner;
            juce::Path p;
            p.startNewSubPath(polar(c, from, a));
            p.lineTo(polar(c, outer, a));
            g.strokePath(
                p,
                juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void knobBody(juce::Graphics& g, juce::Point<float> c, float image, float skirt) const
    {
        const auto& body = part("knob-body.png");
        if (body.isValid())
        {
            drawImageIn(
                g, body, juce::Rectangle<float>(c.x - image * 0.5f, c.y - image * 0.5f, image, image * 1.5f));
            return;
        }
        // Without its image: a black knurled body and an aluminium cap.
        const auto& hw = theme_.hardware;
        g.setColour(hw.knobBody);
        g.fillEllipse(juce::Rectangle<float>(skirt, skirt).withCentre(c));
        g.setColour(hw.aluminium);
        g.fillEllipse(juce::Rectangle<float>(skirt * 0.6f, skirt * 0.6f).withCentre(c));
    }

    void paintKnobs(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // The big four. HEADS's positions are the live layer's (its chosen one is bold).
        for (size_t i = 0; i < bigX.size(); ++i)
        {
            const juce::Point<float> c(bigX[i], bigRow);
            if (i > 0)
            {
                scale(g, c, 40.0f, 46.0f, 2.0f);
                g.setColour(hw.sageInk);
                g.setFont(legendFont(theme_, 18.0f, 600, 0.0f));
                const bool notes = i == 1 && synced_;
                const juce::String marks[] = {
                    notes ? "1/64" : "0", notes ? "1/8" : "5", notes ? "1/1" : "10"};
                for (int m = 0; m < 3; ++m)
                {
                    const auto at = polar(c,
                                          56.0f + (notes && m != 1 ? 10.0f : 0.0f),
                                          -135.0f + 135.0f * static_cast<float>(m));
                    g.drawText(marks[m],
                               juce::Rectangle<float>(50.0f, 22.0f).withCentre(at),
                               juce::Justification::centred,
                               false);
                }
            }
            knobBody(g, c, bigImage, bigSkirt);
            g.setColour(hw.sageInk);
            g.setFont(legendFont(theme_, 21.0f, 700, 0.08f));
            const char* labels[] = {"HEADS", "REPEAT RATE", "INTENSITY", "ECHO VOLUME"};
            g.drawText(labels[i],
                       juce::Rectangle<float>(c.x - 80.0f, 400.0f, 160.0f, 25.0f),
                       juce::Justification::centred,
                       false);
        }
        // The nine small ones.
        const char* names[] = {
            "BASS", "TREBLE", "DRIVE", "WOW", "FLUTTER", "AGE", "SPREAD", "SPRING", "OUTPUT"};
        for (int i = 0; i < 9; ++i)
        {
            const juce::Point<float> c(smallX(i), smallRow);
            scale(g, c, 25.0f, 30.0f, 1.6f);
            knobBody(g, c, smallImage, smallSkirt);
            g.setColour(hw.sageInk);
            g.setFont(legendFont(theme_, 20.0f, 700, 0.06f));
            g.drawText(names[i],
                       juce::Rectangle<float>(c.x - 62.0f, 516.0f, 124.0f, 24.0f),
                       juce::Justification::centred,
                       false);
        }
    }

    void paintLeverLegends(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        const char* names[] = {"STEREO", "SYNC", "DRY"};
        const char* tops[] = {"PAN", "ON", "ON"};
        const char* bottoms[] = {"L/R", "OFF", "KILL"};
        for (size_t i = 0; i < leverX.size(); ++i)
        {
            const float x = leverX[i];
            g.setColour(hw.sageInk);
            g.setFont(legendFont(theme_, 19.0f, 700, 0.08f));
            g.drawText(names[i],
                       juce::Rectangle<float>(x - 62.0f, 274.0f, 124.0f, 23.0f),
                       juce::Justification::centred,
                       false);
            g.setColour(hw.sageInkSoft);
            g.setFont(legendFont(theme_, 17.0f, 600, 0.03f));
            g.drawText(tops[i],
                       juce::Rectangle<float>(x - 62.0f, 297.0f, 124.0f, 20.0f),
                       juce::Justification::centred,
                       false);
            g.drawText(bottoms[i],
                       juce::Rectangle<float>(x - 62.0f, 382.0f, 124.0f, 20.0f),
                       juce::Justification::centred,
                       false);
        }
        // The stereo lever's middle position, beside its nut.
        g.drawText("PING",
                   juce::Rectangle<float>(leverX[0] + 27.0f, leverY - 10.0f, 60.0f, 20.0f),
                   juce::Justification::centredLeft,
                   false);
    }

    // ----- what changes with the values -------------------------------------------------------
    void paintLive(juce::Graphics& g) const
    {
        const auto& hw = theme_.hardware;
        // HEADS's eight positions, the chosen one bold.
        const juce::Point<float> heads(bigX[0], bigRow);
        const char* modes[] = {"1",
                               "2",
                               "3",
                               "4",
                               "1\xc2\xb7"
                               "3",
                               "2\xc2\xb7"
                               "4",
                               "1\xc2\xb7"
                               "3\xc2\xb7"
                               "4",
                               "ALL"};
        g.setColour(hw.sageInk);
        for (int i = 0; i < 8; ++i)
        {
            const float a = -135.0f + 270.0f * static_cast<float>(i) / 7.0f;
            juce::Path p;
            p.startNewSubPath(polar(heads, 39.0f, a));
            p.lineTo(polar(heads, 45.0f, a));
            g.strokePath(
                p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont(legendFont(theme_, 17.0f, i == headsPosition ? 700 : 600, 0.0f));
            g.drawText(juce::String::fromUTF8(modes[i]),
                       juce::Rectangle<float>(58.0f, 20.0f).withCentre(polar(heads, 57.0f, a)),
                       juce::Justification::centred,
                       false);
        }
        // The values under the knobs.
        g.setColour(hw.sageInkSoft);
        g.setFont(legendFont(theme_, 19.0f, 600, 0.03f));
        for (int i = 0; i < 4 && bigValue; ++i)
            g.drawText(bigValue(i),
                       juce::Rectangle<float>(bigX[static_cast<size_t>(i)] - 80.0f, 424.0f, 160.0f, 23.0f),
                       juce::Justification::centred,
                       false);
        g.setFont(legendFont(theme_, 18.0f, 600, 0.03f));
        for (int i = 0; i < 9 && smallValue; ++i)
            g.drawText(smallValue(i),
                       juce::Rectangle<float>(smallX(i) - 62.0f, 539.0f, 124.0f, 22.0f),
                       juce::Justification::centred,
                       false);
    }

    const Theme& theme_;
    bool synced_ = false;
    juce::Image cache_;
    float cacheScale_ = 0.0f;
};

// ----- the card ----------------------------------------------------------------------------

class TapeEchoCard final : public ModuleCard
{
public:
    TapeEchoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), face_(theme), meter_(theme), transport_(theme),
          lamp_(theme)
    {
        addAndMakeVisible(face_);
        face_.addAndMakeVisible(transport_);
        face_.addAndMakeVisible(meter_);
        face_.addAndMakeVisible(lamp_);
        transport_.setBounds(opening);
        meter_.setBounds(meterBounds);
        lamp_.setBounds(lampArea);

        // The knobs bound to their parameters, each a rendered pointer over the
        // body the face draws, their own labels hidden (the face prints them).
        const auto bind = [this](int index, juce::Point<float> centre, float skirt, float image) -> Knob&
        {
            auto& k = addKnob(index);
            prepare(k, centre, skirt, image);
            if (auto* label = labelFor(k))
                label->setVisible(false);
            return k;
        };
        heads_ = &bind(te::heads, {bigX[0], bigRow}, bigSkirt, bigImage);
        heads_->setInterval(1.0);
        heads_->format = [](double v)
        {
            return juce::String::fromUTF8(
                te::headModes[static_cast<size_t>(juce::jlimit(0, 7, static_cast<int>(std::lround(v))))]
                    .name);
        };
        feedback_ = &bind(te::feedback, {bigX[2], bigRow}, bigSkirt, bigImage);
        mix_ = &bind(te::mix, {bigX[3], bigRow}, bigSkirt, bigImage);
        const int small[] = {te::bass,
                             te::treble,
                             te::drive,
                             te::wow,
                             te::flutter,
                             te::age,
                             te::spread,
                             te::spring,
                             te::output};
        for (int i = 0; i < 9; ++i)
            small_[static_cast<size_t>(i)] = &bind(small[i], {smallX(i), smallRow}, smallSkirt, smallImage);

        // REPEAT RATE: the motor's time, or with SYNC on, head 1's note value.
        rate_ = std::make_unique<Knob>(theme_);
        prepare(*rate_, {bigX[1], bigRow}, bigSkirt, bigImage);
        rate_->setTitle("Repeat rate");
        rate_->onDragStart = [this] { beginGesture("Repeat rate"); };
        rate_->onDragEnd = [this] { endGesture(); };
        rate_->onValueChanged = [this](double v)
        {
            if (updatingRate_)
                return;
            setParam(synced() ? te::sync : te::time, static_cast<float>(v));
            face_.repaint();
        };
        face_.addAndMakeVisible(*rate_);

        stereo_ = std::make_unique<Lever>(theme_, "Stereo", std::vector<juce::String>{"Pan", "Ping", "L/R"});
        sync_ = std::make_unique<Lever>(theme_, "Sync", std::vector<juce::String>{"On", "Off"});
        dry_ = std::make_unique<Lever>(theme_, "Dry", std::vector<juce::String>{"On", "Kill"});
        for (size_t i = 0; i < 3; ++i)
        {
            auto* lever = std::array<Lever*, 3>{stereo_.get(), sync_.get(), dry_.get()}[i];
            // The rendered lever's image (119 x 178.5 at full size), its nut at the top square's centre.
            const float w = 119.0f * leverScale, h = 178.5f * leverScale;
            lever->setBounds(
                juce::Rectangle<float>(leverX[i] - w * 0.5f, leverY - w * 0.5f, w, h).toNearestInt());
            face_.addAndMakeVisible(*lever);
        }
        stereo_->onChoose = [this](int p) { leverParam(te::stereo, "Stereo", static_cast<float>(p)); };
        sync_->onChoose = [this](int p)
        {
            // On: the last note value chosen (an eighth the first time); off: free.
            leverParam(te::sync, "Sync", p == 0 ? static_cast<float>(lastNote_) : 0.0f);
        };
        dry_->onChoose = [this](int p) { leverParam(te::killDry, "Kill dry", p == 1 ? 1.0f : 0.0f); };

        face_.bigValue = [this](int i)
        {
            Knob* knobs[] = {heads_, rate_.get(), feedback_, mix_};
            if (i == 0)
            {
                // The heads that play, as the selector's label shows them.
                const auto mask = te::headMask(param(te::heads));
                juce::StringArray on;
                for (int k = 0; k < te::headCount; ++k)
                    if (((mask >> k) & 1) != 0)
                        on.add(juce::String(k + 1));
                return on.joinIntoString(" + ");
            }
            return knobs[i]->accessibleValueText();
        };
        face_.smallValue = [this](int i) { return small_[static_cast<size_t>(i)]->accessibleValueText(); };
        paramsChanged();
    }

    int preferredWidth() const override { return juce::roundToInt(tf::width) + 2 * theme_.spacing.sm; }
    int preferredBodyHeight() const override { return juce::roundToInt(tf::height) + 2 * theme_.spacing.xs; }
    int preferredWidthAt(int bodyHeight) const override
    {
        return tf::widthForHeight(bodyHeight - 2 * theme_.spacing.xs) + 2 * theme_.spacing.sm;
    }

private:
    bool synced() const { return plugins::sync::quarters(param(te::sync)) > 0.0; }

    void prepare(Knob& k, juce::Point<float> centre, float skirt, float image)
    {
        k.setStyle(Knob::Style::rendered);
        k.setRingSize(juce::roundToInt(skirt));
        k.setShowValue(false);
        k.setRenderedPointer(part("knob-pointer.png"));
        const int side = juce::roundToInt(image);
        k.setBounds(juce::roundToInt(centre.x) - side / 2, juce::roundToInt(centre.y) - side / 2, side, side);
        // The face, not the card, holds the knob, so it scales with the face.
        face_.addAndMakeVisible(k);
        k.onValueChanged = [this, prior = k.onValueChanged](double v)
        {
            if (prior)
                prior(v);
            face_.repaint();
        };
    }

    void leverParam(int index, const juce::String& name, float value)
    {
        beginGesture(name);
        setParam(index, value);
        endGesture();
        paramsChanged();
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const float s =
            tf::fitScale(static_cast<float>(body.getWidth()), static_cast<float>(body.getHeight()));
        face_.setVisible(s > 0.0f);
        if (s <= 0.0f)
            return;
        face_.setBounds(0, 0, juce::roundToInt(tf::width), juce::roundToInt(tf::height));
        const float x =
            static_cast<float>(body.getX()) + (static_cast<float>(body.getWidth()) - tf::width * s) * 0.5f;
        face_.setTransform(juce::AffineTransform::scale(s).translated(x, static_cast<float>(body.getY())));
    }

    // The motor, the loop gain and the mode, from the parameters and the frames.
    void update()
    {
        const double bpm = tempoBpm();
        targetSeconds_ = te::headOneSeconds(param(te::time), param(te::sync), bpm);
        const Frame f = historySize() > 0 ? historyAt(historySize() - 1) : Frame{};
        motorSeconds_ = f.extra2 > 0.0f ? static_cast<double>(f.extra2) : targetSeconds_;
        mask_ = te::headMask(param(te::heads));
        loopGain_ = te::loopGain(param(te::feedback),
                                 param(te::bass),
                                 param(te::treble),
                                 param(te::age),
                                 targetSeconds_,
                                 sampleRate());
    }

    // The level on the tape `units` past the record head (what a head there plays now).
    float levelAt(double units) const
    {
        const int size = historySize();
        const int frame = size - 1 - static_cast<int>(units * motorSeconds_ / frameSeconds);
        return frame >= 0 && frame < size ? historyAt(frame).extra : 0.0f;
    }

    void paramsChanged() override
    {
        const bool sync = synced();
        if (sync)
            lastNote_ = plugins::sync::choiceOf(param(te::sync));
        // REPEAT RATE follows the mode: the motor's time, or the note values.
        {
            const juce::ScopedValueSetter<bool> guard(updatingRate_, true);
            if (sync)
            {
                rate_->setLogarithmic(false);
                rate_->setRange(1.0, plugins::sync::choiceCount - 1, 8.0);
                rate_->setInterval(1.0);
                rate_->format = [](double v)
                {
                    return juce::String(plugins::sync::noteValues[static_cast<size_t>(plugins::sync::choiceOf(
                                                                      static_cast<float>(v)))]
                                            .name);
                };
                rate_->setValue(param(te::sync), juce::dontSendNotification);
            }
            else
            {
                const auto& i = info(te::time);
                rate_->setInterval(0.0);
                rate_->setRange(i.minimum, i.maximum, i.defaultValue);
                rate_->setLogarithmic(i.logarithmic);
                rate_->format = [](double v)
                { return juce::String(static_cast<int>(std::lround(v))) + " ms"; };
                rate_->setValue(param(te::time), juce::dontSendNotification);
            }
        }
        face_.setSynced(sync);
        stereo_->setPosition(static_cast<int>(std::lround(param(te::stereo))));
        sync_->setPosition(sync ? 0 : 1);
        dry_->setPosition(param(te::killDry) >= 0.5f ? 1 : 0);
        face_.headsPosition = static_cast<int>(std::lround(param(te::heads)));
        update();
        face_.repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        update();
        const double seconds = elapsedMs * 0.001;
        const bool on = isOn();
        // The meter reads what the record head writes.
        needle_.advance(on ? levelAt(0.0) : 0.0f, seconds);
        meter_.setReading(needle_.level);
        // Each neon as bright as the level passing its head; a head that does not
        // play shows it dimmer.
        bool changed = false;
        for (size_t i = 0; i < transport_.neon.size(); ++i)
        {
            const double units = i == 0 ? 0.0 : te::headSpacing[i - 1];
            const bool plays = i == 0 || ((mask_ >> (i - 1)) & 1) != 0;
            const float target =
                on ? tf::neonBrightness(levelAt(units)) * (plays ? 1.0f : tf::inactiveNeon) : 0.0f;
            const float shown = tf::neonFollow(transport_.neon[i], target, seconds);
            changed = changed || std::abs(shown - transport_.neon[i]) > 0.002f;
            transport_.neon[i] = shown;
        }
        // The rollers turn with the tape while the machine runs.
        if (on && !theme_.motion.reduceMotion)
        {
            transport_.pinchAngle = static_cast<float>(std::fmod(
                transport_.pinchAngle + tf::rollerSpeed(motorSeconds_, tf::pinchRadius) * seconds, 2.0 * pi));
            transport_.tensionAngle = static_cast<float>(std::fmod(
                transport_.tensionAngle - tf::rollerSpeed(motorSeconds_, tf::tensionRadius) * seconds,
                2.0 * pi));
            changed = true;
        }
        if (changed)
            transport_.repaint();
        lamp_.set(loopGain_, on, elapsedMs);
    }

    juce::String headerStatus() const override
    {
        return isOn() && loopGain_ >= 1.0f ? juce::String("Self-oscillating") : juce::String();
    }
    juce::Colour headerStatusColour() const override { return theme_.colours.meterHot; }

    Face face_;
    Meter meter_;
    Transport transport_;
    GainLamp lamp_;
    Knob* heads_ = nullptr;
    Knob* feedback_ = nullptr;
    Knob* mix_ = nullptr;
    std::array<Knob*, 9> small_{};
    std::unique_ptr<Knob> rate_;
    std::unique_ptr<Lever> stereo_, sync_, dry_;
    tf::VuNeedle needle_;
    double motorSeconds_ = 0.18, targetSeconds_ = 0.18;
    float loopGain_ = 0.0f;
    std::uint8_t mask_ = 0;
    int lastNote_ = 8; // an eighth note
    bool updatingRate_ = false;
};
} // namespace

std::unique_ptr<ModuleCard>
createTapeEchoCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<TapeEchoCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
