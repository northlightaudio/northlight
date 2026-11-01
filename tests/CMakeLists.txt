add_subdirectory(unit)

# Source checks, in Python so they run on every platform.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    # The design system's guard: no literal colour in src/ui (docs/design/design-system.md).
    add_test(NAME ui_no_literal_colours
             COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/check-ui-tokens.py" --root "${CMAKE_SOURCE_DIR}")
    # No thread is ever killed (docs/decisions.md).
    add_test(NAME src_never_kills_a_thread
             COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/scripts/check-thread-stops.py" --root "${CMAKE_SOURCE_DIR}")
else()
    message(WARNING "Northlight: no Python 3 found, so ctest skips the literal-colour and thread-stop checks")
endif()
add_subdirectory(unit_juce)

option(NORTHLIGHT_BUILD_HARDWARE_TESTS "Build hardware (manual) test tools" ON)
if(NORTHLIGHT_BUILD_HARDWARE_TESTS)
    add_subdirectory(hardware)
endif()
