# Pure audio code: settings model, latency detection DSP, pass-through render.
# No JUCE dependency so the unit tests stay fast and hardware-free.
add_library(aural_audio STATIC
    AudioSettings.h
    AudioSettings.cpp
    LatencyDetector.h
    LatencyDetector.cpp
    PassThrough.h
    PassThrough.cpp)

target_include_directories(aural_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_audio PUBLIC aural_options aural_rt)
