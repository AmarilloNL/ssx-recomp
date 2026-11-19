#pragma once
// ssx - SSX's shaders for ReGPU's first-boot compile (issue #17): --ssx_collect_shaders writes
// static-shaders.bin beside the exe and quits (ssx_shaders.cpp).
#include <functional>

// How the collector ends the game once the file is written (the window close button's path).
void SsxShadersSetQuit(std::function<void()> quit);
