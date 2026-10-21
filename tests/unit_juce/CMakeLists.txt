# Unit tests for the JUCE-dependent audio layer pieces that need no device:
# stream reading with resampling. Separate from northlight_unit_tests so the pure tests
# stay JUCE-free and fast to link.
add_executable(northlight_juce_tests
    ../unit/NoAssertDialogs.cpp
    JuceStreamReaderTests.cpp
    AudioCallbackTests.cpp
    ProjectSessionTests.cpp
    HostedPluginTests.cpp
    ClapPluginTests.cpp
    BounceTests.cpp
    ExportFormatTests.cpp
    ThemeTests.cpp
    KeyboardMapTests.cpp
    ChannelStripUiTests.cpp
    EditorPanelTests.cpp
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
    MigrationTests.cpp)

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

include(Catch)
catch_discover_tests(northlight_juce_tests)
