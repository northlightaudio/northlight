#include "ui/ExportView.h"

#include <cmath>

namespace northlight::ui
{

namespace
{
constexpr int rowCount = 8;
constexpr int summaryLines = 2;
constexpr int rangeProject = 1, rangeLoop = 2, rangeRegionBase = 10;

juce::String rateText(double rate)
{
    const double k = rate / 1000.0;
    return juce::String(k, std::abs(k - std::round(k)) < 1e-6 ? 0 : 1) + " kHz";
}

juce::String lufsText(double lufs)
{
    juce::String text = juce::String(lufs, 0) + " LUFS";
    if (lufs == -14.0)
        text << "  (streaming)";
    else if (lufs == -16.0)
        text << "  (Apple Music, podcasts)";
    else if (lufs == -23.0)
        text << "  (EBU R128 broadcast)";
    return text;
}
} // namespace

ExportView::ExportView(const Theme& theme, const engine::ExportSettings& initial, Context context)
    : theme_(theme), context_(std::move(context)), settings_(initial.validated()), source_(theme),
      format_(theme), peakMode_(theme)
{
    for (auto* box : {&range_, &depth_, &rate_, &dither_, &loudness_, &ceiling_})
    {
        addAndMakeVisible(*box);
        box->onChange = [this] { update(); };
    }
    for (auto* control : {&source_, &format_, &peakMode_})
        addAndMakeVisible(*control);

    source_.setSegments({"Mix", "Every track", "Selected tracks"});
    source_.setSelected(static_cast<int>(settings_.source), juce::dontSendNotification);
    source_.onChange = [this](int) { update(); };
    source_.setTitle("Export");

    format_.setSegments({"WAV", "AIFF", "FLAC", "Ogg Vorbis"});
    format_.setSelected(static_cast<int>(settings_.format), juce::dontSendNotification);
    format_.onChange = [this](int index)
    {
        // Keep the chosen depth across formats where it exists.
        settings_ = settings();
        settings_.format = static_cast<engine::ExportFormat>(index);
        fillRows();
        update();
    };
    format_.setTitle("Format");

    peakMode_.setSegments({"True peak", "Sample peak"});
    peakMode_.setSelected(settings_.peakMode == engine::PeakMode::truePeak ? 0 : 1,
                          juce::dontSendNotification);
    peakMode_.onChange = [this](int) { update(); };
    peakMode_.setTitle("Peak measurement");

    range_.setTitle("Range");
    range_.addItem("Whole project", rangeProject);
    range_.addItem("Loop region", rangeLoop);
    range_.setItemEnabled(rangeLoop, context_.hasLoop);
    for (size_t i = 0; i < context_.regions.size(); ++i)
        range_.addItem("Region: " + context_.regions[i], rangeRegionBase + static_cast<int>(i));
    range_.setSelectedId(settings_.range == engine::ExportRange::loop && context_.hasLoop ? rangeLoop
                                                                                          : rangeProject,
                         juce::dontSendNotification);

    rate_.setTitle("Sample rate");
    rate_.addItem("Project rate (" + rateText(context_.projectRate) + ")", 1);
    const auto& rates = engine::exportSampleRates();
    for (size_t i = 0; i < rates.size(); ++i)
    {
        rate_.addItem(rateText(rates[i]), 2 + static_cast<int>(i));
        if (rates[i] == settings_.sampleRate)
            rate_.setSelectedId(2 + static_cast<int>(i), juce::dontSendNotification);
    }
    if (rate_.getSelectedId() == 0)
        rate_.setSelectedId(1, juce::dontSendNotification);

    dither_.setTitle("Dither");
    for (const auto mode : {engine::DitherMode::off, engine::DitherMode::tpdf, engine::DitherMode::shaped})
        dither_.addItem(engine::ditherName(mode), static_cast<int>(mode) + 1);
    dither_.setSelectedId(static_cast<int>(settings_.dither) + 1, juce::dontSendNotification);

    loudness_.setTitle("Loudness normalisation");
    loudness_.addItem("Off", 1);
    const auto& targets = engine::loudnessTargets();
    for (size_t i = 0; i < targets.size(); ++i)
    {
        loudness_.addItem(lufsText(targets[i]), 2 + static_cast<int>(i));
        if (settings_.normalise && targets[i] == settings_.targetLufs)
            loudness_.setSelectedId(2 + static_cast<int>(i), juce::dontSendNotification);
    }
    if (loudness_.getSelectedId() == 0)
        loudness_.setSelectedId(1, juce::dontSendNotification);

    ceiling_.setTitle("Peak ceiling");
    const auto& ceilings = engine::peakCeilings();
    for (size_t i = 0; i < ceilings.size(); ++i)
    {
        ceiling_.addItem(juce::String(ceilings[i], 1) + " dB", 1 + static_cast<int>(i));
        if (ceilings[i] == settings_.ceilingDb)
            ceiling_.setSelectedId(1 + static_cast<int>(i), juce::dontSendNotification);
    }
    if (ceiling_.getSelectedId() == 0)
        ceiling_.setSelectedId(4, juce::dontSendNotification); // -1 dB

    fillRows();
    update();
}

int ExportView::preferredHeight(const Theme& theme)
{
    return rowCount * (theme.sizes.control + theme.spacing.sm) +
           summaryLines * static_cast<int>(std::ceil(theme.fonts.size.small * theme.fonts.lineHeight));
}

void ExportView::fillRows()
{
    depth_.clear(juce::dontSendNotification);
    if (settings_.format == engine::ExportFormat::ogg)
    {
        depth_.setTitle("Quality");
        for (int i = 0; i < context_.oggQualities.size(); ++i)
            depth_.addItem(context_.oggQualities[i], i + 1);
        depth_.setSelectedId(settings_.oggQuality + 1, juce::dontSendNotification);
        depths_.clear();
        return;
    }
    depth_.setTitle("Bit depth");
    depths_ = engine::depthsFor(settings_.format);
    const auto wanted = engine::depthFor(settings_.format, settings_.depth);
    for (size_t i = 0; i < depths_.size(); ++i)
    {
        depth_.addItem(engine::depthName(depths_[i]), static_cast<int>(i) + 1);
        if (depths_[i] == wanted)
            depth_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }
}

engine::ExportSettings ExportView::settings() const
{
    engine::ExportSettings s = settings_;
    s.source = static_cast<engine::ExportSource>(source_.selected());
    s.format = static_cast<engine::ExportFormat>(format_.selected());
    const int range = range_.getSelectedId();
    s.range = range == rangeLoop         ? engine::ExportRange::loop
              : range >= rangeRegionBase ? engine::ExportRange::region
                                         : engine::ExportRange::project;
    const int depth = depth_.getSelectedId() - 1;
    if (s.format == engine::ExportFormat::ogg)
    {
        if (depth >= 0)
            s.oggQuality = depth;
    }
    else if (depth >= 0 && static_cast<size_t>(depth) < depths_.size())
        s.depth = depths_[static_cast<size_t>(depth)];
    const auto& rates = engine::exportSampleRates();
    const int rate = rate_.getSelectedId() - 2;
    s.sampleRate =
        rate >= 0 && static_cast<size_t>(rate) < rates.size() ? rates[static_cast<size_t>(rate)] : 0;
    s.dither = static_cast<engine::DitherMode>(std::max(0, dither_.getSelectedId() - 1));
    const auto& targets = engine::loudnessTargets();
    const int target = loudness_.getSelectedId() - 2;
    s.normalise = target >= 0 && static_cast<size_t>(target) < targets.size();
    if (s.normalise)
        s.targetLufs = targets[static_cast<size_t>(target)];
    const auto& ceilings = engine::peakCeilings();
    const int ceiling = ceiling_.getSelectedId() - 1;
    if (ceiling >= 0 && static_cast<size_t>(ceiling) < ceilings.size())
        s.ceilingDb = ceilings[static_cast<size_t>(ceiling)];
    s.peakMode = peakMode_.selected() == 0 ? engine::PeakMode::truePeak : engine::PeakMode::samplePeak;
    return s;
}

void ExportView::update()
{
    const auto s = settings();
    region_ = std::max(0, range_.getSelectedId() - rangeRegionBase);
    const bool lossy = engine::isLossy(s.format);
    dither_.setEnabled(!lossy && engine::isInteger(engine::depthFor(s.format, s.depth)));
    ceiling_.setEnabled(s.normalise);
    peakMode_.setEnabled(s.normalise);
    repaint();
}

juce::String ExportView::summary() const
{
    const auto s = settings();
    const double rate = engine::outputRate(s, context_.projectRate);
    juce::String text;
    switch (s.source)
    {
        case engine::ExportSource::mix:
            text = "One file of the mix";
            break;
        case engine::ExportSource::stems:
            text = "One file per track";
            break;
        case engine::ExportSource::selectedStems:
            text = context_.selectedTracks == 0
                       ? juce::String("No tracks are selected")
                       : "One file for each of the " + juce::String(context_.selectedTracks) + " selected " +
                             (context_.selectedTracks == 1 ? "track" : "tracks");
            break;
    }
    text << ": ";
    if (engine::isLossy(s.format))
        text << engine::formatName(s.format) << " at " << context_.oggQualities[s.oggQuality];
    else
        text << engine::depthName(engine::depthFor(s.format, s.depth)) << " " << engine::formatName(s.format);
    text << ", " << rateText(rate);
    if (rate != context_.projectRate)
        text << " (converted)";
    if (engine::ditherApplies(s))
        text << (s.dither == engine::DitherMode::shaped ? ", noise-shaped TPDF dither" : ", TPDF dither");
    text << ".";
    if (s.normalise)
    {
        text << "\nNormalised to " << juce::String(s.targetLufs, 0) << " LUFS, peaks at most "
             << juce::String(s.ceilingDb, 1)
             << (s.peakMode == engine::PeakMode::truePeak ? " dBTP" : " dBFS");
        text << (s.source == engine::ExportSource::mix ? juce::String(".")
                                                       : juce::String("; every stem gets the mix's gain."));
    }
    return text;
}

void ExportView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const int row = theme_.sizes.control + theme_.spacing.sm;
    const auto depthLabel = settings_.format == engine::ExportFormat::ogg ? "Quality" : "Bit depth";
    const char* labels[rowCount] = {
        "Export", "Range", "Format", depthLabel, "Sample rate", "Dither", "Loudness", "Ceiling"};
    g.setFont(theme_.body());
    for (int i = 0; i < rowCount; ++i)
    {
        const bool enabled = (i != 5 || dither_.isEnabled()) && (i != 7 || ceiling_.isEnabled());
        g.setColour(enabled ? c.textSecondary : c.textDisabled);
        g.drawText(labels[i],
                   0,
                   i * row,
                   theme_.sizes.labelWidth,
                   theme_.sizes.control,
                   juce::Justification::centredLeft);
    }
    g.setFont(theme_.small());
    g.setColour(c.textTertiary);
    const auto note = getLocalBounds().withTrimmedTop(rowCount * row);
    g.drawFittedText(summary(), note, juce::Justification::topLeft, summaryLines);
}

void ExportView::resized()
{
    auto area = getLocalBounds();
    const auto next = [&]
    {
        auto r = area.removeFromTop(theme_.sizes.control + theme_.spacing.sm);
        return r.withTrimmedBottom(theme_.spacing.sm).withTrimmedLeft(theme_.sizes.labelWidth);
    };
    source_.setBounds(next());
    range_.setBounds(next());
    format_.setBounds(next());
    depth_.setBounds(next());
    rate_.setBounds(next());
    dither_.setBounds(next());
    loudness_.setBounds(next());
    auto ceiling = next();
    ceiling_.setBounds(ceiling.removeFromLeft(ceiling.getWidth() / 3));
    ceiling.removeFromLeft(theme_.spacing.sm);
    peakMode_.setBounds(ceiling);
}

} // namespace northlight::ui
