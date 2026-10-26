#include "app/FirstRun.h"

#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "ui/Theme.h"
#include "ui/components/Meter.h"
#include "ui/components/Sheet.h"

#include <algorithm>
#include <array>

namespace northlight
{

namespace
{
// The first-run sheet's content (screens/setup.md 8.3): driver, device, buffer size
// and a live input meter so the user sees signal before the project opens.
class FirstRunContent final : public juce::Component, private juce::Timer
{
public:
    FirstRunContent(const ui::Theme& theme, audio::AudioDeviceLayer& audio)
        : theme_(theme), audio_(audio), meter_(theme, 1, false)
    {
        addAndMakeVisible(driver_);
        addAndMakeVisible(device_);
        addAndMakeVisible(buffer_);
        addAndMakeVisible(meter_);
        types_ = audio_.deviceTypes();
        const auto status = audio_.status();
        for (size_t i = 0; i < types_.size(); ++i)
        {
            driver_.addItem(types_[i].name, static_cast<int>(i) + 1);
            if (types_[i].name == status.deviceType ||
                (status.deviceType.empty() && types_[i].name == "ASIO"))
                driver_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
        if (driver_.getSelectedId() == 0 && !types_.empty())
            driver_.setSelectedId(1, juce::dontSendNotification);
        driver_.onChange = [this]
        {
            fillDevices();
            repaint(); // the driver's hint
        };
        fillDevices();
        // The sizes the device offers, its own among them (Windows Audio often runs
        // at 480), each with what it costs in latency.
        const double rate = status.sampleRate > 0.0 ? status.sampleRate : 48000.0;
        for (const int size : audio_.bufferSizeChoices())
            buffer_.addItem(juce::String(size) + " samples  " +
                                juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + "  " +
                                juce::String(1000.0 * size / rate, 1) + " ms",
                            size);
        buffer_.setSelectedId(status.bufferSize > 0 ? status.bufferSize : 128, juce::dontSendNotification);
        startTimerHz(20);
    }
    ~FirstRunContent() override { stopTimer(); }

    // Opens the chosen device; the error, if any.
    std::optional<std::string> apply()
    {
        audio::AudioSettings settings = audio_.settings();
        const int type = driver_.getSelectedId() - 1;
        const int device = device_.getSelectedId() - 1;
        if (type >= 0 && static_cast<size_t>(type) < types_.size() && device >= 0 &&
            static_cast<size_t>(device) < names_.outputs.size())
            settings = audio::AudioDeviceLayer::withDevice(settings,
                                                           types_[static_cast<size_t>(type)],
                                                           names_,
                                                           names_.outputs[static_cast<size_t>(device)]);
        settings.bufferSize = buffer_.getSelectedId();
        return audio_.apply(settings);
    }

    static constexpr int rowHeight = 56;
    static int preferredHeight(const ui::Theme&) { return 4 * rowHeight; }

    // A card of four rows (screens/setup.md 8.3): a label with a one-line hint on the
    // left, the control on the right, hairlines between the rows.
    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const auto card = getLocalBounds().toFloat();
        g.setColour(c.surface1);
        g.fillRoundedRectangle(card, theme_.radius.md);
        g.setColour(c.lineSubtle);
        g.drawRoundedRectangle(card.reduced(0.5f), theme_.radius.md, 1.0f);
        const bool asio = driver_.getText() == "ASIO";
        const std::array<std::pair<const char*, juce::String>, 4> rows{{
            {"Driver",
             asio ? juce::String("The lowest latency, for recording and monitoring")
                  : juce::String("ASIO, if your interface has it, is lowest latency")},
            {"Device", "Where Northlight plays and records"},
            {"Buffer size", "Smaller is more responsive; larger avoids clicks"},
            {"Input", "Play or sing: the meter should move"},
        }};
        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto row = rowBounds(static_cast<int>(i));
            if (i > 0)
            {
                g.setColour(c.lineSubtle);
                g.fillRect(
                    row.getX() + theme_.spacing.lg, row.getY(), row.getWidth() - 2 * theme_.spacing.lg, 1);
            }
            auto text = row.reduced(theme_.spacing.lg, 0).withTrimmedRight(controlWidth + theme_.spacing.lg);
            const int labelH = juce::roundToInt(theme_.fonts.size.label * theme_.fonts.lineHeight);
            const int hintH = juce::roundToInt(theme_.fonts.size.caption * theme_.fonts.lineHeight);
            auto block = text.withSizeKeepingCentre(text.getWidth(), labelH + theme_.spacing.xxs + hintH);
            g.setFont(theme_.label());
            g.setColour(c.textPrimary);
            g.drawText(rows[i].first, block.removeFromTop(labelH), juce::Justification::centredLeft);
            block.removeFromTop(theme_.spacing.xxs);
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary);
            g.drawText(rows[i].second, block, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        const auto place = [&](juce::Component& control, int row, int height)
        {
            const auto r = rowBounds(row).reduced(theme_.spacing.lg, 0);
            control.setBounds(
                r.withLeft(r.getRight() - controlWidth).withSizeKeepingCentre(controlWidth, height));
        };
        place(driver_, 0, theme_.sizes.control);
        place(device_, 1, theme_.sizes.control);
        place(buffer_, 2, theme_.sizes.control);
        place(meter_, 3, theme_.sizes.meterWidth * 2 + theme_.spacing.xs);
    }

private:
    static constexpr int controlWidth = 260;
    juce::Rectangle<int> rowBounds(int row) const { return {0, row * rowHeight, getWidth(), rowHeight}; }

    void fillDevices()
    {
        const int type = driver_.getSelectedId() - 1;
        device_.clear(juce::dontSendNotification);
        if (type < 0 || static_cast<size_t>(type) >= types_.size())
            return;
        names_ = audio_.deviceNames(types_[static_cast<size_t>(type)].name);
        for (size_t i = 0; i < names_.outputs.size(); ++i)
            device_.addItem(names_.outputs[i], static_cast<int>(i) + 1);
        if (const int current =
                audio::AudioDeviceLayer::indexOf(names_.outputs, audio_.status().outputDevice);
            current >= 0)
            device_.setSelectedId(current + 1, juce::dontSendNotification);
        if (device_.getSelectedId() == 0 && names_.defaultOutput >= 0)
            device_.setSelectedId(names_.defaultOutput + 1, juce::dontSendNotification);
    }
    void timerCallback() override
    {
        const float peak = peaks_.take(audio_.callback().inputPeaks());
        meter_.push(&peak, 1, 50.0);
    }

    const ui::Theme& theme_;
    audio::AudioDeviceLayer& audio_;
    juce::ComboBox driver_, device_, buffer_;
    ui::Meter meter_;
    rt::PeakHistory::Reader peaks_; // the sheet's own place in the input peaks
    std::vector<audio::AudioDeviceLayer::DeviceType> types_;
    audio::AudioDeviceLayer::DeviceNames names_;
};
} // namespace

FirstRun::FirstRun(Host host) : host_(std::move(host)) {}

void FirstRun::show()
{
    auto content = std::make_unique<FirstRunContent>(host_.theme, host_.audio);
    auto* raw = content.get();
    ui::SheetOptions options;
    options.hero = true;
    options.eyebrow = "Step 1 of 2";
    options.footnote = "You can change this any time in Audio Setup.";
    options.width = 600;
    host_.openSheet = ui::Sheet::show(
        host_.window,
        host_.theme,
        "Choose your audio device",
        "Pick the interface you record and listen through. Northlight checks it before you start.",
        {{"Skip", false, false}, {"Use this device", true, false}},
        [this, raw](int result)
        {
            host_.openSheet = nullptr;
            if (result == 1)
                if (const auto error = raw->apply())
                    host_.toast(ui::ToastStack::Kind::warning,
                                "The device could not be opened: " + juce::String::fromUTF8(error->c_str()),
                                "Audio Setup",
                                host_.showAudioSetup);
            juce::Component::SafePointer<juce::Component> safe(&host_.window);
            juce::MessageManager::callAsync(
                [this, safe]
                {
                    if (safe != nullptr)
                        offerScan();
                });
        },
        std::move(content),
        FirstRunContent::preferredHeight(host_.theme),
        options);
}

void FirstRun::offerScan()
{
    ui::SheetOptions options;
    options.hero = true;
    options.eyebrow = "Step 2 of 2";
    options.footnote = "Scan again any time from the Browser.";
    options.width = 600;
    host_.openSheet = ui::Sheet::show(
        host_.window,
        host_.theme,
        "Find your plugins",
        "Northlight looks in the standard VST3 and CLAP folders. The scan runs in the background, so you "
        "can start working straight away.",
        {{"Later", false, false}, {"Scan now", true, false}},
        [this](int result)
        {
            host_.openSheet = nullptr;
            if (result == 1)
                host_.session.scanPlugins(true);
        },
        nullptr,
        0,
        options);
}

} // namespace northlight
