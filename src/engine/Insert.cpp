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
    if (const auto it = instances_.find(insert.id); it != instances_.end())
    {
        if (it->second->typeName() == insert.type)
            instance = it->second;
        else
            instances_.erase(it);
    }
    if (instance == nullptr)
    {
        auto created = factory_.create(insert.type);
        if (created == nullptr)
            return nullptr;
        created->prepare(sampleRate_, maxBlockSize_);
        if (!insert.state.empty())
            created->setState(insert.state);
        instance = std::move(created);
        instances_[insert.id] = instance;
    }
    const auto count = static_cast<int>(instance->parameters().size());
    for (int i = 0; i < count && static_cast<size_t>(i) < insert.params.size(); ++i)
        instance->setParameter(i, insert.params[static_cast<size_t>(i)]);
    instance->setBypassed(insert.bypass);
    return instance;
}

std::shared_ptr<InsertProcessor> InsertCache::find(core::Id insertId) const
{
    const auto it = instances_.find(insertId);
    return it != instances_.end() ? it->second : nullptr;
}

void InsertCache::prune()
{
    std::erase_if(instances_, [](const auto& entry) { return entry.second.use_count() == 1; });
}

} // namespace aural::engine
