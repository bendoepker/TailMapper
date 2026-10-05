cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(tailmapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(tailmapper
    src/main.cc
    src/packet_map.cc
    src/tailnet.cc
    src/common.cc
    src/gui.cc

    app.rc
    app.manifest

    # imgui vendored dependencies
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_impl_glfw.cpp
    vendor/imgui/imgui_impl_opengl3.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
)

target_compile_definitions(tailmapper PRIVATE
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:TM_DEBUG>
)

target_include_directories(tailmapper PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/windivert/include
    ${CMAKE_SOURCE_DIR}/vendor/nlohmann
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)

target_link_libraries(tailmapper PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/windivert/x64/WinDivert.lib
    iphlpapi
    ${CMAKE_SOURCE_DIR}/vendor/imgui/GLFW/glfw3.lib
    opengl32
)

target_link_options(tailmapper PRIVATE
    "/MANIFESTUAC:NO"
    "/SUBSYSTEM:CONSOLE"
)

add_custom_command(
    TARGET tailmapper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/vendor/windivert/x64/WinDivert.dll"
        "$<TARGET_FILE_DIR:tailmapper>/WinDivert.dll"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/vendor/windivert/x64/WinDivert64.sys"
        "$<TARGET_FILE_DIR:tailmapper>/WinDivert64.sys"
)
