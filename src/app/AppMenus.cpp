#include "app/AppMenus.h"

#include "ui/Shortcuts.h"
#include "ui/timeline/SnapGrids.h"

namespace northlight::menus
{

namespace
{
// Adds items with their actions; ids only number them (JUCE needs one per item).
class Builder
{
public:
    explicit Builder(juce::PopupMenu& menu, int& nextId) : menu_(menu), nextId_(nextId) {}

    void add(const juce::String& text, std::function<void()> action, bool enabled = true, bool ticked = false)
    {
        juce::PopupMenu::Item item(text);
        item.itemID = nextId_++;
        item.isEnabled = enabled && action != nullptr;
        item.isTicked = ticked;
        item.action = std::move(action);
        menu_.addItem(std::move(item));
    }
    // An item with its key from the keyboard map (the current binding, so a
    // rebinding shows the next time the menu opens).
    void add(const juce::String& text, std::function<void()> action, ui::Command key, bool enabled = true)
    {
        juce::PopupMenu::Item item(text);
        item.itemID = nextId_++;
        item.isEnabled = enabled && action != nullptr;
        item.action = std::move(action);
        item.shortcutKeyDescription = ui::Shortcuts::keyText(key);
        menu_.addItem(std::move(item));
    }

private:
    juce::PopupMenu& menu_;
    int& nextId_;
};

// `f` bound to its argument, or nothing when there is no `f`.
template <typename F, typename T> std::function<void()> bind(const F& f, T value)
{
    if (!f)
        return {};
    return [f, value] { f(value); };
}
} // namespace

juce::PopupMenu appMenu(const AppMenu& a)
{
    int id = 1;
    juce::PopupMenu menu;
    Builder items(menu, id);
    items.add("About Northlight", a.about);
    menu.addSeparator();
    items.add("Audio Setup...", a.audioSetup, ui::Command::audioSetup);
    items.add("Plugins...", a.plugins, ui::Command::plugins);
    items.add("Close all plugin windows",
              a.closePluginWindows,
              ui::Command::closePluginWindows,
              a.pluginWindowsOpen);
    items.add("Preferences...", a.preferences, ui::Command::preferences);
    juce::PopupMenu layout;
    Builder layouts(layout, id);
    layouts.add("Edit", bind(a.layout, juce::String("Edit")));
    layouts.add("Mix", bind(a.layout, juce::String("Mix")));
    layout.addSeparator();
    layouts.add("Reset layout", a.resetLayout);
    menu.addSubMenu("Layout", layout);
    menu.addSeparator();
    items.add("Quit", a.quit);
    return menu;
}

juce::PopupMenu projectMenu(const ProjectMenu& m)
{
    int id = 1;
    juce::PopupMenu menu;
    Builder items(menu, id);
    items.add("New", m.newProject, ui::Command::newProject);
    juce::PopupMenu fromTemplate;
    Builder templates(fromTemplate, id);
    for (const auto& file : m.templates)
        templates.add(file.getFileNameWithoutExtension(), bind(m.newFromTemplate, file));
    menu.addSubMenu("New from template", fromTemplate, !m.templates.empty());
    items.add("Open...", m.open, ui::Command::open);
    juce::PopupMenu recent;
    Builder recents(recent, id);
    // A file that has gone (moved, deleted, a drive not connected) stays listed,
    // disabled, with "Not found" as its hint; Clear recent empties the list.
    for (const auto& file : m.recent)
        recents.add(file.existsAsFile() ? file.getFileNameWithoutExtension()
                                        : file.getFileNameWithoutExtension() + "	Not found",
                    bind(m.openRecent, file),
                    file.existsAsFile());
    if (!m.recent.empty())
    {
        recent.addSeparator();
        recents.add("Clear recent", m.clearRecent);
    }
    menu.addSubMenu("Recent", recent, !m.recent.empty());
    items.add("Close project", m.closeProject);
    menu.addSeparator();
    items.add("Save", m.save, ui::Command::save);
    items.add("Save As...", m.saveAs, ui::Command::saveAs);
    items.add("Revert to saved...", m.revert, m.canRevert);
    items.add("Save as template...", m.saveTemplate);
    menu.addSeparator();
    juce::PopupMenu media;
    Builder mediaItems(media, id);
    mediaItems.add("Gather media into the project folder", m.gatherMedia, m.saved);
    // An unsaved project's unused media are its own recordings in the shared folder.
    mediaItems.add("Remove unused media...", m.removeUnusedMedia);
    menu.addSubMenu("Media", media);
    menu.addSeparator();
    items.add("Import audio...", m.importAudio, ui::Command::importAudio);
    items.add("Import MIDI file...", m.importMidi);
    items.add("Add audio track", m.addAudioTrack, ui::Command::addAudioTrack);
    items.add("Add MIDI track", m.addMidiTrack, ui::Command::addMidiTrack);
    juce::PopupMenu fxTracks;
    Builder fx(fxTracks, id);
    for (size_t i = 0; i < m.fxTracks.size(); ++i)
        fx.add(juce::String(m.fxTracks[i]), bind(m.addFxTrack, i));
    menu.addSubMenu("Add FX track", fxTracks);
    items.add("Add bus", m.addBus, ui::Command::addBus);
    menu.addSeparator();
    items.add("Export...", m.exportMix, ui::Command::exportMix);
    items.add("Export MIDI clips...", m.exportMidi);
    return menu;
}

juce::PopupMenu snapMenu(bool snapOn, int grid, std::function<void(int divisions)> chosen)
{
    // The straight grids at the top; triplets and dotted grids in submenus
    // (interaction.md 10.3), each ticked where the current grid is.
    int id = 1;
    juce::PopupMenu menu, triplets, dotted;
    Builder items(menu, id), tripletItems(triplets, id), dottedItems(dotted, id);
    bool inTriplets = false, inDotted = false;
    for (const auto& c : ui::snapGridChoices())
    {
        const bool current = snapOn && grid == c.code;
        if (c.family == ui::SnapFamily::triplet)
        {
            tripletItems.add(c.name, bind(chosen, c.code), true, current);
            inTriplets = inTriplets || current;
        }
        else if (c.family == ui::SnapFamily::dotted)
        {
            dottedItems.add(c.name, bind(chosen, c.code), true, current);
            inDotted = inDotted || current;
        }
        else
            items.add(c.name, bind(chosen, c.code), true, current);
    }
    menu.addSubMenu("Triplet", triplets, true, nullptr, inTriplets);
    menu.addSubMenu("Dotted", dotted, true, nullptr, inDotted);
    menu.addSeparator();
    items.add("Off", bind(chosen, -1), true, !snapOn);
    return menu;
}

} // namespace northlight::menus
