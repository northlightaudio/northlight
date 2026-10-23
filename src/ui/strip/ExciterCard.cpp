// The exciter's card (screens/plugins.md 7.2a): the harmonic spectrum (the input
// in grey and what the exciter adds in the hue, from the processor's taps, with
// the harmonics' crossover and the bass frequency as handles to drag), the
// excitation history (the input's level and the added level over the last three
// seconds, so the dynamics and transients can be seen working) with the added
// level as a readout, and the controls in four groups: harmonics, low end,
// dynamics and output.

#include "plugins/Exciter.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace northlight::ui::strip
{

namespace px = plugins::exciter;

namespace
{
constexpr double framesPerSecond = 1.0 / engine::ProcessorFeedback::frameSeconds;
constexpr float spectrumTopDb = 0.0f, spectrumBottomDb = -96.0f;
// Tilted so a typical mix (falling about 3 dB an octave) reads level, as the EQ's analyser.
constexpr double tiltDbPerOctave = 3.0;
constexpr float historySeconds = 3.0f, historyTopDb = 0.0f, historyBottomDb = -72.0f;
// The added level's readout: the power ratio over this much of the history.
constexpr float readoutSeconds = 0.3f;

// What is added against the input (dB) over the newest `seconds` of the card's
// history; nothing while the input is silent.
std::optional<float> addedRelativeDb(const ModuleCard& card, float seconds)
{
    const int size = card.historySize();
    const int n = std::min(size, static_cast<int>(seconds * framesPerSecond));
    double input = 0.0, added = 0.0;
    for (int i = size - n; i < size; ++i)
    {
        const auto& f = card.historyAt(i);
        input += static_cast<double>(f.extra) * f.extra;
        added += static_cast<double>(f.extra2) * f.extra2;
    }
    if (n == 0 || input < n * 1.0e-8) // below -80 dB: nothing to compare with
        return std::nullopt;
    return static_cast<float>(10.0 * std::log10(std::max(added / input, 1.0e-12)));
}

juce::String relativeText(std::optional<float> db)
{
    if (!db)
        return juce::String::fromUTF8("\xe2\x80\x93");
    if (*db <= -96.0f)
        return juce::String::fromUTF8("-\xe2\x88\x9e dB");
    return juce::String(*db, 1) + " dB";
}

// A bipolar percentage named by its side ("Dark 40", "Flat").
std::function<juce::String(double)> sided(const char* below, const char* zero, const char* above)
{
    return [below, zero, above](double v)
    {
        const int n = static_cast<int>(std::lround(std::abs(v)));
        if (n == 0)
            return juce::String(zero);
        return juce::String(v < 0.0 ? below : above) + " " + juce::String(n);
    };
}

// ----- the harmonic spectrum ----------------------------------------------------------------

class HarmonicSpectrum final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HarmonicSpectrum(ModuleCard& card) : card_(card)
    {
        setName("Harmonic spectrum");
        setTooltip("Drag: frequencies");
        window_.resize(fftSize);
        for (int i = 0; i < fftSize; ++i)
            window_[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi *
                                                                     static_cast<float>(i) / (fftSize - 1));
        fftData_.assign(static_cast<size_t>(fftSize) * 2, 0.0f);
        samples_.assign(static_cast<size_t>(fftSize), 0.0f);
    }

    ~HarmonicSpectrum() override
    {
        if (auto* fb = card_.liveFeedback())
            fb->setTapsEnabled(false);
    }

    // Each tick: the analyser runs about 30 times a second while the card shows.
    void update(double elapsedMs)
    {
        auto* fb = card_.liveFeedback();
        if (fb == nullptr)
            return;
        if (!fb->tapsEnabled())
            fb->setTapsEnabled(true);
        sinceAnalysisMs_ += elapsedMs;
        if (sinceAnalysisMs_ < 30.0)
            return;
        analyse(0, spectrum_[0], sinceAnalysisMs_);
        analyse(1, spectrum_[1], sinceAnalysisMs_);
        sinceAnalysisMs_ = 0.0;
        repaint();
    }

    // Whether the input has shown in the analyser (for the empty state and tests).
    bool showingSignal() const noexcept
    {
        const auto& input = spectrum_[0];
        return std::any_of(input.begin(), input.end(), [](float v) { return v > spectrumBottomDb + 6.0f; });
    }

    void resized() override
    {
        for (auto& s : spectrum_)
            s.clear();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        drawWell(g, theme, getLocalBounds().toFloat());
        const auto p = plot();
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(getLocalBounds().reduced(1));
            paintGrid(g, p);
            g.reduceClipRegion(p.toNearestInt());

            // The two regions the sections work in, faintly in the hue while they add.
            const float xCross = hzToX(card_.param(px::frequency), p);
            const float xBass = hzToX(card_.param(px::bassFrequency), p);
            if (card_.param(px::amount) > 0.0f)
            {
                g.setColour(hue.withAlpha(0.07f));
                g.fillRect(p.withLeft(xCross));
            }
            if (card_.param(px::bass) > 0.0f)
            {
                g.setColour(hue.withAlpha(0.07f));
                g.fillRect(p.withRight(xBass));
            }

            const int columns = static_cast<int>(p.getWidth());
            const auto colX = [&](int col) { return p.getX() + static_cast<float>(col) + 0.5f; };
            const auto yOf = [&](float db) { return dbToY(db, spectrumTopDb, spectrumBottomDb, p); };
            const auto outline = [&](const std::vector<float>& s)
            {
                juce::Path line;
                for (int col = 0; col < columns; ++col)
                {
                    // A light three-column average smooths the outline.
                    const float v =
                        (s[static_cast<size_t>(std::max(0, col - 1))] + s[static_cast<size_t>(col)] * 2.0f +
                         s[static_cast<size_t>(std::min(columns - 1, col + 1))]) *
                        0.25f;
                    if (col == 0)
                        line.startNewSubPath(colX(col), yOf(v));
                    else
                        line.lineTo(colX(col), yOf(v));
                }
                return line;
            };
            const auto filled = [&](const juce::Path& line)
            {
                juce::Path area(line);
                area.lineTo(colX(columns - 1), p.getBottom());
                area.lineTo(colX(0), p.getBottom());
                area.closeSubPath();
                return area;
            };
            const bool ready = columns > 1 && static_cast<int>(spectrum_[0].size()) == columns &&
                               static_cast<int>(spectrum_[1].size()) == columns;
            if (ready)
            {
                const auto input = outline(spectrum_[0]);
                g.setColour(c.textTertiary.withAlpha(0.16f));
                g.fillPath(filled(input));
                g.setColour(c.textSecondary.withAlpha(0.45f));
                g.strokePath(input, juce::PathStrokeType(1.0f));
                // What is added, over the input: where it rises out of the grey is new energy.
                const auto added = outline(spectrum_[1]);
                g.setGradientFill(juce::ColourGradient(
                    hue.withAlpha(0.45f), 0.0f, p.getY(), hue.withAlpha(0.08f), 0.0f, p.getBottom(), false));
                g.fillPath(filled(added));
                g.setColour(hue.withAlpha(0.95f));
                g.strokePath(
                    added,
                    juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (!ready || !showingSignal())
            {
                g.setFont(theme.caption());
                g.setColour(c.textTertiary);
                g.drawText("Play something to see what is added",
                           p.withSizeKeepingCentre(p.getWidth(), 16.0f),
                           juce::Justification::centred);
            }

            paintLegend(g, p);
            paintHandle(g, p, Handle::bass, xBass);
            paintHandle(g, p, Handle::crossover, xCross);
        }
        drawWellFrame(g, theme, getLocalBounds().toFloat());
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto h = handleAt(static_cast<float>(e.x));
        setMouseCursor(h != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                                         : juce::MouseCursor::NormalCursor);
        if (h != hovered_)
        {
            hovered_ = h;
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hovered_ = Handle::none;
        repaint();
    }
    // A press away from the handles moves the nearer one there.
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = nearest(static_cast<float>(e.x));
        card_.beginGesture(card_.info(param(dragging_)).name);
        setFromX(static_cast<float>(e.x));
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_ != Handle::none)
            setFromX(static_cast<float>(e.x));
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_ == Handle::none)
            return;
        dragging_ = Handle::none;
        card_.endGesture();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const int index = param(nearest(static_cast<float>(e.x)));
        card_.beginGesture(card_.info(index).name);
        card_.setParam(index, card_.info(index).defaultValue);
        card_.endGesture();
        repaint();
    }

private:
    enum class Handle
    {
        none,
        crossover,
        bass
    };
    static constexpr int fftOrder = 13; // 8192 points: 6 Hz bins at 48 kHz, for the bass
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr float handleReach = 6.0f;
    static constexpr float handleSize = 9.0f;

    static int param(Handle h) { return h == Handle::bass ? px::bassFrequency : px::frequency; }

    // Room at the bottom for the frequency labels.
    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(14.0f);
    }

    float handleX(Handle h) const { return hzToX(card_.param(param(h)), plot()); }

    Handle handleAt(float x) const
    {
        const float dc = std::abs(x - handleX(Handle::crossover)), db = std::abs(x - handleX(Handle::bass));
        if (std::min(dc, db) > handleReach)
            return Handle::none;
        return dc <= db ? Handle::crossover : Handle::bass;
    }

    Handle nearest(float x) const
    {
        return std::abs(x - handleX(Handle::crossover)) <= std::abs(x - handleX(Handle::bass))
                   ? Handle::crossover
                   : Handle::bass;
    }

    void setFromX(float x)
    {
        card_.setParam(param(dragging_), static_cast<float>(xToHz(x, plot())));
        repaint();
    }

    void paintGrid(juce::Graphics& g, juce::Rectangle<float> p) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
        for (const double hz :
             {30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0, 20000.0})
        {
            const float x = hzToX(hz, p);
            const bool major = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
            g.setColour(major ? c.gridBar : c.gridBeat);
            g.drawVerticalLine(static_cast<int>(x), p.getY(), p.getBottom());
            if (hz == 30.0 || hz == 20000.0)
                continue;
            g.setColour(c.textTertiary);
            const auto label = hz >= 1000.0 ? juce::String(static_cast<int>(hz / 1000.0)) + "k"
                                            : juce::String(static_cast<int>(hz));
            g.drawText(label,
                       juce::Rectangle<float>(x - 16.0f, p.getBottom() + 1.0f, 32.0f, 12.0f),
                       juce::Justification::centred);
        }
        for (float db = spectrumTopDb - 24.0f; db > spectrumBottomDb; db -= 24.0f)
        {
            g.setColour(c.gridBeat);
            g.drawHorizontalLine(
                static_cast<int>(dbToY(db, spectrumTopDb, spectrumBottomDb, p)), p.getX(), p.getRight());
        }
    }

    // Top left: a swatch and a name for each trace.
    void paintLegend(juce::Graphics& g, juce::Rectangle<float> p) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        g.setFont(theme.caption());
        float x = p.getX() + 8.0f;
        const float y = p.getY() + 6.0f;
        for (const auto& [name, colour] : {std::pair{juce::String("Input"), c.textSecondary.withAlpha(0.6f)},
                                           std::pair{juce::String("Added"), card_.colour()}})
        {
            g.setColour(colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(x, y + 3.0f, 8.0f, 8.0f), 2.0f);
            x += 12.0f;
            const float w =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.caption(), name));
            g.setColour(c.textSecondary);
            g.drawText(name, juce::Rectangle<float>(x, y, w + 1.0f, 14.0f), juce::Justification::centredLeft);
            x += w + 12.0f;
        }
    }

    // A handle: a line down the plot, the shared handle disc near the bottom and
    // the frequency beside it (on the side with room).
    void paintHandle(juce::Graphics& g, juce::Rectangle<float> p, Handle h, float x) const
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto hue = card_.colour();
        const bool hot = hovered_ == h || dragging_ == h;
        const bool active = card_.param(h == Handle::bass ? px::bass : px::amount) > 0.0f;
        if (active)
        {
            g.setColour(hot ? c.textPrimary.withAlpha(0.85f) : hue.withAlpha(0.7f));
            g.fillRect(
                juce::Rectangle<float>(x - (hot ? 0.75f : 0.5f), p.getY(), hot ? 1.5f : 1.0f, p.getHeight()));
        }
        else
        {
            const float dashes[] = {3.0f, 3.0f};
            g.setColour(hot ? c.textPrimary.withAlpha(0.7f) : c.textTertiary.withAlpha(0.6f));
            g.drawDashedLine(juce::Line<float>(x, p.getY(), x, p.getBottom()), dashes, 2, hot ? 1.5f : 1.0f);
        }
        const juce::Point<float> centre(x, p.getBottom() - 12.0f);
        const auto disc = juce::Rectangle<float>(handleSize, handleSize).withCentre(centre);
        g.setColour(active ? c.textPrimary : c.textSecondary);
        g.fillEllipse(disc);
        g.setColour(hue);
        g.drawEllipse(disc.expanded(0.75f), hot ? 2.0f : 1.5f);

        g.setFont(theme.value());
        const auto text = formatHz(card_.param(param(h)));
        const float w =
            static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.value(), text)) + 2.0f;
        const bool right = x + 8.0f + w <= p.getRight() - 4.0f;
        const auto box = right ? juce::Rectangle<float>(x + 9.0f, centre.y - 7.0f, w, 14.0f)
                               : juce::Rectangle<float>(x - 9.0f - w, centre.y - 7.0f, w, 14.0f);
        // On a plate of the well, so the traces never run through the figures.
        g.setColour(theme.material.meterWell.withAlpha(0.8f));
        g.fillRoundedRectangle(box.expanded(3.0f, 1.0f), theme.radius.sm);
        g.setColour(hot ? c.textPrimary : c.textSecondary);
        g.drawText(text, box, right ? juce::Justification::centredLeft : juce::Justification::centredRight);
    }

    void analyse(int tap, std::vector<float>& smoothed, double elapsedMs)
    {
        auto* fb = card_.liveFeedback();
        const auto p = plot();
        const int columns = std::max(1, static_cast<int>(p.getWidth()));
        if (static_cast<int>(smoothed.size()) != columns)
            smoothed.assign(static_cast<size_t>(columns), spectrumBottomDb);
        const float fall = static_cast<float>(elapsedMs * 0.001 * 40.0); // dB per second
        const auto position = fb->tapPosition(tap);
        const bool fresh = position != lastTap_[static_cast<size_t>(tap)];
        lastTap_[static_cast<size_t>(tap)] = position;
        if (!fresh || !fb->readTap(tap, samples_.data(), fftSize))
        {
            // Nothing new (the device stopped): let it fall away.
            for (auto& v : smoothed)
                v = std::max(spectrumBottomDb, v - fall);
            return;
        }
        std::fill(fftData_.begin(), fftData_.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftData_[static_cast<size_t>(i)] =
                samples_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)];
        fft_.performFrequencyOnlyForwardTransform(fftData_.data(), true);
        const double binHz = fb->sampleRate() / fftSize;
        const double reference = fftSize / 4.0; // a full-scale sine through the Hann window
        const auto binDb = [&](int bin)
        {
            const double m = fftData_[static_cast<size_t>(juce::jlimit(0, fftSize / 2, bin))] / reference;
            return 20.0 * std::log10(std::max(1.0e-9, m));
        };
        for (int col = 0; col < columns; ++col)
        {
            const double f0 = xToHz(p.getX() + static_cast<float>(col), p);
            const double f1 = xToHz(p.getX() + static_cast<float>(col) + 1.0f, p);
            const double b0 = f0 / binHz, b1 = f1 / binHz;
            double db;
            if (b1 - b0 < 1.0)
            {
                // Narrower than a bin: interpolate between the neighbours.
                const double centre = 0.5 * (b0 + b1);
                const int lo = static_cast<int>(std::floor(centre));
                const double t = centre - lo;
                db = binDb(lo) * (1.0 - t) + binDb(lo + 1) * t;
            }
            else
            {
                db = -200.0;
                for (int bin = static_cast<int>(std::ceil(b0)); bin <= static_cast<int>(b1); ++bin)
                    db = std::max(db, binDb(bin));
            }
            db += tiltDbPerOctave * std::log2(0.5 * (f0 + f1) / 1000.0);
            const float v =
                static_cast<float>(juce::jlimit<double>(spectrumBottomDb, spectrumTopDb + 12.0, db));
            auto& s = smoothed[static_cast<size_t>(col)];
            s = v > s ? s + (v - s) * 0.6f : std::max(v, s - fall);
        }
    }

    ModuleCard& card_;
    Handle hovered_ = Handle::none, dragging_ = Handle::none;
    juce::dsp::FFT fft_{fftOrder};
    std::vector<float> window_, fftData_, samples_;
    std::array<std::vector<float>, 2> spectrum_; // per column, dBFS (smoothed); 0 = input, 1 = added
    std::array<std::uint64_t, 2> lastTap_{};
    double sinceAnalysisMs_ = 0.0;
};

// ----- the excitation history ---------------------------------------------------------------

class ExcitationHistory final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ExcitationHistory(ModuleCard& card) : card_(card) {}

    void refresh()
    {
        relative_ = addedRelativeDb(card_, readoutSeconds);
        repaint();
    }

    std::optional<float> relativeDb() const noexcept { return relative_; }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto area = getLocalBounds().toFloat();
        drawWell(g, theme, area);
        const auto p = area.reduced(1.0f);
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(p.toNearestInt());
            g.setFont(
                theme.font(Theme::Face::mono, theme.fonts.size.caption - 1.0f, theme.fonts.weight.regular));
            for (float db = historyTopDb - 12.0f; db > historyBottomDb; db -= 12.0f)
            {
                const float y = dbToY(db, historyTopDb, historyBottomDb, p);
                g.setColour(c.gridBar);
                g.drawHorizontalLine(static_cast<int>(y), p.getX(), p.getRight());
                g.setColour(c.textTertiary);
                g.drawText(juce::String(static_cast<int>(db)),
                           juce::Rectangle<float>(p.getRight() - 26.0f, y - 12.0f, 24.0f, 11.0f),
                           juce::Justification::centredRight);
            }
            collect(p);
            if (!input_.empty())
            {
                const auto x = [&](int col) { return p.getX() + static_cast<float>(col) + 0.5f; };
                const auto yOf = [&](float linear)
                { return std::min(p.getBottom(), dbToY(toDb(linear), historyTopDb, historyBottomDb, p)); };
                const auto trace = [&](const std::vector<float>& v)
                {
                    juce::Path line;
                    for (int col = 0; col < static_cast<int>(v.size()); ++col)
                    {
                        const float y = yOf(v[static_cast<size_t>(col)]);
                        if (col == 0)
                            line.startNewSubPath(x(col), y);
                        else
                            line.lineTo(x(col), y);
                    }
                    return line;
                };
                const auto filled = [&](juce::Path line, int columns)
                {
                    line.lineTo(x(columns - 1), p.getBottom());
                    line.lineTo(x(0), p.getBottom());
                    line.closeSubPath();
                    return line;
                };
                const int columns = static_cast<int>(input_.size());
                g.setColour(c.textTertiary.withAlpha(0.32f));
                g.fillPath(filled(trace(input_), columns));
                const auto added = trace(added_);
                const auto hue = card_.colour();
                g.setColour(hue.withAlpha(0.42f));
                g.fillPath(filled(added, columns));
                g.setColour(hue.withAlpha(0.95f));
                g.strokePath(
                    added,
                    juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // ADDED and the level, top left.
            auto readout =
                juce::Rectangle<float>(p.getX() + 6.0f, p.getY() + 4.0f, p.getWidth() - 40.0f, 14.0f);
            g.setFont(theme.captionCaps());
            g.setColour(c.textTertiary);
            const juce::String caption = "ADDED";
            const float captionW =
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(theme.captionCaps(), caption));
            g.drawText(caption, readout.removeFromLeft(captionW), juce::Justification::centredLeft);
            readout.removeFromLeft(6.0f);
            g.setFont(theme.value());
            g.setColour(relative_ ? card_.colour() : c.textTertiary);
            g.drawText(relativeText(relative_), readout, juce::Justification::centredLeft);
        }
        drawWellFrame(g, theme, area);
    }

private:
    // Per column: the largest input and added RMS of its frames, columns counted
    // from the start of the history so they keep their frames as it scrolls.
    void collect(juce::Rectangle<float> p)
    {
        const int columns = std::max(1, static_cast<int>(p.getWidth()));
        input_.assign(static_cast<size_t>(columns), 0.0f);
        added_.assign(static_cast<size_t>(columns), 0.0f);
        const auto perColumn =
            static_cast<std::uint64_t>(std::max(1L, std::lround(historySeconds * framesPerSecond / columns)));
        const int size = card_.historySize();
        const auto total = card_.historyTotal();
        if (total == 0)
        {
            input_.clear();
            return;
        }
        const auto first = total - static_cast<std::uint64_t>(size);
        const auto newestBucket = (total - 1) / perColumn;
        for (int col = 0; col < columns; ++col)
        {
            const auto fromRight = static_cast<std::uint64_t>(columns - 1 - col);
            if (fromRight > newestBucket)
                continue;
            const auto bucket = newestBucket - fromRight;
            for (auto a = bucket * perColumn; a < (bucket + 1) * perColumn && a < total; ++a)
            {
                if (a < first)
                    continue;
                const auto& f = card_.historyAt(static_cast<int>(a - first));
                const auto i = static_cast<size_t>(col);
                input_[i] = std::max(input_[i], f.extra);
                added_[i] = std::max(added_[i], f.extra2);
            }
        }
    }

    ModuleCard& card_;
    std::optional<float> relative_;
    std::vector<float> input_, added_; // per column, reused
};

// ----- the card -----------------------------------------------------------------------------

class ExciterCard final : public ModuleCard
{
public:
    ExciterCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), spectrum_(*this), history_(*this)
    {
        addAndMakeVisible(spectrum_);
        addAndMakeVisible(history_);

        // Harmonics.
        character_ = std::make_unique<RotarySelector>(theme_);
        {
            std::vector<juce::String> labels;
            for (const auto& choice : info(px::character).choices)
                labels.push_back(juce::String::fromUTF8(choice.c_str()));
            character_->setPositions(labels, labels, "Character");
        }
        character_->setHue(colour());
        character_->setDefault(static_cast<int>(std::lround(info(px::character).defaultValue)));
        character_->onChange = [this](int value) { setParam(px::character, static_cast<float>(value)); };
        character_->onGestureBegin = [this] { beginGesture("Character"); };
        character_->onGestureEnd = [this] { endGesture(); };
        addAndMakeVisible(*character_);
        characterLabel_ = &addCaption("Character");

        amount_ = &addKnob(px::amount);
        drive_ = &addKnob(px::drive);
        frequency_ = &addKnob(px::frequency, "Frequency");
        focus_ = &addKnob(px::focus);
        focus_->format = sided("Dark", "Flat", "Edge");

        // Low end.
        bass_ = &addKnob(px::bass, "Amount");
        bassFrequency_ = &addKnob(px::bassFrequency, "Frequency");
        for (auto* k : {amount_, bass_})
            k->setRingSize(bigRing);

        // Dynamics.
        dynamics_ = &addKnob(px::dynamics);
        dynamics_->format = sided("Fill", "Static", "Push");
        transients_ = &addKnob(px::transients);

        // Output.
        stereo_ = &addKnob(px::stereo, "Width");
        mix_ = &addKnob(px::mix);
        output_ = &addKnob(px::output);
        autoGain_ = &addToggle(px::autoGain, "Match");
        autoGain_->setTooltip("Auto gain");
        autoGainLabel_ = &addCaption("Auto gain");
        paramsChanged();
    }

    int preferredWidth() const override
    {
        return selectorWidth() + 2 * bigCellWidth + 10 * knobCellWidth() + 3 * (2 * theme_.spacing.sm + 1) +
               2 * theme_.spacing.sm;
    }
    int preferredBodyHeight() const override { return 300; }

    void paint(juce::Graphics& g) override
    {
        ModuleCard::paint(g);
        if (isCollapsed())
            return;
        const auto& c = theme_.colours;
        // Scale marks around the large knobs.
        for (auto* knob : {amount_, bass_})
        {
            const auto b = knob->getBounds().toFloat();
            const juce::Point<float> centre(b.getCentreX(), b.getY() + bigRing * 0.5f);
            for (int i = 0; i <= 10; ++i)
            {
                const float angle =
                    juce::MathConstants<float>::pi * (1.25f + 1.5f * static_cast<float>(i) / 10.0f);
                const auto p = centre.getPointOnCircumference(bigRing * 0.5f + 4.0f, angle);
                const float d = i % 5 == 0 ? 3.0f : 2.0f;
                g.setColour(i % 5 == 0 ? c.textSecondary : c.textTertiary);
                g.fillEllipse(juce::Rectangle<float>(d, d).withCentre(p));
            }
        }
        // The groups: a caption over each, faint rules between them.
        g.setFont(theme_.captionCaps());
        g.setColour(c.textTertiary);
        for (const auto& group : groups_)
            g.drawText(group.text, group.area, juce::Justification::centredLeft);
        g.setColour(c.lineSubtle);
        for (const int x : dividers_)
            g.drawVerticalLine(x, static_cast<float>(dividerTop_), static_cast<float>(dividerBottom_));
        // Auto gain's live trim, where a knob shows its value.
        if (!autoGainValue_.isEmpty())
        {
            g.setFont(theme_.value());
            g.setColour(param(px::autoGain) >= 0.5f ? c.textSecondary : c.textTertiary);
            g.drawText(autoGainValue_, autoGainValueArea_, juce::Justification::centredTop);
        }
    }

private:
    static constexpr int bigRing = 46;
    static constexpr int bigCellWidth = 84;

    struct Group
    {
        juce::String text;
        juce::Rectangle<int> area;
    };

    int selectorWidth() const noexcept { return character_ != nullptr ? character_->preferredWidth() : 104; }

    juce::Label& addCaption(const juce::String& text)
    {
        auto& label = captions_.emplace_back(std::make_unique<juce::Label>());
        label->setText(text, juce::dontSendNotification);
        label->setFont(theme_.caption());
        label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        label->setJustificationType(juce::Justification::centred);
        label->setMinimumHorizontalScale(0.75f);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
        return *label;
    }

    void layoutBody(juce::Rectangle<int> body) override
    {
        const int valueH = static_cast<int>(theme_.fonts.size.value) + 4;
        const int labelH = static_cast<int>(theme_.fonts.size.caption) + 4;
        const int bigCellH = bigRing + 6 + valueH + labelH;
        const int selectorH = character_->preferredHeight() + labelH;
        const int controlsH = std::max({bigCellH, selectorH, knobCellHeight()});
        const int captionH = theme_.spacing.lg;
        auto display =
            body.removeFromTop(std::max(96, body.getHeight() - controlsH - captionH - theme_.spacing.sm));
        body.removeFromTop(theme_.spacing.sm);
        history_.setBounds(display.removeFromRight(std::min(historyWidth, display.getWidth() / 3)));
        display.removeFromRight(theme_.spacing.sm);
        spectrum_.setBounds(display);

        auto captionRow = body.removeFromTop(captionH);
        auto controls = body.removeFromTop(controlsH);
        dividers_.clear();
        groups_.clear();
        dividerTop_ = captionRow.getY() + 2;
        dividerBottom_ = controls.getBottom() - 4;
        int groupStart = controls.getX();
        const auto endGroup = [&](const juce::String& caption, bool rule)
        {
            groups_.push_back({caption, captionRow.withX(groupStart).withRight(controls.getX())});
            if (!rule)
                return;
            controls.removeFromLeft(theme_.spacing.sm);
            dividers_.push_back(controls.getX());
            controls.removeFromLeft(theme_.spacing.sm + 1);
            groupStart = controls.getX();
        };
        // A large knob's cell: the ring, its value and its label, on the row's bottom line.
        const auto bigCell = [&](Knob* knob)
        {
            auto cell = controls.removeFromLeft(bigCellWidth);
            if (auto* label = labelFor(*knob))
                label->setBounds(cell.removeFromBottom(labelH));
            knob->setBounds(cell.removeFromBottom(bigCellH - labelH - 6));
        };
        const auto knobs = [&](std::initializer_list<Knob*> list)
        {
            const int width = static_cast<int>(list.size()) * knobCellWidth();
            layoutKnobs(controls.removeFromLeft(width).removeFromBottom(knobCellHeight()),
                        static_cast<int>(list.size()),
                        list);
        };

        {
            auto cell = controls.removeFromLeft(selectorWidth());
            characterLabel_->setBounds(cell.removeFromBottom(labelH));
            character_->setBounds(cell.removeFromBottom(character_->preferredHeight()));
        }
        bigCell(amount_);
        knobs({drive_, frequency_, focus_});
        endGroup("HARMONICS", true);
        bigCell(bass_);
        knobs({bassFrequency_});
        endGroup("LOW END", true);
        knobs({dynamics_, transients_});
        endGroup("DYNAMICS", true);
        knobs({stereo_, mix_, output_});
        {
            // Auto gain: the pill where a knob's ring sits, its live trim where the
            // value sits, its name on the labels' line.
            auto cell = controls.removeFromLeft(knobCellWidth()).removeFromBottom(knobCellHeight());
            autoGainLabel_->setBounds(cell.removeFromBottom(labelH));
            const int ring = theme_.sizes.knob;
            auto ringArea = cell.removeFromTop(ring);
            autoGain_->setBounds(ringArea.withSizeKeepingCentre(knobCellWidth() - theme_.spacing.sm, 20));
            autoGainValueArea_ = cell;
        }
        endGroup("OUTPUT", false);
    }

    void paramsChanged() override
    {
        character_->setSelected(static_cast<int>(std::lround(param(px::character))),
                                juce::dontSendNotification);
        updateAutoGainValue();
        spectrum_.repaint();
    }

    void framesArrived(double elapsedMs) override
    {
        spectrum_.update(elapsedMs);
        history_.refresh();
        updateAutoGainValue();
    }

    void updateAutoGainValue()
    {
        juce::String text = "Off";
        if (param(px::autoGain) >= 0.5f)
        {
            const float trim = historySize() > 0 ? historyAt(historySize() - 1).reduction : 0.0f;
            text = trim >= 0.05f ? "-" + juce::String(trim, 1) + " dB" : juce::String("0.0 dB");
        }
        if (text != autoGainValue_)
        {
            autoGainValue_ = text;
            repaint(autoGainValueArea_);
        }
    }

    juce::String headerStatus() const override
    {
        const auto db = history_.relativeDb();
        return isOn() && db && *db > -96.0f ? relativeText(db) : juce::String();
    }
    juce::Colour headerStatusColour() const override { return colour(); }

    HarmonicSpectrum spectrum_;
    ExcitationHistory history_;
    static constexpr int historyWidth = 208;
    std::unique_ptr<RotarySelector> character_;
    std::vector<std::unique_ptr<juce::Label>> captions_;
    juce::Label* characterLabel_ = nullptr;
    juce::Label* autoGainLabel_ = nullptr;
    Knob *amount_ = nullptr, *drive_ = nullptr, *frequency_ = nullptr, *focus_ = nullptr;
    Knob *bass_ = nullptr, *bassFrequency_ = nullptr;
    Knob *dynamics_ = nullptr, *transients_ = nullptr;
    Knob *stereo_ = nullptr, *mix_ = nullptr, *output_ = nullptr;
    PillToggle* autoGain_ = nullptr;
    juce::String autoGainValue_;
    juce::Rectangle<int> autoGainValueArea_;
    std::vector<Group> groups_;
    std::vector<int> dividers_;
    int dividerTop_ = 0, dividerBottom_ = 0;
};
} // namespace

std::unique_ptr<ModuleCard>
createExciterCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<ExciterCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
