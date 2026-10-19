# Plugin hosting: the registry of known plugins (JUCE-free), the out-of-process
# scanner driver, hosted plugins behind the engine's insert interface and the
# factory the app hands the engine.
add_library(aural_hosting_core STATIC
    PluginRegistry.h
    PluginRegistry.cpp)

target_include_directories(aural_hosting_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_hosting_core
    PUBLIC aural_options
    PRIVATE nlohmann_json::nlohmann_json)

add_library(aural_hosting STATIC
    PluginPresets.h
    PluginScanner.h
    PluginScanner.cpp
    HostedPlugin.h
    HostedPlugin.cpp
    ClapPlugin.h
    ClapPlugin.cpp
    HostFactory.h
    HostFactory.cpp)

target_include_directories(aural_hosting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_hosting
    PUBLIC aural_hosting_core aural_engine aural_plugins aural_juce clap)

# The scanner: one plugin file per process, descriptions out as XML. Copied next
# to the app and the JUCE tests so they find it beside themselves.
add_executable(aural_plugin_scan ScannerMain.cpp)
target_link_libraries(aural_plugin_scan PRIVATE aural_options aural_juce clap)
set_target_properties(aural_plugin_scan PROPERTIES WIN32_EXECUTABLE FALSE)
