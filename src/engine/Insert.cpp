#include "engine/Insert.h"

#include "engine/rt/RealtimeGuard.h"

#include <cctype>

namespace northlight::engine
{

namespace
{
// A crashed processor is never freed: the plugin's memory may be damaged and its
// destructor is its own code. Kept (leaked on purpose) until the process ends.
void keepIfCrashed(const std::shared_ptr<InsertProcessor>& instance)
{
    static auto* kept = new std::vector<std::shared_ptr<InsertProcessor>>();
    if (instance != nullptr && instance->crashed())
        kept->push_back(instance);
}
} // namespace

std::string InsertProcessor::parameterKey(int index) const
{
    const auto& infos = parameters();
    if (index < 0 || static_cast<size_t>(index) >= infos.size())
        return {};
    const auto& info = infos[static_cast<size_t>(index)];
    if (!info.id.empty())
        return info.id;
    // From the name: lower case, runs of anything else become one '-'.
    std::string key;
    for (const char ch : info.name)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0)
            key += static_cast<char>(std::tolower(c));
        else if (!key.empty() && key.back() != '-')
            key += '-';
    }
    while (!key.empty() && key.back() == '-')
        key.pop_back();
    return key;
}

void InsertCache::prepare(double sampleRate, int maxBlockSize)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("InsertCache::prepare");
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    for (auto& [id, instance] : instances_)
        instance->prepare(sampleRate_, maxBlockSize_);
}

std::shared_ptr<InsertProcessor> InsertCache::get(const core::Insert& insert)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("InsertCache::get");
    std::shared_ptr<InsertProcessor> instance;
    std::string carriedState; // from an instance being replaced
    if (const auto it = instances_.find(insert.id); it != instances_.end())
    {
        if (it->second->typeName() != insert.type)
        {
            keepIfCrashed(it->second);
            instances_.erase(it);
        }
        else if (it->second->needsRestart())
        {
            // The plugin asked for a restart: a fresh instance with its current
            // state replaces it; the old one lives on in the snapshots that hold it.
            carriedState = it->second->state();
            instances_.erase(it);
        }
        else
            instance = it->second;
    }
    const bool created_now = instance == nullptr;
    if (instance == nullptr)
    {
        auto created = factory_.create(insert.type);
        if (created == nullptr)
            return nullptr;
        created->prepare(sampleRate_, maxBlockSize_);
        const auto& state = !carriedState.empty() ? carriedState : insert.state;
        if (!state.empty())
            created->setState(state);
        appliedState_[insert.id] = state;
        instance = std::move(created);
        instances_[insert.id] = instance;
    }
    else if (!instance->ownsParameters())
    {
        // A built-in's state is the model's (the Sampler's file): a command that
        // changed it reaches the live instance here.
        auto& applied = appliedState_[insert.id];
        if (applied != insert.state)
        {
            instance->setState(insert.state);
            applied = insert.state;
        }
    }
    // A processor that owns its parameters keeps what its state says; the model's
    // list only reaches it through commands (applyLiveValues) and on first creation
    // of one with no state to restore.
    if (!instance->ownsParameters() || (created_now && carriedState.empty() && insert.state.empty()))
    {
        const auto count = static_cast<int>(instance->parameters().size());
        for (int i = 0; i < count && static_cast<size_t>(i) < insert.params.size(); ++i)
            instance->setParameter(i, insert.params[static_cast<size_t>(i)]);
    }
    instance->setBypassed(insert.bypass);
    latencyAtCompile_[insert.id] = instance->latencySamples();
    return instance;
}

std::shared_ptr<InsertProcessor> InsertCache::find(core::Id insertId) const
{
    const auto it = instances_.find(insertId);
    return it != instances_.end() ? it->second : nullptr;
}

void InsertCache::forget(core::Id insertId)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("InsertCache::forget");
    const auto it = instances_.find(insertId);
    if (it == instances_.end())
        return;
    keepIfCrashed(it->second);
    instances_.erase(it);
    latencyAtCompile_.erase(insertId);
    appliedState_.erase(insertId);
}

void InsertCache::clear()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("InsertCache::clear");
    for (const auto& [id, instance] : instances_)
        keepIfCrashed(instance);
    instances_.clear();
    latencyAtCompile_.clear();
    appliedState_.clear();
}

bool InsertCache::restartRequested() const noexcept
{
    for (const auto& [id, instance] : instances_)
        if (instance->needsRestart())
            return true;
    return false;
}

bool InsertCache::latencyChanged() const noexcept
{
    for (const auto& [id, instance] : instances_)
        if (const auto it = latencyAtCompile_.find(id);
            it != latencyAtCompile_.end() && it->second != instance->latencySamples())
            return true;
    return false;
}

void InsertCache::prune()
{
    std::erase_if(instances_,
                  [](const auto& entry)
                  {
                      if (entry.second.use_count() != 1)
                          return false;
                      keepIfCrashed(entry.second);
                      return true;
                  });
    std::erase_if(latencyAtCompile_, [this](const auto& entry) { return !instances_.contains(entry.first); });
    std::erase_if(appliedState_, [this](const auto& entry) { return !instances_.contains(entry.first); });
}

} // namespace northlight::engine
