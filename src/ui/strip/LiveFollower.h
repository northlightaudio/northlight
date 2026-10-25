#pragma once

#include "engine/Insert.h"
#include "ui/MeterBallistics.h"
#include "ui/Theme.h"
#include "ui/strip/FeedbackHistory.h"

#include <cstdint>
#include <memory>

namespace northlight::ui::strip
{

// What a card follows of its live processor (ModuleCard's): the engine's
// instance, its feedback frames read into the history, and the meters' readings
// with their ballistics. Reading marks the processor watched, so it measures
// only while a card shows it.
class LiveFollower
{
public:
    using Frame = engine::ProcessorFeedback::Frame;

    explicit LiveFollower(int historyCapacity) : history_(historyCapacity) {}
    ~LiveFollower() { stop(); }
    LiveFollower(const LiveFollower&) = delete;
    LiveFollower& operator=(const LiveFollower&) = delete;

    // The engine's instance now (null before it has one): a new one is read from
    // its newest frame on, and the old one is no longer watched.
    void attach(std::shared_ptr<engine::InsertProcessor> live);
    engine::InsertProcessor* processor() const noexcept { return live_.get(); }
    engine::ProcessorFeedback* feedback() const noexcept
    {
        return live_ != nullptr ? live_->feedback() : nullptr;
    }

    // Not shown for a while (a collapsed card): the processor stops measuring
    // and the frames it writes meanwhile are skipped.
    void pause();
    // Hidden: the processor stops measuring and its taps stop.
    void stop();
    // Reads the new frames into the history and moves the meters `elapsedMs` on;
    // true when the displays have something to show (frames came, or a meter or
    // readout is still moving).
    bool read(double elapsedMs, const Theme::Motion& motion);

    const FeedbackHistory<Frame>& history() const noexcept { return history_; }

    // Meter readings in dB, with ballistics.
    float inputDb() const noexcept { return inDb_; }
    float outputDb() const noexcept { return outDb_; }
    float reductionDb() const noexcept { return grDb_; }
    float extraDb() const noexcept { return extraDb_; }
    // The loudest of each quarter second, for a printed value.
    float inputReadoutDb() const noexcept { return inReadout_.value(); }
    float outputReadoutDb() const noexcept { return outReadout_.value(); }
    float reductionReadoutDb() const noexcept { return grReadout_.value(); }

private:
    std::shared_ptr<engine::InsertProcessor> live_;
    std::uint64_t cursor_ = 0;
    FeedbackHistory<Frame> history_;
    float inDb_ = -120.0f, outDb_ = -120.0f, grDb_ = 0.0f, extraDb_ = -120.0f;
    MeterReadout inReadout_, outReadout_, grReadout_{0.0f};
};

} // namespace northlight::ui::strip
