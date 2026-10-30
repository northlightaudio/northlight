cmake_minimum_required(VERSION 3.28)

project(Northlight
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

# The C++ runtime is linked into each executable (/MT, /MTd), so the app and its
# portable zip run on a PC without the Visual C++ Redistributable (decisions.md).
# Set before any dependency is fetched so every target uses the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(NORTHLIGHT_BUILD_TESTS "Build unit tests" ON)
option(NORTHLIGHT_RT_CHECKS "Enable real-time safety checks (allocation/lock detection on RT threads)" ON)
option(NORTHLIGHT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
# The version's label (CONTRIBUTING.md, "Versions and releases"): "dev" for every
# development build, a pre-release such as "beta.1", or empty for a final release.
set(NORTHLIGHT_VERSION_LABEL "dev" CACHE STRING "Version label: dev, a pre-release such as beta.1, or empty for a release")

# ASIO uses the ASIO SDK header that JUCE bundles under the SDK's GPLv3 option, so
# it needs no download. NORTHLIGHT_ASIO_SDK_DIR optionally points at a local copy of
# Steinberg's ASIO SDK (the folder that contains common/) to use its header instead.
# The VST3 SDK is bundled inside JUCE too.
option(NORTHLIGHT_ASIO "Enable ASIO audio devices (Windows only)" ON)
set(NORTHLIGHT_ASIO_SDK_DIR "" CACHE PATH "Optional: an ASIO SDK root whose header replaces the one bundled with JUCE")

# Build speed (docs/building.md, "Build speed"). Precompiled headers are on by
# default. The compiler cache is for CI: sccache cannot cache a compile that uses a
# precompiled header, so the ci workflow turns the headers off and the cache on.
option(NORTHLIGHT_PCH "Precompile the standard, JUCE and Catch2 headers" ON)
option(NORTHLIGHT_COMPILER_CACHE "Compile through sccache (needs sccache on PATH)" OFF)
include(NorthlightPrecompiled)

if(NORTHLIGHT_COMPILER_CACHE)
    find_program(NORTHLIGHT_SCCACHE sccache REQUIRED)
    # Set before the dependencies are fetched, so JUCE and Catch2 are cached too.
    set(CMAKE_C_COMPILER_LAUNCHER "${NORTHLIGHT_SCCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${NORTHLIGHT_SCCACHE}")
    # sccache caches an MSVC compile only when its debug information is inside the
    # object file (/Z7), not in a PDB that every compile of a target shares (/Zi).
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    message(STATUS "Northlight: compiling through ${NORTHLIGHT_SCCACHE}")
endif()

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

# The CLAP headers (MIT) for hosting CLAP plugins against the C ABI; pinned to a tag.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(clap)

# Project file serialisation (header-only).
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Time-stretch and pitch-shift for clips (MIT, header-only; brings Signalsmith's
# MIT "linear" FFT/STFT library with it).
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(signalsmith_stretch)

if(NORTHLIGHT_BUILD_TESTS)
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
set(NORTHLIGHT_HAS_ASIO 0)
set(NORTHLIGHT_ASIO_EXTERNAL_SDK 0)
if(NORTHLIGHT_ASIO AND WIN32)
    set(NORTHLIGHT_HAS_ASIO 1)
    if(NORTHLIGHT_ASIO_SDK_DIR AND EXISTS "${NORTHLIGHT_ASIO_SDK_DIR}/common/iasiodrv.h")
        set(NORTHLIGHT_ASIO_EXTERNAL_SDK 1)
        message(STATUS "Northlight: ASIO enabled (header from the SDK at ${NORTHLIGHT_ASIO_SDK_DIR})")
    elseif(NORTHLIGHT_ASIO_SDK_DIR)
        message(WARNING "Northlight: NORTHLIGHT_ASIO_SDK_DIR is set but common/iasiodrv.h was not found; using JUCE's bundled ASIO header")
    else()
        message(STATUS "Northlight: ASIO enabled (header bundled with JUCE)")
    endif()
else()
    message(STATUS "Northlight: ASIO disabled")
endif()

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------
add_library(northlight_options INTERFACE)
target_compile_features(northlight_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(northlight_options INTERFACE
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
    target_compile_definitions(northlight_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
    if(NORTHLIGHT_WARNINGS_AS_ERRORS)
        target_compile_options(northlight_options INTERFACE /WX)
    endif()
else()
    target_compile_options(northlight_options INTERFACE -Wall -Wextra -Wpedantic)
    if(NORTHLIGHT_WARNINGS_AS_ERRORS)
        target_compile_options(northlight_options INTERFACE -Werror)
    endif()
endif()

target_compile_definitions(northlight_options INTERFACE
    $<$<CONFIG:Debug>:NORTHLIGHT_DEBUG=1>
    $<$<BOOL:${NORTHLIGHT_RT_CHECKS}>:NORTHLIGHT_RT_CHECKS=1>)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(NORTHLIGHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
