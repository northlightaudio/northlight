#include "app/ExportController.h"

#include "app/Bounce.h"
#include "app/ProjectSession.h"
#include "app/UserSettings.h"
#include "ui/ExportView.h"
#include "ui/Theme.h"
#include "ui/components/Sheet.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>

namespace northlight
{

ExportController::ExportController(Host host) : host_(std::move(host)) {}

ExportController::~ExportController() = default;

engine::ExportSettings ExportController::lastSettings() const
{
    std::map<std::string, std::string> values;
    for (const auto& [key, fallback] : engine::ExportSettings{}.toKeyValues())
        if (auto value = host_.settings.value(key); !value.empty())
            values[key] = value;
    return engine::ExportSettings::fromKeyValues(values);
}

void ExportController::showSheet()
{
    auto& session = host_.session;
    if (host_.openSheet != nullptr || session.isBouncing())
        return;
    ui::ExportView::Context context;
    context.hasLoop = session.regionEnd() > session.regionStart();
    std::vector<core::Marker> regions;
    for (const auto& marker : session.project().markers)
        if (marker.isRegion())
        {
            regions.push_back(marker);
            context.regions.push_back(juce::String::fromUTF8(marker.name.c_str()));
        }
    context.selectedTracks = static_cast<int>(host_.selectedTracks().size());
    context.projectRate = session.project().sampleRate;
    context.oggQualities = juce::OggVorbisAudioFormat().getQualityOptions();
    auto view = std::make_unique<ui::ExportView>(host_.theme, lastSettings(), std::move(context));
    auto* raw = view.get();
    host_.openSheet = ui::Sheet::show(
        host_.sheetHost,
        host_.theme,
        "Export",
        "Renders offline, faster than real time, with the plugins as they are now.",
        {{"Cancel", false, false}, {"Export...", true, false}},
        [this, raw, regions](int result)
        {
            host_.openSheet = nullptr;
            if (result != 1)
                return;
            const auto chosen = raw->settings();
            for (const auto& [key, value] : chosen.toKeyValues())
                host_.settings.setValue(key, value);
            std::optional<core::Marker> region;
            if (chosen.range == engine::ExportRange::region &&
                static_cast<size_t>(raw->region()) < regions.size())
                region = regions[static_cast<size_t>(raw->region())];
            start(chosen, region);
        },
        std::move(view),
        ui::ExportView::preferredHeight(host_.theme));
}

void ExportController::start(const engine::ExportSettings& chosen, std::optional<core::Marker> region)
{
    auto& session = host_.session;
    const auto report = [this](const std::string& error)
    {
        if (!error.empty() && host_.failed)
            host_.failed("Export: " + juce::String::fromUTF8(error.c_str()));
        if (host_.refresh)
            host_.refresh();
    };
    BounceJob::Options options;
    options.format = chosen;
    options.stems = chosen.source != engine::ExportSource::mix;
    if (chosen.source == engine::ExportSource::selectedStems)
    {
        options.stemTracks = host_.selectedTracks();
        if (options.stemTracks.empty())
        {
            report("no tracks are selected");
            return;
        }
    }
    juce::String suffix;
    if (chosen.range == engine::ExportRange::loop && session.regionEnd() > session.regionStart())
    {
        options.start = session.regionStart();
        options.end = session.regionEnd();
        suffix = " loop";
    }
    else if (region.has_value())
    {
        options.start = region->position;
        options.end = region->end();
        suffix = " " + juce::File::createLegalFileName(juce::String::fromUTF8(region->name.c_str())).trim();
    }
    const auto documents = host_.folder();
    const auto base = host_.projectName() + suffix;
    const auto extension = juce::String(engine::extensionFor(chosen.format));
    if (options.stems)
    {
        chooser_ =
            std::make_unique<juce::FileChooser>("Stems folder", documents.getChildFile(base + " Stems"));
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectDirectories,
                              [this, report, options](const juce::FileChooser& fc) mutable
                              {
                                  const auto folder = fc.getResult();
                                  if (folder == juce::File())
                                      return;
                                  options.output = folder;
                                  report(host_.session.startBounce(options));
                              });
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>(
        "Export mix", documents.getChildFile(base + "." + extension), "*." + extension);
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, report, options, extension](const juce::FileChooser& fc) mutable
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (!file.hasFileExtension(extension))
                                  file = file.withFileExtension(extension);
                              options.output = file;
                              report(host_.session.startBounce(options));
                          });
}

} // namespace northlight
