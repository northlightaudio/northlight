# Real-time utilities. Header-mostly; no JUCE dependency so it can be unit tested
# quickly and reused by the scanner process later.
add_library(northlight_rt STATIC
    rt/RealtimeGuard.h
    rt/RealtimeGuard.cpp
    rt/SpscQueue.h
    rt/SnapshotPublisher.h
    rt/CallbackStats.h
    rt/Denormals.h
    rt/TelemetryRing.h
    rt/ThreadPriority.h
    rt/ThreadPriority.cpp
    Meters.h)

target_include_directories(northlight_rt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(northlight_rt PUBLIC northlight_options)
if(WIN32)
    target_link_libraries(northlight_rt PRIVATE avrt) # MMCSS for the render workers
endif()

# The global operator new/delete override must live in the final executable's link,
# not in a static library (where the linker could discard it). An OBJECT library's
# object files are linked directly into whatever links it.
add_library(northlight_rt_checks OBJECT
    rt/RealtimeAllocationHook.cpp)
target_link_libraries(northlight_rt_checks PUBLIC northlight_rt)

# The engine: snapshot compiler, transport, renderer and the controller/RT façade.
# JUCE-free so it runs offline in tests.
add_library(northlight_engine STATIC
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
    GlitchDetector.h
    GlitchDetector.cpp
    OutputTap.h
    OutputTap.cpp
    Click.h
    Click.cpp
    Preview.h
    Preview.cpp
    Resampling.h
    Insert.h
    Feedback.h
    Midi.h
    Insert.cpp
    Delay.h
    Delay.cpp
    Loudness.h
    Stretch.h
    Stretch.cpp
    TempoDetect.h
    TempoDetect.cpp
    ExportSettings.h
    ExportSettings.cpp
    ExportDsp.h
    Gain.h
    ExportDsp.cpp
    pitch/BoxSmooth.h
    pitch/PitchAnalysis.h
    pitch/PitchAnalysis.cpp
    pitch/VocalRender.h
    pitch/VocalRender.cpp
    pitch/Correction.h
    pitch/Correction.cpp
    pitch/VocalService.h
    pitch/VocalService.cpp
    pitch/VocalOps.h
    pitch/VocalOps.cpp)

target_link_libraries(northlight_engine PUBLIC northlight_rt northlight_core PRIVATE signalsmith-stretch)
