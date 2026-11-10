# ssx - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Game version: the disc release (v0.0.0.2), or its last title update (v0.0.3.2) when
# scripts/build-ssx.ps1 put the update's default.xexp next to default.xex (codegen applies it
# from there). The version's recompiler config goes where ssx_manifest.toml includes it, and the
# hooks pick their addresses from SSX_TITLE_UPDATE (src/ssx_guest.h). Reconfigures when the
# folder changes, so adding or removing the update switches the build.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game-dump/files/default.xexp")
    set(SSX_GAME_VERSION "0.0.3.2")
else()
    set(SSX_GAME_VERSION "0.0.0.2")
endif()
message(STATUS "SSX game version: ${SSX_GAME_VERSION}")
configure_file(ssx_config_v${SSX_GAME_VERSION}.toml
               "${CMAKE_CURRENT_SOURCE_DIR}/game-dump/ssx_version.toml" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/game-dump/files")

# Sources
set(SSX_SOURCES
    src/main.cpp
    src/ssx_hooks.cpp
    src/ssx_input_lock.cpp
    src/ssx_dlc.cpp
    src/ssx_frame_stats.cpp
    src/ssx_game_path.cpp
    src/ssx_settings.cpp
    src/ssx_settings_dialog.cpp
    src/ssx_restart.cpp
    src/ssx_profile.cpp
)

if(WIN32)
    add_executable(ssx WIN32 ${SSX_SOURCES})
else()
    add_executable(ssx ${SSX_SOURCES})
endif()

rexglue_setup_target(ssx GPU_PLUGINS xenos)
if(SSX_GAME_VERSION STREQUAL "0.0.3.2")
    target_compile_definitions(ssx PRIVATE SSX_TITLE_UPDATE=1)
endif()

# With the SDK built in-tree, the plugin is only staged when ssx itself relinks;
# also stage it whenever the plugin is rebuilt, so plugin-only fixes take effect.
if(TARGET rexgpu-xenos)
    add_custom_target(ssx_stage_gpu_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rexgpu-xenos> $<TARGET_FILE_DIR:ssx>
        DEPENDS rexgpu-xenos ssx
        VERBATIM)
endif()
