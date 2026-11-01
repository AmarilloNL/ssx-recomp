// ssx - serialize the guest's XAM input calls.
//
// SSX polls the pad from several threads at once. The SDK's InputSystem
// rebuilds its device list on every call with no lock, so concurrent calls
// free the vector under each other (access violation in
// std::_Destroy_range<DeviceInfo>). Each wrapper takes one lock and forwards
// to the runtime's export.
//
// Testing aid: SSX_TEST_INPUT="12:start,18:a,25:down+a" presses pad buttons at
// the given seconds after the first poll (each held 200 ms), so automated runs
// can get past the title screen.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/input/input_system.h>

#include "ssx_input_lock.h"
#include "ssx_runtime_export.h"

namespace {

std::recursive_mutex input_mutex;

struct ScriptedPress {
  double at_seconds;
  uint16_t buttons;
};

std::vector<ScriptedPress> ParseScript(const char* text) {
  static const struct { const char* name; uint16_t mask; } kButtons[] = {
      {"up", 0x0001},    {"down", 0x0002},  {"left", 0x0004}, {"right", 0x0008},
      {"start", 0x0010}, {"back", 0x0020},  {"lb", 0x0100},   {"rb", 0x0200},
      {"a", 0x1000},     {"b", 0x2000},     {"x", 0x4000},    {"y", 0x8000}};
  std::vector<ScriptedPress> presses;
  std::stringstream items(text);
  std::string item;
  while (std::getline(items, item, ',')) {
    auto colon = item.find(':');
    if (colon == std::string::npos) continue;
    ScriptedPress press{std::atof(item.substr(0, colon).c_str()), 0};
    std::stringstream names(item.substr(colon + 1));
    std::string name;
    while (std::getline(names, name, '+')) {
      for (const auto& b : kButtons) {
        if (name == b.name) press.buttons |= b.mask;
      }
    }
    presses.push_back(press);
  }
  return presses;
}

uint16_t ScriptedButtons() {
  static const char* text = std::getenv("SSX_TEST_INPUT");
  if (!text) return 0;
  static const std::vector<ScriptedPress> presses = ParseScript(text);
  static const auto start = std::chrono::steady_clock::now();
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  uint16_t buttons = 0;
  for (const auto& p : presses) {
    if (now >= p.at_seconds && now < p.at_seconds + 0.2) buttons |= p.buttons;
  }
  return buttons;
}

}  // namespace

#define SSX_LOCKED_IMPORT(name)                                              \
  extern "C" REX_FUNC(__imp__##name) {                                       \
    static const GuestFunc original = ResolveRuntimeExport("__imp__" #name); \
    std::lock_guard lock(input_mutex);                                       \
    original(ctx, base);                                                     \
  }

SSX_LOCKED_IMPORT(XamInputGetCapabilities)
SSX_LOCKED_IMPORT(XamInputSetState)
SSX_LOCKED_IMPORT(XamInputGetKeystrokeEx)

// XamInputGetState(r3 = user, r4 = flags, r5 = X_INPUT_STATE*); wButtons is the
// big-endian u16 at +4.
extern "C" REX_FUNC(__imp__XamInputGetState) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamInputGetState");
  std::lock_guard lock(input_mutex);
  uint32_t state = ctx.r5.u32;
  uint32_t user = ctx.r3.u32;
  original(ctx, base);
  if (uint16_t extra = ScriptedButtons(); extra && user == 0 && state && ctx.r3.u32 == 0) {
    uint16_t buttons;
    std::memcpy(&buttons, base + state + 4, 2);
    buttons = __builtin_bswap16(uint16_t(__builtin_bswap16(buttons) | extra));
    std::memcpy(base + state + 4, &buttons, 2);
  }
}

bool SsxReadPadForUI(rex::input::InputSystem* input, rex::input::X_INPUT_STATE* state) {
  std::lock_guard lock(input_mutex);
  return input->GetStateForUI(0, state) == 0;
}
