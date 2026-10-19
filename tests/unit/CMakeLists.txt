add_executable(aural_unit_tests
    rt/RealtimeGuardTests.cpp)

target_link_libraries(aural_unit_tests PRIVATE
    aural_options
    aural_rt
    aural_rt_checks
    Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(aural_unit_tests)
