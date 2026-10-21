juce_add_gui_app(aural
    PRODUCT_NAME        "Aural"
    COMPANY_NAME        "Leafshade"
    BUNDLE_ID           "io.leafshade.aural"
    VERSION             "${PROJECT_VERSION}"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE)

# The project session and user settings as a library so the JUCE tests can drive
# a session without the window.
add_library(aural_app_session STATIC
    CrashReports.h
    CrashReports.cpp
    UserSettings.h
    UserSettings.cpp
    ProjectSession.h
    ProjectSession.cpp
    DemoSong.h
    DemoSong.cpp
    Bounce.h
    Bounce.cpp
    MidiFiles.h
    MidiFiles.cpp)

target_include_directories(aural_app_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_app_session PUBLIC
    aural_options
    aural_rt
    aural_rt_checks
    aural_core
    aural_engine
    aural_plugins
    aural_hosting
    aural_audio_device
    aural_juce)

# The scanner lives beside the app so the app finds it, and so do the assets:
# the token file (docs/design/tokens.json is the source) and the bundled fonts
# and icons under assets/.
add_custom_command(TARGET aural POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aural_plugin_scan> $<TARGET_FILE_DIR:aural>
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:aural>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/design/tokens.json" "$<TARGET_FILE_DIR:aural>/assets/tokens.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:aural>/assets")
add_dependencies(aural aural_plugin_scan)

target_sources(aural PRIVATE
    Main.cpp
    MainWindow.h
    MainWindow.cpp)

target_compile_definitions(aural PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:aural,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:aural,JUCE_VERSION>")

target_link_libraries(aural PRIVATE
    aural_app_session
    aural_ui)
