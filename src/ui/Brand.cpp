#include "ui/Brand.h"

namespace northlight::ui::brand
{
namespace
{
juce::File assets()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("assets");
}
} // namespace

const juce::Image& photo()
{
    static const juce::Image image =
        juce::ImageFileFormat::loadFrom(assets().getChildFile("splash/gytis-bukauskas-moon-over-wheat.jpg"));
    return image;
}

std::unique_ptr<juce::Drawable> drawable(const juce::String& fileName)
{
    return juce::Drawable::createFromSVGFile(assets().getChildFile("brand").getChildFile(fileName));
}

void drawWordmark(juce::Graphics& g, const juce::Drawable& wordmark, float x, float baselineY, float size)
{
    // The wordmark was set at 40 units to the em.
    const float scale = size / 40.0f;
    wordmark.draw(g,
                  1.0f,
                  juce::AffineTransform::scale(scale).translated(x - wordmarkInset * scale,
                                                                 baselineY - wordmarkBaseline * scale));
}

} // namespace northlight::ui::brand
