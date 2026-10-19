cmake_minimum_required(VERSION 3.28)

project(Aural
    VERSION 0.1.0
    DESCRIPTION "A native, high-performance digital audio workstation"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(AURAL_BUILD_TESTS "Build unit tests" ON)
option(AURAL_RT_CHECKS "Enable real-time safety checks (allocation/lock detection on RT threads)" ON)
option(AURAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Steinberg SDKs are not vendored. Point this at a local copy of the ASIO SDK
# (the directory that contains common/, host/, driver/) to enable ASIO support.
# The VST3 SDK is bundled inside JUCE, so VST3 hosting needs nothing extra.
set(AURAL_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK root (enables ASIO when set)")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)

# JUCE's own warning flags are applied per-target via juce::juce_recommended_warning_flags.
FetchContent_MakeAvailable(JUCE)

if(AURAL_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

# ---------------------------------------------------------------------------
# ASIO
# ---------------------------------------------------------------------------
set(AURAL_HAS_ASIO 0)
if(AURAL_ASIO_SDK_DIR)
    if(EXISTS "${AURAL_ASIO_SDK_DIR}/common/iasiodrv.h")
        set(AURAL_HAS_ASIO 1)
        message(STATUS "Aural: ASIO enabled (SDK at ${AURAL_ASIO_SDK_DIR})")
    else()
        message(WARNING "Aural: AURAL_ASIO_SDK_DIR is set but common/iasiodrv.h was not found; ASIO disabled")
    endif()
else()
    message(STATUS "Aural: ASIO disabled (set AURAL_ASIO_SDK_DIR to enable)")
endif()

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------
add_library(aural_options INTERFACE)
target_compile_features(aural_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(aural_options INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
        /MP
        /EHsc
        # C5105: "macro expansion producing 'defined'" fires inside JUCE's bundled
        # harfbuzz under /Zc:preprocessor; third-party code, not ours.
        /wd5105)
    target_compile_definitions(aural_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
    if(AURAL_WARNINGS_AS_ERRORS)
        target_compile_options(aural_options INTERFACE /WX)
    endif()
else()
    target_compile_options(aural_options INTERFACE -Wall -Wextra -Wpedantic)
    if(AURAL_WARNINGS_AS_ERRORS)
        target_compile_options(aural_options INTERFACE -Werror)
    endif()
endif()

target_compile_definitions(aural_options INTERFACE
    $<$<CONFIG:Debug>:AURAL_DEBUG=1>
    $<$<BOOL:${AURAL_RT_CHECKS}>:AURAL_RT_CHECKS=1>)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(AURAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
