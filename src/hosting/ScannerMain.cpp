// aural_plugin_scan: loads one plugin file in its own process and prints what it
// contains, so a plugin that crashes or hangs while being inspected takes down
// this process and not the DAW. Output is an XML document of PluginDescriptions:
//
//   aural_plugin_scan <file-or-bundle>
//
// Exit code 0 with "<plugins>...</plugins>" on stdout between the lines
// "<<AURAL-SCAN>>" and "<<END-AURAL-SCAN>>" (so anything a plugin prints while
// loading is ignored); anything else means the file could not be inspected.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: aural_plugin_scan <file>\n");
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juceInit; // VST3 inspection wants a message manager
    juce::VST3PluginFormat format;
    const juce::String path = juce::String::fromUTF8(argv[1]);
    if (!format.fileMightContainThisPluginType(path))
    {
        std::printf("\n<<AURAL-SCAN>>\n<plugins/>\n<<END-AURAL-SCAN>>\n");
        return 0;
    }
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, path);
    juce::XmlElement root("plugins");
    for (const auto* description : found)
        root.addChildElement(description->createXml().release());
    const auto text = root.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
    std::printf("\n<<AURAL-SCAN>>\n%s\n<<END-AURAL-SCAN>>\n", text.toRawUTF8());
    std::fflush(stdout);
    return 0;
}
