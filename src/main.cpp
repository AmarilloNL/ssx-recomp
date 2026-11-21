// ssx - ReXGlue Recompiled Project

#include <cstdint>

#include "generated/default/ssx_init.h"

#include "ssx_app.h"

REX_DEFINE_APP(ssx, SsxApp::Create)

#ifdef _WIN32
// Laptops with two GPUs (Intel or AMD graphics in the CPU next to an NVIDIA or AMD card): asks the
// drivers for the fast card. Without these, Windows' default is often the built-in one, slower and
// with little memory of its own. The drivers only read them from the program's .exe: the SDK's
// copies are in a renderer DLL (rexgpu-xenos.dll).
extern "C" {
__declspec(dllexport) uint32_t NvOptimusEnablement = 1;
__declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}
#endif
