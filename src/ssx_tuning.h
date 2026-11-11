#pragma once

#include <filesystem>

// The folder the game's tuning update is kept in and looked for (<user data>/tuning); see
// ssx_tuning.cpp. Call once before the game starts.
void SsxSetTuningDir(const std::filesystem::path& dir);
