#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Audio files as the browser and the drop targets see them: the extensions the
// media browser lists, and the description an audio file carries when it is
// dragged inside the app ("file:<path>", as plugins carry "plugin:<type>").

namespace northlight::ui
{

inline const juce::StringArray& audioFileExtensions()
{
    static const juce::StringArray extensions{".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg"};
    return extensions;
}

inline bool isAudioFile(const juce::File& file)
{
    return audioFileExtensions().contains(file.getFileExtension().toLowerCase());
}

inline juce::var fileDragDescription(const juce::File& file)
{
    return "file:" + file.getFullPathName();
}

// The file a drag description names (a non-existent File for anything else).
inline juce::File fileFromDragDescription(const juce::var& description)
{
    const auto text = description.toString();
    if (!text.startsWith("file:") || !juce::File::isAbsolutePath(text.substring(5)))
        return {};
    return juce::File(text.substring(5));
}

} // namespace northlight::ui
