# rexgpu-regpu.dll: ReGPU as a ReXGlue GPU plugin for SSX (ssx.exe --gpu_plugin=regpu).
# Adapted from Glue2GPU (https://github.com/zer0ducksgiven/Glue2GPU, MIT). Built by scripts/build-regpu.ps1.
cmake_minimum_required(VERSION 3.25)
project(rexgpu_regpu CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SSX_REGPU_SOURCE "" CACHE PATH "ReGPU checkout (rexglue-driver branch)")
set(SSX_REXGLUE_SOURCE "" CACHE PATH "ReXGlue source (headers and SIMDe)")

find_package(rexglue CONFIG REQUIRED)

# ReGPU without its Vd* exports, against ReXGlue's PPCContext (regpu::driver).
set(REGPU_GUEST_ABI rexglue)
set(REGPU_REXGLUE_DIR "${SSX_REXGLUE_SOURCE}")
add_subdirectory("${SSX_REGPU_SOURCE}" regpu)

# ReXGlue's loader looks for rexgpu-<name>.dll beside the executable.
add_library(rexgpu-regpu SHARED
    src/plugin_main.cpp
    src/overlay.cpp
    src/regpu_graphics_system.cpp
    src/stack_dump.cpp)
if(WIN32)
    target_link_libraries(rexgpu-regpu PRIVATE regpu rex::runtime dbghelp)
else()
    # Linux: librexgpu-regpu.so; the window's Vulkan surface comes from linux_surface.cpp, and
    # libdxcompiler.so is shipped next to the game ($ORIGIN).
    target_sources(rexgpu-regpu PRIVATE src/linux_surface.cpp)
    # SDL's headers (its functions are in ReXGlue's runtime library).
    target_include_directories(rexgpu-regpu PRIVATE "${SSX_REXGLUE_SOURCE}/thirdparty/sdl3/include")
    target_link_libraries(rexgpu-regpu PRIVATE regpu rex::runtime)
    set_target_properties(rexgpu-regpu PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
