# Lightweight, dependency-free test executables registered with CTest.

add_executable(vide_tests
    test_status.cpp
    test_syntax.cpp
    test_bits.cpp
    test_block_syntax.cpp
    test_cache_eviction.cpp
    test_stress.cpp
    test_vpk.cpp
    test_bitstream_index.cpp
    test_main.cpp
)

target_link_libraries(vide_tests PRIVATE vide_core)
target_include_directories(vide_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_test(NAME vide_tests COMMAND vide_tests)
set_tests_properties(vide_tests PROPERTIES TIMEOUT 120)

# Session-layer integration tests (raw video, multi-stream, open/close cycles).
# These use CodecSession, which is only built when the FFmpeg adapter is enabled.
if(VIDE_WITH_FFMPEG)
    add_executable(vide_integration test_integration.cpp)
    target_link_libraries(vide_integration PRIVATE "$<LINK_GROUP:RESCAN,vide_codec,vide_core>")
    target_include_directories(vide_integration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME vide_integration COMMAND vide_integration)
    set_tests_properties(vide_integration PROPERTIES TIMEOUT 120)
    if(TARGET vide_ffmpeg_runtime)
        add_dependencies(vide_integration vide_ffmpeg_runtime)
    endif()
    if(UNIX)
        set_target_properties(vide_integration PROPERTIES
            BUILD_RPATH "${VIDE_DEPS_DIR}/ffmpeg/lib;$ORIGIN")
    endif()
endif()
