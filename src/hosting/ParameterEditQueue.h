#pragma once

#include "engine/Insert.h"
#include "engine/rt/MpscQueue.h"

#include <cstddef>

namespace northlight::hosting
{

// The edits a hosted plugin made itself (its editor, its output events) on their
// way to the message thread, which hands them to the session as commands. Any
// thread may push: a VST3 parameter listener fires on whichever thread the plugin
// changed the value on (the audio thread included), a CLAP plugin's edits come
// from process() or a flush. Pushing never allocates, locks or waits; a full
// queue drops the edit. The message thread pops.
class ParameterEditQueue
{
public:
    using Edit = engine::InsertProcessor::ParameterEdit;
    static constexpr std::size_t defaultCapacity = 4096;

    explicit ParameterEditQueue(std::size_t capacity = defaultCapacity) : queue_(capacity) {}

    bool push(const Edit& edit) noexcept { return queue_.push(edit); }
    bool pop(Edit& edit) noexcept { return queue_.pop(edit); }

private:
    rt::MpscQueue<Edit> queue_;
};

} // namespace northlight::hosting
