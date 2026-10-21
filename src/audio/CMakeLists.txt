# Pure audio code: settings model, latency detection DSP, pass-through render.
# No JUCE dependency so the unit tests stay fast and hardware-free.
add_library(northlight_audio STATIC
    AudioSettings.h
    AudioSettings.cpp
    LatencyDetector.h
    LatencyDetector.cpp
    PassThrough.h
    PassThrough.cpp)

target_include_directories(northlight_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_audio PUBLIC northlight_options northlight_rt)

# Device layer: wraps juce::AudioDeviceManager, owns the real-time callback and the
# loopback calibrator.
add_library(northlight_audio_device STATIC
    AudioCallback.h
    AudioCallback.cpp
    AudioDeviceLayer.h
    AudioDeviceLayer.cpp
    LatencyCalibrator.h
    LatencyCalibrator.cpp
    MidiDeviceLayer.h
    MidiDeviceLayer.cpp
    JuceMediaLoader.h
    JuceMediaLoader.cpp
    JuceStreamReader.h
    JuceStreamReader.cpp
    PeakStore.h
    PeakStore.cpp)

target_link_libraries(northlight_audio_device PUBLIC northlight_audio northlight_engine northlight_juce)
