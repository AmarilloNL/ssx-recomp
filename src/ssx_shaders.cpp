// ssx - SSX's shaders for ReGPU's first-boot compile (issue #17). Work in progress.
//
// ReGPU compiles every shader it gets from Host::staticShaders once, behind its "PREPARING SHADERS"
// screen, and caches the result; without that list a new install meets each shader for the first time
// mid-ride. SSX's shaders are in its 92 effect files (data\xenon\shaders\<name>.fxo, in data0.big),
// which the game only loads as materials need them. With --ssx_collect_shaders the game loads all of them
// through its own loader as soon as it loads its first one at startup, then reads every shader out of
// memory and writes them to static-shaders.bin beside the exe (the player's own copy of the game: nothing
// of it ships), and quits. The ReGPU plugin hands that file to Host::staticShaders.
//
// A loaded effect keeps its shaders as Xbox 360 shader containers in physical memory: big-endian words,
// [0] 0x102A1100 (pixel) or 0x102A1101 (vertex), [1] the offset of the code block, [2] the code's size,
// [5] 0 or the offset of a block whose word 4 is its header size; that header is followed by the code's
// offset within the code block and its size. (Checked against every shader ReGPU had met in a day of play.)
#include "ssx_shaders.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "generated/default/ssx_pch.h"
#include "ssx_guest.h"
#include "ssx_guest_memory.h"

REXCVAR_DEFINE_BOOL(ssx_collect_shaders, false, "SSX",
                    "Load every effect, write their shaders to static-shaders.bin for the renderer, then quit");

REX_EXTERN(SSX_FN_LOAD_EFFECT);  // hooked below

namespace {
// The effects the game can load: every "<name>.fxo" in the archive indexes it has loaded (in memory by its
// first effect load), so the port carries no list of the game's files and DLC archives count too. A stray
// match (2 fragments of other names in a test) is harmless: the loader still returns, and the shaders
// collected were the same 3701 as with the 92 real names alone.
std::vector<std::string> EffectNames() {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  std::set<std::string> names;
  auto is_name = [](uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
  for (uint32_t heap : {0x00010000u, 0x40000000u, 0x80000000u, 0x90000000u, 0xA0000000u, 0xC0000000u, 0xE0000000u}) {
    SsxScanGuestHeap(memory, heap, 64, [&](uint32_t, const uint8_t* p, uint32_t bytes) {
      for (uint32_t i = 3; i + 4 <= bytes && i < (1u << 20); ++i) {
        if (p[i] != '.' || p[i + 1] != 'f' || p[i + 2] != 'x' || p[i + 3] != 'o') continue;
        uint32_t s = i;
        while (s > 0 && i - s < 48 && is_name(p[s - 1])) --s;
        if (i - s >= 3) names.insert(std::string(reinterpret_cast<const char*>(p + s), i - s));
      }
    });
  }
  return {names.begin(), names.end()};
}

std::function<void()> g_quit;

// Loads every effect through the game's loader (out slot, -, name ".fx"); the slots keep them loaded.
void LoadAllEffects(PPCContext& ctx, uint8_t* base) {
  const std::vector<std::string> effects = EffectNames();
  const uint32_t n = uint32_t(effects.size());
  if (!n) return;
  const uint32_t block = rex::system::kernel_memory()->SystemHeapAlloc(n * 4 + n * 64);
  if (!block) return;
  const PPCContext saved = ctx;
  uint32_t loaded = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t slot = block + i * 4, name = block + n * 4 + i * 64;
    const std::string s = effects[i].substr(0, 58) + ".fx";
    REX_STORE_U32(slot, 0);
    for (size_t k = 0; k <= s.size(); ++k) REX_STORE_U8(name + uint32_t(k), k < s.size() ? uint8_t(s[k]) : 0);
    ctx = saved;
    ctx.r1.u32 = saved.r1.u32 - 1024;  // a frame of our own below the caller's
    ctx.r3.u64 = slot;
    ctx.r4.u64 = 0;
    ctx.r5.u64 = name;
    SSX_FN_LOAD_EFFECT(ctx, base);
    if (REX_LOAD_U32(slot)) ++loaded;
  }
  ctx = saved;
  REXLOG_INFO("shaders: {} of {} effect names found in memory loaded", loaded, n);
}

using Shaders = std::map<std::vector<uint8_t>, bool>;  // code -> pixel

// Adds every shader container in physical memory to `shaders` (de-duplicated); returns the containers seen.
size_t ScanShaders(Shaders& shaders) {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  std::vector<uint32_t> containers;
  SsxScanGuestHeap(memory, 0xA0000000u, 4, [&](uint32_t at, const uint8_t* p, uint32_t bytes) {
    for (uint32_t i = 0; i + 4 <= bytes && i < (1u << 20); i += 4) {
      if (p[i] == 0x10 && p[i + 1] == 0x2A && p[i + 2] == 0x11 && p[i + 3] <= 1) containers.push_back(at + i);
    }
  });
  auto word = [&](uint32_t a) { return __builtin_bswap32(*memory->TranslateVirtual<const uint32_t*>(a)); };
  for (uint32_t c : containers) {
    if (!SsxGuestReadable(memory, c, 24)) continue;
    const bool pixel = (word(c) & 0xFF) == 0;
    const uint32_t code_block = word(c + 4), block = word(c + 20);
    uint32_t start = c + code_block, size = word(c + 8);
    if (code_block > 0x10000) continue;
    if (block) {
      if (block > 0x10000 || !SsxGuestReadable(memory, c + block, 20)) continue;
      const uint32_t header = word(c + block + 16), pair = c + block + 20 + header;
      if (header > 0x200 || !SsxGuestReadable(memory, pair, 8)) continue;
      start += word(pair);
      size = word(pair + 4);
    }
    if (size < 8 || size > 0x8000 || (size & 3) || !SsxGuestReadable(memory, start, size)) continue;
    const uint8_t* code = memory->TranslateVirtual<const uint8_t*>(start);
    shaders.emplace(std::vector<uint8_t>(code, code + size), pixel);
  }
  return containers.size();
}

// The shaders to static-shaders.bin: "SSXS", the count, then per shader u8 pixel, u32 dwords and the
// big-endian code.
void WriteShaders(const Shaders& shaders, size_t scans) {
  const auto path = rex::filesystem::GetExecutableFolder() / "static-shaders.bin";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const uint32_t count = uint32_t(shaders.size());
  out.write("SSXS", 4);
  out.write(reinterpret_cast<const char*>(&count), 4);
  for (const auto& [code, pixel] : shaders) {
    const uint8_t type = pixel;
    const uint32_t dwords = uint32_t(code.size() / 4);
    out.write(reinterpret_cast<const char*>(&type), 1);
    out.write(reinterpret_cast<const char*>(&dwords), 4);
    out.write(reinterpret_cast<const char*>(code.data()), std::streamsize(code.size()));
  }
  REXLOG_INFO("shaders: {} shaders from {} scans written to {}", count, scans, path.string());
}
}  // namespace

void SsxShadersSetQuit(std::function<void()> quit) { g_quit = std::move(quit); }

// The game's effect loader: with --ssx_collect_shaders its first call (at startup) loads all of them; the
// shaders are read out right after the loads, before the game draws with them, then the game quits the
// normal way. (A read 10 s later, as before, also saw copies the game had patched in place for its draws:
// a different set on every PC, 3701 here vs 3684 on the Linux box, so a pipeline list recorded on one PC
// named shaders another didn't have. Right after the loads both read the same 3687.)
REX_EXTERN(SSX_FN_LOAD_EFFECT_IMP);
REX_HOOK_RAW(SSX_FN_LOAD_EFFECT) {
  SSX_FN_LOAD_EFFECT_IMP(ctx, base);
  static bool started = false;
  if (started || !REXCVAR_GET(ssx_collect_shaders)) return;
  started = true;  // (also keeps LoadAllEffects' own calls from coming back here)
  LoadAllEffects(ctx, base);
  Shaders shaders;
  const size_t containers = ScanShaders(shaders);
  REXLOG_INFO("shaders: {} containers right after the loads", containers);
  WriteShaders(shaders, 1);
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(1));  // (quit from outside the game's own call)
    if (g_quit) g_quit();
  }).detach();
}
