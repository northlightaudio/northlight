# Precompiled headers for the targets that parse the same large headers in every
# file: JUCE in the UI, app, device and hosting code, Catch2 in the tests. Each
# target compiles its own copy once, then every source in it starts from it.
#
# The headers are force-included, so a file that forgets an include still compiles
# with them on. The CI build has them off (sccache cannot cache a compile that uses
# a precompiled header), which also catches a missing include before it merges.
#
# Only standard, JUCE and Catch2 headers go in: they change only when a dependency
# does. Project headers stay out, so editing one rebuilds what includes it and no
# more. JUCE-free targets (src/core, src/engine, src/plugins, tests/unit) never get
# the JUCE set.

set(NORTHLIGHT_PCH_STD
    <algorithm>
    <array>
    <atomic>
    <chrono>
    <cmath>
    <cstdint>
    <functional>
    <map>
    <memory>
    <optional>
    <span>
    <string>
    <utility>
    <vector>)

# juce_audio_utils pulls in every JUCE module the app's code includes: core,
# events, data structures, graphics, GUI, audio basics, devices, formats and
# processors.
set(NORTHLIGHT_PCH_JUCE <juce_audio_utils/juce_audio_utils.h>)

set(NORTHLIGHT_PCH_CATCH2 <catch2/catch_test_macros.hpp> <catch2/catch_approx.hpp>)

# northlight_precompile(<target> [JUCE] [CATCH2])
# Adds the standard set to <target>, and the JUCE and Catch2 sets when named.
function(northlight_precompile target)
    if(NOT NORTHLIGHT_PCH)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "JUCE;CATCH2" "" "")
    set(headers ${NORTHLIGHT_PCH_STD})
    if(arg_JUCE)
        list(APPEND headers ${NORTHLIGHT_PCH_JUCE})
    endif()
    if(arg_CATCH2)
        list(APPEND headers ${NORTHLIGHT_PCH_CATCH2})
    endif()
    target_precompile_headers(${target} PRIVATE ${headers})
endfunction()
