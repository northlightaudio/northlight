add_subdirectory(unit)
add_subdirectory(unit_juce)

option(AURAL_BUILD_HARDWARE_TESTS "Build hardware (manual) test tools" ON)
if(AURAL_BUILD_HARDWARE_TESTS)
    add_subdirectory(hardware)
endif()
