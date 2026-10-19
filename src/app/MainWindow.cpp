#include "MainWindow.h"

namespace aural
{

class MainWindow::PlaceholderContent final : public juce::Component
{
public:
    PlaceholderContent() { setSize(1280, 800); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour::fromRGB(0x14, 0x15, 0x18));
        g.setColour(juce::Colour::fromRGB(0x9a, 0x9f, 0xa8));
        g.setFont(juce::FontOptions{20.0f});
        g.drawText("Aural", getLocalBounds(), juce::Justification::centred);
    }
};

MainWindow::MainWindow(const juce::String& name)
    : juce::DocumentWindow(name, juce::Colour::fromRGB(0x14, 0x15, 0x18), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new PlaceholderContent(), true);
    setResizable(true, false);
    setResizeLimits(640, 400, 10000, 10000);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace aural
