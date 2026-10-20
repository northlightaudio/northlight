# Unit tests for the JUCE-dependent audio layer pieces that need no device:
# stream reading with resampling. Separate from aural_unit_tests so the pure tests
# stay JUCE-free and fast to link.
add_executable(aural_juce_tests
    ../unit/NoAssertDialogs.cpp
    JuceStreamReaderTests.cpp
    AudioCallbackTests.cpp
    ProjectSessionTests.cpp
    HostedPluginTests.cpp
    ClapPluginTests.cpp
    BounceTests.cpp
    ThemeTests.cpp
    KeyboardMapTests.cpp
    ChannelStripUiTests.cpp
    EditorPanelTests.cpp
    CrashGuardTests.cpp
    AutosaveTests.cpp
    AutomationWriteTests.cpp
    TemplatesMediaTests.cpp
    CrashReportTests.cpp)

target_link_libraries(aural_juce_tests PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_audio_device
    aural_plugins
    aural_app_session
    aural_hosting
    aural_ui
    aural_juce
    Catch2::Catch2WithMain)

# A scanner stand-in that crashes, hangs or talks nonsense on request (mode from
# the environment), driven through PluginScanner::inspect with its path.
add_executable(aural_scan_stub ScanStub.cpp)
target_link_libraries(aural_scan_stub PRIVATE aural_options)
set_target_properties(aural_scan_stub PROPERTIES WIN32_EXECUTABLE FALSE)
add_dependencies(aural_juce_tests aural_scan_stub)

# A CLAP effect built here so the CLAP host is tested without a third-party
# plugin: a .clap next to the tests (a CLAP is a DLL with that extension).
add_library(aural_test_clap SHARED ../clap_test_plugin/TestClapPlugin.cpp)
target_link_libraries(aural_test_clap PRIVATE aural_options clap)
set_target_properties(aural_test_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "AuralTestGain"
    SUFFIX ".clap"
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:aural_juce_tests>)
add_dependencies(aural_juce_tests aural_test_clap)

# The scanner beside the tests, where PluginScanner looks for it.
add_custom_command(TARGET aural_juce_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aural_plugin_scan> $<TARGET_FILE_DIR:aural_juce_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aural_scan_stub> $<TARGET_FILE_DIR:aural_juce_tests>)
add_dependencies(aural_juce_tests aural_plugin_scan)

include(Catch)
catch_discover_tests(aural_juce_tests)
