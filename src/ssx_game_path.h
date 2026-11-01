#pragma once

#include <filesystem>
#include <optional>

// Returns the game folder (containing default.xex) or the disc .iso, asking
// the user if needed, and remembers it in the config. nullopt = user cancelled.
std::optional<std::filesystem::path> SsxLocateGame(const std::filesystem::path& exe_dir,
                                                   const std::filesystem::path& config_path);
