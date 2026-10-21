#pragma once

#include <juce_core/juce_core.h>

// Northlight was called Aural. The first time Northlight starts on a computer that
// ran Aural, this brings the old per-user files across:
//
// - %APPDATA%\Aural (settings, plugin list, autosaves, crash reports) is copied to
//   %APPDATA%\Northlight, with Aural.settings renamed to Northlight.settings and
//   the built-in processor names in it ("aural.eq") updated.
// - Documents\Aural\Templates and Documents\Aural\Strip Presets are copied into
//   Documents\Northlight. Recordings stay where they are: projects refer to them by
//   their full paths.
//
// Nothing is moved or deleted, and nothing is copied over an existing Northlight
// folder, so it runs once and is safe to run again. Project files keep working
// without migration: the loader reads the old format name and processor names.

namespace northlight::migration
{

struct Result
{
    bool settingsCopied = false;
    juce::StringArray documentsCopied; // the Documents subfolders copied
};

// `appData` and `documents` are the per-user folders (the real ones when omitted).
Result fromAural(const juce::File& appData = {}, const juce::File& documents = {});

// The settings file's text with every built-in processor's old name replaced.
juce::String migrateSettingsText(const juce::String& text);

} // namespace northlight::migration
