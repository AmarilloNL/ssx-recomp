#pragma once

#include <filesystem>
#include <optional>

// Why the game stops before it starts (its exit code; recomplauncher.json explains them, 1 is
// ReXGlue's own for a disc read error or a failed start): 10 = no copy of the game (none found,
// and the file picker was cancelled), 11 = built for the title update but its files are missing.
constexpr int kSsxExitNoGame = 10;
constexpr int kSsxExitNoUpdate = 11;
// Logs why, flushes the log and ends the process with that code (before the runtime starts).
[[noreturn]] void SsxExit(int code, const char* why);

// Returns the game folder (containing default.xex) or the disc .iso, asking
// the user if needed, and remembers it in the config. nullopt = user cancelled.
// --game <path> (the .iso, or the folder with default.xex) is used first, for this start only.
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
