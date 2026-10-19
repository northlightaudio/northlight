add_executable(aural_unit_tests
    rt/RealtimeGuardTests.cpp
    audio/AudioSettingsTests.cpp
    audio/LatencyDetectorTests.cpp
    audio/PassThroughTests.cpp
    core/ModelTests.cpp
    core/CommandTests.cpp
    core/ProjectJsonTests.cpp
    engine/RtPrimitivesTests.cpp
    engine/EngineTests.cpp
    engine/StreamingTests.cpp
    engine/PeaksTests.cpp
    engine/CallbackStatsTests.cpp
    engine/WorkerPoolTests.cpp
    engine/RecordingTests.cpp)

target_link_libraries(aural_unit_tests PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_audio
    aural_core
    aural_engine
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(aural_unit_tests)
