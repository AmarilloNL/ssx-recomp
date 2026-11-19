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
// The effects in data0.big (their names, without .fxo).
const char* const kEffects[] = {
    "adaptfixedshader",
    "antialiasing",
    "debug_mipview_default",
    "debug_mipview_instancing",
    "debug_mipview_proxy",
    "debug_mipview_terrain",
    "debug_shader",
    "debug_shader_instancing",
    "debug_shader_proxy",
    "debug_shader_terrain",
    "debugdisplaytexture",
    "debughelper",
    "defaultmanip",
    "displacement_accum",
    "displacement_debug",
    "eatext_glyphshader",
    "errormissing",
    "fixedcolor",
    "fontwriter",
    "fxaa",
    "globe_default",
    "hdr",
    "hizmanip",
    "lynx",
    "lynx_animtexture",
    "lynx_avalanche",
    "lynx_cloud_trans",
    "lynx_dynamic_lighting",
    "lynx_glare",
    "lynx_glare_occluded",
    "lynx_instance",
    "lynx_occluded",
    "lynx_ribbon",
    "lynx_snow",
    "lynx_snowfall",
    "lynx_snowspray",
    "lynx_snowspray_zfeather",
    "lynx_vbeam",
    "lynx_volumetric",
    "lynx_zfeather",
    "mayablinn",
    "mayalambert",
    "motionblur",
    "object_advanced",
    "object_alpha",
    "object_alpha_anim",
    "object_default",
    "object_default_anim",
    "object_default_skinned",
    "object_foliage",
    "object_foliage_anim",
    "object_geotag",
    "object_shadow",
    "object_texture_anim",
    "occlusionbbox",
    "particlefx",
    "particlefx_bloom",
    "particlefx_tonemap",
    "player_apparel",
    "player_board",
    "player_body",
    "player_eye",
    "player_ghost",
    "player_hair",
    "player_secondary",
    "player_wingsuit",
    "post_copy",
    "postfx",
    "prelighting",
    "recolortexture",
    "rendersplashscreen",
    "rendertexture",
    "ribbon",
    "screenfade",
    "shadowbuffer",
    "shadowtest",
    "sky",
    "ssao",
    "terrain_default",
    "terrain_default_simple",
    "terrain_foreground",
    "terrain_midground",
    "terrain_midground_fe",
    "terrain_oceanwave",
    "terrain_oceanwave_fe",
    "terrain_proxy",
    "texturecopy",
    "texturecopyfmv",
    "texturemip",
    "ui_background",
    "ui_trickmeter_disc",
    "userscreenshot",
};

std::function<void()> g_quit;

// Loads every effect through the game's loader (out slot, -, name ".fx"); the slots keep them loaded.
void LoadAllEffects(PPCContext& ctx, uint8_t* base) {
  const uint32_t n = uint32_t(std::size(kEffects));
  const uint32_t block = rex::system::kernel_memory()->SystemHeapAlloc(n * 4 + n * 64);
  if (!block) return;
  const PPCContext saved = ctx;
  uint32_t loaded = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t slot = block + i * 4, name = block + n * 4 + i * 64;
    const std::string s = std::string(kEffects[i]) + ".fx";
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
  REXLOG_INFO("shaders: {} of {} effects loaded", loaded, n);
}

// Every shader container in physical memory, de-duplicated, to static-shaders.bin: "SSXS", the count, then
// per shader u8 pixel, u32 dwords and the big-endian code.
void WriteShaders() {
  rex::memory::Memory* memory = rex::system::kernel_memory();
  std::vector<uint32_t> containers;
  SsxScanGuestHeap(memory, 0xA0000000u, 4, [&](uint32_t at, const uint8_t* p, uint32_t bytes) {
    for (uint32_t i = 0; i + 4 <= bytes && i < (1u << 20); i += 4) {
      if (p[i] == 0x10 && p[i + 1] == 0x2A && p[i + 2] == 0x11 && p[i + 3] <= 1) containers.push_back(at + i);
    }
  });
  auto word = [&](uint32_t a) { return __builtin_bswap32(*memory->TranslateVirtual<const uint32_t*>(a)); };
  std::map<std::vector<uint8_t>, bool> shaders;  // code -> pixel
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
  REXLOG_INFO("shaders: {} containers, {} shaders written to {}", containers.size(), count, path.string());
}
}  // namespace

void SsxShadersSetQuit(std::function<void()> quit) { g_quit = std::move(quit); }

// The game's effect loader: with --ssx_collect_shaders its first call (at startup) loads all of them; the
// shaders are read out once the loads have had time to finish, then the game quits the normal way.
// ponytail: a fixed 10 s wait (3700 shaders in a test, all within it); wait for the container count to
// settle instead if a slower disc or PC ever misses some.
REX_EXTERN(SSX_FN_LOAD_EFFECT_IMP);
REX_HOOK_RAW(SSX_FN_LOAD_EFFECT) {
  SSX_FN_LOAD_EFFECT_IMP(ctx, base);
  static bool started = false;
  if (started || !REXCVAR_GET(ssx_collect_shaders)) return;
  started = true;  // (also keeps LoadAllEffects' own calls from coming back here)
  LoadAllEffects(ctx, base);
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    WriteShaders();
    if (g_quit) g_quit();
  }).detach();
}
