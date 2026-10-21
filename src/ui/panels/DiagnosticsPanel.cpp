#include "ui/panels/DiagnosticsPanel.h"

#include "ui/NorthlightLookAndFeel.h"

namespace northlight::ui
{

DiagnosticsPanel::DiagnosticsPanel(const Theme& theme) : theme_(theme)
{
    setOpaque(true);
    copy_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    copy_.setTooltip("Copy a text summary for a bug report");
    copy_.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(report()); };
    addAndMakeVisible(copy_);
}

void DiagnosticsPanel::push(const Sample& sample)
{
    last_ = sample;
    for (auto* series : {&p50_, &p99_, &max_})
        if (series->size() >= history)
            series->pop_front();
    p50_.push_back(sample.loadP50);
    p99_.push_back(sample.loadP99);
    max_.push_back(sample.loadMax);
    repaint();
}

juce::String DiagnosticsPanel::report() const
{
    juce::String text;
    text << "Northlight diagnostics\n";
    text << "Device: " << last_.device << " at " << juce::String(last_.sampleRate / 1000.0, 1) << " kHz, "
         << last_.bufferSize << " samples\n";
    text << "Callback load: p50 " << juce::String(last_.loadP50 * 100.0, 1) << "%, p99 "
         << juce::String(last_.loadP99 * 100.0, 1) << "%, max " << juce::String(last_.loadMax * 100.0, 1)
         << "%\n";
    text << "Overruns: " << last_.overruns << ", dropouts: " << last_.dropouts << "\n";
    text << "Worker threads: " << last_.workerThreads << ", output latency: " << last_.outputLatencySamples
         << " samples\n";
    text << "UI frame time: p50 " << juce::String(last_.frameP50Ms, 2) << " ms, p99 "
         << juce::String(last_.frameP99Ms, 2) << " ms, max " << juce::String(last_.frameMaxMs, 2) << " ms\n";
    text << "Paint time (arrange, mixer): p50 " << juce::String(last_.paintP50Ms, 2) << " ms, p99 "
         << juce::String(last_.paintP99Ms, 2) << " ms, max " << juce::String(last_.paintMaxMs, 2) << " ms\n";
    for (const auto& [name, latency] : last_.trackLatencies)
        text << "  " << name << ": " << latency << " samples of plugin latency\n";
    return text;
}

void DiagnosticsPanel::drawSparkline(juce::Graphics& g,
                                     juce::Rectangle<int> area,
                                     const std::deque<double>& values,
                                     juce::Colour colour) const
{
    g.setColour(theme_.colours.surface1);
    g.fillRoundedRectangle(area.toFloat(), theme_.radius.sm);
    if (values.size() < 2)
        return;
    juce::Path line;
    const float step = static_cast<float>(area.getWidth()) / static_cast<float>(history - 1);
    for (size_t i = 0; i < values.size(); ++i)
    {
        const float x =
            static_cast<float>(area.getX()) + static_cast<float>(history - values.size() + i) * step;
        const float y =
            static_cast<float>(area.getBottom()) -
            static_cast<float>(juce::jlimit(0.0, 1.0, values[i])) * static_cast<float>(area.getHeight() - 2) -
            1.0f;
        if (i == 0)
            line.startNewSubPath(x, y);
        else
            line.lineTo(x, y);
    }
    g.setColour(colour);
    g.strokePath(line, juce::PathStrokeType(1.0f));
    // The deadline.
    g.setColour(theme_.colours.lineStrong);
    g.fillRect(area.getX(), area.getY() + 1, area.getWidth(), 1);
}

void DiagnosticsPanel::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    auto area = getLocalBounds().reduced(theme_.spacing.md);
    area.removeFromTop(theme_.sizes.controlCompact + theme_.spacing.sm); // the button row
    const int labelW = 160;
    const int rowH = theme_.sizes.controlCompact;
    const auto colourFor = [&](double load)
    {
        return load > 0.9 ? c.danger : load > 0.7 ? c.warning : c.textPrimary;
    };
    const auto row = [&](const juce::String& label,
                         const juce::String& value,
                         juce::Colour colour,
                         const std::deque<double>* series = nullptr)
    {
        auto r = area.removeFromTop(rowH);
        g.setFont(theme_.small());
        g.setColour(c.textSecondary);
        g.drawText(label, r.removeFromLeft(labelW), juce::Justification::centredLeft);
        g.setFont(theme_.value());
        g.setColour(colour);
        g.drawText(value, r.removeFromLeft(100), juce::Justification::centredLeft);
        if (series != nullptr)
            drawSparkline(g,
                          r.reduced(theme_.spacing.sm, theme_.spacing.xs)
                              .withWidth(std::min(240, r.getWidth() - theme_.spacing.sm)),
                          *series,
                          colour);
    };
    row("Callback p50", juce::String(last_.loadP50 * 100.0, 1) + "%", colourFor(last_.loadP50), &p50_);
    row("Callback p99", juce::String(last_.loadP99 * 100.0, 1) + "%", colourFor(last_.loadP99), &p99_);
    row("Callback max", juce::String(last_.loadMax * 100.0, 1) + "%", colourFor(last_.loadMax), &max_);
    row("Overruns", juce::String(last_.overruns), last_.overruns > 0 ? c.warning : c.textPrimary);
    row("Dropouts", juce::String(last_.dropouts), last_.dropouts > 0 ? c.warning : c.textPrimary);
    row("Device",
        last_.device.isEmpty() ? juce::String("none")
                               : last_.device + "  " + juce::String(last_.sampleRate / 1000.0, 1) +
                                     " kHz / " + juce::String(last_.bufferSize),
        last_.device.isEmpty() ? c.warning : c.textPrimary);
    row("Worker threads", juce::String(last_.workerThreads), c.textPrimary);
    row("Output latency", juce::String(last_.outputLatencySamples) + " smp", c.textPrimary);
    row("UI frame p50 / p99 / max",
        juce::String(last_.frameP50Ms, 1) + " / " + juce::String(last_.frameP99Ms, 1) + " / " +
            juce::String(last_.frameMaxMs, 1) + " ms",
        last_.frameP99Ms > 16.0 ? c.warning : c.textPrimary);
    row("Paint p50 / p99 / max",
        juce::String(last_.paintP50Ms, 1) + " / " + juce::String(last_.paintP99Ms, 1) + " / " +
            juce::String(last_.paintMaxMs, 1) + " ms",
        last_.paintP99Ms > 16.0 ? c.warning : c.textPrimary);
    if (!last_.trackLatencies.empty())
    {
        area.removeFromTop(theme_.spacing.sm);
        g.setFont(theme_.captionCaps());
        g.setColour(c.textTertiary);
        g.drawText("PLUGIN LATENCY PER TRACK", area.removeFromTop(rowH), juce::Justification::centredLeft);
        for (const auto& [name, latency] : last_.trackLatencies)
            row(name, juce::String(latency) + " smp", c.textPrimary);
    }
}

void DiagnosticsPanel::resized()
{
    auto top = getLocalBounds().reduced(theme_.spacing.md).removeFromTop(theme_.sizes.controlCompact);
    copy_.setBounds(top.removeFromRight(120));
}

} // namespace northlight::ui
