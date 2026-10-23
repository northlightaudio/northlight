# Built-in processors (gain, EQ, compressor, limiter) behind the engine's insert
# interface. JUCE-free. VST3/CLAP hosting joins this library in Phase 5.
add_library(northlight_plugins STATIC
    BuiltIns.h
    BuiltIns.cpp
    MeteredBuiltIn.h
    Aurora.h
    Aurora.cpp
    Pulsar.h
    SynthVoices.h
    Pulsar.cpp
    Nebula.h
    Nebula.cpp
    Starbit.h
    Starbit.cpp
    Sampler.h
    Sampler.cpp
    Biquad.h
    ProcessorBase.h
    ChannelStrip.h
    ChannelStrip.cpp
    Glue.h
    Glue.cpp
    StripPresets.h
    StripPresets.cpp
    FxTracks.h
    FxTracks.cpp)

target_include_directories(northlight_plugins PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_plugins PUBLIC northlight_engine)
