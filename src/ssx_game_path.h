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

#ifndef _WIN32
#include <string>
// Linux: the desktop's file picker (kdialog or zenity). `patterns` like "*.png *.jpg"; empty if cancelled.
std::string SsxPickFile(const char* title, const char* filter_name, const char* patterns);
// Linux: a message box (kdialog or zenity), and the text on stderr.
void SsxMessage(const char* text, bool error);
#endif
