#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/AddSlot.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The Instruments panel (07-plugins.md 7.4): every instrument in the project
// as a rack unit, one per instrument channel: power, the instrument's name and
// format, the channel and the MIDI tracks that play it, its outputs (the main
// pair is the channel itself; each further pair is an output track, or a
// dashed chip that makes one) and its editor. "+ Instrument" makes an
// instrument channel and a MIDI track aimed at it. The owner gives the model, the live
// instances and the registry, and takes commands back.
class InstrumentRack final : public juce::Component
{
public:
    explicit InstrumentRack(const Theme& theme);
    ~InstrumentRack() override;

    void setProject(const core::Project* project);
    void setRegistry(const hosting::PluginRegistry* registry) { registry_ = registry; }

    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id)> liveInsert;
    std::function<std::string(const std::string& type)> displayName;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(core::Id trackId, core::Id insertId)> onOpenEditor;
    std::function<void(core::Id trackId)> onSelectTrack;
    // An instrument was chosen (the owner remembers it as recent).
    std::function<void(const std::string& type)> onInserted;

    // Makes a new instrument channel playing `type`, and a MIDI track for it, (from the panel's "+" or a
    // plugin dropped on the panel).
    void addInstrumentTrack(const std::string& type);

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int unitHeight = 64;

private:
    class Unit;
    void rebuild();
    void showAddMenu(juce::Component& target);

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    const hosting::PluginRegistry* registry_ = nullptr;
    juce::Viewport viewport_;
    juce::Component list_;
    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<core::Id> builtFor_;
    AddSlot add_{theme_, "Instrument"};
};

} // namespace aural::ui
