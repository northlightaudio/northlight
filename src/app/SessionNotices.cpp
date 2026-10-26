#include "app/SessionNotices.h"

#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"

#include <algorithm>

namespace northlight
{

SessionNotices::SessionNotices(Host host) : host_(std::move(host)) {}

void SessionNotices::drain()
{
    for (const auto& notice : host_.session.takeNotices())
        show(notice);
}

void SessionNotices::show(const Notice& notice)
{
    const auto text = juce::String::fromUTF8(notice.text.c_str());
    if (notice.kind == Notice::Kind::status)
    {
        host_.status(text);
        return;
    }
    const auto kind = notice.kind == Notice::Kind::error     ? ui::ToastStack::Kind::error
                      : notice.kind == Notice::Kind::warning ? ui::ToastStack::Kind::warning
                      : notice.kind == Notice::Kind::success ? ui::ToastStack::Kind::success
                                                             : ui::ToastStack::Kind::info;
    if (notice.reveal != juce::File{})
        host_.toast(kind, text, "Show in folder", [file = notice.reveal] { file.revealToUser(); });
    else if (!notice.action.empty() && notice.onAction)
        host_.toast(kind, text, juce::String::fromUTF8(notice.action.c_str()), notice.onAction);
    else if (notice.audioSetup)
        host_.toast(kind, text, "Audio Setup", host_.showAudioSetup);
    else
        host_.toast(kind, text, {}, {});
}

int DropoutCounter::count(const Raw& raw, double nowMs)
{
    const bool restarted = raw.overruns < overrunBaseline_ || raw.driverXruns < xrunBaseline_;
    if (raw.device != device_ || restarted)
    {
        device_ = raw.device;
        settleUntilMs_ = nowMs + settleMs;
    }
    if (nowMs < settleUntilMs_)
    {
        // Settling: what the device does now is not counted. (Disk gaps are the
        // session's and keep counting.)
        xrunBaseline_ = raw.driverXruns;
        overrunBaseline_ = raw.overruns;
    }
    return std::max(0, raw.driverXruns - xrunBaseline_) + static_cast<int>(raw.overruns - overrunBaseline_) +
           diskGaps(raw);
}

int DropoutCounter::diskGaps(const Raw& raw) const
{
    return raw.diskGaps >= diskBaseline_ ? static_cast<int>(raw.diskGaps - diskBaseline_) : 0;
}

void DropoutCounter::clear(const Raw& raw)
{
    xrunBaseline_ = raw.driverXruns;
    overrunBaseline_ = raw.overruns;
    diskBaseline_ = raw.diskGaps;
}

DropoutCounter::Raw SessionNotices::raw() const
{
    const auto s = host_.audio.status();
    DropoutCounter::Raw r;
    if (s.open)
        r.device = s.deviceType + "/" + s.outputDevice + "/" + std::to_string(s.sampleRate) + "/" +
                   std::to_string(s.bufferSize) + (s.running ? "" : "/stopped");
    r.driverXruns = std::max(0, s.driverXruns);
    r.overruns = host_.audio.callback().stats().summary().overruns;
    r.diskGaps = host_.session.diskStarvations();
    return r;
}

int SessionNotices::dropouts()
{
    const auto counters = raw();
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Disk gaps count as dropouts too: they are just as audible.
    const int dropouts = counter_.count(counters, now);
    const int diskGaps = counter_.diskGaps(counters);
    // Dropouts over the threshold: a toast with the fix, at most once a minute.
    // The fix depends on the cause; most of the new ones coming from the disk
    // means a bigger buffer would not help.
    if (dropouts - dropoutsToasted_ >= 10 && now - lastDropoutToastMs_ > 60000.0)
    {
        const bool mostlyDisk = 2 * (diskGaps - diskGapsToasted_) > dropouts - dropoutsToasted_;
        dropoutsToasted_ = dropouts;
        diskGapsToasted_ = diskGaps;
        lastDropoutToastMs_ = now;
        if (mostlyDisk)
            host_.toast(ui::ToastStack::Kind::warning,
                        "Audio dropouts. The disk is not keeping up with the streamed clips. Close programs "
                        "that are using the disk, or move the project to a faster drive.",
                        {},
                        {});
        else
            host_.toast(ui::ToastStack::Kind::warning,
                        "Audio dropouts. The buffer is too small for this project on this device. Raise the "
                        "buffer size in Audio Setup.",
                        "Audio Setup",
                        host_.showAudioSetup);
    }
    return dropouts;
}

void SessionNotices::clearDropouts()
{
    counter_.clear(raw());
    dropoutsToasted_ = 0;
    diskGapsToasted_ = 0;
}

} // namespace northlight
