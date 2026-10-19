# Unit tests for the JUCE-dependent audio layer pieces that need no device:
# stream reading with resampling. Separate from aural_unit_tests so the pure tests
# stay JUCE-free and fast to link.
add_executable(aural_juce_tests
    ../unit/NoAssertDialogs.cpp
    JuceStreamReaderTests.cpp
    AudioCallbackTests.cpp)

target_link_libraries(aural_juce_tests PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_audio_device
    aural_plugins
    aural_juce
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(aural_juce_tests)
