#include "hosting/HostedPlugin.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cstdint>

namespace aural::hosting
{

HostedPlugin::Layout
HostedPlugin::negotiateLayout(juce::AudioPluginInstance& instance, bool& sidechain, int& auxOutputs)
{
    const auto tryLayout = [&](const juce::AudioChannelSet& main, bool withSidechain, int aux)
    {
        juce::AudioProcessor::BusesLayout layout;
        for (int b = 0; b < instance.getBusCount(true); ++b)
            layout.inputBuses.add(b == 0                      ? main
                                  : (b == 1 && withSidechain) ? juce::AudioChannelSet::stereo()
                                                              : juce::AudioChannelSet::disabled());
        for (int b = 0; b < instance.getBusCount(false); ++b)
            layout.outputBuses.add(b == 0     ? main
                                   : b <= aux ? juce::AudioChannelSet::stereo()
                                              : juce::AudioChannelSet::disabled());
        return instance.checkBusesLayoutSupported(layout) && instance.setBusesLayout(layout);
    };
    sidechain = false;
    auxOutputs = 0;
    if (instance.getBusCount(true) >= 2 && tryLayout(juce::AudioChannelSet::stereo(), true, 0) &&
        instance.getTotalNumInputChannels() == 4 && instance.getTotalNumOutputChannels() == 2)
    {
        sidechain = true;
        return Layout::stereo;
    }
    // An instrument: as many further stereo output buses as it accepts, so a
    // multi-output instrument's pairs can feed aux tracks.
    if (instance.getBusCount(true) == 0 && instance.getBusCount(false) >= 2)
        for (int aux = instance.getBusCount(false) - 1; aux >= 1; --aux)
            if (tryLayout(juce::AudioChannelSet::stereo(), false, aux) &&
                instance.getTotalNumOutputChannels() == 2 + 2 * aux)
            {
                auxOutputs = aux;
                return Layout::stereo;
            }
    if (tryLayout(juce::AudioChannelSet::stereo(), false, 0) && instance.getTotalNumInputChannels() <= 2 &&
        instance.getTotalNumOutputChannels() == 2)
        return Layout::stereo;
    if (tryLayout(juce::AudioChannelSet::mono(), false, 0) && instance.getTotalNumInputChannels() <= 1 &&
        instance.getTotalNumOutputChannels() == 1)
        return Layout::mono;
    return Layout::unsupported;
}

HostedPlugin::HostedPlugin(std::string typeName,
                           std::unique_ptr<juce::AudioPluginInstance> instance,
                           Layout layout,
                           bool sidechain,
                           int auxOutputs)
    : typeName_(std::move(typeName)), instance_(std::move(instance)), layout_(layout), auxOutputs_(auxOutputs)
{
    sidechain_.store(sidechain, std::memory_order_release);
    acceptsMidi_ = instance_->acceptsMidi();
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

    // The layout was negotiated when the instance was created (see negotiateLayout).
    instance_->setNonRealtime(false);
    instance_->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    instance_->prepareToPlay(sampleRate, maxBlockSize);
    midi_.ensureSize(1024);
    keyLeft_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    keyRight_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    mono_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    aux_.assign(static_cast<size_t>(auxOutputs_) * 2,
                std::vector<float>(static_cast<size_t>(maxBlockSize), 0.0f));
    pointers_.assign(2 + aux_.size(), nullptr);
    for (size_t i = 0; i < aux_.size(); ++i)
        pointers_[2 + i] = aux_[i].data();
    dryLeft_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    dryRight_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    bypassStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.010));
    bypassMix_ = isBypassed() ? 1.0f : 0.0f;
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

void HostedPlugin::processMidi(const engine::MidiEventList& events,
                               float* left,
                               float* right,
                               int numSamples) noexcept
{
    pendingMidi_ = &events;
    run(left, right, nullptr, nullptr, numSamples);
    pendingMidi_ = nullptr;
}

void HostedPlugin::processBuffer(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept
{
    midi_.clear();
    if (pendingMidi_ != nullptr)
        for (const auto& e : *pendingMidi_)
        {
            // Raw bytes, so no MidiMessage is built (the buffer was sized in prepare).
            std::uint8_t bytes[3] = {0, e.note, e.velocity};
            switch (e.type)
            {
                case engine::MidiEvent::Type::noteOn:
                    bytes[0] = static_cast<std::uint8_t>(0x90 | (e.channel & 15));
                    break;
                case engine::MidiEvent::Type::noteOff:
                    bytes[0] = static_cast<std::uint8_t>(0x80 | (e.channel & 15));
                    break;
                case engine::MidiEvent::Type::controller:
                    bytes[0] = static_cast<std::uint8_t>(0xB0 | (e.channel & 15));
                    break;
                case engine::MidiEvent::Type::allNotesOff:
                    bytes[0] = static_cast<std::uint8_t>(0xB0 | (e.channel & 15));
                    bytes[1] = 123;
                    bytes[2] = 0;
                    break;
            }
            midi_.addEvent(bytes, 3, e.sampleOffset);
        }
    if (bypassed)
        instance_->processBlockBypassed(buffer, midi_); // the plugin's own bypass keeps its latency
    else
        instance_->processBlock(buffer, midi_);
}

void HostedPlugin::run(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    if (!prepared_.load(std::memory_order_acquire) || layout_ == Layout::unsupported ||
        numSamples > static_cast<int>(dryLeft_.size()))
        return;

    // Bypass: ramp between the processed and the bypassed signal over 10 ms. While
    // the ramp runs the plugin processes normally and the input (through the
    // plugin's own bypassed path once settled) is faded in; settled bypass runs
    // processBlockBypassed only, so the latency and the tail stay consistent.
    const float target = isBypassed() ? 1.0f : 0.0f;
    const bool settled = bypassMix_ == target;
    if (settled && target == 1.0f)
    {
        // Fully bypassed.
        if (layout_ == Layout::mono)
        {
            for (int i = 0; i < numSamples; ++i)
                mono_[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
            float* pointers[1] = {mono_.data()};
            juce::AudioBuffer<float> buffer(pointers, 1, numSamples);
            processBuffer(buffer, true);
            std::copy(mono_.begin(), mono_.begin() + numSamples, left);
            std::copy(mono_.begin(), mono_.begin() + numSamples, right);
            return;
        }
        if (sidechain_.load(std::memory_order_acquire))
        {
            std::fill(keyLeft_.begin(), keyLeft_.begin() + numSamples, 0.0f);
            std::fill(keyRight_.begin(), keyRight_.begin() + numSamples, 0.0f);
            float* pointers[4] = {left, right, keyLeft_.data(), keyRight_.data()};
            juce::AudioBuffer<float> buffer(pointers, 4, numSamples);
            processBuffer(buffer, true);
            return;
        }
        pointers_[0] = left;
        pointers_[1] = right;
        juce::AudioBuffer<float> buffer(pointers_.data(), static_cast<int>(pointers_.size()), numSamples);
        processBuffer(buffer, true);
        return;
    }

    if (!settled)
    {
        std::copy(left, left + numSamples, dryLeft_.data());
        std::copy(right, right + numSamples, dryRight_.data());
    }

    if (layout_ == Layout::mono)
    {
        for (int i = 0; i < numSamples; ++i)
            mono_[static_cast<size_t>(i)] = 0.5f * (left[i] + right[i]);
        float* pointers[1] = {mono_.data()};
        juce::AudioBuffer<float> buffer(pointers, 1, numSamples);
        processBuffer(buffer, false);
        std::copy(mono_.begin(), mono_.begin() + numSamples, left);
        std::copy(mono_.begin(), mono_.begin() + numSamples, right);
    }
    else if (sidechain_.load(std::memory_order_acquire))
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
        processBuffer(buffer, false);
    }
    else
    {
        // The main pair, then the aux pairs of a multi-output instrument.
        pointers_[0] = left;
        pointers_[1] = right;
        juce::AudioBuffer<float> buffer(pointers_.data(), static_cast<int>(pointers_.size()), numSamples);
        processBuffer(buffer, false);
    }

    if (!settled)
    {
        // Crossfade towards the target: mix 1 is the dry input (the plugin's
        // bypassed path takes over once the ramp has settled).
        for (int i = 0; i < numSamples; ++i)
        {
            bypassMix_ = target > bypassMix_ ? std::min(target, bypassMix_ + bypassStep_)
                                             : std::max(target, bypassMix_ - bypassStep_);
            left[i] = left[i] * (1.0f - bypassMix_) + dryLeft_[static_cast<size_t>(i)] * bypassMix_;
            right[i] = right[i] * (1.0f - bypassMix_) + dryRight_[static_cast<size_t>(i)] * bypassMix_;
        }
    }
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
