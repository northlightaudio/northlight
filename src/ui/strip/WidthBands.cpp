#include "ui/strip/WidthBands.h"

#include "plugins/Widener.h"
#include "ui/strip/CardParts.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/TapSpectrum.h"
#include "ui/strip/WidthScale.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace northlight::ui::strip::widthbands
{

namespace pw = plugins::widener;

WidthBands::WidthBands(ModuleCard& card)
    : card_(card), window_(hannWindow(fftSize)), in_(fftSize), out_(fftSize)
{
    setTooltip("Drag: widths, crossovers");
    mid_.fill(0.0f);
    side_.fill(0.0f);
}

void WidthBands::update(const StereoTaps& taps, bool fresh)
{
    // Drawn from the last analysis: nothing new, nothing to repaint.
    if (!fresh)
        return;
    analyse(taps);
    repaint();
}

void WidthBands::paint(juce::Graphics& g)
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    const auto hue = card_.colour();
    const auto bounds = getLocalBounds().toFloat();
    drawWell(g, theme, bounds);
    const auto p = plot();
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(bounds.reduced(1.0f).toNearestInt());
        drawGrid(g, p);

        // Mono below: the region under it shaded, its edge a line.
        const float monoHz = card_.param(pw::monoBelow);
        const bool monoOn = monoHz > pw::monoOffHz + 0.5f;
        const float monoX = monoOn ? hzToX(monoHz, p) : p.getX();

        // The bands.
        const float x1 = hzToX(card_.param(pw::lowSplit), p);
        const float x2 = hzToX(card_.param(pw::highSplit), p);
        const std::array<float, 4> edges{p.getX(), x1, x2, p.getRight()};
        const float guard = 1.0f - card_.reductionDb() * 0.01f; // the frames' reduction is the guard's %
        for (int b = 0; b < 3; ++b)
        {
            const float left = edges[static_cast<size_t>(b)], right = edges[static_cast<size_t>(b) + 1];
            const float w = card_.param(pw::lowWidth + b);
            const float y = widthToY(w, p);
            const bool active = drag_ == static_cast<Drag>(b) || (drag_ == Drag::none && hover_ == b);
            const auto block = juce::Rectangle<float>::leftTopRightBottom(left, y, right, p.getBottom());
            g.setColour(hue.withAlpha(active ? 0.22f : 0.14f));
            g.fillRect(block);
            g.setColour(hue.withAlpha(active ? 1.0f : 0.85f));
            g.fillRect(juce::Rectangle<float>(left, y - 1.0f, right - left, 2.0f));
            if (card_.param(pw::guard) >= 0.5f && guard < 0.99f && w > 0.5f)
            {
                const float dashes[] = {4.0f, 3.0f};
                const float gy = widthToY(w * guard, p);
                g.setColour(c.warning.withAlpha(0.9f));
                g.drawDashedLine({left + 2.0f, gy, right - 2.0f, gy}, dashes, 2, 1.2f);
            }
        }
        if (monoOn)
        {
            g.setColour(c.canvas.withAlpha(0.8f)); // no side here: the bands' fill goes
            g.fillRect(juce::Rectangle<float>::leftTopRightBottom(p.getX(), p.getY(), monoX, p.getBottom()));
            if (monoX - p.getX() > 44.0f)
            {
                g.setFont(theme.captionCaps());
                g.setColour(c.textSecondary);
                g.drawText("MONO",
                           juce::Rectangle<float>(
                               p.getX() + 6.0f, p.getBottom() - 18.0f, monoX - p.getX() - 8.0f, 12.0f),
                           juce::Justification::centredLeft);
            }
        }

        // The measured width.
        drawMeasured(g, p);

        // The crossovers and Mono below's edge.
        for (const auto& [x, which] : {std::pair{x1, Drag::lowSplit}, std::pair{x2, Drag::highSplit}})
        {
            const bool active = drag_ == which || (drag_ == Drag::none && hoverLine_ == which);
            g.setColour(active ? c.textPrimary : c.lineStrong);
            g.fillRect(juce::Rectangle<float>::leftTopRightBottom(x - (active ? 0.75f : 0.5f),
                                                                  bounds.getY() + 1.0f,
                                                                  x + (active ? 0.75f : 0.5f),
                                                                  p.getBottom()));
        }
        {
            const bool active =
                drag_ == Drag::monoBelow || (drag_ == Drag::none && hoverLine_ == Drag::monoBelow);
            const float dashes[] = {3.0f, 3.0f};
            g.setColour(active ? c.textPrimary : c.textSecondary);
            if (monoOn)
                g.drawDashedLine(
                    {monoX + 0.5f, p.getY(), monoX + 0.5f, p.getBottom()}, dashes, 2, active ? 1.5f : 1.0f);
        }
    }
    drawWellFrame(g, theme, bounds);

    // The handles: each crossover's at the foot of its line, Mono below's
    // at the top, each band's on its width at the band's middle.
    const auto& names = bandNames();
    const float x1 = hzToX(card_.param(pw::lowSplit), p);
    const float x2 = hzToX(card_.param(pw::highSplit), p);
    const std::array<float, 4> edges{p.getX(), x1, x2, p.getRight()};
    for (int b = 0; b < 3; ++b)
    {
        const float left = edges[static_cast<size_t>(b)], right = edges[static_cast<size_t>(b) + 1];
        const float w = card_.param(pw::lowWidth + b);
        const float y = widthToY(w, p);
        const bool active = drag_ == static_cast<Drag>(b) || (drag_ == Drag::none && hover_ == b);
        drawHandle(g, theme, hue, {(left + right) * 0.5f, y}, active);
        // The band's name and width at its top, where there is room.
        const auto label = juce::Rectangle<float>(
            left + 6.0f, p.getY() - static_cast<float>(captionRow) + 3.0f, right - left - 12.0f, 13.0f);
        const auto value = w < 0.5f ? juce::String("Mono") : juce::String(juce::roundToInt(w)) + "%";
        const float nameW = static_cast<float>(
            juce::GlyphArrangement::getStringWidthInt(theme.captionCaps(), names[static_cast<size_t>(b)]));
        const float valueW =
            static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), value));
        if (label.getWidth() >= nameW + 6.0f + valueW)
        {
            auto row = label;
            g.setFont(theme.captionCaps());
            g.setColour(c.textTertiary);
            g.drawText(
                names[static_cast<size_t>(b)], row.removeFromLeft(nameW), juce::Justification::centredLeft);
            row.removeFromLeft(6.0f);
            g.setFont(theme.value());
            g.setColour(active ? c.textPrimary : c.textSecondary);
            g.drawText(value, row, juce::Justification::centredLeft);
        }
        else if (label.getWidth() >= valueW)
        {
            g.setFont(theme.value());
            g.setColour(active ? c.textPrimary : c.textSecondary);
            g.drawText(value, label, juce::Justification::centredLeft);
        }
    }
    for (const auto& [x, which] : {std::pair{x1, Drag::lowSplit}, std::pair{x2, Drag::highSplit}})
    {
        const bool active = drag_ == which || (drag_ == Drag::none && hoverLine_ == which);
        drawGrip(g, {x, p.getBottom() - 10.0f}, active);
        if (active)
            drawFrequency(
                g, x, which == Drag::lowSplit ? card_.param(pw::lowSplit) : card_.param(pw::highSplit));
    }
    {
        const float monoHz = card_.param(pw::monoBelow);
        const bool monoOn = monoHz > pw::monoOffHz + 0.5f;
        const float monoX = monoOn ? hzToX(monoHz, p) : p.getX() + 5.0f;
        const bool active =
            drag_ == Drag::monoBelow || (drag_ == Drag::none && hoverLine_ == Drag::monoBelow);
        drawGrip(g, {monoX, monoHandleY(p)}, active, !monoOn); // off: parked at the left
        if (active)
            drawFrequency(g, monoX, monoHz);
    }
}

void WidthBands::mouseMove(const juce::MouseEvent& e)
{
    const auto line = lineAt(e.position);
    const int band = line == Drag::none ? bandAt(e.position.x) : -1;
    if (line != hoverLine_ || band != hover_)
    {
        hoverLine_ = line;
        hover_ = band;
        repaint();
    }
    setMouseCursor(line != Drag::none ? juce::MouseCursor::LeftRightResizeCursor
                                      : juce::MouseCursor::UpDownResizeCursor);
}

void WidthBands::mouseExit(const juce::MouseEvent&)
{
    hover_ = -1;
    hoverLine_ = Drag::none;
    repaint();
}

void WidthBands::mouseDown(const juce::MouseEvent& e)
{
    drag_ = lineAt(e.position);
    if (drag_ == Drag::none)
        drag_ = static_cast<Drag>(bandAt(e.position.x));
    card_.beginGesture(card_.info(paramOf(drag_)).name);
    mouseDrag(e);
}

void WidthBands::mouseDrag(const juce::MouseEvent& e)
{
    const auto p = plot();
    switch (drag_)
    {
        case Drag::low:
        case Drag::mid:
        case Drag::high:
        {
            float w = widthscale::settableAt(e.position.y, p);
            // Settle on 100% (as it came) and on mono near them.
            if (std::abs(w - 100.0f) < 3.0f)
                w = 100.0f;
            else if (w < 3.0f)
                w = 0.0f;
            card_.setParam(paramOf(drag_), std::round(w));
            break;
        }
        case Drag::lowSplit:
        case Drag::highSplit:
            card_.setParam(paramOf(drag_), static_cast<float>(xToHz(e.position.x, p)));
            break;
        case Drag::monoBelow:
        {
            const double hz = xToHz(e.position.x, p);
            card_.setParam(pw::monoBelow, hz < 22.0 ? pw::monoOffHz : static_cast<float>(hz));
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void WidthBands::mouseUp(const juce::MouseEvent&)
{
    if (drag_ != Drag::none)
        card_.endGesture();
    drag_ = Drag::none;
    repaint();
}

void WidthBands::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto which = lineAt(e.position);
    if (which == Drag::none)
        which = static_cast<Drag>(bandAt(e.position.x));
    const int index = paramOf(which);
    card_.beginGesture(card_.info(index).name);
    card_.setParam(index, card_.info(index).defaultValue);
    card_.endGesture();
}

const std::array<juce::String, 3>& WidthBands::bandNames()
{
    static const std::array<juce::String, 3> names{"LOW", "MID", "HIGH"};
    return names;
}

int WidthBands::paramOf(Drag d)
{
    switch (d)
    {
        case Drag::low:
            return pw::lowWidth;
        case Drag::mid:
            return pw::midWidth;
        case Drag::high:
            return pw::highWidth;
        case Drag::lowSplit:
            return pw::lowSplit;
        case Drag::highSplit:
            return pw::highSplit;
        case Drag::monoBelow:
            return pw::monoBelow;
        case Drag::none:
            break;
    }
    return pw::midWidth;
}

juce::Rectangle<float> WidthBands::plot() const
{
    // Room at the left for the width scale and below for the frequencies.
    return getLocalBounds()
        .toFloat()
        .reduced(1.0f)
        .withTrimmedLeft(34.0f)
        .withTrimmedBottom(15.0f)
        .withTrimmedTop(static_cast<float>(captionRow));
}

int WidthBands::bandAt(float x) const
{
    const auto p = plot();
    if (x < hzToX(card_.param(pw::lowSplit), p))
        return 0;
    return x < hzToX(card_.param(pw::highSplit), p) ? 1 : 2;
}

WidthBands::Drag WidthBands::lineAt(juce::Point<float> at) const
{
    const auto p = plot();
    const float monoHz = card_.param(pw::monoBelow);
    const float monoX = monoHz > pw::monoOffHz + 0.5f ? hzToX(monoHz, p) : p.getX() + 5.0f;
    const float x1 = hzToX(card_.param(pw::lowSplit), p), x2 = hzToX(card_.param(pw::highSplit), p);
    // The nearest line within reach; Mono below's only near its handle when off.
    Drag best = Drag::none;
    float bestDistance = 6.0f;
    const auto consider = [&](float x, Drag which)
    {
        const float d = std::abs(at.x - x);
        if (d <= bestDistance)
        {
            bestDistance = d;
            best = which;
        }
    };
    consider(x1, Drag::lowSplit);
    consider(x2, Drag::highSplit);
    if (monoHz > pw::monoOffHz + 0.5f || std::abs(at.y - monoHandleY(p)) < 10.0f)
        consider(monoX, Drag::monoBelow);
    return best;
}

void WidthBands::drawGrid(juce::Graphics& g, juce::Rectangle<float> p) const
{
    const auto& theme = card_.theme();
    const auto& c = theme.colours;
    static constexpr std::array lines{50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
    drawFrequencyGrid(g, theme, p, lines, 2.0f); // the width labels share its font
    for (const float w : {50.0f, 100.0f, 150.0f})
    {
        const float y = widthToY(w, p);
        g.setColour(w == 100.0f ? c.lineStrong : c.gridBeat);
        g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
    }
    // The headroom strip over 200%: what the output measures past what a band
    // can be set to, compressed.
    const float edge = widthscale::settableTop(p);
    g.setColour(c.textPrimary.withAlpha(0.035f));
    g.fillRect(juce::Rectangle<float>::leftTopRightBottom(p.getX(), p.getY(), p.getRight(), edge));
    const float dashes[] = {3.0f, 3.0f};
    g.setColour(c.gridBar);
    g.drawDashedLine({p.getX(), edge, p.getRight(), edge}, dashes, 2, 1.0f);
    g.setColour(c.textTertiary);
    for (const float w : {0.0f, 100.0f, 200.0f})
    {
        const float y = juce::jlimit(p.getY() + 6.0f, p.getBottom() - 6.0f, widthToY(w, p));
        g.drawText(juce::String(static_cast<int>(w)) + "%",
                   juce::Rectangle<float>(p.getX() - 34.0f, y - 6.0f, 30.0f, 12.0f),
                   juce::Justification::centredRight);
    }
    g.setFont(theme.captionCaps());
    const float overW = juce::GlyphArrangement::getStringWidth(theme.captionCaps(), "OVER");
    const float lineH = static_cast<float>(theme.lineHeight(theme.fonts.size.caption));
    g.drawText("OVER",
               juce::Rectangle<float>(p.getX() - 4.0f - overW - 2.0f, p.getY() - 3.0f, overW + 2.0f, lineH),
               juce::Justification::centredRight);
}

void WidthBands::drawGrip(juce::Graphics& g, juce::Point<float> at, bool active, bool dim) const
{
    const auto& c = card_.theme().colours;
    const auto r = juce::Rectangle<float>(7.0f, 16.0f).withCentre(at);
    if (active)
    {
        g.setColour(c.textPrimary.withAlpha(0.18f));
        g.fillRoundedRectangle(r.expanded(3.0f), 5.0f);
    }
    g.setColour(active ? c.textPrimary : dim ? c.textTertiary : c.textSecondary);
    g.fillRoundedRectangle(r, 3.5f);
    g.setColour(c.surface0);
    g.drawHorizontalLine(static_cast<int>(at.y) - 2, r.getX() + 2.0f, r.getRight() - 2.0f);
    g.drawHorizontalLine(static_cast<int>(at.y) + 1, r.getX() + 2.0f, r.getRight() - 2.0f);
}

void WidthBands::drawFrequency(juce::Graphics& g, float x, float hz) const
{
    const auto& theme = card_.theme();
    const auto p = plot();
    const auto text = hz <= pw::monoOffHz + 0.5f ? juce::String("Off") : hzText(hz);
    const float w =
        static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), text)) + 10.0f;
    auto box = juce::Rectangle<float>(w, 16.0f).withCentre({x, monoHandleY(p) + 20.0f});
    box = box.withX(juce::jlimit(p.getX() + 2.0f, p.getRight() - w - 2.0f, box.getX()));
    g.setColour(theme.colours.surface3);
    g.fillRoundedRectangle(box, theme.radius.sm);
    g.setColour(theme.colours.textPrimary);
    g.setFont(theme.value());
    g.drawText(text, box, juce::Justification::centred);
}

void WidthBands::drawMeasured(juce::Graphics& g, juce::Rectangle<float> p) const
{
    juce::Path line;
    bool drawing = false;
    for (int b = 0; b < spectrumBands; ++b)
    {
        const auto w = widthOf(b);
        if (!w)
        {
            drawing = false;
            continue;
        }
        const float x = hzToX(centreHz(b), p);
        const float y = widthToY(*w, p);
        if (!drawing)
            line.startNewSubPath(x, y);
        else
            line.lineTo(x, y);
        drawing = true;
    }
    g.setColour(card_.theme().colours.textPrimary.withAlpha(0.6f));
    g.strokePath(line,
                 juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawOverMarkers(g, p);
}

void WidthBands::drawOverMarkers(juce::Graphics& g, juce::Rectangle<float> p) const
{
    const auto& theme = card_.theme();
    const auto font = theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.medium);
    float lastRight = p.getX() - 1.0f;
    int b = 0;
    while (b < spectrumBands)
    {
        const auto w = widthOf(b);
        if (!w || *w <= widthscale::settable)
        {
            ++b;
            continue;
        }
        int peak = b;
        float most = *w;
        for (; b < spectrumBands; ++b)
        {
            const auto v = widthOf(b);
            if (!v || *v <= widthscale::settable)
                break;
            if (*v > most)
            {
                most = *v;
                peak = b;
            }
        }
        // A run that only grazes the edge gets no marker (it would flicker).
        if (most < widthscale::settable * 1.05f)
            continue;
        const float x = hzToX(centreHz(peak), p);
        const float top = p.getY() + 2.0f;
        juce::Path arrow;
        arrow.addTriangle(x - 4.0f, top + 6.0f, x + 4.0f, top + 6.0f, x, top);
        g.setColour(theme.colours.warning);
        g.fillPath(arrow);
        const auto text = most >= widthscale::ceiling
                              ? ">" + juce::String(juce::roundToInt(widthscale::ceiling)) + "%"
                              : juce::String(juce::roundToInt(most)) + "%";
        const float textW = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text)) + 2.0f;
        const auto box = juce::Rectangle<float>(x + 6.0f, top - 2.0f, textW, 11.0f);
        if (box.getX() > lastRight && box.getRight() < p.getRight() - 2.0f)
        {
            g.setFont(font);
            g.drawText(text, box, juce::Justification::centredLeft);
            lastRight = box.getRight() + 4.0f;
        }
    }
}

double WidthBands::centreHz(int band)
{
    return minHz * std::pow(maxHz / minHz, (static_cast<double>(band) + 0.5) / spectrumBands);
}

std::optional<float> WidthBands::widthOf(int band) const
{
    float m = 0.0f, s = 0.0f;
    for (int b = std::max(0, band - 1); b <= std::min(spectrumBands - 1, band + 1); ++b)
    {
        const float weight = b == band ? 2.0f : 1.0f;
        m += weight * mid_[static_cast<size_t>(b)];
        s += weight * side_[static_cast<size_t>(b)];
    }
    const float own = mid_[static_cast<size_t>(band)] + side_[static_cast<size_t>(band)];
    if (own < quietPower_ || own < loudest_ * 1.0e-6f)
        return std::nullopt;
    return 100.0f * std::sqrt(s / std::max(m, 1.0e-20f));
}

void WidthBands::analyse(const StereoTaps& taps)
{
    const auto& l = taps.left();
    const auto& r = taps.right();
    const double rate = card_.sampleRate();
    // Left and right go through one complex FFT: left real, right imaginary.
    for (int i = 0; i < fftSize; ++i)
        in_[static_cast<size_t>(i)] = {l[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)],
                                       r[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)]};
    fft_.perform(in_.data(), out_.data(), false);
    std::array<float, spectrumBands> mid{}, side{};
    std::array<int, spectrumBands> bins{};
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const double hz = k * rate / fftSize;
        if (hz < minHz || hz >= maxHz)
            continue;
        // Untangle the two real spectra from the one complex one.
        const auto z = out_[static_cast<size_t>(k)];
        const auto zc = std::conj(out_[static_cast<size_t>(fftSize - k)]);
        const auto left = (z + zc) * 0.5f;
        const auto right = (z - zc) * juce::dsp::Complex<float>(0.0f, -0.5f);
        const auto m = (left + right) * 0.5f, s = (left - right) * 0.5f;
        const auto band =
            static_cast<size_t>(juce::jlimit(0, spectrumBands - 1, static_cast<int>(bandOf(hz))));
        mid[band] += std::norm(m);
        side[band] += std::norm(s);
        ++bins[band];
    }
    // A band too narrow for a bin takes its neighbour's.
    for (int b = 0; b < spectrumBands; ++b)
        if (bins[static_cast<size_t>(b)] == 0)
            for (int d = 1; d < spectrumBands; ++d)
            {
                const int n = b + d < spectrumBands && bins[static_cast<size_t>(b + d)] > 0 ? b + d : -1;
                if (n >= 0)
                {
                    mid[static_cast<size_t>(b)] =
                        mid[static_cast<size_t>(n)] / static_cast<float>(bins[static_cast<size_t>(n)]);
                    side[static_cast<size_t>(b)] =
                        side[static_cast<size_t>(n)] / static_cast<float>(bins[static_cast<size_t>(n)]);
                    bins[static_cast<size_t>(b)] = -1; // borrowed, already per bin
                    break;
                }
            }
    loudest_ = 0.0f;
    for (size_t b = 0; b < static_cast<size_t>(spectrumBands); ++b)
    {
        // About 150 ms of averaging at the display's rate: steady enough to read.
        const float n = bins[b] > 0 ? static_cast<float>(bins[b]) : 1.0f;
        mid_[b] += (mid[b] / n - mid_[b]) * 0.12f;
        side_[b] += (side[b] / n - side_[b]) * 0.12f;
        loudest_ = std::max(loudest_, mid_[b] + side_[b]);
    }
}

} // namespace northlight::ui::strip::widthbands
