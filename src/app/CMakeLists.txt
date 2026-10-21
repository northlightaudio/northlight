juce_add_gui_app(northlight
    PRODUCT_NAME        "Northlight"
    COMPANY_NAME        "Leafshade"
    BUNDLE_ID           "org.northlightaudio.northlight"
    VERSION             "${PROJECT_VERSION}"
    NEEDS_CURL          FALSE
    NEEDS_WEB_BROWSER   FALSE)

# The project session and user settings as a library so the JUCE tests can drive
# a session without the window.
add_library(northlight_app_session STATIC
    CrashReports.h
    CrashReports.cpp
    UserSettings.h
    UserSettings.cpp
    Migration.h
    Migration.cpp
    ProjectSession.h
    ProjectSession.cpp
    SampleFiles.h
    SampleFiles.cpp
    DemoSong.h
    DemoSong.cpp
    Bounce.h
    Bounce.cpp
    MidiFiles.h
    MidiFiles.cpp)

target_include_directories(northlight_app_session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_app_session PUBLIC
    northlight_options
    northlight_rt
    northlight_rt_checks
    northlight_core
    northlight_engine
    northlight_plugins
    northlight_hosting
    northlight_audio_device
    northlight_juce)

# The scanner lives beside the app so the app finds it, and so do the assets:
# the token file (docs/design/tokens.json is the source) and the bundled fonts
# and icons under assets/.
add_custom_command(TARGET northlight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:northlight_plugin_scan> $<TARGET_FILE_DIR:northlight>
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:northlight>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/design/tokens.json" "$<TARGET_FILE_DIR:northlight>/assets/tokens.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:northlight>/assets"
    # The licence and the third-party notices ship with every build.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:northlight>/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:northlight>/THIRD_PARTY_NOTICES.md")
add_dependencies(northlight northlight_plugin_scan)

target_sources(northlight PRIVATE
    Main.cpp
    MainWindow.h
    MainWindow.cpp)

target_compile_definitions(northlight PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:northlight,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:northlight,JUCE_VERSION>")

target_link_libraries(northlight PRIVATE
    northlight_app_session
    northlight_ui)
