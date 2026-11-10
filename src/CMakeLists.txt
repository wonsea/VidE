# Core library: no UI, no third-party codec libraries. This keeps the analysis
# model usable from the CLI and from automated tests.

add_library(vide_core STATIC
    common/block_syntax.cpp
    common/helper_common.cpp
    common/status.cpp
    common/syntax_json.cpp
    common/temp_dir.cpp
    common/types.cpp
    common/vpk.cpp
    common/xxhash.cpp
    core/bitstream_index.cpp
    core/cache_store.cpp
    core/helper_process.cpp
    core/session.cpp
    core/workspace.cpp
)

target_include_directories(vide_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(vide_core PUBLIC cxx_std_17)

# vide_core uses std::thread/mutex, and the reference helpers link static
# libaom/libvpx archives that also call pthread APIs. On glibc < 2.34 those
# symbols live in libpthread, so propagate the threads link dependency.
find_package(Threads REQUIRED)
target_link_libraries(vide_core PUBLIC Threads::Threads)

# All VidE scratch files live under <repo>/tmp unless $VIDE_TMP_DIR overrides it.
target_compile_definitions(vide_core PUBLIC
    VIDE_DEFAULT_TMP_DIR="${CMAKE_SOURCE_DIR}/tmp")

# All third-party build outputs (HM/VTM/libaom/libvpx) live under one directory
# so the repo root stays clean and the layout is identical across platforms.
set(VIDE_DEPS_DIR "${CMAKE_SOURCE_DIR}/build/deps" CACHE PATH
    "Unified directory for third-party build outputs")

if(MSVC)
    target_compile_options(vide_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(vide_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Codec adapters --------------------------------------------------------
# The FFmpeg adapter dispatches to VidE's own bitstream parsers and invokes the
# reference-decoder helper executables for block-level data.
add_library(vide_codec STATIC
    codec/av1/av1_syntax.cpp
    codec/h264/h264_syntax.cpp
    codec/hevc/hevc_syntax.cpp
    codec/image/jpeg_syntax.cpp
    codec/image/png_syntax.cpp
    codec/raw/raw_video_adapter.cpp
    codec/vp9/vp9_syntax.cpp
    codec/vvc/vvc_syntax.cpp
)

target_link_libraries(vide_codec PUBLIC vide_core)

if(VIDE_WITH_FFMPEG)
    # FFmpeg is downloaded (pinned, SHA-256 verified) and built as LGPL shared
    # libraries by scripts/build-ffmpeg.{ps1,sh} into build/deps/ffmpeg. Linking
    # dynamically keeps VidE MIT-compatible; the av*.dll/.so are replaceable.
    set(VIDE_FFMPEG_ROOT "${VIDE_DEPS_DIR}/ffmpeg")
    set(VIDE_FFMPEG_LIBDIR "${VIDE_FFMPEG_ROOT}/lib")
    if(NOT EXISTS "${VIDE_FFMPEG_ROOT}/include/libavformat/avformat.h")
        message(FATAL_ERROR "Vendored FFmpeg not found in ${VIDE_FFMPEG_ROOT}. "
                            "Run scripts/build-ffmpeg.ps1 (or scripts/build-ffmpeg.sh).")
    endif()
    target_sources(vide_codec PRIVATE
        core/codec_session.cpp
        core/codec_session_syntax.cpp
        core/codec_session_display.cpp
        core/codec_session_thumbnail.cpp
        core/media_source.cpp)
    target_compile_definitions(vide_codec PRIVATE VIDE_WITH_FFMPEG=1)
    target_include_directories(vide_codec PRIVATE "${VIDE_FFMPEG_ROOT}/include")
    if(WIN32)
        set(VIDE_FFMPEG_LIBS
            "${VIDE_FFMPEG_LIBDIR}/libavformat.dll.a"
            "${VIDE_FFMPEG_LIBDIR}/libavcodec.dll.a"
            "${VIDE_FFMPEG_LIBDIR}/libswscale.dll.a"
            "${VIDE_FFMPEG_LIBDIR}/libswresample.dll.a"
            "${VIDE_FFMPEG_LIBDIR}/libavutil.dll.a")
    else()
        set(VIDE_FFMPEG_LIBS
            "${VIDE_FFMPEG_LIBDIR}/libavformat.so"
            "${VIDE_FFMPEG_LIBDIR}/libavcodec.so"
            "${VIDE_FFMPEG_LIBDIR}/libswscale.so"
            "${VIDE_FFMPEG_LIBDIR}/libswresample.so"
            "${VIDE_FFMPEG_LIBDIR}/libavutil.so")
    endif()
    target_link_libraries(vide_codec PRIVATE ${VIDE_FFMPEG_LIBS})
    if(WIN32)
        target_link_libraries(vide_codec PRIVATE
            bcrypt secur32 ws2_32 user32 advapi32 ole32 oleaut32 shell32 uuid)
    endif()
    message(STATUS "VidE: FFmpeg adapter enabled (vendored LGPL shared)")
endif()

if(VIDE_WITH_AV1)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AOM REQUIRED IMPORTED_TARGET aom)
    target_compile_definitions(vide_codec PRIVATE VIDE_WITH_LIBAOM=1)
    target_link_libraries(vide_codec PRIVATE PkgConfig::AOM)
    message(STATUS "VidE: libaom linked for AV1 header parsing")
endif()

# ---- Reference-decoder helper executables ----------------------------------
# HM, VTM and JM define colliding global symbols and cannot coexist in one
# process. Each is built as its own helper that decodes a stream and writes a
# VidE Pack cache (.vpk, see common/vpk.h) while reporting progress over stderr.
# The main process runs them as subprocesses and reads the cache.

if(VIDE_WITH_HEVC_HM)
    set(VIDE_HM_ROOT "${CMAKE_SOURCE_DIR}/external/HM")
    set(VIDE_HM_BUILD "${VIDE_DEPS_DIR}/hm")
    file(GLOB_RECURSE VIDE_HM_LIBS
         "${VIDE_HM_BUILD}/lib/libTLibCommon.a"
         "${VIDE_HM_BUILD}/lib/libTLibDecoder.a"
         "${VIDE_HM_BUILD}/lib/libUtilities.a")
    if(VIDE_HM_LIBS)
        add_executable(vide_hevc_helper "${CMAKE_SOURCE_DIR}/tools/hevc_helper/main.cpp")
        target_link_libraries(vide_hevc_helper PRIVATE vide_core)
        target_include_directories(vide_hevc_helper PRIVATE "${VIDE_HM_ROOT}/source/Lib")
        target_link_libraries(vide_hevc_helper PRIVATE
            "-Wl,--start-group" ${VIDE_HM_LIBS} "-Wl,--end-group")
        target_compile_definitions(vide_codec PRIVATE VIDE_WITH_HEVC_HM=1)
        message(STATUS "VidE: HEVC/HM helper enabled")
    else()
        message(WARNING "VidE: VIDE_WITH_HEVC_HM is ON but HM libraries were not found. "
                        "Run scripts/build-hm.ps1 to build them.")
    endif()
endif()

if(VIDE_WITH_VVC_VTM)
    set(VIDE_VTM_ROOT "${CMAKE_SOURCE_DIR}/external/VTM")
    set(VIDE_VTM_BUILD "${VIDE_DEPS_DIR}/vtm")
    file(GLOB_RECURSE VIDE_VTM_LIBS
         "${VIDE_VTM_BUILD}/lib/libCommonLib.a"
         "${VIDE_VTM_BUILD}/lib/libDecoderLib.a"
         "${VIDE_VTM_BUILD}/lib/libUtilities.a")
    if(VIDE_VTM_LIBS)
        add_executable(vide_vvc_helper "${CMAKE_SOURCE_DIR}/tools/vvc_helper/main.cpp")
        target_link_libraries(vide_vvc_helper PRIVATE vide_core)
        # Match the compile definitions VTM's own libraries were built with;
        # otherwise the data-structure ABI (e.g. Pel size) disagrees.
        target_compile_definitions(vide_vvc_helper PRIVATE
            RExt__HIGH_BIT_DEPTH_SUPPORT=1)
        target_include_directories(vide_vvc_helper PRIVATE
            "${VIDE_VTM_ROOT}/source/Lib"
            "${VIDE_VTM_ROOT}/source/Lib/CommonLib"
            "${VIDE_VTM_ROOT}/source/Lib/DecoderLib"
            "${VIDE_VTM_ROOT}/source/Lib/Utilities")
        target_link_libraries(vide_vvc_helper PRIVATE
            "-Wl,--start-group" ${VIDE_VTM_LIBS} "-Wl,--end-group")
        target_compile_definitions(vide_codec PRIVATE VIDE_WITH_VVC_VTM=1)
        message(STATUS "VidE: VVC/VTM helper enabled")
    else()
        message(WARNING "VidE: VIDE_WITH_VVC_VTM is ON but VTM libraries were not found. "
                        "Run scripts/build-vtm.ps1 to build them.")
    endif()
endif()

# ---- AVC/JM helper ---------------------------------------------------------
# JM is a small C codebase; compile its decoder + lcommon sources straight into
# a static library that vide_avc_helper links against.
if(VIDE_WITH_AVC_JM)
    set(VIDE_JM_ROOT "${CMAKE_SOURCE_DIR}/external/JM")
    if(EXISTS "${VIDE_JM_ROOT}/source/app/ldecod/ldecod.c")
        file(GLOB VIDE_JM_LDECOD_SRC "${VIDE_JM_ROOT}/source/app/ldecod/*.c")
        file(GLOB VIDE_JM_LCOMMON_SRC "${VIDE_JM_ROOT}/source/lib/lcommon/*.c")
        add_library(vide_jm_decoder STATIC ${VIDE_JM_LDECOD_SRC} ${VIDE_JM_LCOMMON_SRC})
        target_include_directories(vide_jm_decoder PUBLIC
            "${VIDE_JM_ROOT}/source/app/ldecod"
            "${VIDE_JM_ROOT}/source/lib/lcommon")
        # JM's file-target helpers are only used by the CLI drivers, not the
        # decoder core; silence unused warnings from the batch build.
        target_compile_definitions(vide_jm_decoder PRIVATE TRACE=0)
        if(WIN32)
            target_link_libraries(vide_jm_decoder PUBLIC ws2_32)
        endif()

        add_executable(vide_avc_helper
            "${CMAKE_SOURCE_DIR}/tools/avc_helper/main.cpp"
            "${CMAKE_SOURCE_DIR}/tools/avc_helper/avc_bridge.c")
        target_include_directories(vide_avc_helper PRIVATE
            "${CMAKE_SOURCE_DIR}/tools/avc_helper")
        target_link_libraries(vide_avc_helper PRIVATE vide_core vide_jm_decoder)
        target_compile_definitions(vide_codec PRIVATE VIDE_WITH_AVC_JM=1)
        message(STATUS "VidE: AVC/JM helper enabled")
    else()
        message(WARNING "VidE: VIDE_WITH_AVC_JM is ON but JM sources were not found "
                        "at ${VIDE_JM_ROOT}. Clone external/JM first.")
    endif()
endif()

# ---- VP9/libvpx helper -----------------------------------------------------
# libvpx is built by scripts/build-vpx.sh into a static library with the VidE
# capture hook (vp9_capture.c) compiled in.
if(VIDE_WITH_VP9)
    set(VIDE_VPX_ROOT "${CMAKE_SOURCE_DIR}/external/libvpx")
    set(VIDE_VPX_LIB "${VIDE_DEPS_DIR}/vpx/libvpx.a")
    if(EXISTS "${VIDE_VPX_LIB}")
        add_executable(vide_vp9_helper
            "${CMAKE_SOURCE_DIR}/tools/vp9_helper/main.cpp"
            "${CMAKE_SOURCE_DIR}/tools/vp9_helper/vp9_bridge.c")
        target_include_directories(vide_vp9_helper PRIVATE
            "${CMAKE_SOURCE_DIR}/tools/vp9_helper"
            "${VIDE_VPX_ROOT}")
        target_link_libraries(vide_vp9_helper PRIVATE vide_core "${VIDE_VPX_LIB}")
        target_compile_definitions(vide_codec PRIVATE VIDE_WITH_VP9=1)
        message(STATUS "VidE: VP9/libvpx helper enabled")
    else()
        message(WARNING "VidE: VIDE_WITH_VP9 is ON but libvpx was not found "
                        "(${VIDE_VPX_LIB}). Run scripts/build-vpx.sh first.")
    endif()
endif()

# ---- AV1/libaom helper -----------------------------------------------------
# libaom is built by hand (see docs/05) into a static library with the VidE
# capture hook (av1_capture.c) compiled in.
if(VIDE_WITH_AV1)
    set(VIDE_AOM_ROOT "${VIDE_DEPS_DIR}/aom")
    set(VIDE_AOM_LIB "${VIDE_AOM_ROOT}/libaom.a")
    if(EXISTS "${VIDE_AOM_LIB}")
        add_executable(vide_av1_helper
            "${CMAKE_SOURCE_DIR}/tools/av1_helper/main.cpp"
            "${CMAKE_SOURCE_DIR}/tools/av1_helper/av1_bridge.c")
        target_include_directories(vide_av1_helper PRIVATE
            "${CMAKE_SOURCE_DIR}/tools/av1_helper"
            "${CMAKE_SOURCE_DIR}/external/libaom"
            "${VIDE_AOM_ROOT}")
        target_link_libraries(vide_av1_helper PRIVATE vide_core "${VIDE_AOM_LIB}")
        target_compile_definitions(vide_codec PRIVATE VIDE_WITH_AV1_HELPER=1)
        message(STATUS "VidE: AV1/libaom helper enabled")
    else()
        message(WARNING "VidE: VIDE_WITH_AV1 is ON but libaom was not found "
                        "(${VIDE_AOM_LIB}). Build libaom first (see docs/05).")
    endif()
endif()

# ---- Application (Dear ImGui front-end) ------------------------------------
add_executable(vide
    app/main.cpp
    render/renderer.cpp
    ui/file_dialog.cpp
    ui/layout.cpp
    ui/toolbar_panel.cpp
    ui/syntax_panel.cpp
    ui/syntax_view.cpp
    ui/ui_util.cpp
    ui/bitstream_view.cpp
    ui/frame_panel.cpp
    ui/bitstream_panel.cpp
    ui/thumbnail_panel.cpp
    ui/image_panel.cpp
    ui/ctu_view_panel.cpp
    ui/ctu_panel.cpp
    ui/block_info_panel.cpp
)

target_include_directories(vide PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# vide_core (Session/Workspace) references CodecSession symbols defined in
# vide_codec, which in turn links vide_core: a cyclic archive dependency, so the
# two must be rescanned as a group for the static link to resolve.
target_link_libraries(vide PRIVATE "$<LINK_GROUP:RESCAN,vide_codec,vide_core>" imgui)

# Make the shared FFmpeg runnable from build/bin: copy the DLLs on Windows and
# point the Linux binaries at the build-tree .so directory (and $ORIGIN for the
# packaged/dist copy).
if(VIDE_WITH_FFMPEG)
    if(WIN32)
        add_custom_target(vide_ffmpeg_runtime
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${VIDE_FFMPEG_ROOT}/bin" "${CMAKE_BINARY_DIR}/bin")
        add_dependencies(vide vide_ffmpeg_runtime)
    elseif(UNIX)
        set_target_properties(vide PROPERTIES
            BUILD_RPATH "${VIDE_FFMPEG_LIBDIR};$ORIGIN"
            INSTALL_RPATH "$ORIGIN")
    endif()
endif()

if(MSVC)
    target_compile_options(vide PRIVATE /W4 /permissive-)
else()
    target_compile_options(vide PRIVATE -Wall -Wextra -Wpedantic)
endif()
