#pragma once

#include <cstdint>
#include <filesystem>

#include <rex/cvar.h>

REXCVAR_DECLARE(int32_t, ssx_framerate);
REXCVAR_DECLARE(std::string, ssx_render_resolution);
REXCVAR_DECLARE(int32_t, ssx_volume);

// Frame rate the game should run at: ssx_framerate (0 = the main display's refresh rate), or
// at 3x / 4x resolution scale ssx_framerate_3x / ssx_framerate_4x.
int32_t SsxTargetFps();

// Adaptive frame pacing above 60 fps when the GPU can't keep up; call once per
// presented frame (VdSwap).
void SsxPaceFrameEnd();

// Pushes SSX settings into the runtime; call from OnPreSetup after the GPU
// plugin is loaded (its cvars must exist).
void SsxApplyStartupSettings();

// Writes config-file and runtime-set cvars to `path` (not command-line ones).
void SsxSaveConfig(const std::filesystem::path& path);
