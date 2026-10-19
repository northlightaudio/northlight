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
    MainWindow.cpp
    UserSettings.h
    UserSettings.cpp
    ProjectSession.h
    ProjectSession.cpp)

target_compile_definitions(aural PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:aural,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:aural,JUCE_VERSION>")

target_link_libraries(aural PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_core
    aural_engine
    aural_plugins
    aural_audio_device
    aural_ui
    aural_juce)
