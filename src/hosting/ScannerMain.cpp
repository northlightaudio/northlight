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

#include <clap/clap.h>

#include <cstdio>
#include <cstring>

namespace
{
// A .clap is a DLL with a clap_entry: list its factory's descriptors as
// <CLAP .../> elements of the same document.
bool scanClap(const juce::String& path, juce::XmlElement& root)
{
    juce::DynamicLibrary library;
    if (!library.open(path))
    {
        root.setAttribute("error", "the library could not be loaded");
        return false;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(library.getFunction("clap_entry"));
    if (entry == nullptr)
    {
        root.setAttribute("error", "not a CLAP plugin (no clap_entry)");
        return false;
    }
    if (!clap_version_is_compatible(entry->clap_version))
    {
        root.setAttribute("error", "incompatible CLAP version");
        return false;
    }
    if (!entry->init(path.toRawUTF8()))
    {
        root.setAttribute("error", "the plugin's entry point refused to initialise");
        return false;
    }
    if (const auto* factory =
            static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID)))
    {
        const uint32_t count = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* d = factory->get_plugin_descriptor(factory, i);
            if (d == nullptr || d->id == nullptr)
                continue;
            auto* e = root.createNewChildElement("CLAP");
            e->setAttribute("id", d->id);
            e->setAttribute("name", d->name != nullptr ? d->name : d->id);
            e->setAttribute("vendor", d->vendor != nullptr ? d->vendor : "");
            e->setAttribute("version", d->version != nullptr ? d->version : "");
            e->setAttribute("file", path);
            juce::String category;
            bool instrument = false;
            for (const char* const* f = d->features; f != nullptr && *f != nullptr; ++f)
            {
                if (std::strcmp(*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0)
                    instrument = true;
                category << (category.isEmpty() ? "" : "|") << *f;
            }
            e->setAttribute("category", category);
            e->setAttribute("instrument", instrument ? 1 : 0);
        }
    }
    entry->deinit();
    return true;
}
} // namespace

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
    juce::XmlElement root("plugins");
    if (path.endsWithIgnoreCase(".clap"))
    {
        scanClap(path, root);
    }
    else if (format.fileMightContainThisPluginType(path))
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile(found, path);
        for (const auto* description : found)
            root.addChildElement(description->createXml().release());
    }
    const auto text = root.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
    std::printf("\n<<AURAL-SCAN>>\n%s\n<<END-AURAL-SCAN>>\n", text.toRawUTF8());
    std::fflush(stdout);
    return 0;
}
