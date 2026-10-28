#include "CrashReports.h"
#include "MainWindow.h"
#include "Migration.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "app/LaunchRun.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "core/Comping.h"
#include "core/ProjectJson.h"
#include "core/Settings.h"
#include "core/TempoMap.h"
#include "engine/Gain.h"
#include "engine/OutputTap.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "plugins/StripPresets.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PianoRoll.h"
#include "ui/PreferencesView.h"
#include "ui/SplashScreen.h"
#include "ui/strip/ModuleCard.h"
#include "version/Version.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace northlight
{

class NorthlightApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    // The full version with its commit (version/Version.h): what crash reports and
    // bug reports need. The About box and splash show version::display().
    const juce::String getApplicationVersion() override { return juce::String(version::full()); }
    // Instances: a launch with developer flags (--capture, --offscreen, ...) is
    // always an instance of its own. Any other launch takes part in forwarding
    // (decisions.md): the first holds JUCE's instance lock and listens, and a
    // later launch with a project file (Explorer's Open with) hands the file to
    // it and quits, so the project opens in the window already there. A later
    // launch without one still opens a window of its own.
    bool moreThanOneInstanceAllowed() override { return true; } // forwarding is done in initialiseApp

    bool initialiseApp() override
    {
        const auto commandLine = getCommandLineParameters();
        if (!hasDeveloperFlags(commandLine))
        {
            // Takes the lock when this is the first instance, else tells the first
            // one this command line (it ignores one without a project).
            const bool another = sendCommandLineToPreexistingInstance();
            if (another && launchProjectFile(commandLine) != juce::File())
                return false; // the running instance opens it
            primaryInstance_ = !another;
        }
        // JUCEApplication's own step only adds the macOS menu bar.
        return JUCEApplicationBase::initialiseApp();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        // Every instance that took part hears the broadcast; only the one holding
        // the lock (the first) answers it.
        if (mainWindow_ == nullptr || !primaryInstance_)
            return;
        if (const auto file = launchProjectFile(commandLine); file != juce::File())
            mainWindow_->openFromShell(file);
    }

    // Whether a flag is on the command line as its own argument (not part of a
    // longer flag such as --capture-scale, or of a path).
    static bool hasFlag(const juce::String& commandLine, const char* flag)
    {
        for (const auto& arg : juce::ArgumentList("Northlight", commandLine).arguments)
            if (arg == flag)
                return true;
        return false;
    }

    void initialise(const juce::String& commandLine) override
    {
        // --capture keeps its settings in a folder of its own, emptied each run, so a
        // capture always shows the default layout and the built-in plugins, and leaves
        // the user's settings alone.
        if (hasFlag(commandLine, "--capture"))
        {
            capture_ = true;
            // --capture-plugins: the user's scanned plugins, so a capture can show them.
            const auto userPlugins = UserSettings().file().getSiblingFile("plugins.json");
            UserSettings::useFolder("Northlight Capture");
            const auto folder = UserSettings().file().getParentDirectory();
            if (folder.getFileName() == "Northlight Capture")
                folder.deleteRecursively();
            if (hasFlag(commandLine, "--capture-plugins") && userPlugins.existsAsFile() &&
                folder.createDirectory())
                userPlugins.copyFileTo(folder.getChildFile("plugins.json"));
        }
        // The very first launch (no settings file yet) gets the device sheet.
        // Northlight was called Aural: bring its settings and folders across once.
        migration::fromAural();
        firstRun_ = !UserSettings().file().existsAsFile();
        settings_ = std::make_unique<UserSettings>();
        // A crash from here on leaves a report in the settings folder.
        crash::install(settings_->file().getSiblingFile("Crashes").getFullPathName().toWideCharPointer(),
                       getApplicationVersion().toStdString());
        // The design tokens: shipped beside the app, with the user's tokens.json and
        // the accent and contrast Preferences chose beside the settings file on top.
        // High contrast when chosen, or when it follows Windows and a contrast theme is on.
        const settings::Reader read([this](const std::string& key) { return settings_->value(key); });
        ui::Theme::initialise(settings_->file().getSiblingFile(ui::Theme::userOverrideName),
                              settings_->file().getSiblingFile(ui::Theme::preferencesOverrideName),
                              settings::followsOrChooses(
                                  read.text(settings::contrast), "high", ui::Theme::systemHighContrast()));
        lookAndFeel_ = std::make_unique<ui::NorthlightLookAndFeel>(ui::Theme::dark());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        // The splash, once the theme is loaded; it reports each step below.
        // --offscreen: no window of any kind reaches the screen (no splash either).
        ui::offscreenMode() = hasFlag(commandLine, "--offscreen");
        juce::Component::SafePointer<ui::SplashScreen> splash;
        if (!hasFlag(commandLine, "--no-splash") && !ui::offscreenMode())
            splash = ui::SplashScreen::showStartup(ui::Theme::dark(),
                                                   juce::String::fromUTF8(version::display().c_str()));
        const auto step = [&splash](const juce::String& text, float progress)
        {
            if (splash != nullptr)
                splash->setStatus(text, progress);
        };

        step("Preparing the audio engine", 0.25f);
        audio_ = std::make_unique<audio::AudioDeviceLayer>(*settings_);
        if (capture_)
            audio_->callback().setOutputMuted(true); // a capture plays but is not heard
        step("Loading the plugin list", 0.45f);
        session_ =
            std::make_unique<ProjectSession>(*audio_, settings_->file().getSiblingFile("plugins.json"));
        session_->setAutosave(settings_->file().getSiblingFile("Autosave"),
                              [this]
                              {
                                  const settings::Reader read([this](const std::string& key)
                                                              { return settings_->value(key); });
                                  return read.integer(settings::autosaveMinutes) * 60;
                              });
        step("Opening the main window", 0.65f);
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_, *session_, *settings_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint. The session is already attached to the callback.
        step("Opening the audio device", 0.85f);
        audio_->initialise();
        if (splash != nullptr)
            splash->finish();

        // Recordings interrupted by a crash are offered back as tracks.
        mainWindow_->offerRecovery();

        // The developer launch flags (LaunchRun.cpp has the table; --help prints it).
        launch_ = std::make_unique<LaunchRun>(*session_, *mainWindow_, *audio_);
        launch_->apply(commandLine);
        if ((firstRun_ && !capture_) || hasFlag(commandLine, "--first-run"))
            mainWindow_->showFirstRun();

        // A project passed by Explorer, else (when Preferences say so) the last one.
        if (const auto file = launchProjectFile(commandLine); file != juce::File())
            mainWindow_->openFromShell(file);
        else if (!hasDeveloperFlags(commandLine) && !firstRun_)
            if (settings::Reader([this](const std::string& key) { return settings_->value(key); })
                    .flag(settings::reopenLast))
                if (const auto last = settings_->value("recent.0"); !last.empty())
                    if (const juce::File lastFile(juce::String::fromUTF8(last.c_str()));
                        lastFile.existsAsFile())
                        mainWindow_->openWithProgress(lastFile);
    }

    void shutdown() override
    {
        // Nothing a launch flag scheduled may run once the window starts going.
        if (launch_ != nullptr)
            launch_->cancel();
        // A developer run (launch flags: a capture, a benchmark) ends without the
        // question and its edits are not the user's: no autosave is left to offer.
        if (session_ != nullptr && hasDeveloperFlags(getCommandLineParameters()))
            session_->discardChanges();
        mainWindow_.reset();
        audio_->shutdown(); // stop the device before the engine goes away
        if (auto tap = launch_ != nullptr ? launch_->takeOutputTap() : nullptr)
        {
            // The device is stopped: no callback can reach the tap any more.
            audio_->callback().setOutputTap(nullptr);
            tap->stop();
        }
        launch_.reset();
        session_.reset();
        audio_.reset();
        settings_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    // The app menu's Quit, and Windows ending the session (WM_QUERYENDSESSION):
    // the window asks about unsaved changes first. While the sheet waits, Windows
    // shows Northlight as keeping it from shutting down; if the user forces it,
    // the autosave written before the sheet came up is offered at the next launch.
    void systemRequestedQuit() override
    {
        if (mainWindow_ != nullptr)
            mainWindow_->closeButtonPressed();
        else
            quit();
    }

private:
    std::unique_ptr<ui::NorthlightLookAndFeel> lookAndFeel_;
    bool firstRun_ = false;
    bool capture_ = false;         // --capture: its own settings folder, no first-run sheet
    bool primaryInstance_ = false; // holds JUCE's instance lock: hears other launches' files
    std::unique_ptr<UserSettings> settings_;
    std::unique_ptr<audio::AudioDeviceLayer> audio_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<LaunchRun> launch_; // the launch flags and what they scheduled
};

} // namespace northlight

START_JUCE_APPLICATION(northlight::NorthlightApplication)
