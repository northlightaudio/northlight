# Unit tests for the JUCE-dependent audio layer pieces that need no device:
# stream reading with resampling. Separate from aural_unit_tests so the pure tests
# stay JUCE-free and fast to link.
add_executable(aural_juce_tests
    ../unit/NoAssertDialogs.cpp
    JuceStreamReaderTests.cpp
    AudioCallbackTests.cpp
    ProjectSessionTests.cpp
    HostedPluginTests.cpp)

target_link_libraries(aural_juce_tests PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_audio_device
    aural_plugins
    aural_app_session
    aural_hosting
    aural_juce
    Catch2::Catch2WithMain)

# The scanner beside the tests, where PluginScanner looks for it.
add_custom_command(TARGET aural_juce_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aural_plugin_scan> $<TARGET_FILE_DIR:aural_juce_tests>)
add_dependencies(aural_juce_tests aural_plugin_scan)

include(Catch)
catch_discover_tests(aural_juce_tests)
