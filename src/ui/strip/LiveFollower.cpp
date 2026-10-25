#include "ui/strip/LiveFollower.h"

#include "ui/strip/StripStyle.h"

#include <algorithm>

namespace northlight::ui::strip
{

void LiveFollower::attach(std::shared_ptr<engine::InsertProcessor> live)
{
    if (live == live_)
        return;
    if (auto* old = feedback())
        old->setWatched(false);
    live_ = std::move(live);
    cursor_ = 0;
    if (auto* fb = feedback())
        cursor_ = fb->framesWritten();
}

void LiveFollower::pause()
{
    if (auto* fb = feedback())
    {
        fb->setWatched(false);
        cursor_ = fb->framesWritten();
    }
}

void LiveFollower::stop()
{
    if (auto* fb = feedback())
    {
        fb->setWatched(false);
        fb->setTapsEnabled(false);
    }
}

bool LiveFollower::read(double elapsedMs, const Theme::Motion& motion)
{
    float in = 0.0f, out = 0.0f, gr = 0.0f, extra = 0.0f;
    int n = 0;
    if (auto* fb = feedback())
    {
        fb->setWatched(true);
        Frame frames[256];
        n = fb->readHistory(cursor_, frames, 256);
        for (int i = 0; i < n; ++i)
        {
            history_.push(frames[i]);
            in = std::max(in, frames[i].input);
            out = std::max(out, frames[i].output);
            gr = std::max(gr, frames[i].reduction);
            extra = std::max(extra, frames[i].extra);
        }
    }
    // The meters' ballistics (ui/MeterBallistics.h): levels rise over the
    // token time and fall at the token rate; gain reduction moves like a
    // compressor's needle, quick in and a third of a second out.
    const auto ballistic = [&](float& shown, float level)
    { shown = followLevel(shown, toDb(level), elapsedMs, motion.meterRiseMs, motion.meterFallDbPerSec); };
    ballistic(inDb_, in);
    ballistic(outDb_, out);
    ballistic(extraDb_, extra);
    grDb_ = followReduction(grDb_, gr, elapsedMs, motion.meterRiseMs * 0.5, 300.0);
    bool readoutsMoved = inReadout_.update(toDb(in), elapsedMs);
    readoutsMoved = outReadout_.update(toDb(out), elapsedMs) || readoutsMoved;
    readoutsMoved = grReadout_.update(gr, elapsedMs) || readoutsMoved;
    return n > 0 || inDb_ > -119.0f || grDb_ > 0.0f || readoutsMoved;
}

} // namespace northlight::ui::strip
