#include "ui/SplashScreen.h"

#include "ui/Brand.h"

#include <array>

namespace northlight::ui
{
namespace
{
// The splash stays up at least this long, then fades out over fadeMs.
constexpr double minimumShowMs = 1800.0;
constexpr int fadeMs = 280;
} // namespace

SplashScreen::SplashScreen(const Theme& theme, const juce::String& version, Mode mode)
    : theme_(theme), version_(version), mode_(mode)
{
    photo_ = brand::photo();
    mark_ = brand::drawable("northlight-mark.svg");
    // On the photograph's overcast sky the Overcast horizon disappears: draw the
    // whole mark in moonlight, like the photograph's own moon.
    if (mark_ != nullptr)
        mark_->replaceColour(theme_.colours.accent, theme_.colours.textPrimary);
    wordmark_ = brand::drawable("northlight-wordmark.svg");
    setOpaque(true);
    setSize(width, height);
    setWantsKeyboardFocus(mode_ == Mode::about);
    setTitle("Northlight " + version_);
    shownAtMs_ = juce::Time::getMillisecondCounterHiRes();
}

juce::Component::SafePointer<SplashScreen> SplashScreen::showStartup(const Theme& theme,
                                                                     const juce::String& version)
{
    auto* splash = new SplashScreen(theme, version, Mode::startup);
    const auto area =
        juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt();
    splash->setBounds(area.withSizeKeepingCentre(width, height));
    splash->setAlwaysOnTop(true);
    splash->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
    splash->setVisible(true);
    splash->setStatus("Starting", 0.05f);
    return splash;
}

void SplashScreen::showAbout(const Theme& theme, const juce::String& version, juce::Component* over)
{
    auto* splash = new SplashScreen(theme, version, Mode::about);
    splash->status_ = "Click anywhere to close";
    const auto centre = over != nullptr ? over->getScreenBounds().getCentre()
                                        : juce::Desktop::getInstance()
                                              .getDisplays()
                                              .getPrimaryDisplay()
                                              ->userBounds.toNearestInt()
                                              .getCentre();
    splash->setBounds(juce::Rectangle<int>(width, height).withCentre(centre));
    splash->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
    splash->setVisible(true);
    splash->toFront(true);
    splash->grabKeyboardFocus();
}

void SplashScreen::setStatus(const juce::String& text, float progress)
{
    status_ = text;
    progress_ = juce::jlimit(0.0f, 1.0f, progress);
    repaint();
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
}

void SplashScreen::finish()
{
    if (finishing_)
        return;
    finishing_ = true;
    status_ = "Ready";
    progress_ = 1.0f;
    repaint();
    const double shown = juce::Time::getMillisecondCounterHiRes() - shownAtMs_;
    startTimer(static_cast<int>(std::max(1.0, minimumShowMs - shown)));
}

void SplashScreen::timerCallback()
{
    if (!closing_)
    {
        close();
        return;
    }
    // Fading: each tick takes the alpha down a step; at zero the splash goes.
    const float alpha = getAlpha() - 1.0f / (static_cast<float>(fadeMs) / 16.0f);
    if (alpha <= 0.0f)
    {
        stopTimer();
        juce::Component::SafePointer<SplashScreen> safe(this);
        juce::MessageManager::callAsync(
            [safe]
            {
                if (safe != nullptr)
                    delete safe.getComponent();
            });
        return;
    }
    setAlpha(alpha);
}

void SplashScreen::close()
{
    if (closing_)
        return;
    closing_ = true;
    startTimer(16);
}

void SplashScreen::mouseDown(const juce::MouseEvent&)
{
    // The About box closes on a click; the startup splash once the app is up.
    if (mode_ == Mode::about || finishing_)
        close();
}

bool SplashScreen::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    return false;
}

void SplashScreen::focusLost(FocusChangeType)
{
    if (mode_ == Mode::about)
        close();
}

void SplashScreen::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto& s = theme_.spacing;
    const auto bounds = getLocalBounds().toFloat();

    // The photograph, filling the window; the field at night when it is missing.
    g.fillAll(c.canvas);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    if (photo_.isValid())
        g.drawImage(photo_, bounds, juce::RectanglePlacement::fillDestination);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    // Settle the photograph into the palette, then let the field darken into the
    // canvas where the text sits.
    g.setColour(c.canvas.withAlpha(0.10f));
    g.fillRect(bounds);
    juce::ColourGradient dusk(c.canvas.withAlpha(0.0f),
                              0.0f,
                              bounds.getHeight() * 0.40f,
                              c.canvas.withAlpha(0.94f),
                              0.0f,
                              bounds.getBottom(),
                              false);
    dusk.addColour(0.55, c.canvas.withAlpha(0.62f));
    g.setGradientFill(dusk);
    g.fillRect(bounds);

    auto content = getLocalBounds().reduced(s.xxl);
    const int line = juce::roundToInt(theme_.fonts.size.caption * theme_.fonts.lineHeight) + s.xxs;

    // Right: copyright, licence and the photo credit, one quiet block.
    {
        auto block = content.removeFromRight(content.getWidth() / 2).removeFromBottom(3 * line);
        g.setFont(theme_.caption());
        const juce::String year(juce::Time::getCompilationDate().getYear());
        const std::array<std::pair<juce::String, juce::Colour>, 3> lines{{
            {juce::String(juce::CharPointer_UTF8("\xc2\xa9 ")) + year + " the Northlight contributors",
             c.textSecondary},
            {"Free software under the GNU AGPLv3", c.textSecondary},
            {brand::photoCredit, c.textTertiary},
        }};
        for (const auto& [text, colour] : lines)
        {
            g.setColour(colour);
            g.drawText(text, block.removeFromTop(line), juce::Justification::centredRight);
        }
    }

    // Top left: the mark, small, in the sky. On its 64-unit grid the horizon
    // starts 4.75 units in and the moon's top is at 13; align those to the margin.
    if (mark_ != nullptr)
    {
        constexpr float grid = 64.0f, px = 28.0f, left = 4.75f, top = 13.0f;
        const float scale = px / grid;
        mark_->draw(g,
                    1.0f,
                    juce::AffineTransform::scale(scale).translated(static_cast<float>(s.xxl) - left * scale,
                                                                   static_cast<float>(s.xxl) - top * scale));
    }

    // Left: the wordmark, the version and what is loading.
    {
        auto block = content;
        g.setFont(theme_.caption());
        g.setColour(c.textTertiary);
        g.drawText(status_, block.removeFromBottom(line), juce::Justification::centredLeft);
        g.setFont(theme_.small());
        g.setColour(c.textSecondary);
        g.drawText("Version " + version_, block.removeFromBottom(line), juce::Justification::centredLeft);
        block.removeFromBottom(s.xs);
        if (wordmark_ != nullptr)
        {
            // The wordmark's 48-unit box (scripts/brand-assets.py) has its baseline
            // at 36, room below for the g, and the N starting 1.42 units in. At 0.9
            // the type is 36 px, its descender space sitting on the version line.
            constexpr float box = 48.0f, scale = 0.9f, inset = 1.42f;
            wordmark_->draw(g,
                            1.0f,
                            juce::AffineTransform::scale(scale).translated(
                                static_cast<float>(block.getX()) - inset * scale,
                                static_cast<float>(block.getBottom()) - box * scale));
        }
    }

    // While starting: a hairline of progress along the bottom edge.
    if (mode_ == Mode::startup)
    {
        g.setColour(c.accent);
        g.fillRect(bounds.withTop(bounds.getBottom() - 2.0f).withWidth(bounds.getWidth() * progress_));
    }
    g.setColour(c.lineStrong);
    g.drawRect(bounds, 1.0f);
}

} // namespace northlight::ui
