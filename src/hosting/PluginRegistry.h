#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// What the app knows about the plugins on this machine: every description the
// scanner found, the files it must never load again (they crashed or hung the
// scanner), and the modification time of each file it has looked at so a rescan
// only touches what changed. Persisted as JSON next to the user settings. JUCE-free
// so it is unit-tested without a message loop.

namespace northlight::hosting
{

struct PluginInfo
{
    std::string format = "VST3"; // "VST3" or "CLAP"
    std::string id;              // the format's identifier string; the insert type is "<format>:" + id
    std::string name;
    std::string manufacturer;
    std::string category;
    std::string version;
    std::string path; // the file or bundle on disk
    int uniqueId = 0; // the format's class id within the file
    int deprecatedUid = 0;
    bool isInstrument = false;
    int numInputs = 0;
    int numOutputs = 0;
    bool operator==(const PluginInfo&) const = default;
};

class PluginRegistry
{
public:
    static constexpr const char* typePrefix = "vst3:";
    static constexpr const char* clapPrefix = "clap:";

    const std::vector<PluginInfo>& plugins() const noexcept { return plugins_; }
    const PluginInfo* find(const std::string& id) const noexcept;
    const PluginInfo* findByType(const std::string& insertType) const noexcept;
    static std::string typeFor(const PluginInfo& info)
    {
        return (info.format == "CLAP" ? clapPrefix : typePrefix) + info.id;
    }
    static bool isClapType(const std::string& insertType) noexcept;
    static bool isHostedType(const std::string& insertType) noexcept;

    // Replaces everything known about a file with what a scan of it found.
    void
    setFileResults(const std::string& path, std::int64_t modificationTime, std::vector<PluginInfo> found);
    // Forgets a file (deleted from disk).
    void removeFile(const std::string& path);
    // Whether a scan of this file is needed: never seen, or changed since.
    bool needsScan(const std::string& path, std::int64_t modificationTime) const noexcept;

    // The blocklist: files the scanner crashed or hung on. Never loaded again
    // until the user clears it.
    void block(const std::string& path, const std::string& reason);
    bool isBlocked(const std::string& path) const noexcept;
    const std::map<std::string, std::string>& blocked() const noexcept { return blocked_; }
    const std::map<std::string, std::int64_t>& scannedFiles() const noexcept { return scannedFiles_; }
    void clearBlocklist();
    // Forgets one blocked file (and its scan record) so the next scan tries it again.
    void unblock(const std::string& path);

    std::string toJson() const;
    static std::optional<PluginRegistry> fromJson(const std::string& text, std::string* error = nullptr);

    bool operator==(const PluginRegistry&) const = default;

private:
    std::vector<PluginInfo> plugins_;                  // sorted by name, then id
    std::map<std::string, std::int64_t> scannedFiles_; // path -> modification time
    std::map<std::string, std::string> blocked_;       // path -> reason
};

} // namespace northlight::hosting
