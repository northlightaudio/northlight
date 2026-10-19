#include "engine/Insert.h"

#include "engine/rt/RealtimeGuard.h"

namespace aural::engine
{

void InsertCache::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("InsertCache::prepare");
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    for (auto& [id, instance] : instances_)
        instance->prepare(sampleRate_, maxBlockSize_);
}

std::shared_ptr<InsertProcessor> InsertCache::get(const core::Insert& insert)
{
    AURAL_ASSERT_NOT_REALTIME("InsertCache::get");
    std::shared_ptr<InsertProcessor> instance;
    std::string carriedState; // from an instance being replaced
    if (const auto it = instances_.find(insert.id); it != instances_.end())
    {
        if (it->second->typeName() != insert.type)
            instances_.erase(it);
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
        instance = std::move(created);
        instances_[insert.id] = instance;
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

void InsertCache::clear()
{
    AURAL_ASSERT_NOT_REALTIME("InsertCache::clear");
    instances_.clear();
    latencyAtCompile_.clear();
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
    std::erase_if(instances_, [](const auto& entry) { return entry.second.use_count() == 1; });
    std::erase_if(latencyAtCompile_, [this](const auto& entry) { return !instances_.contains(entry.first); });
}

} // namespace aural::engine
