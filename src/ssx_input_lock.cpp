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
// can get past the title screen. SSX_TEST_INPUT_FILE=path presses each line
// appended to that file instead (see LiveFileButtons).

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
  uint32_t buttons;  // XINPUT button bits; bits 16-19 = left stick left/right/up/down
};

std::vector<ScriptedPress> ParseScript(const char* text) {
  static const struct { const char* name; uint32_t mask; } kButtons[] = {
      {"up", 0x0001},    {"down", 0x0002},  {"left", 0x0004}, {"right", 0x0008},
      {"start", 0x0010}, {"back", 0x0020},  {"lb", 0x0100},   {"rb", 0x0200},
      {"a", 0x1000},     {"b", 0x2000},     {"x", 0x4000},    {"y", 0x8000},
      {"lleft", 0x10000}, {"lright", 0x20000}, {"lup", 0x40000}, {"ldown", 0x80000}};
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

uint32_t ParseButtons(const std::string& combo) {
  auto presses = ParseScript(("0:" + combo).c_str());
  return presses.empty() ? 0 : presses[0].buttons;
}

// SSX_TEST_INPUT_FILE=path: each line appended to the file ("a", "down+a") is
// pressed once, in order, 200 ms each with 200 ms between - lets a test steer
// the menus step by step while watching screenshots.
uint32_t LiveFileButtons(const char* path) {
  using clock = std::chrono::steady_clock;
  static size_t lines_done = 0;
  static std::vector<uint32_t> queue;
  static clock::time_point next_read, press_start;
  static bool pressing = false;
  auto now = clock::now();
  if (now >= next_read) {
    next_read = now + std::chrono::milliseconds(100);
    std::ifstream file(path);
    std::string line;
    for (size_t i = 0; std::getline(file, line); ++i) {
      if (i >= lines_done) {
        queue.push_back(ParseButtons(line));
        lines_done = i + 1;
      }
    }
  }
  if (pressing && now - press_start >= std::chrono::milliseconds(400)) {
    pressing = false;
    queue.erase(queue.begin());
  }
  if (!pressing && !queue.empty()) {
    pressing = true;
    press_start = now;
  }
  return pressing && now - press_start < std::chrono::milliseconds(200) ? queue.front() : 0;
}

uint32_t ScriptedButtons() {
  static const char* live = std::getenv("SSX_TEST_INPUT_FILE");
  if (live) return LiveFileButtons(live);
  static const char* text = std::getenv("SSX_TEST_INPUT");
  if (!text) return 0;
  static const std::vector<ScriptedPress> presses = ParseScript(text);
  static const auto start = std::chrono::steady_clock::now();
  double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  uint32_t buttons = 0;
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
// big-endian u16 at +4, sThumbLX / sThumbLY the big-endian s16s at +8 / +10.
extern "C" REX_FUNC(__imp__XamInputGetState) {
  static const GuestFunc original = ResolveRuntimeExport("__imp__XamInputGetState");
  std::lock_guard lock(input_mutex);
  uint32_t state = ctx.r5.u32;
  uint32_t user = ctx.r3.u32;
  original(ctx, base);
  if (uint32_t extra = ScriptedButtons(); extra && user == 0 && state && ctx.r3.u32 == 0) {
    uint16_t buttons;
    std::memcpy(&buttons, base + state + 4, 2);
    buttons = __builtin_bswap16(uint16_t(__builtin_bswap16(buttons) | uint16_t(extra)));
    std::memcpy(base + state + 4, &buttons, 2);
    auto set_axis = [&](uint32_t offset, uint32_t negative, uint32_t positive) {
      if (!(extra & (negative | positive))) return;
      uint16_t value = __builtin_bswap16(uint16_t(int16_t(extra & negative ? -32767 : 32767)));
      std::memcpy(base + state + offset, &value, 2);
    };
    set_axis(8, 0x10000, 0x20000);   // left stick X
    set_axis(10, 0x80000, 0x40000);  // left stick Y (up is positive)
  }
}

bool SsxReadPadForUI(rex::input::InputSystem* input, rex::input::X_INPUT_STATE* state) {
  std::lock_guard lock(input_mutex);
  return input->GetStateForUI(0, state) == 0;
}
