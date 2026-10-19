#include "audio/MidiDeviceLayer.h"

#include "engine/AudioEngine.h"

#include <algorithm>

namespace aural::audio
{

MidiDeviceLayer::MidiDeviceLayer() = default;

MidiDeviceLayer::~MidiDeviceLayer()
{
    shutdown();
}

void MidiDeviceLayer::setEngine(engine::AudioEngine* engine) noexcept
{
    engine_.store(engine, std::memory_order_release);
}

void MidiDeviceLayer::initialise(const std::optional<std::vector<std::string>>& enabledInputs,
                                 const std::string& output)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    enabledInputs_ = enabledInputs;
    outputName_ = output;
    initialised_ = true;
    connection_ = juce::MidiDeviceListConnection::make([this] { refresh(); });
    refresh();
}

void MidiDeviceLayer::shutdown()
{
    if (!initialised_)
        return;
    connection_ = {};
    std::vector<OpenInput> closing;
    {
        const std::lock_guard<std::mutex> lock(openMutex_);
        closing.swap(open_);
    }
    for (auto& input : closing)
        input.device->stop();
    closing.clear();
    output_.reset();
    initialised_ = false;
}

bool MidiDeviceLayer::isEnabled(const std::string& name) const
{
    if (!enabledInputs_.has_value())
        return true;
    return std::find(enabledInputs_->begin(), enabledInputs_->end(), name) != enabledInputs_->end();
}

std::vector<MidiDeviceLayer::Device> MidiDeviceLayer::inputs() const
{
    std::vector<Device> out;
    const std::lock_guard<std::mutex> lock(openMutex_);
    for (const auto& info : juce::MidiInput::getAvailableDevices())
    {
        Device d;
        d.name = info.name.toStdString();
        d.identifier = info.identifier.toStdString();
        d.enabled = isEnabled(d.name);
        d.open =
            std::any_of(open_.begin(), open_.end(), [&](const OpenInput& o) { return o.name == d.name; });
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<MidiDeviceLayer::Device> MidiDeviceLayer::outputs() const
{
    std::vector<Device> out;
    for (const auto& info : juce::MidiOutput::getAvailableDevices())
    {
        Device d;
        d.name = info.name.toStdString();
        d.identifier = info.identifier.toStdString();
        d.enabled = d.name == outputName_;
        d.open = d.enabled && output_ != nullptr;
        out.push_back(std::move(d));
    }
    return out;
}

std::vector<std::string> MidiDeviceLayer::openInputNames() const
{
    std::vector<std::string> names;
    const std::lock_guard<std::mutex> lock(openMutex_);
    for (const auto& o : open_)
        names.push_back(o.name);
    return names;
}

void MidiDeviceLayer::setInputEnabled(const std::string& name, bool enabled)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (!enabledInputs_.has_value())
    {
        // "All" becomes an explicit list of what is available now.
        enabledInputs_.emplace();
        for (const auto& info : juce::MidiInput::getAvailableDevices())
            enabledInputs_->push_back(info.name.toStdString());
    }
    auto& list = *enabledInputs_;
    const auto it = std::find(list.begin(), list.end(), name);
    if (enabled && it == list.end())
        list.push_back(name);
    else if (!enabled && it != list.end())
        list.erase(it);
    refresh();
    if (onChanged)
        onChanged();
}

void MidiDeviceLayer::setOutput(const std::string& name)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (name == outputName_)
        return;
    outputName_ = name;
    refresh();
    if (onChanged)
        onChanged();
}

void MidiDeviceLayer::refresh()
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (!initialised_)
        return;
    openEnabledInputs();

    // The output: open the chosen one if it is present, drop it when it is gone.
    const auto available = juce::MidiOutput::getAvailableDevices();
    const auto* wanted =
        std::find_if(available.begin(),
                     available.end(),
                     [&](const juce::MidiDeviceInfo& i) { return i.name.toStdString() == outputName_; });
    if (wanted == available.end() || outputName_.empty())
        output_.reset();
    else if (output_ == nullptr || output_->getIdentifier() != wanted->identifier)
        output_ = juce::MidiOutput::openDevice(wanted->identifier);
    sendChangeMessage();
}

void MidiDeviceLayer::openEnabledInputs()
{
    const auto available = juce::MidiInput::getAvailableDevices();
    std::vector<OpenInput> closing;
    {
        const std::lock_guard<std::mutex> lock(openMutex_);
        // Close what vanished or was disabled.
        for (auto it = open_.begin(); it != open_.end();)
        {
            const bool present = std::any_of(available.begin(),
                                             available.end(),
                                             [&](const juce::MidiDeviceInfo& i)
                                             { return i.identifier == it->device->getIdentifier(); });
            if (!present || !isEnabled(it->name))
            {
                closing.push_back(std::move(*it));
                it = open_.erase(it);
            }
            else
                ++it;
        }
    }
    for (auto& input : closing)
        input.device->stop();
    closing.clear();

    // Open what is enabled and not open yet (device creation outside the lock:
    // the callback could otherwise wait on it).
    std::vector<OpenInput> opened;
    for (const auto& info : available)
    {
        const auto name = info.name.toStdString();
        if (!isEnabled(name))
            continue;
        bool already = false;
        {
            const std::lock_guard<std::mutex> lock(openMutex_);
            already =
                std::any_of(open_.begin(),
                            open_.end(),
                            [&](const OpenInput& o) { return o.device->getIdentifier() == info.identifier; });
        }
        if (already)
            continue;
        if (auto device = juce::MidiInput::openDevice(info.identifier, this))
            opened.push_back(OpenInput{std::move(device), name});
    }
    {
        const std::lock_guard<std::mutex> lock(openMutex_);
        for (auto& o : opened)
            open_.push_back(std::move(o));
    }
    // Start after they are listed, so the callback can find the name.
    const std::lock_guard<std::mutex> lock(openMutex_);
    for (auto& o : open_)
        o.device->start();
}

void MidiDeviceLayer::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    auto* engine = engine_.load(std::memory_order_acquire);
    if (engine == nullptr)
        return;
    engine::MidiEvent event;
    event.sampleOffset = 0;
    event.channel = static_cast<std::uint8_t>(std::clamp(message.getChannel() - 1, 0, 15));
    if (message.isNoteOn(false))
    {
        event.type = engine::MidiEvent::Type::noteOn;
        event.note = static_cast<std::uint8_t>(message.getNoteNumber());
        event.velocity = static_cast<std::uint8_t>(message.getVelocity());
    }
    else if (message.isNoteOff(true))
    {
        event.type = engine::MidiEvent::Type::noteOff;
        event.note = static_cast<std::uint8_t>(message.getNoteNumber());
        event.velocity = static_cast<std::uint8_t>(message.getVelocity());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        event.type = engine::MidiEvent::Type::allNotesOff;
    }
    else if (message.isController())
    {
        event.type = engine::MidiEvent::Type::controller;
        event.note = static_cast<std::uint8_t>(message.getControllerNumber());
        event.velocity = static_cast<std::uint8_t>(message.getControllerValue());
    }
    else
        return;

    const std::lock_guard<std::mutex> lock(openMutex_);
    for (const auto& o : open_)
        if (o.device.get() == source)
        {
            engine->pushMidiInput(event, o.name);
            return;
        }
    engine->pushMidiInput(event, {});
}

} // namespace aural::audio
