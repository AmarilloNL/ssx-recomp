#pragma once

#include <filesystem>

// Your own music in SSX's "Personalize Your Music" menu: every folder in `dir` is a playlist (see
// ssx_music.cpp). Call once before the game starts.
void SsxSetMusicDir(const std::filesystem::path& dir);
