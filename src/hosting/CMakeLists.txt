# Plugin hosting: the registry of known plugins (JUCE-free), the out-of-process
# scanner driver, hosted plugins behind the engine's insert interface and the
# factory the app hands the engine.
add_library(northlight_hosting_core STATIC
    PluginRegistry.h
    PluginRegistry.cpp)

target_include_directories(northlight_hosting_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_hosting_core
    PUBLIC northlight_options
    PRIVATE nlohmann_json::nlohmann_json)

add_library(northlight_hosting STATIC
    PluginPresets.h
    PluginPresets.cpp
    PluginEditor.h
    ParameterEditQueue.h
    ClapEditor.h
    ClapEditor.cpp
    PluginScanner.h
    PluginScanner.cpp
    HostedPlugin.h
    HostedPlugin.cpp
    ClapPlugin.h
    ClapPlugin.cpp
    HostFactory.h
    HostFactory.cpp
    CrashGuard.h
    CrashGuard.cpp)

target_include_directories(northlight_hosting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_hosting
    PUBLIC northlight_hosting_core northlight_engine northlight_plugins northlight_juce clap
    PRIVATE northlight_version)

# The scanner: one plugin file per process, descriptions out as XML. Copied next
# to the app and the JUCE tests so they find it beside themselves.
add_executable(northlight_plugin_scan ScannerMain.cpp)
target_link_libraries(northlight_plugin_scan PRIVATE northlight_options northlight_juce clap)
set_target_properties(northlight_plugin_scan PROPERTIES WIN32_EXECUTABLE FALSE)
