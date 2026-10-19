add_subdirectory(unit)

option(AURAL_BUILD_HARDWARE_TESTS "Build hardware (manual) test tools" ON)
if(AURAL_BUILD_HARDWARE_TESTS)
    add_subdirectory(hardware)
endif()
