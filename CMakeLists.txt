cmake_minimum_required(VERSION 3.20)

project(VidE
    VERSION 1.0.0
    DESCRIPTION "Video bitstream analyzer"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---- Feature options (third-party integrations, off until their milestone) ----
option(VIDE_WITH_FFMPEG    "Enable the FFmpeg codec adapter (M1)"          ON)
option(VIDE_WITH_AVC_JM    "Enable the AVC/JM reference adapter (M5)"      ON)
option(VIDE_WITH_HEVC_HM   "Enable the HEVC/HM reference adapter (M4)"     ON)
option(VIDE_WITH_VVC_VTM   "Enable the VVC/VTM reference adapter (M5)"     ON)
option(VIDE_WITH_VP9       "Enable the VP9/libvpx adapter (M5)"            ON)
option(VIDE_WITH_AV1       "Enable the AV1/libaom adapter (M5)"            OFF)
option(VIDE_WITH_IMAGE     "Enable the JPEG/PNG image adapters (M5)"       OFF)

option(VIDE_BUILD_TESTS    "Build the test suite"                          ON)

message(STATUS "VidE ${PROJECT_VERSION} | build type: ${CMAKE_BUILD_TYPE}")

# The bundled LGPL FFmpeg libraries depend on each other (e.g. libavcodec ->
# libswresample). A DT_RUNPATH only resolves the executable's direct
# dependencies, so use the legacy transitive DT_RPATH on Linux.
if(UNIX AND NOT APPLE)
    add_link_options(-Wl,--disable-new-dtags)
endif()

add_subdirectory(external)
add_subdirectory(src)

if(VIDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
