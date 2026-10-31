#include "hosting/PluginScanner.h"

#include <cstdlib>
#include <thread>

namespace northlight::hosting
{

PluginScanner::PluginScanner(PluginRegistry& registry) : juce::Thread("Plugin scan"), registry_(registry) {}

PluginScanner::~PluginScanner()
{
    cancel();
    alive_->store(false); // results still queued for the message thread are dropped
}

void PluginScanner::cancel()
{
    // No time limit, so the thread is never killed (docs/decisions.md): an
    // inspection in progress ends its child process within 100 ms.
    stopThread(-1);
}

juce::File PluginScanner::scannerExecutable()
{
    if (const char* env = std::getenv("NORTHLIGHT_PLUGIN_SCANNER"); env != nullptr && *env != '\0')
        return juce::File(juce::String::fromUTF8(env));
    const auto self = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto beside = self.getSiblingFile("northlight_plugin_scan.exe");
    if (beside.existsAsFile())
        return beside;
    return self.getSiblingFile("northlight_plugin_scan");
}

std::int64_t PluginScanner::modificationTime(const juce::File& file)
{
    // A bundle's own time does not change when its binary is replaced; the newest
    // thing inside it does.
    std::int64_t newest = file.getLastModificationTime().toMilliseconds();
    if (file.isDirectory())
        for (const auto& child : file.findChildFiles(juce::File::findFiles, true))
            newest = std::max<std::int64_t>(newest, child.getLastModificationTime().toMilliseconds());
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
                            int timeoutMs,
                            const std::function<bool()>& shouldStop)
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
    // Waited in short slices, so a cancel (quitting mid-scan) ends a slow
    // plugin's inspection at once instead of after the whole timeout.
    bool finished = false, stopped = false;
    for (int waited = 0; !finished && waited < timeoutMs; waited += 100)
    {
        finished = process.waitForProcessToFinish(std::min(100, timeoutMs - waited));
        if (!finished && shouldStop && shouldStop())
        {
            stopped = true;
            break;
        }
    }
    if (!finished)
        process.kill();
    reader.join();
    if (!finished)
    {
        error = stopped ? "stopped" : "timed out";
        return false;
    }
    if (process.getExitCode() != 0)
    {
        error = "the scanner process failed (exit code " + std::to_string(process.getExitCode()) + ")";
        return false;
    }
    // Only the block between the sentinels is the result; a plugin may print
    // anything else while it loads.
    const juce::String begin("<<NORTHLIGHT-SCAN>>"), end("<<END-NORTHLIGHT-SCAN>>");
    const int from = output.indexOf(begin);
    const int to = output.indexOf(end);
    if (from < 0 || to < from)
    {
        error = "the scanner produced no result";
        return false;
    }
    const auto xml = juce::XmlDocument::parse(output.substring(from + begin.length(), to));
    if (xml == nullptr || !xml->hasTagName("plugins"))
    {
        error = "the scanner produced no result";
        return false;
    }
    // A file the scanner could open but that refused to initialise is failed with
    // the reason (and blocklisted), not recorded as holding no plugins.
    if (xml->hasAttribute("error"))
    {
        error = xml->getStringAttribute("error").toStdString();
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
    scanPaths_ = extraPaths_;
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
    for (const auto& extra : scanPaths_)
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
        std::vector<std::int64_t> modified; // per candidate, read on this thread
        std::vector<Job> jobs;
        std::vector<std::string> known; // the registry's files, to look for on this thread
        juce::WaitableEvent ready;
    };
    // Shared with the message thread so a cancel while it is still planning
    // leaves nothing dangling.
    auto planning = std::make_shared<Planning>();
    planning->candidates = format.searchPathsForPlugins(paths, true, false);
    // The CLAP folders. Windows: %COMMONPROGRAMFILES%\CLAP and the per-user one under
    // the local (not roaming) application data. Linux: ~/.clap and /usr/lib/clap.
    // Then every entry of CLAP_PATH (';'-separated on Windows, ':' elsewhere).
    juce::FileSearchPath clapPaths;
#if JUCE_WINDOWS
    const auto commonFiles = juce::SystemStats::getEnvironmentVariable("COMMONPROGRAMFILES", "");
    clapPaths.add(commonFiles.isNotEmpty() ? juce::File(commonFiles).getChildFile("CLAP")
                                           : juce::File("C:\\Program Files\\Common Files\\CLAP"));
    clapPaths.add(juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                      .getChildFile("Programs")
                      .getChildFile("Common")
                      .getChildFile("CLAP"));
    const char* clapPathSeparator = ";";
#else
    clapPaths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".clap"));
    clapPaths.add(juce::File("/usr/lib/clap"));
    const char* clapPathSeparator = ":";
#endif
    for (const auto& entry : juce::StringArray::fromTokens(
             juce::SystemStats::getEnvironmentVariable("CLAP_PATH", ""), clapPathSeparator, "\""))
        if (entry.trim().isNotEmpty())
            clapPaths.add(juce::File(entry.trim()));
    for (const auto& extra : scanPaths_)
        clapPaths.add(juce::File(juce::String::fromUTF8(extra.c_str())));
    for (int i = 0; i < clapPaths.getNumPaths(); ++i)
        for (const auto& file : clapPaths[i].findChildFiles(juce::File::findFiles, true, "*.clap"))
            planning->candidates.addIfNotAlreadyThere(file.getFullPathName());
    // The disk is read here: every candidate's time now, the registry's files below.
    planning->modified.reserve(static_cast<size_t>(planning->candidates.size()));
    for (const auto& path : planning->candidates)
        planning->modified.push_back(modificationTime(juce::File(path)));
    {
        // The registry belongs to the message thread: decide there which files
        // need a look, so the scan thread never touches it.
        const bool rescanAll = rescanAll_;
        juce::MessageManager::callAsync(
            [this, alive = alive_, planning, rescanAll]
            {
                if (!alive->load())
                    return;
                for (int i = 0; i < planning->candidates.size(); ++i)
                {
                    const auto& path = planning->candidates[i];
                    const auto modified = planning->modified[static_cast<size_t>(i)];
                    if (rescanAll ? !registry_.isBlocked(path.toStdString())
                                  : registry_.needsScan(path.toStdString(), modified))
                        planning->jobs.push_back({juce::File(path), modified});
                }
                for (const auto& [path, time] : registry_.scannedFiles())
                    planning->known.push_back(path);
                for (const auto& [path, reason] : registry_.blocked())
                    planning->known.push_back(path);
                planning->ready.signal();
            });
        while (!planning->ready.wait(50))
            if (threadShouldExit())
                return;
    }
    // Files the registry knows that are gone from disk are forgotten.
    {
        std::vector<std::string> gone;
        for (const auto& path : planning->known)
            if (!juce::File(juce::String::fromUTF8(path.c_str())).exists())
                gone.push_back(path);
        if (!gone.empty())
            juce::MessageManager::callAsync(
                [this, alive = alive_, gone = std::move(gone)]
                {
                    if (!alive->load())
                        return;
                    for (const auto& path : gone)
                        registry_.removeFile(path);
                });
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
        const bool ok =
            inspect(scanner, job.file, found, error, 60000, [this] { return threadShouldExit(); });
        if (!ok && threadShouldExit())
            break; // stopped, not a bad plugin: nothing to blocklist
        const auto path = job.file.getFullPathName().toStdString();
        const auto modified = job.modified;
        juce::MessageManager::callAsync(
            [this, alive = alive_, ok, path, modified, found = std::move(found), error]
            {
                if (!alive->load())
                    return;
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
        [this, alive = alive_]
        {
            if (alive->load() && onFinished)
                onFinished();
        });
}

} // namespace northlight::hosting
