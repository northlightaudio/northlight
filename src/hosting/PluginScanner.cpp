#include "hosting/PluginScanner.h"

#include <cstdlib>
#include <thread>

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
    // ChildProcess reads block until the child exits (or the request is filled),
    // so the output is drained on a helper thread while this one waits with the
    // deadline; a kill on timeout ends the child and unblocks the reader. The
    // reader also keeps a chatty plugin from filling the pipe and stalling.
    juce::String output;
    std::thread reader([&process, &output] { output = process.readAllProcessOutput(); });
    const bool finished = process.waitForProcessToFinish(timeoutMs);
    if (!finished)
        process.kill();
    reader.join();
    if (!finished)
    {
        error = "timed out";
        return false;
    }
    if (process.getExitCode() != 0)
    {
        error = "the scanner process failed (exit code " + std::to_string(process.getExitCode()) + ")";
        return false;
    }
    // Only the block between the sentinels is the result; a plugin may print
    // anything else while it loads.
    const int from = output.indexOf("<<AURAL-SCAN>>");
    const int to = output.indexOf("<<END-AURAL-SCAN>>");
    if (from < 0 || to < from)
    {
        error = "the scanner produced no result";
        return false;
    }
    const auto xml = juce::XmlDocument::parse(output.substring(from + 14, to));
    if (xml == nullptr || !xml->hasTagName("plugins"))
    {
        error = "the scanner produced no result";
        return false;
    }
    for (const auto* child : xml->getChildIterator())
    {
        if (child->hasTagName("CLAP"))
        {
            PluginInfo info;
            info.format = "CLAP";
            info.id = child->getStringAttribute("id").toStdString();
            info.name = child->getStringAttribute("name").toStdString();
            info.manufacturer = child->getStringAttribute("vendor").toStdString();
            info.category = child->getStringAttribute("category").toStdString();
            info.version = child->getStringAttribute("version").toStdString();
            info.path = child->getStringAttribute("file").toStdString();
            info.isInstrument = child->getIntAttribute("instrument") != 0;
            info.numInputs = 2;
            info.numOutputs = 2;
            found.push_back(info);
            continue;
        }
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

    // Listing only looks at names; nothing is loaded here. CLAP files live in the
    // common CLAP folders (and the extra paths); they are scanned by the same
    // process.
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
    juce::FileSearchPath clapPaths;
    clapPaths.add(
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("CLAP"));
    clapPaths.add(juce::File("C:\\Program Files\\Common Files\\CLAP"));
    clapPaths.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("Programs")
                      .getChildFile("Common")
                      .getChildFile("CLAP"));
    for (const auto& extra : extraPaths_)
        clapPaths.add(juce::File(juce::String::fromUTF8(extra.c_str())));
    for (int i = 0; i < clapPaths.getNumPaths(); ++i)
        for (const auto& file : clapPaths[i].findChildFiles(juce::File::findFiles, true, "*.clap"))
            planning->candidates.addIfNotAlreadyThere(file.getFullPathName());
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
                // Files the registry knows that are gone from disk are forgotten.
                std::vector<std::string> gone;
                for (const auto& [path, time] : registry_.scannedFiles())
                    if (!juce::File(juce::String::fromUTF8(path.c_str())).exists())
                        gone.push_back(path);
                for (const auto& [path, reason] : registry_.blocked())
                    if (!juce::File(juce::String::fromUTF8(path.c_str())).exists())
                        gone.push_back(path);
                for (const auto& path : gone)
                    registry_.removeFile(path);
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
