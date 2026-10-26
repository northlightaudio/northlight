# Unit tests for the JUCE-dependent audio layer pieces that need no device:
# stream reading with resampling. Separate from northlight_unit_tests so the pure tests
# stay JUCE-free and fast to link.
add_executable(northlight_juce_tests
    ../unit/NoAssertDialogs.cpp
    JuceStreamReaderTests.cpp
    PeakStoreTests.cpp
    AudioCallbackTests.cpp
    ProjectSessionTests.cpp
    ProjectLoadTests.cpp
    HostedPluginTests.cpp
    ClapPluginTests.cpp
    BounceTests.cpp
    ExportFormatTests.cpp
    ThemeTests.cpp
    KeyboardMapTests.cpp
    ChannelStripUiTests.cpp
    EditorPanelTests.cpp
    ChoiceControlTests.cpp
    VesperCardTests.cpp
    CrashGuardTests.cpp
    DbTests.cpp
    MotionTests.cpp
    MeterBallisticsTests.cpp
    AutosaveTests.cpp
    AutomationWriteTests.cpp
    TemplatesMediaTests.cpp
    CrashReportTests.cpp
    MediaBrowserTests.cpp
    MidiFileTests.cpp
    MigrationTests.cpp
    InputChoicesTests.cpp
    PreferencesViewTests.cpp
    HeaderBarTests.cpp
    WindowPlacementTests.cpp
    TooltipsTests.cpp
    AudioEditorTests.cpp
    ArrangeSelectionTests.cpp
    MarkerControllerTests.cpp
    ArrangeKeymapTests.cpp
    ArrangeGestureTests.cpp
    ControllerLaneTests.cpp
    TimelineWorkBudgetTests.cpp
    WellsTests.cpp
    MixerViewTests.cpp
    GlueCardTests.cpp
    WidenerCardTests.cpp
    TapeEchoCardTests.cpp
    OrbitCardTests.cpp
    CoronaCardTests.cpp
    RenderedFaceTests.cpp
    NumberTests.cpp
    PatinaCardTests.cpp
    PanelLayoutTests.cpp
    UiWorkBudgetTests.cpp
    UiPaintTimeTests.cpp
    PluginMenuTests.cpp
    EffectPresetLibraryTests.cpp
    EffectCardTooltipsTests.cpp
    WidthScaleTests.cpp
    ExciterCardTests.cpp
    BloomCardTests.cpp
    CardSnapshotTests.cpp
    FeedbackHistoryTests.cpp
    ParamBinderTests.cpp)

target_link_libraries(northlight_juce_tests PRIVATE
    northlight_options
    northlight_rt
    northlight_rt_checks
    northlight_audio_device
    northlight_plugins
    northlight_app_session
    northlight_hosting
    northlight_ui
    northlight_juce
    Catch2::Catch2WithMain)

# A scanner stand-in that crashes, hangs or talks nonsense on request (mode from
# the environment), driven through PluginScanner::inspect with its path.
add_executable(northlight_scan_stub ScanStub.cpp)
target_link_libraries(northlight_scan_stub PRIVATE northlight_options)
set_target_properties(northlight_scan_stub PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(northlight_juce_tests northlight_scan_stub)

# A CLAP effect built here so the CLAP host is tested without a third-party
# plugin: a .clap next to the tests (a CLAP is a DLL with that extension).
add_library(northlight_test_clap SHARED ../clap_test_plugin/TestClapPlugin.cpp)
target_link_libraries(northlight_test_clap PRIVATE northlight_options clap)
set_target_properties(northlight_test_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "NorthlightTestGain"
    SUFFIX ".clap"
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:northlight_juce_tests>)
add_dependencies(northlight_juce_tests northlight_test_clap)

# The scanner beside the tests, where PluginScanner looks for it.
add_custom_command(TARGET northlight_juce_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:northlight_plugin_scan> $<TARGET_FILE_DIR:northlight_juce_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:northlight_scan_stub> $<TARGET_FILE_DIR:northlight_juce_tests>)
add_dependencies(northlight_juce_tests northlight_plugin_scan)

# Test material shared by the test targets (tests/support).
target_include_directories(northlight_juce_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../support")

include(Catch)
catch_discover_tests(northlight_juce_tests)

# The UI paint-time budgets (docs/ui-performance.md): timed, so only in a Release
# build, where the times mean something, and labelled so a run can pick or skip them.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME ui_paint_time COMMAND northlight_juce_tests "[.painttime]")
    set_tests_properties(ui_paint_time PROPERTIES LABELS perf RUN_SERIAL TRUE)
endif()
