// The Sampler's editor (screens/plugins.md 7.4f). The sample is the centrepiece:
// its waveform in a well, the part that plays (start to end) bright and the
// rest dimmed, the loop shaded with its crossfade, and a line for every voice
// sounding. Start and end are handles on the top edge, the loop's on the
// bottom edge. Under it a keyboard shows the key range and the root; click a
// key for the root, drag the range's ends. Then four trays of knobs: pitch,
// playback, the envelope and the filter, each display in a well. Drop an audio
// file anywhere on the waveform to load it.

#include "ui/strip/SamplerEditor.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "engine/Gain.h"
#include "plugins/Sampler.h"
#include "ui/MediaFiles.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/SynthParts.h"

#include <cmath>
#include <functional>
#include <vector>

namespace northlight::ui::strip
{

namespace
{
namespace sp = plugins::sampler;

struct Hues
{
    juce::Colour sample, loop, keys, envelope, filter;
    explicit Hues(const Theme& t)
        : sample(paletteColour(t, "Lichen")), loop(paletteColour(t, "Gorse")),
          keys(paletteColour(t, "Sea grass")), envelope(paletteColour(t, "Overcast")),
          filter(paletteColour(t, "Loch"))
    {
    }
};

juce::String noteText(double v)
{
    return juce::String(core::noteName(static_cast<int>(std::lround(v))));
}

// What the displays need from the card beyond its parameters.
struct SampleSource
{
    std::function<std::shared_ptr<const sp::Sample>()> sample;
    std::function<std::string()> error;
    std::function<std::string()> path;
    std::function<void(const juce::File&)> load;
};

// ----- the waveform -------------------------------------------------------------------------------

class WaveDisplay final : public juce::Component,
                          public juce::SettableTooltipClient,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget
{
public:
    WaveDisplay(ModuleCard& card, const Hues& hues, SampleSource& source)
        : card_(card), hues_(hues), source_(source)
    {
        setTooltip("Drag the top handles for the start and end, the bottom ones for the loop. "
                   "Drop an audio file here to load it.");
    }

    std::vector<sp::VoiceInfo> voices;

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto sample = source_.sample ? source_.sample() : nullptr;
        const auto area = waveArea();
        if (sample == nullptr || sample->frames() == 0)
        {
            const auto path = source_.path ? source_.path() : std::string{};
            g.setFont(theme.body());
            if (path.empty())
            {
                g.setColour(c.textTertiary);
                g.drawText("Drop an audio file here, or Load one", b, juce::Justification::centred);
            }
            else
            {
                g.setColour(c.warning);
                const auto name = juce::File(juce::String::fromUTF8(path.c_str())).getFileName();
                const auto error =
                    source_.error ? juce::String::fromUTF8(source_.error().c_str()) : juce::String();
                g.drawText(name + juce::String::fromUTF8(" \xe2\x80\x94 ") +
                               (error.isEmpty() ? "missing" : error),
                           b.reduced(12.0f, 0.0f),
                           juce::Justification::centred,
                           true);
            }
        }
        else
        {
            buildColumns(*sample, static_cast<int>(area.getWidth()));
            const float s = card_.param(sp::start), e = card_.param(sp::end);
            const float from = std::min(s, e), to = std::max(s, e);
            const bool loop = card_.param(sp::loop) >= 0.5f && card_.param(sp::oneShot) < 0.5f;
            const float ls = juce::jlimit(from, to, card_.param(sp::loopStart));
            const float le = juce::jlimit(from, to, card_.param(sp::loopEnd));
            // The loop, and the crossfade before its end.
            if (loop && le > ls)
            {
                const float x0 = xFor(std::min(ls, le)), x1 = xFor(std::max(ls, le));
                g.setColour(hues_.loop.withAlpha(0.12f));
                g.fillRect(juce::Rectangle<float>(x0, area.getY(), x1 - x0, area.getHeight()));
                const double fadeFrames =
                    std::min({static_cast<double>(card_.param(sp::loopFade)) * 0.001 * sample->sampleRate,
                              (le - ls) * 0.5 * static_cast<double>(sample->frames()),
                              static_cast<double>(ls) * static_cast<double>(sample->frames())});
                const float fadeW =
                    static_cast<float>(fadeFrames / static_cast<double>(sample->frames())) * area.getWidth();
                if (fadeW > 1.0f)
                {
                    g.setGradientFill(juce::ColourGradient::horizontal(
                        hues_.loop.withAlpha(0.0f), x1 - fadeW, hues_.loop.withAlpha(0.3f), x1));
                    g.fillRect(juce::Rectangle<float>(x1 - fadeW, area.getY(), fadeW, area.getHeight()));
                }
            }
            // The waveform: what plays in the hue, the rest dimmed.
            const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
            for (size_t i = 0; i < columns_.size(); ++i)
            {
                const float x = area.getX() + static_cast<float>(i);
                const float f = (static_cast<float>(i) + 0.5f) / area.getWidth();
                const bool inside = f >= from && f <= to;
                g.setColour(inside ? hues_.sample.withAlpha(theme.track.waveformAlpha)
                                   : hues_.sample.withAlpha(theme.track.waveformMutedAlpha));
                const float top = mid - columns_[i].second * half, bottom = mid - columns_[i].first * half;
                g.fillRect(x, top, 1.0f, std::max(1.0f, bottom - top));
            }
            g.setColour(c.gridBeat);
            g.fillRect(area.getX(), mid, area.getWidth(), 1.0f);
            // The voices sounding.
            for (const auto& v : voices)
            {
                const float x =
                    area.getX() +
                    static_cast<float>(v.position / static_cast<double>(sample->frames())) * area.getWidth();
                g.setColour(c.textPrimary.withAlpha(v.releasing ? 0.35f : 0.8f));
                g.fillRect(x, area.getY(), 1.0f, area.getHeight());
            }
            // The markers: start and end at the top, the loop's at the bottom.
            const auto marker =
                [&](float f, bool topEdge, juce::Colour hue, const juce::String& label, int which)
            {
                const float x = xFor(f);
                g.setColour(hue.withAlpha(0.8f));
                g.fillRect(x - 0.5f, area.getY(), 1.0f, area.getHeight());
                const float y = topEdge ? area.getY() : area.getBottom();
                drawHandle(g, theme, hue, {x, y}, dragging_ == which || hover_ == which);
                g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.semibold));
                g.setColour(hue);
                const auto text = juce::Rectangle<float>(
                    x + 7.0f, topEdge ? b.getY() + 1.0f : b.getBottom() - 13.0f, 40.0f, 12.0f);
                g.drawText(label, text, juce::Justification::centredLeft);
            };
            marker(s, true, c.textPrimary, "S", sp::start);
            marker(e, true, c.textPrimary, "E", sp::end);
            if (loop)
            {
                marker(card_.param(sp::loopStart), false, hues_.loop, "L", sp::loopStart);
                marker(card_.param(sp::loopEnd), false, hues_.loop, "L", sp::loopEnd);
            }
            // The readout: the part that plays, in seconds.
            g.setFont(theme.value());
            g.setColour(c.textSecondary);
            const double seconds = static_cast<double>(to - from) * static_cast<double>(sample->frames()) /
                                   std::max(1.0, sample->sampleRate);
            g.drawText(juce::String(seconds, seconds < 10.0 ? 2 : 1) + " s",
                       b.reduced(8.0f, 1.0f).removeFromTop(12.0f),
                       juce::Justification::centredTop);
        }
        drawWellFrame(g, theme, b);
        if (dropping_)
        {
            g.setColour(c.dropTarget);
            g.drawRoundedRectangle(b.reduced(1.0f), theme.radius.sm, 2.0f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int near = markerAt(e.position);
        if (near != hover_)
        {
            hover_ = near;
            setMouseCursor(near >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                     : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        hover_ = -1;
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging_ = markerAt(e.position);
        if (dragging_ < 0)
            return;
        card_.beginGesture(card_.info(dragging_).name);
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_ < 0)
            return;
        const auto area = waveArea();
        float f = juce::jlimit(0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        // Each point stays on its side of its partner.
        constexpr float minimum = 0.001f;
        if (dragging_ == sp::start)
            f = std::min(f, card_.param(sp::end) - minimum);
        else if (dragging_ == sp::end)
            f = std::max(f, card_.param(sp::start) + minimum);
        else if (dragging_ == sp::loopStart)
            f = std::min(f, card_.param(sp::loopEnd) - minimum);
        else if (dragging_ == sp::loopEnd)
            f = std::max(f, card_.param(sp::loopStart) + minimum);
        card_.setParam(dragging_, juce::jlimit(0.0f, 1.0f, f));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_ >= 0)
            card_.endGesture();
        dragging_ = -1;
        repaint();
    }

    // Explorer drops.
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        return files.size() == 1 && isAudioFile(juce::File(files[0]));
    }
    void fileDragEnter(const juce::StringArray&, int, int) override { setDropping(true); }
    void fileDragExit(const juce::StringArray&) override { setDropping(false); }
    void filesDropped(const juce::StringArray& files, int, int) override
    {
        setDropping(false);
        if (files.size() == 1 && source_.load)
            source_.load(juce::File(files[0]));
    }
    // Drops from the browser's Files tab.
    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return isAudioFile(fileFromDragDescription(details.description));
    }
    void itemDragEnter(const SourceDetails&) override { setDropping(true); }
    void itemDragExit(const SourceDetails&) override { setDropping(false); }
    void itemDropped(const SourceDetails& details) override
    {
        setDropping(false);
        if (const auto file = fileFromDragDescription(details.description);
            file.existsAsFile() && source_.load)
            source_.load(file);
    }

private:
    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().reduced(10.0f, 14.0f); }
    float xFor(float fraction) const
    {
        const auto a = waveArea();
        return a.getX() + juce::jlimit(0.0f, 1.0f, fraction) * a.getWidth();
    }

    // The marker under a point: start and end in the top half, the loop's in
    // the bottom half (when the loop is on), the nearest within reach.
    int markerAt(juce::Point<float> p) const
    {
        const auto sample = source_.sample ? source_.sample() : nullptr;
        if (sample == nullptr)
            return -1;
        const bool top = p.y < getHeight() * 0.5f;
        std::vector<int> candidates;
        if (top)
            candidates = {sp::start, sp::end};
        else if (card_.param(sp::loop) >= 0.5f && card_.param(sp::oneShot) < 0.5f)
            candidates = {sp::loopStart, sp::loopEnd};
        int best = -1;
        float bestDistance = 8.0f;
        for (const int m : candidates)
            if (const float d = std::abs(xFor(card_.param(m)) - p.x); d <= bestDistance)
            {
                best = m;
                bestDistance = d;
            }
        return best;
    }

    void setDropping(bool on)
    {
        dropping_ = on;
        repaint();
    }

    // Min and max per pixel column (both channels), rebuilt when the sample or width changes.
    void buildColumns(const sp::Sample& sample, int width)
    {
        if (&sample == builtFor_ && width == static_cast<int>(columns_.size()))
            return;
        builtFor_ = &sample;
        columns_.assign(static_cast<size_t>(std::max(0, width)), {0.0f, 0.0f});
        const auto frames = sample.frames();
        if (width <= 0 || frames <= 0)
            return;
        for (int x = 0; x < width; ++x)
        {
            const auto from = frames * x / width, to = std::max(from + 1, frames * (x + 1) / width);
            float lo = 0.0f, hi = 0.0f;
            // Long columns are sampled, not read whole: a few thousand reads a column at most.
            const auto step = std::max<std::int64_t>(1, (to - from) / 2048);
            for (auto i = from; i < to; i += step)
            {
                const float l = sample.left[static_cast<size_t>(i)];
                const float r = sample.stereo() ? sample.right[static_cast<size_t>(i)] : l;
                lo = std::min({lo, l, r});
                hi = std::max({hi, l, r});
            }
            columns_[static_cast<size_t>(x)] = {juce::jlimit(-1.0f, 1.0f, lo), juce::jlimit(-1.0f, 1.0f, hi)};
        }
    }

    ModuleCard& card_;
    const Hues& hues_;
    SampleSource& source_;
    const sp::Sample* builtFor_ = nullptr;
    std::vector<std::pair<float, float>> columns_;
    int dragging_ = -1, hover_ = -1;
    bool dropping_ = false;
};

// ----- the keyboard -------------------------------------------------------------------------------

// All 128 notes: the key range lit in the hue, the root solid, C labels. A
// click sets the root; a drag that starts at the range's end moves that end.
class KeyStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    KeyStrip(ModuleCard& card, const Hues& hues) : card_(card), hues_(hues)
    {
        setTooltip("Click a key for the root; drag the lit range's ends for the lowest and highest notes");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto& c = theme.colours;
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto keys = keyArea();
        const float w = keys.getWidth() / 128.0f;
        const int low = noteParam(sp::keyLow), high = noteParam(sp::keyHigh), root = noteParam(sp::root);
        for (int n = 0; n < 128; ++n)
        {
            const bool black = core::isBlackKey(n);
            auto r = juce::Rectangle<float>(keys.getX() + n * w, keys.getY(), w, keys.getHeight());
            if (black)
                r = r.withHeight(keys.getHeight() * 0.62f);
            juce::Colour colour = black ? c.surface0 : c.surface3;
            if (n >= std::min(low, high) && n <= std::max(low, high))
                colour = colour.interpolatedWith(hues_.keys, black ? 0.35f : 0.45f);
            if (n == root)
                colour = hues_.keys;
            g.setColour(colour);
            g.fillRect(r.reduced(0.25f, 0.0f));
        }
        g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.caption - 2.0f, theme.fonts.weight.medium));
        for (int n = 0; n < 128; n += 12)
        {
            g.setColour(c.textTertiary);
            g.drawText(
                juce::String(core::noteName(n)),
                juce::Rectangle<float>(keys.getX() + n * w + 1.0f, keys.getBottom() - 11.0f, 24.0f, 10.0f),
                juce::Justification::centredLeft);
        }
        drawWellFrame(g, theme, b);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int n = noteAt(e.position.x);
        const int low = noteParam(sp::keyLow), high = noteParam(sp::keyHigh);
        dragging_ = std::abs(n - low) <= 1 && n != noteParam(sp::root)    ? sp::keyLow
                    : std::abs(n - high) <= 1 && n != noteParam(sp::root) ? sp::keyHigh
                                                                          : sp::root;
        card_.beginGesture(card_.info(dragging_).name);
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging_ < 0)
            return;
        int n = noteAt(e.position.x);
        if (dragging_ == sp::keyLow)
            n = std::min(n, noteParam(sp::keyHigh));
        if (dragging_ == sp::keyHigh)
            n = std::max(n, noteParam(sp::keyLow));
        card_.setParam(dragging_, static_cast<float>(n));
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging_ >= 0)
            card_.endGesture();
        dragging_ = -1;
    }

private:
    juce::Rectangle<float> keyArea() const { return getLocalBounds().toFloat().reduced(6.0f, 4.0f); }
    int noteAt(float x) const
    {
        const auto keys = keyArea();
        return juce::jlimit(0, 127, static_cast<int>((x - keys.getX()) / keys.getWidth() * 128.0f));
    }
    int noteParam(int index) const { return static_cast<int>(std::lround(card_.param(index))); }

    ModuleCard& card_;
    const Hues& hues_;
    int dragging_ = -1;
};

// ----- the envelope and the filter ----------------------------------------------------------------

// The amplitude over a held note and its release (the times in proportion).
class EnvelopeDisplay final : public juce::Component
{
public:
    EnvelopeDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto r = b.reduced(8.0f, 8.0f);
        // Times on a gentle log scale so short and long envelopes both read.
        const auto span = [](float ms) { return std::log1p(std::max(0.0f, ms) / 20.0f); };
        const float a = span(card_.param(sp::attack)), d = span(card_.param(sp::decay)),
                    rel = span(card_.param(sp::release)), hold = 1.2f;
        const float total = std::max(0.001f, a + d + hold + rel);
        const float s = juce::jlimit(0.0f, 1.0f, card_.param(sp::sustain));
        const auto x = [&](float t) { return r.getX() + t / total * r.getWidth(); };
        const auto y = [&](float level) { return r.getBottom() - level * r.getHeight(); };
        juce::Path p;
        p.startNewSubPath(x(0.0f), y(0.0f));
        p.lineTo(x(a), y(1.0f));
        p.quadraticTo(x(a + d * 0.25f), y(s), x(a + d), y(s));
        p.lineTo(x(a + d + hold), y(s));
        p.quadraticTo(x(a + d + hold + rel * 0.25f), y(0.0f), x(total), y(0.0f));
        juce::Path fill(p);
        fill.lineTo(x(total), y(0.0f));
        fill.closeSubPath();
        g.setColour(hue_.withAlpha(0.18f));
        g.fillPath(fill);
        g.setColour(hue_);
        g.strokePath(p, juce::PathStrokeType(1.5f));
        if (card_.param(sp::oneShot) >= 0.5f)
        {
            g.setFont(theme.font(Theme::Face::mono, theme.fonts.size.micro, theme.fonts.weight.semibold));
            g.setColour(theme.colours.textTertiary);
            g.drawText("ONE-SHOT", b.reduced(6.0f, 4.0f), juce::Justification::topRight);
        }
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// The low-pass's response, and how far the envelope moves its cutoff (a
// second, fainter curve where the envelope takes it).
class FilterDisplay final : public juce::Component
{
public:
    FilterDisplay(ModuleCard& card, juce::Colour hue) : card_(card), hue_(hue) {}
    void paint(juce::Graphics& g) override
    {
        const auto& theme = card_.theme();
        const auto b = getLocalBounds().toFloat();
        drawWell(g, theme, b);
        const auto r = b.reduced(6.0f, 8.0f);
        const float k = 2.0f - 1.96f * juce::jlimit(0.0f, 1.0f, card_.param(sp::resonance));
        const auto curve = [&](double cutoffHz)
        {
            juce::Path p;
            for (int i = 0; i <= 64; ++i)
            {
                const float px = r.getX() + r.getWidth() * static_cast<float>(i) / 64.0f;
                const double w = xToHz(px, r) / cutoffHz;
                const double mag = 1.0 / std::sqrt((1.0 - w * w) * (1.0 - w * w) + (k * w) * (k * w));
                const float py = dbToY(static_cast<float>(engine::gainToDb(mag, -120.0)), 18.0f, -36.0f, r);
                if (i == 0)
                    p.startNewSubPath(px, py);
                else
                    p.lineTo(px, py);
            }
            return p;
        };
        const double cutoff = card_.param(sp::cutoff);
        const float amount = card_.param(sp::filterEnv);
        if (std::abs(amount) > 0.01f)
        {
            g.setColour(hue_.withAlpha(0.35f));
            g.strokePath(curve(juce::jlimit(20.0, 20000.0, cutoff * std::exp2(amount))),
                         juce::PathStrokeType(1.0f));
        }
        g.setColour(hue_);
        g.strokePath(curve(cutoff), juce::PathStrokeType(1.5f));
        g.setFont(theme.value());
        g.setColour(theme.colours.textSecondary);
        g.drawText(cutoff >= 19500.0 && std::abs(amount) <= 0.01f ? juce::String("Open") : hzText(cutoff),
                   b.reduced(6.0f, 3.0f),
                   juce::Justification::topRight);
        drawWellFrame(g, theme, b);
    }

private:
    ModuleCard& card_;
    juce::Colour hue_;
};

// ----- the editor ---------------------------------------------------------------------------------

class SamplerCard final : public ModuleCard
{
public:
    SamplerCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
        : ModuleCard(theme, host, channelId, insert), hues_(theme), wave_(*this, hues_, source_),
          keys_(*this, hues_), envelope_(*this, hues_.envelope), filter_(*this, hues_.filter)
    {
        state_ = insert.state;
        source_.sample = [this]() -> std::shared_ptr<const sp::Sample>
        {
            const auto live = host_.liveInsert ? host_.liveInsert(moduleId_) : nullptr;
            return live != nullptr ? sp::currentSample(*live) : nullptr;
        };
        source_.error = [this]
        {
            const auto live = host_.liveInsert ? host_.liveInsert(moduleId_) : nullptr;
            return live != nullptr ? sp::loadError(*live) : std::string{};
        };
        source_.path = [this] { return sp::decodeState(state_); };
        source_.load = [this](const juce::File& file) { loadFile(file); };
        for (auto* c : std::initializer_list<juce::Component*>{&wave_, &keys_, &envelope_, &filter_})
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
        const auto percent = [](double v)
        { return juce::String(static_cast<int>(std::lround(v * 100.0))) + "%"; };
        const auto signedInt = [](const char* suffix)
        {
            return [suffix](double v)
            {
                const int n = static_cast<int>(std::lround(v));
                return (n > 0 ? "+" : "") + juce::String(n) + suffix;
            };
        };
        pitchKnobs_ = {knob(sp::root, "Root", hues_.keys, noteText),
                       knob(sp::keyLow, "Low", hues_.keys, noteText),
                       knob(sp::keyHigh, "High", hues_.keys, noteText),
                       knob(sp::tune, "Tune", hues_.sample, signedInt(" st")),
                       knob(sp::fine, "Fine", hues_.sample, signedInt(" ct")),
                       knob(sp::glide, "Glide", hues_.sample)};
        playKnobs_ = {knob(sp::loopFade, "Loop fade", hues_.loop),
                      knob(sp::voices,
                           "Voices",
                           hues_.sample,
                           [](double v) { return juce::String(static_cast<int>(std::lround(v))); }),
                      knob(sp::velocity, "Velocity", hues_.sample, percent),
                      knob(sp::level, "Level", hues_.sample)};
        loop_ = &addToggle(sp::loop, "Loop");
        oneShot_ = &addToggle(sp::oneShot, "One-shot");
        loop_->setTooltip("Loop between the loop points while the note is held");
        oneShot_->setTooltip("Play to the end point whatever the note's length");
        envelopeKnobs_ = {knob(sp::attack, "Attack", hues_.envelope),
                          knob(sp::decay, "Decay", hues_.envelope),
                          knob(sp::sustain, "Sustain", hues_.envelope, percent),
                          knob(sp::release, "Release", hues_.envelope)};
        filterKnobs_ = {knob(sp::cutoff, "Cutoff", hues_.filter, [](double v) { return hzText(v); }),
                        knob(sp::resonance, "Reso", hues_.filter, percent),
                        knob(sp::filterEnv,
                             "Env",
                             hues_.filter,
                             [](double v) { return (v > 0.005 ? "+" : "") + juce::String(v, 1) + " oct"; })};

        load_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        load_.setTooltip("Load an audio file (or drop one on the waveform)");
        load_.onClick = [this] { chooseFile(); };
        addAndMakeVisible(load_);
    }

    int preferredWidth() const override { return 13 * cellW + 4 * 2 * pad + 3 * gap + 2 * theme_.spacing.sm; }
    // The header, the sample, then panels two knob rows tall.
    int preferredBodyHeight() const override
    {
        return headerH + 4 + sampleH + gap + 2 * pad + captionH + 2 * knobCellHeight() + 6;
    }

private:
    static constexpr int headerH = 46, sampleH = 196, captionH = 18, cellW = 56, gap = 8, pad = 8;

    struct Section
    {
        const char* title;
        juce::Colour hue;
        juce::Rectangle<int> bounds;
    };

    void layoutBody(juce::Rectangle<int> body) override
    {
        backdrop_ = getLocalBounds();
        auto header = body.removeFromTop(headerH);
        wordmark_ = header.removeFromLeft(150);
        auto right = header.removeFromRight(240);
        load_.setBounds(right.removeFromRight(72).withSizeKeepingCentre(72, theme_.sizes.controlCompact));
        right.removeFromRight(theme_.spacing.sm);
        voicesArea_ = right;
        fileArea_ = header.reduced(theme_.spacing.sm, 0);
        body.removeFromTop(4);

        sections_.clear();
        // The sample: the waveform and the keyboard across the whole width.
        auto top = body.removeFromTop(sampleH);
        sections_.push_back({"SAMPLE", hues_.sample, top});
        auto inner = top.reduced(pad);
        inner.removeFromTop(captionH);
        keys_.setBounds(inner.removeFromBottom(30));
        inner.removeFromBottom(4);
        wave_.setBounds(inner);
        body.removeFromTop(gap);

        int x = body.getX();
        const int panelH = body.getHeight();
        const auto panel = [&](const char* title, juce::Colour hue, int columns) -> juce::Rectangle<int>
        {
            const juce::Rectangle<int> r(x, body.getY(), columns * cellW + 2 * pad, panelH);
            sections_.push_back({title, hue, r});
            x += r.getWidth() + gap;
            auto area = r.reduced(pad);
            area.removeFromTop(captionH);
            return area;
        };
        {
            auto area = panel("PITCH", hues_.keys, 3);
            layoutKnobs(area.removeFromTop(2 * knobCellHeight()), 3, pitchKnobs_);
        }
        {
            auto area = panel("PLAY", hues_.loop, 3);
            layoutKnobs(
                area.removeFromTop(knobCellHeight()), 3, {playKnobs_[0], playKnobs_[1], playKnobs_[2]});
            auto second = area.removeFromTop(knobCellHeight());
            layoutKnobs(second.removeFromLeft(cellW), 1, {playKnobs_[3]});
            // The two pills in a column beside Level, one control height each.
            auto pills = second.withSizeKeepingCentre(second.getWidth() - theme_.spacing.sm,
                                                      2 * theme_.sizes.controlCompact + theme_.spacing.xs);
            loop_->setBounds(pills.removeFromTop(theme_.sizes.controlCompact));
            pills.removeFromTop(theme_.spacing.xs);
            oneShot_->setBounds(pills.removeFromTop(theme_.sizes.controlCompact));
        }
        {
            auto area = panel("ENVELOPE", hues_.envelope, 4);
            const int knobsH = knobCellHeight();
            envelope_.setBounds(area.removeFromTop(area.getHeight() - knobsH - pad));
            area.removeFromTop(pad);
            layoutKnobs(area.removeFromTop(knobsH), 4, envelopeKnobs_);
        }
        {
            auto area = panel("FILTER", hues_.filter, 3);
            const int knobsH = knobCellHeight();
            filter_.setBounds(area.removeFromTop(area.getHeight() - knobsH - pad));
            area.removeFromTop(pad);
            layoutKnobs(area.removeFromTop(knobsH), 3, filterKnobs_);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto bounds = backdrop_.toFloat();
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addRoundedRectangle(bounds, theme_.radius.md);
            g.reduceClipRegion(clip);
            g.setGradientFill(juce::ColourGradient(
                c.canvas.darker(0.3f), 0.0f, bounds.getY(), c.surface0, 0.0f, bounds.getBottom(), false));
            g.fillRect(bounds);
        }
        // The trays: surface1 with a subtle rim, a square of the hue before the caption.
        for (const auto& s : sections_)
        {
            const auto r = s.bounds.toFloat();
            g.setColour(c.surface1);
            g.fillRoundedRectangle(r, theme_.radius.md);
            g.setColour(c.lineSubtle);
            g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.md, 1.0f);
            auto caption = s.bounds.reduced(pad).removeFromTop(captionH - 4);
            g.setColour(s.hue);
            g.fillRoundedRectangle(caption.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f),
                                   2.0f);
            caption.removeFromLeft(theme_.spacing.sm);
            g.setFont(theme_.captionCaps());
            g.setColour(c.textSecondary);
            g.drawText(s.title, caption, juce::Justification::centredLeft);
        }
        paintHeader(g);
    }

    void paintHeader(juce::Graphics& g) const
    {
        const auto& c = theme_.colours;
        // The wordmark: the name in the hue over a little waveform.
        auto mark = wordmark_.toFloat().reduced(4.0f, 8.0f);
        juce::Path squiggle;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = static_cast<float>(i) / 40.0f;
            const float y = mark.getBottom() - 4.0f -
                            3.5f * std::sin(t * juce::MathConstants<float>::twoPi * 3.0f) * (1.0f - t);
            if (i == 0)
                squiggle.startNewSubPath(mark.getX(), y);
            else
                squiggle.lineTo(mark.getX() + t * 110.0f, y);
        }
        g.setColour(hues_.sample.withAlpha(0.5f));
        g.strokePath(squiggle, juce::PathStrokeType(1.5f));
        g.setColour(hues_.sample);
        g.setFont(theme_.font(Theme::Face::ui, theme_.fonts.size.heading, theme_.fonts.weight.semibold));
        g.drawText("Sampler", mark.withTrimmedBottom(8.0f), juce::Justification::centredLeft);

        // The file: its name, and its rate, channels and length.
        const auto sample = source_.sample ? source_.sample() : nullptr;
        const auto path = juce::String::fromUTF8(sp::decodeState(state_).c_str());
        auto file = fileArea_.toFloat();
        g.setFont(theme_.label());
        g.setColour(path.isEmpty() ? c.textTertiary : sample == nullptr ? c.warning : c.textPrimary);
        g.drawText(path.isEmpty() ? juce::String("No sample") : juce::File(path).getFileName(),
                   file.removeFromTop(file.getHeight() * 0.55f),
                   juce::Justification::bottomLeft,
                   true);
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        juce::String details;
        if (sample != nullptr)
            details << juce::String(sample->sampleRate / 1000.0, 1) << " kHz  "
                    << juce::String::fromUTF8("\xc2\xb7  ") << (sample->stereo() ? "Stereo" : "Mono") << "  "
                    << juce::String::fromUTF8("\xc2\xb7  ")
                    << juce::String(static_cast<double>(sample->frames()) / sample->sampleRate, 2) << " s";
        else if (path.isNotEmpty())
            details = "Not loaded: " + juce::String::fromUTF8(source_.error().c_str());
        else
            details = "Drop an audio file on the waveform";
        g.drawText(details, file, juce::Justification::topLeft, true);

        // The voices sounding, as a count with a light.
        const auto light = voicesArea_.toFloat().removeFromRight(10.0f).withSizeKeepingCentre(8.0f, 8.0f);
        g.setColour(sounding_ > 0 ? hues_.sample : c.surface3);
        g.fillEllipse(light);
        g.setFont(theme_.value());
        g.setColour(sounding_ > 0 ? c.textSecondary : c.textTertiary);
        g.drawText(juce::String(sounding_) + (sounding_ == 1 ? " voice" : " voices"),
                   voicesArea_.toFloat().withTrimmedRight(16.0f),
                   juce::Justification::centredRight);
    }

    // ----- loading --------------------------------------------------------------------------------

    void chooseFile()
    {
        const auto current = juce::File(juce::String::fromUTF8(sp::decodeState(state_).c_str()));
        juce::String patterns;
        for (const auto& extension : audioFileExtensions())
            patterns << (patterns.isEmpty() ? "" : ";") << "*" << extension;
        chooser_ = std::make_unique<juce::FileChooser>(
            "Load a sample",
            current.existsAsFile() ? current.getParentDirectory()
                                   : juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            patterns);
        juce::Component::SafePointer<SamplerCard> safe(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [safe](const juce::FileChooser& chooser)
                              {
                                  if (safe != nullptr && chooser.getResult().existsAsFile())
                                      safe->loadFile(chooser.getResult());
                              });
    }

    // A new file, as one undo step: its path into the state, and the points
    // back to the whole sample.
    void loadFile(const juce::File& file)
    {
        if (!file.existsAsFile() || !host_.onCommand)
            return;
        beginGesture("Load sample");
        host_.onCommand(std::make_unique<core::SetInsertState>(
            channelId_, moduleId_, sp::encodeState(file.getFullPathName().toStdString()), "Load sample"));
        setParam(sp::start, 0.0f);
        setParam(sp::end, 1.0f);
        setParam(sp::loopStart, 0.0f);
        setParam(sp::loopEnd, 1.0f);
        endGesture();
        repaint();
    }

    // ----- live -----------------------------------------------------------------------------------

    void insertChanged(const core::Insert& insert) override
    {
        if (insert.state != state_)
        {
            state_ = insert.state;
            repaint();
        }
    }

    void paramsChanged() override
    {
        for (auto* c : std::initializer_list<juce::Component*>{&wave_, &keys_, &envelope_, &filter_})
            c->repaint();
    }

    void framesArrived(double) override
    {
        const auto live = host_.liveInsert ? host_.liveInsert(moduleId_) : nullptr;
        auto voices = live != nullptr ? sp::soundingVoices(*live) : std::vector<sp::VoiceInfo>{};
        int sounding = 0;
        for (const auto& v : voices)
            sounding += v.releasing ? 0 : 1;
        if (!voices.empty() || !wave_.voices.empty())
        {
            wave_.voices = std::move(voices);
            wave_.repaint();
        }
        if (sounding != sounding_)
        {
            sounding_ = sounding;
            repaint(voicesArea_);
        }
        // A sample loaded or swapped since the last look (the command's
        // recompile has run by now).
        const auto sample = source_.sample();
        if (sample.get() != shownSample_)
        {
            shownSample_ = sample.get();
            repaint();
        }
    }

    Hues hues_;
    SampleSource source_;
    WaveDisplay wave_;
    KeyStrip keys_;
    EnvelopeDisplay envelope_;
    FilterDisplay filter_;
    std::vector<Knob*> pitchKnobs_, playKnobs_, envelopeKnobs_, filterKnobs_;
    PillToggle* loop_ = nullptr;
    PillToggle* oneShot_ = nullptr;
    juce::TextButton load_{juce::String::fromUTF8("Load\xe2\x80\xa6")};
    std::unique_ptr<juce::FileChooser> chooser_;
    std::vector<Section> sections_;
    juce::Rectangle<int> backdrop_, wordmark_, fileArea_, voicesArea_;
    std::string state_;
    const sp::Sample* shownSample_ = nullptr;
    int sounding_ = 0;
};

} // namespace

std::unique_ptr<ModuleCard>
createSamplerEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert)
{
    return std::make_unique<SamplerCard>(theme, host, channelId, insert);
}

} // namespace northlight::ui::strip
