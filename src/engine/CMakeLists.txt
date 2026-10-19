# Real-time utilities. Header-mostly; no JUCE dependency so it can be unit tested
# quickly and reused by the scanner process later.
add_library(aural_rt STATIC
    rt/RealtimeGuard.h
    rt/RealtimeGuard.cpp
    rt/SpscQueue.h
    rt/SnapshotPublisher.h
    rt/CallbackStats.h
    rt/Denormals.h
    Meters.h)

target_include_directories(aural_rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_rt PUBLIC aural_options)

# The global operator new/delete override must live in the final executable's link,
# not in a static library (where the linker could discard it). An OBJECT library's
# object files are linked directly into whatever links it.
add_library(aural_rt_checks OBJECT
    rt/RealtimeAllocationHook.cpp)
target_link_libraries(aural_rt_checks PUBLIC aural_rt)

# The engine: snapshot compiler, transport, renderer and the controller/RT façade.
# JUCE-free so it runs offline in tests.
add_library(aural_engine STATIC
    AudioSource.h
    AudioSource.cpp
    Snapshot.h
    Snapshot.cpp
    Transport.h
    Renderer.h
    Renderer.cpp
    AudioEngine.h
    AudioEngine.cpp
    StreamingAudioSource.h
    StreamingAudioSource.cpp
    DiskThread.h
    DiskThread.cpp
    Peaks.h
    Peaks.cpp
    WorkerPool.h
    WorkerPool.cpp
    WavFile.h
    WavFile.cpp
    Recording.h
    Recording.cpp
    Click.h
    Click.cpp
    Resampling.h
    Insert.h
    Midi.h
    Insert.cpp
    Delay.h
    Delay.cpp
    Loudness.h)

target_link_libraries(aural_engine PUBLIC aural_rt aural_core)
