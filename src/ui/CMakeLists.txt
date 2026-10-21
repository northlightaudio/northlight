# UI: theme tokens, LookAndFeel and views. Depends on the device layer for the
# Audio Setup screen; later views bind to the engine controller instead.

# The design tokens (docs/design/tokens.json) are embedded as the theme's
# defaults, so the app and the tests have them without any file beside them.
set(AURAL_TOKENS_FILE "${CMAKE_SOURCE_DIR}/docs/design/tokens.json")
file(READ "${AURAL_TOKENS_FILE}" AURAL_TOKENS_JSON)
configure_file(TokensEmbedded.inc.in "${CMAKE_CURRENT_BINARY_DIR}/generated/TokensEmbedded.inc" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AURAL_TOKENS_FILE}")

# The arrange view's clip edits as plain functions (JUCE-free, unit tested).
add_library(aural_ui_edit STATIC
    edit/ClipOps.h
    edit/ClipOps.cpp)
target_include_directories(aural_ui_edit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_ui_edit PUBLIC aural_core aural_options)

add_library(aural_ui STATIC
    Theme.h
    Theme.cpp
    AuralLookAndFeel.h
    AuralLookAndFeel.cpp
    Wells.h
    PluginMenu.h
    PluginMenu.cpp
    components/AddSlot.h
    components/Fader.h
    components/Fader.cpp
    components/Knob.h
    components/Knob.cpp
    components/Meter.h
    components/Meter.cpp
    components/SegmentedControl.h
    components/SegmentedControl.cpp
    components/NumericField.h
    components/NumericField.cpp
    components/PanelHeader.h
    components/PanelHeader.cpp
    components/Toast.h
    components/Tooltips.h
    components/Toast.cpp
    components/Sheet.h
    components/Sheet.cpp
    Icons.h
    Icons.cpp
    shell/Panels.h
    shell/Panels.cpp
    shell/HeaderBar.h
    shell/HeaderBar.cpp
    shell/StatusBar.h
    shell/StatusBar.cpp
    Selection.h
    Db.h
    Motion.h
    SendMenu.h
    SidechainMenu.h
    panels/EditorPanel.h
    panels/EditorPanel.cpp
    panels/InstrumentRack.h
    panels/InstrumentRack.cpp
    InstrumentOutputs.h
    strip/StripStyle.h
    strip/ModuleCard.h
    strip/ModuleCard.cpp
    strip/ModuleCards.cpp
    strip/Graphs.h
    strip/Graphs.cpp
    strip/EqGraph.h
    strip/EqGraph.cpp
    strip/BuiltInCards.h
    strip/BuiltInCards.cpp
    strip/SynthParts.h
    strip/AuroraEditor.h
    strip/AuroraEditor.cpp
    strip/PulsarEditor.h
    strip/PulsarEditor.cpp
    strip/NebulaEditor.h
    strip/NebulaEditor.cpp
    strip/StarbitEditor.h
    strip/StarbitEditor.cpp
    strip/SamplerEditor.h
    strip/SamplerEditor.cpp
    strip/ChannelStripView.h
    strip/ChannelStripView.cpp
    strip/UserStripPresets.h
    strip/UserStripPresets.cpp
    panels/InspectorPanel.h
    panels/InspectorPanel.cpp
    panels/DiagnosticsPanel.h
    panels/DiagnosticsPanel.cpp
    AudioSetupView.h
    AudioSetupView.cpp
    ArrangeView.h
    ArrangeView.cpp
    AutomationOverlay.h
    AutomationOverlay.cpp
    PianoRoll.h
    PianoRoll.cpp
    AudioEditor.h
    AudioEditor.cpp
    TrackHeader.h
    TrackHeader.cpp
    MixerView.h
    MixerView.cpp
    InsertEditor.h
    InsertEditor.cpp
    PluginWindow.h
    PluginWindow.cpp
    MediaFiles.h
    PluginBrowser.h
    PluginBrowser.cpp
    PreferencesView.cpp
    ClapEditor.h
    ClapEditor.cpp)

target_include_directories(aural_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(aural_ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(aural_ui PUBLIC aural_ui_edit aural_core aural_plugins aural_hosting aural_audio_device aural_juce)
