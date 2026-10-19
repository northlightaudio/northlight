#include "hosting/HostedPlugin.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

namespace aural::hosting
{

HostedPlugin::HostedPlugin(std::string typeName, std::unique_ptr<juce::AudioPluginInstance> instance)
    : typeName_(std::move(typeName)), instance_(std::move(instance))
{
}

HostedPlugin::~HostedPlugin()
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::~HostedPlugin");
    if (prepared_.load(std::memory_order_acquire))
        instance_->releaseResources();
}

void HostedPlugin::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::prepare");
    if (prepared_.exchange(false, std::memory_order_acq_rel))
        instance_->releaseResources();

    // The chain is stereo: ask for a stereo main bus each way and leave any other
    // bus (sidechain, auxiliary outputs) disabled. A plugin that refuses keeps its
    // own layout; the processing below only ever touches the first two channels.
    juce::AudioProcessor::BusesLayout layout;
    if (instance_->getBusCount(true) > 0)
        layout.inputBuses.add(juce::AudioChannelSet::stereo());
    for (int i = 1; i < instance_->getBusCount(true); ++i)
        layout.inputBuses.add(juce::AudioChannelSet::disabled());
    if (instance_->getBusCount(false) > 0)
        layout.outputBuses.add(juce::AudioChannelSet::stereo());
    for (int i = 1; i < instance_->getBusCount(false); ++i)
        layout.outputBuses.add(juce::AudioChannelSet::disabled());
    if (!instance_->checkBusesLayoutSupported(layout) || !instance_->setBusesLayout(layout))
        instance_->enableAllBuses();

    instance_->setNonRealtime(false);
    instance_->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    instance_->prepareToPlay(sampleRate, maxBlockSize);
    midi_.ensureSize(1024);
    prepared_.store(true, std::memory_order_release);
}

void HostedPlugin::process(float* left, float* right, int numSamples) noexcept
{
    if (isBypassed() || !prepared_.load(std::memory_order_acquire))
        return;
    const int channels =
        juce::jmax(instance_->getTotalNumInputChannels(), instance_->getTotalNumOutputChannels());
    if (channels <= 0)
        return;
    // Two channels of external data: AudioBuffer keeps the pointers in its own
    // preallocated space, so this is allocation-free. A plugin that ended up with
    // more channels than two (it refused the stereo layout) gets its extra channels
    // pointed at silence it may overwrite.
    if (channels > 2)
        return; // it refused the stereo layout; see prepare()
    float* pointers[2] = {left, right};
    juce::AudioBuffer<float> buffer(pointers, channels, numSamples);
    midi_.clear();
    instance_->processBlock(buffer, midi_);
    if (channels == 1)
        juce::FloatVectorOperations::copy(right, left, numSamples);
}

std::vector<std::string> HostedPlugin::factoryPrograms() const
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::factoryPrograms");
    std::vector<std::string> names;
    const int count = instance_->getNumPrograms();
    if (count <= 1)
        return names; // JUCE reports one unnamed program for plugins without any
    for (int i = 0; i < count; ++i)
        names.push_back(instance_->getProgramName(i).toStdString());
    return names;
}

int HostedPlugin::currentProgram() const
{
    return instance_->getCurrentProgram();
}

void HostedPlugin::setProgram(int index)
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::setProgram");
    if (index >= 0 && index < instance_->getNumPrograms())
        instance_->setCurrentProgram(index);
}

bool HostedPlugin::supportsPresetFiles() const
{
    return const_cast<juce::AudioPluginInstance&>(*instance_).getVST3Client() != nullptr;
}

juce::File HostedPlugin::userPresetFolder() const
{
    const auto description = instance_->getPluginDescription();
    const auto safe = [](const juce::String& s) { return juce::File::createLegalFileName(s).trim(); };
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("VST3 Presets")
        .getChildFile(safe(description.manufacturerName).isEmpty() ? "Unknown"
                                                                   : safe(description.manufacturerName))
        .getChildFile(safe(description.name).isEmpty() ? "Plugin" : safe(description.name));
}

std::vector<juce::File> HostedPlugin::userPresetFiles() const
{
    std::vector<juce::File> files;
    const auto folder = userPresetFolder();
    if (!folder.isDirectory())
        return files;
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, true, "*.vstpreset"))
        files.push_back(f);
    std::sort(files.begin(),
              files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0; });
    return files;
}

bool HostedPlugin::loadPresetFile(const juce::File& file)
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::loadPresetFile");
    auto* client = instance_->getVST3Client();
    if (client == nullptr)
        return false;
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() == 0)
        return false;
    return client->setPreset(data);
}

bool HostedPlugin::savePresetFile(const juce::File& file) const
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::savePresetFile");
    auto* client = const_cast<juce::AudioPluginInstance&>(*instance_).getVST3Client();
    if (client == nullptr)
        return false;
    const auto data = client->getPreset();
    if (data.getSize() == 0)
        return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithData(data.getData(), data.getSize());
}

std::string HostedPlugin::state() const
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::state");
    juce::MemoryBlock block;
    instance_->getStateInformation(block);
    return block.toBase64Encoding().toStdString();
}

void HostedPlugin::setState(const std::string& state)
{
    AURAL_ASSERT_NOT_REALTIME("HostedPlugin::setState");
    if (state.empty())
        return;
    juce::MemoryBlock block;
    if (block.fromBase64Encoding(juce::String(state)) && block.getSize() > 0)
        instance_->setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}

} // namespace aural::hosting
