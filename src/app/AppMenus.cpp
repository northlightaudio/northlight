#include "app/AppMenus.h"

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
    items.add("Audio Setup...", a.audioSetup);
    items.add("Plugins...", a.plugins);
    items.add("Preferences...", a.preferences);
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
    items.add("New", m.newProject);
    juce::PopupMenu fromTemplate;
    Builder templates(fromTemplate, id);
    for (const auto& file : m.templates)
        templates.add(file.getFileNameWithoutExtension(), bind(m.newFromTemplate, file));
    menu.addSubMenu("New from template", fromTemplate, !m.templates.empty());
    items.add("Open...", m.open);
    juce::PopupMenu recent;
    Builder recents(recent, id);
    for (const auto& file : m.recent)
        recents.add(file.getFileNameWithoutExtension(), bind(m.openRecent, file));
    menu.addSubMenu("Recent", recent, !m.recent.empty());
    menu.addSeparator();
    items.add("Save", m.save);
    items.add("Save As...", m.saveAs);
    items.add("Save as template...", m.saveTemplate);
    menu.addSeparator();
    juce::PopupMenu media;
    Builder mediaItems(media, id);
    mediaItems.add("Gather media into the project folder", m.gatherMedia, m.saved);
    mediaItems.add("Remove unused media...", m.removeUnusedMedia, m.saved);
    menu.addSubMenu("Media", media);
    menu.addSeparator();
    items.add("Import audio...", m.importAudio);
    items.add("Import MIDI file...", m.importMidi);
    items.add("Add audio track", m.addAudioTrack);
    items.add("Add MIDI track", m.addMidiTrack);
    juce::PopupMenu fxTracks;
    Builder fx(fxTracks, id);
    for (size_t i = 0; i < m.fxTracks.size(); ++i)
        fx.add(juce::String(m.fxTracks[i]), bind(m.addFxTrack, i));
    menu.addSubMenu("Add FX track", fxTracks);
    items.add("Add bus", m.addBus);
    menu.addSeparator();
    items.add("Export...", m.exportMix);
    items.add("Export MIDI clips...", m.exportMidi);
    return menu;
}

const std::vector<SnapGrid>& snapGrids()
{
    static const std::vector<SnapGrid> grids{
        {0, "Adaptive"}, {1, "Bar"}, {4, "Beat"}, {8, "1/8"}, {16, "1/16"}, {32, "1/32"}};
    return grids;
}

juce::PopupMenu snapMenu(bool snapOn, int grid, std::function<void(int divisions)> chosen)
{
    int id = 1;
    juce::PopupMenu menu;
    Builder items(menu, id);
    for (const auto& g : snapGrids())
        items.add(g.name, bind(chosen, g.divisions), true, snapOn && grid == g.divisions);
    menu.addSeparator();
    items.add("Off", bind(chosen, -1), true, !snapOn);
    return menu;
}

} // namespace northlight::menus
