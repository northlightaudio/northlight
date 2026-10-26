#include "app/LoadDriver.h"

#include "app/ProjectSession.h"
#include "ui/Theme.h"
#include "ui/components/ProgressRow.h"
#include "ui/components/Sheet.h"

namespace northlight
{

LoadDriver::LoadDriver(Host host) : host_(std::move(host)) {}

LoadDriver::~LoadDriver()
{
    stopTimer();
}

void LoadDriver::watch(const juce::String& title,
                       std::function<void(const std::string&)> onFinished,
                       std::optional<LoadProgress::Stage> holdAt)
{
    finished_ = std::move(onFinished);
    holdAt_ = holdAt;
    held_ = false;
    const auto& theme = host_.theme;
    auto row = std::make_unique<ui::ProgressRow>(theme);
    row_ = row.get();
    ui::SheetOptions options;
    options.blocking = true;
    options.appearAfterMs = holdAt.has_value() ? 0 : theme.motion.progressDelayMs;
    sheet_ = ui::Sheet::show(
        host_.sheetHost,
        theme,
        title,
        {},
        {{"Cancel", false, false, true}},
        [this, title](int)
        {
            // Cancel (or Esc) while the current project is still the old one.
            if (!host_.session.canCancelLoad())
                return;
            host_.session.cancelLoad();
            stopTimer();
            finished_ = nullptr;
            if (host_.cancelled)
                host_.cancelled(title);
        },
        std::move(row),
        ui::ProgressRow::preferredHeight(theme),
        options);
    step();
    if (host_.session.isLoading() && !held_)
        startTimer(stepMs);
}

void LoadDriver::step()
{
    auto& session = host_.session;
    // Slow steps ahead (third-party plugins hold the message thread, so the
    // sheet's delay and entrance could not run between them): bring the sheet
    // in now and take no step until it is fully on screen.
    if (sheet_ != nullptr && !sheet_->hasAppeared() && session.loadHasSlowStepsAhead())
    {
        sheet_->appearNow();
        showProgress();
        return;
    }
    const bool wasCancellable = session.canCancelLoad();
    const auto outcome = session.stepLoad();
    if (outcome.has_value())
    {
        finish(*outcome);
        return;
    }
    if (wasCancellable && !session.canCancelLoad())
    {
        // The new project is the session's now: the window shows it behind the
        // sheet, and the load can no longer be cancelled.
        if (host_.projectSwapped)
            host_.projectSwapped();
        if (sheet_ != nullptr)
            sheet_->setButtonEnabled(0, false);
    }
    const auto progress = showProgress();
    if (holdAt_.has_value() && progress.stage >= *holdAt_)
    {
        held_ = true;
        stopTimer();
    }
    // Paint now: the next step may make a plugin that holds the thread a while.
    if (auto* peer = host_.sheetHost.getPeer())
        peer->performAnyPendingRepaintsNow();
}

LoadProgress LoadDriver::showProgress()
{
    const auto progress = host_.session.loadProgress();
    if (row_ != nullptr)
        row_->setProgress(progress.fraction, juce::String::fromUTF8(describe(progress).c_str()));
    return progress;
}

void LoadDriver::finish(const std::string& error)
{
    stopTimer();
    if (sheet_ != nullptr)
        sheet_->dismiss();
    sheet_ = nullptr;
    auto finished = std::move(finished_);
    finished_ = nullptr;
    if (finished)
        finished(error);
}

} // namespace northlight
