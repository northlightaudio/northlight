add_subdirectory(unit)

# The design system's guard: no literal colour in src/ui (docs/design/design-system.md).
if(WIN32)
    add_test(NAME ui_no_literal_colours
             COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_SOURCE_DIR}/scripts/check-ui-tokens.ps1" -Root "${CMAKE_SOURCE_DIR}")
endif()
add_subdirectory(unit_juce)

option(NORTHLIGHT_BUILD_HARDWARE_TESTS "Build hardware (manual) test tools" ON)
if(NORTHLIGHT_BUILD_HARDWARE_TESTS)
    add_subdirectory(hardware)
endif()
