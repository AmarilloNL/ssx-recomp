#pragma once

#include <filesystem>
#include <optional>

// Returns the game folder (containing default.xex) or the disc .iso, asking
// the user if needed, and remembers it in the config. nullopt = user cancelled.
std::optional<std::filesystem::path> SsxLocateGame(const std::filesystem::path& exe_dir,
                                                   const std::filesystem::path& config_path);

// The title update's files for a build made for it: <exe_dir>/update with default.xexp and
// patch0.big (put there by scripts/build-ssx.ps1). Tells the player and returns nullopt if missing.
std::optional<std::filesystem::path> SsxLocateUpdate(const std::filesystem::path& exe_dir);
