# Third-party dependencies.
#
# `external/` contains unmodified upstream sources. New VidE code must never be
# added here; adapters in `src/codec/` are the only place that talks to these
# libraries.

# ---- Dear ImGui (docking branch) -------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

# ---- GLFW (window + input) -------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(imgui PUBLIC glfw)

# ---- OpenGL ----------------------------------------------------------------
find_package(OpenGL REQUIRED)
# Some GLFW/OpenGL loader builds call dlopen(); on glibc < 2.34 that lives in
# libdl, which is not pulled in automatically by the OpenGL target.
target_link_libraries(imgui PUBLIC OpenGL::GL ${CMAKE_DL_LIBS})
