# The application version (CONTRIBUTING.md, "Versions and releases"). The number is
# project()'s VERSION; the label is NORTHLIGHT_VERSION_LABEL; the git commit is read
# on every build by cmake/GenerateVersion.cmake into a generated header.

set(northlight_version_header "${CMAKE_CURRENT_BINARY_DIR}/generated/NorthlightVersion.h")

add_custom_target(northlight_version_header
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DOUTPUT=${northlight_version_header}
        -DVERSION=${PROJECT_VERSION}
        -DLABEL=${NORTHLIGHT_VERSION_LABEL}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${northlight_version_header}
    COMMENT "Reading the version and git commit"
    VERBATIM)

add_library(northlight_version STATIC
    Version.h
    Version.cpp)
add_dependencies(northlight_version northlight_version_header)
target_include_directories(northlight_version
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(northlight_version PRIVATE northlight_options)
