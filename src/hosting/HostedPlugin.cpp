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

    // The chain is stereo: ask for a stereo main bus each way, a stereo second
    // input bus when the plugin has one (its sidechain), and leave any other bus
    // (auxiliary outputs) disabled. A plugin that refuses keeps its own layout;
    // the processing below only ever touches the first two channels of each.
    const auto tryLayout = [&](bool withSidechain)
    {
        juce::AudioProcessor::BusesLayout layout;
        for (int b = 0; b < instance_->getBusCount(true); ++b)
            layout.inputBuses.add(b == 0 || (b == 1 && withSidechain) ? juce::AudioChannelSet::stereo()
                                                                      : juce::AudioChannelSet::disabled());
        for (int b = 0; b < instance_->getBusCount(false); ++b)
            layout.outputBuses.add(b == 0 ? juce::AudioChannelSet::stereo()
                                          : juce::AudioChannelSet::disabled());
        return instance_->checkBusesLayoutSupported(layout) && instance_->setBusesLayout(layout);
    };
    bool sidechain = false;
    if (instance_->getBusCount(true) >= 2 && tryLayout(true))
        sidechain = true;
    else if (!tryLayout(false))
        instance_->enableAllBuses();
    // Only a layout of exactly main stereo plus stereo key is driven as a sidechain.
    sidechain = sidechain && instance_->getTotalNumInputChannels() == 4 &&
                instance_->getTotalNumOutputChannels() == 2;

    instance_->setNonRealtime(false);
    instance_->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    instance_->prepareToPlay(sampleRate, maxBlockSize);
    midi_.ensureSize(1024);
    keyLeft_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    keyRight_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    sidechain_.store(sidechain, std::memory_order_release);
    prepared_.store(true, std::memory_order_release);
}

void HostedPlugin::process(float* left, float* right, int numSamples) noexcept
{
    run(left, right, nullptr, nullptr, numSamples);
}

void HostedPlugin::processWithSidechain(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    run(left, right, keyLeft, keyRight, numSamples);
}

void HostedPlugin::run(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    if (isBypassed() || !prepared_.load(std::memory_order_acquire))
        return;
    if (sidechain_.load(std::memory_order_acquire) && numSamples <= static_cast<int>(keyLeft_.size()))
    {
        // Main pair plus the key pair; the key is silence when no source is set.
        if (keyLeft != nullptr && keyRight != nullptr)
        {
            std::copy(keyLeft, keyLeft + numSamples, keyLeft_.data());
            std::copy(keyRight, keyRight + numSamples, keyRight_.data());
        }
        else
        {
            std::fill(keyLeft_.begin(), keyLeft_.begin() + numSamples, 0.0f);
            std::fill(keyRight_.begin(), keyRight_.begin() + numSamples, 0.0f);
        }
        float* pointers[4] = {left, right, keyLeft_.data(), keyRight_.data()};
        juce::AudioBuffer<float> buffer(pointers, 4, numSamples);
        midi_.clear();
        instance_->processBlock(buffer, midi_);
        return;
    }
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
