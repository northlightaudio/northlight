# Built-in processors (gain, EQ, compressor, limiter) behind the engine's insert
# interface. JUCE-free. VST3/CLAP hosting joins this library in Phase 5.
add_library(aural_plugins STATIC
    BuiltIns.h
    BuiltIns.cpp
    MeteredBuiltIn.h
    Aurora.h
    Aurora.cpp
    Pulsar.h
    Pulsar.cpp
    Nebula.h
    Nebula.cpp
    Biquad.h
    ProcessorBase.h
    ChannelStrip.h
    ChannelStrip.cpp
    StripPresets.h
    StripPresets.cpp
    FxTracks.h
    FxTracks.cpp)

target_include_directories(aural_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_plugins PUBLIC aural_engine)
