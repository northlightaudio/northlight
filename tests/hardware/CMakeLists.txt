# Hardware tests: need a real audio device and a human (or a loopback cable). They are
# built so they cannot rot, but are never registered with CTest.
juce_add_console_app(aural_loopback_tool PRODUCT_NAME "aural_loopback_tool")

target_sources(aural_loopback_tool PRIVATE LoopbackLatencyTool.cpp)

target_link_libraries(aural_loopback_tool PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_audio_device
    aural_juce)

# Offline recording soak: N synthetic inputs for M minutes through the engine's
# record path, verified sample-exactly. JUCE-free; run manually.
add_executable(aural_record_soak RecordSoakTool.cpp)
target_link_libraries(aural_record_soak PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    aural_engine)
