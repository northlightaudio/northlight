#include "hosting/PluginRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace aural::hosting
{

using json = nlohmann::json;

namespace
{
bool before(const PluginInfo& a, const PluginInfo& b) noexcept
{
    return a.name != b.name ? a.name < b.name : a.id < b.id;
}
} // namespace

const PluginInfo* PluginRegistry::find(const std::string& id) const noexcept
{
    for (const auto& p : plugins_)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool PluginRegistry::isHostedType(const std::string& insertType) noexcept
{
    return insertType.starts_with(typePrefix) || insertType.starts_with(clapPrefix);
}

bool PluginRegistry::isClapType(const std::string& insertType) noexcept
{
    return insertType.starts_with(clapPrefix);
}

const PluginInfo* PluginRegistry::findByType(const std::string& insertType) const noexcept
{
    if (!isHostedType(insertType))
        return nullptr;
    const auto id = insertType.substr(std::char_traits<char>::length(typePrefix)); // both prefixes are 5 long
    const std::string format = isClapType(insertType) ? "CLAP" : "VST3";
    for (const auto& p : plugins_)
        if (p.id == id && p.format == format)
            return &p;
    return nullptr;
}

void PluginRegistry::setFileResults(const std::string& path,
                                    std::int64_t modificationTime,
                                    std::vector<PluginInfo> found)
{
    std::erase_if(plugins_, [&](const PluginInfo& p) { return p.path == path; });
    for (auto& p : found)
    {
        p.path = path;
        plugins_.push_back(std::move(p));
    }
    std::sort(plugins_.begin(), plugins_.end(), before);
    scannedFiles_[path] = modificationTime;
    blocked_.erase(path);
}

void PluginRegistry::removeFile(const std::string& path)
{
    std::erase_if(plugins_, [&](const PluginInfo& p) { return p.path == path; });
    scannedFiles_.erase(path);
    blocked_.erase(path);
}

bool PluginRegistry::needsScan(const std::string& path, std::int64_t modificationTime) const noexcept
{
    if (blocked_.contains(path))
        return false;
    const auto it = scannedFiles_.find(path);
    return it == scannedFiles_.end() || it->second != modificationTime;
}

void PluginRegistry::block(const std::string& path, const std::string& reason)
{
    std::erase_if(plugins_, [&](const PluginInfo& p) { return p.path == path; });
    scannedFiles_.erase(path);
    blocked_[path] = reason;
}

bool PluginRegistry::isBlocked(const std::string& path) const noexcept
{
    return blocked_.contains(path);
}

void PluginRegistry::clearBlocklist()
{
    blocked_.clear();
}

void PluginRegistry::unblock(const std::string& path)
{
    blocked_.erase(path);
    scannedFiles_.erase(path);
}

std::string PluginRegistry::toJson() const
{
    json plugins = json::array();
    for (const auto& p : plugins_)
        plugins.push_back(json{{"format", p.format},
                               {"id", p.id},
                               {"name", p.name},
                               {"manufacturer", p.manufacturer},
                               {"category", p.category},
                               {"version", p.version},
                               {"path", p.path},
                               {"uniqueId", p.uniqueId},
                               {"deprecatedUid", p.deprecatedUid},
                               {"instrument", p.isInstrument},
                               {"inputs", p.numInputs},
                               {"outputs", p.numOutputs}});
    json files = json::object();
    for (const auto& [path, time] : scannedFiles_)
        files[path] = time;
    json blocked = json::object();
    for (const auto& [path, reason] : blocked_)
        blocked[path] = reason;
    const json j{{"format", "aural.plugins"},
                 {"version", 1},
                 {"plugins", std::move(plugins)},
                 {"files", std::move(files)},
                 {"blocked", std::move(blocked)}};
    return j.dump(2);
}

std::optional<PluginRegistry> PluginRegistry::fromJson(const std::string& text, std::string* error)
{
    const auto fail = [error](const std::string& message) -> std::optional<PluginRegistry>
    {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    };
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return fail("not valid JSON");
    if (j.value("format", std::string{}) != "aural.plugins")
        return fail("not a plugin registry");
    if (j.value("version", 0) != 1)
        return fail("unsupported registry version");
    try
    {
        PluginRegistry r;
        if (const auto it = j.find("plugins"); it != j.end() && it->is_array())
            for (const auto& p : *it)
                r.plugins_.push_back(PluginInfo{p.value("format", std::string{"VST3"}),
                                                p.value("id", std::string{}),
                                                p.value("name", std::string{}),
                                                p.value("manufacturer", std::string{}),
                                                p.value("category", std::string{}),
                                                p.value("version", std::string{}),
                                                p.value("path", std::string{}),
                                                p.value("uniqueId", 0),
                                                p.value("deprecatedUid", 0),
                                                p.value("instrument", false),
                                                p.value("inputs", 0),
                                                p.value("outputs", 0)});
        std::sort(r.plugins_.begin(), r.plugins_.end(), before);
        if (const auto it = j.find("files"); it != j.end() && it->is_object())
            for (const auto& [path, time] : it->items())
                if (time.is_number_integer())
                    r.scannedFiles_[path] = time.get<std::int64_t>();
        if (const auto it = j.find("blocked"); it != j.end() && it->is_object())
            for (const auto& [path, reason] : it->items())
                if (reason.is_string())
                    r.blocked_[path] = reason.get<std::string>();
        return r;
    }
    catch (const json::exception& e)
    {
        return fail(std::string("malformed registry: ") + e.what());
    }
}

} // namespace aural::hosting
