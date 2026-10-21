# Hardware tests: need a real audio device and a human (or a loopback cable). They are
# built so they cannot rot, but are never registered with CTest.
juce_add_console_app(northlight_loopback_tool PRODUCT_NAME "northlight_loopback_tool")

target_sources(northlight_loopback_tool PRIVATE LoopbackLatencyTool.cpp)

target_link_libraries(northlight_loopback_tool PRIVATE
    northlight_options
    northlight_rt
    northlight_rt_checks
    northlight_audio_device
    northlight_juce)

# Offline recording soak: N synthetic inputs for M minutes through the engine's
# record path, verified sample-exactly. JUCE-free; run manually.
add_executable(northlight_record_soak RecordSoakTool.cpp)
target_link_libraries(northlight_record_soak PRIVATE
    northlight_options
    northlight_rt
    northlight_rt_checks
    northlight_engine
    northlight_plugins)
