juce_add_gui_app(aural
    PRODUCT_NAME        "Aural"
    COMPANY_NAME        "Leafshade"
    BUNDLE_ID           "io.leafshade.aural"
    VERSION             "${PROJECT_VERSION}"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE)

target_sources(aural PRIVATE
    Main.cpp
    MainWindow.h
    MainWindow.cpp)

target_compile_definitions(aural PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:aural,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:aural,JUCE_VERSION>"
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
    target_include_directories(aural PRIVATE "${AURAL_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(aural PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags)
