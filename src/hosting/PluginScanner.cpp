#include "hosting/PluginScanner.h"

#include <chrono>
#include <cstdlib>

namespace aural::hosting
{

PluginScanner::PluginScanner(PluginRegistry& registry) : juce::Thread("Plugin scan"), registry_(registry) {}

PluginScanner::~PluginScanner()
{
    cancel();
}

void PluginScanner::cancel()
{
    stopThread(5000);
}

juce::File PluginScanner::scannerExecutable()
{
    if (const char* env = std::getenv("AURAL_PLUGIN_SCANNER"); env != nullptr && *env != '\0')
        return juce::File(juce::String::fromUTF8(env));
    const auto self = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto beside = self.getSiblingFile("aural_plugin_scan.exe");
    if (beside.existsAsFile())
        return beside;
    return self.getSiblingFile("aural_plugin_scan");
}

std::int64_t PluginScanner::modificationTime(const juce::File& file)
{
    // A bundle's own time does not change when its binary is replaced; the newest
    // thing inside it does.
    std::int64_t newest = file.getLastModificationTime().toMilliseconds();
    if (file.isDirectory())
        for (const auto& child : file.findChildFiles(juce::File::findFiles, true))
            newest = std::max(newest, child.getLastModificationTime().toMilliseconds());
    return newest;
}

PluginInfo PluginScanner::toInfo(const juce::PluginDescription& d)
{
    PluginInfo info;
    info.id = d.createIdentifierString().toStdString();
    info.name = d.name.toStdString();
    info.manufacturer = d.manufacturerName.toStdString();
    info.category = d.category.toStdString();
    info.version = d.version.toStdString();
    info.path = d.fileOrIdentifier.toStdString();
    info.uniqueId = d.uniqueId;
    info.deprecatedUid = d.deprecatedUid;
    info.isInstrument = d.isInstrument;
    info.numInputs = d.numInputChannels;
    info.numOutputs = d.numOutputChannels;
    return info;
}

bool PluginScanner::inspect(const juce::File& scanner,
                            const juce::File& file,
                            std::vector<PluginInfo>& found,
                            std::string& error,
                            int timeoutMs)
{
    found.clear();
    if (!scanner.existsAsFile())
    {
        error = "scanner executable not found: " + scanner.getFullPathName().toStdString();
        return false;
    }
    juce::ChildProcess process;
    if (!process.start(juce::StringArray{scanner.getFullPathName(), file.getFullPathName()},
                       juce::ChildProcess::wantStdOut))
    {
        error = "could not start the scanner";
        return false;
    }
    // Read while it runs so a chatty plugin cannot fill the pipe and block.
    juce::MemoryOutputStream output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    char chunk[4096];
    for (;;)
    {
        const int got = process.readProcessOutput(chunk, sizeof(chunk));
        if (got > 0)
            output.write(chunk, static_cast<size_t>(got));
        else if (!process.isRunning())
            break;
        else if (std::chrono::steady_clock::now() > deadline)
        {
            process.kill();
            error = "timed out";
            return false;
        }
        else
            juce::Thread::sleep(5);
    }
    if (process.getExitCode() != 0)
    {
        error = "the scanner process failed (exit code " + std::to_string(process.getExitCode()) + ")";
        return false;
    }
    const auto xml = juce::XmlDocument::parse(output.toString());
    if (xml == nullptr || !xml->hasTagName("plugins"))
    {
        error = "the scanner produced no result";
        return false;
    }
    for (const auto* child : xml->getChildIterator())
    {
        juce::PluginDescription description;
        if (description.loadFromXml(*child))
            found.push_back(toInfo(description));
    }
    return true;
}

void PluginScanner::start(bool rescanAll)
{
    if (isThreadRunning())
        return;
    rescanAll_ = rescanAll;
    done_.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    startThread();
}

std::string PluginScanner::status() const
{
    if (!isThreadRunning())
        return {};
    const juce::ScopedLock lock(statusLock_);
    return "Scanning plugins " + std::to_string(done_.load()) + "/" + std::to_string(total_.load()) +
           (current_.empty() ? std::string{} : ": " + current_);
}

void PluginScanner::run()
{
    juce::VST3PluginFormat format;
    juce::FileSearchPath paths = format.getDefaultLocationsToSearch();
    for (const auto& extra : extraPaths_)
        paths.add(juce::File(juce::String::fromUTF8(extra.c_str())));

    // Listing only looks at names; nothing is loaded here.
    struct Job
    {
        juce::File file;
        std::int64_t modified;
    };
    struct Planning
    {
        juce::StringArray candidates;
        std::vector<Job> jobs;
        juce::WaitableEvent ready;
    };
    // Shared with the message thread so a cancel while it is still planning
    // leaves nothing dangling.
    auto planning = std::make_shared<Planning>();
    planning->candidates = format.searchPathsForPlugins(paths, true, false);
    {
        // The registry belongs to the message thread: decide there which files
        // need a look, so the scan thread never touches it.
        const bool rescanAll = rescanAll_;
        juce::MessageManager::callAsync(
            [this, planning, rescanAll]
            {
                for (const auto& path : planning->candidates)
                {
                    const juce::File file(path);
                    const auto modified = modificationTime(file);
                    if (rescanAll ? !registry_.isBlocked(path.toStdString())
                                  : registry_.needsScan(path.toStdString(), modified))
                        planning->jobs.push_back({file, modified});
                }
                planning->ready.signal();
            });
        while (!planning->ready.wait(50))
            if (threadShouldExit())
                return;
    }
    const auto& jobs = planning->jobs;
    total_.store(static_cast<int>(jobs.size()), std::memory_order_relaxed);

    const auto scanner = scannerExecutable();
    for (const auto& job : jobs)
    {
        if (threadShouldExit())
            break;
        {
            const juce::ScopedLock lock(statusLock_);
            current_ = job.file.getFileName().toStdString();
        }
        std::vector<PluginInfo> found;
        std::string error;
        const bool ok = inspect(scanner, job.file, found, error);
        const auto path = job.file.getFullPathName().toStdString();
        const auto modified = job.modified;
        juce::MessageManager::callAsync(
            [this, ok, path, modified, found = std::move(found), error]
            {
                if (ok)
                    registry_.setFileResults(path, modified, found);
                else
                    registry_.block(path, error);
                done_.fetch_add(1, std::memory_order_relaxed);
                if (onProgress)
                    onProgress();
            });
    }
    juce::MessageManager::callAsync(
        [this]
        {
            if (onFinished)
                onFinished();
        });
}

} // namespace aural::hosting
