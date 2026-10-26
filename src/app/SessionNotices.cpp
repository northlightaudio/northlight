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
    else
        host_.toast(kind, text, {}, {});
}

int SessionNotices::dropouts()
{
    const auto s = host_.audio.status();
    const auto stats = host_.audio.callback().stats().summary();
    // Disk gaps count as dropouts too: they are just as audible.
    const auto diskGaps = static_cast<int>(host_.session.diskStarvations() - diskBaseline_);
    const int dropouts = std::max(0, std::max(0, s.driverXruns) - dropoutBaseline_) +
                         static_cast<int>(std::max<std::int64_t>(
                             0, static_cast<std::int64_t>(stats.overruns) - overrunBaseline_)) +
                         diskGaps;
    // Dropouts over the threshold: a toast with the fix, at most once a minute.
    // The fix depends on the cause; most of the new ones coming from the disk
    // means a bigger buffer would not help.
    const double now = juce::Time::getMillisecondCounterHiRes();
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
    dropoutBaseline_ = std::max(0, host_.audio.status().driverXruns);
    overrunBaseline_ = static_cast<std::int64_t>(host_.audio.callback().stats().summary().overruns);
    diskBaseline_ = host_.session.diskStarvations();
    dropoutsToasted_ = 0;
    diskGapsToasted_ = 0;
}

} // namespace northlight
