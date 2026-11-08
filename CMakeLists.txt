# ssx - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ssx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

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

# With the SDK built in-tree, the plugin is only staged when ssx itself relinks;
# also stage it whenever the plugin is rebuilt, so plugin-only fixes take effect.
if(TARGET rexgpu-xenos)
    add_custom_target(ssx_stage_gpu_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rexgpu-xenos> $<TARGET_FILE_DIR:ssx>
        DEPENDS rexgpu-xenos ssx
        VERBATIM)
endif()
