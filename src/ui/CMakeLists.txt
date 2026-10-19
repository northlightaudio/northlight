# UI: theme tokens, LookAndFeel and views. Depends on the device layer for the
# Audio Setup screen; later views bind to the engine controller instead.
add_library(aural_ui STATIC
    Theme.h
    Theme.cpp
    AuralLookAndFeel.h
    AuralLookAndFeel.cpp
    AudioSetupView.h
    AudioSetupView.cpp
    ArrangeView.h
    ArrangeView.cpp
    TrackHeader.h
    TrackHeader.cpp)

target_include_directories(aural_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(aural_ui PUBLIC aural_core aural_audio_device aural_juce)
