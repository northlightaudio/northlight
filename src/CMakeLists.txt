# ---------------------------------------------------------------------------
# aural_juce: the JUCE modules, compiled once.
#
# JUCE modules are INTERFACE targets that add their unity-build sources to
# whatever links them, so two targets linking the same module get two copies and
# duplicate symbols. This static library links the modules PRIVATE (compiling them
# here) and re-exports the include paths and definitions so every other target can
# include JUCE headers and link against this one copy.
# ---------------------------------------------------------------------------
add_library(aural_juce STATIC)
target_sources(aural_juce PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/JuceModules.cpp")

target_link_libraries(aural_juce
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        aural_options
        juce::juce_recommended_config_flags)

target_compile_definitions(aural_juce PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=${AURAL_HAS_ASIO}
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(AURAL_HAS_ASIO)
    target_include_directories(aural_juce PRIVATE "${AURAL_ASIO_SDK_DIR}/common")
endif()

# Re-export what the modules put on this target so consumers see the same headers
# and module settings without recompiling the module sources.
target_include_directories(aural_juce INTERFACE
    $<TARGET_PROPERTY:aural_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(aural_juce INTERFACE
    $<TARGET_PROPERTY:aural_juce,COMPILE_DEFINITIONS>)

add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(plugins)
add_subdirectory(hosting)
add_subdirectory(audio)
add_subdirectory(ui)
add_subdirectory(app)
