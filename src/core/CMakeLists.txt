# The project model, commands/undo and project file serialisation. No JUCE: this is
# tested without a message loop and reused by the scanner process later.
add_library(northlight_core STATIC
    Model.h
    Model.cpp
    TempoMap.h
    TempoMap.cpp
    Vocal.h
    Command.h
    Command.cpp
    Comping.h
    Comping.cpp
    ProjectJson.h
    ProjectJson.cpp)

target_include_directories(northlight_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_core
    PUBLIC northlight_options
    PRIVATE nlohmann_json::nlohmann_json)
