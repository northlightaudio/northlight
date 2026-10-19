#include "hosting/HostedPlugin.h"

#include "engine/rt/RealtimeGuard.h"

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
