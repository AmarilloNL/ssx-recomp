// ssx - PC settings and the guest hooks that apply them.
//
// All settings are cvars, so they load from / save to ssx.toml next to the exe
// and show up in the SDK's F4 overlay as well as the SSX settings menu.

#include "ssx_settings.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/string/numeric.h>

#include "generated/default/ssx_pch.h"


REXCVAR_DEFINE_INT32(ssx_framerate, 60, "SSX",
                     "Frame rate: 30 (original) to 360, or 0 = match the main display")
    .range(0, 360);
REXCVAR_DEFINE_STRING(ssx_render_resolution, "high", "SSX",
                      "3D render size: original (1120x584) or high (1200x640) (restart)")
    .allowed({"original", "high", "720p"})  // 720p: older configs, now means high
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(ssx_volume, 100, "SSX", "Master volume (0-100)").range(0, 100);

namespace {

void ApplyVolume(int32_t volume) { rex::audio::SetOutputGain(volume / 100.0f); }

}  // namespace

int32_t SsxTargetFps() {
  int32_t fps = REXCVAR_GET(ssx_framerate);
  if (fps == 0) {  // match the main display
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    fps = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
              ? int32_t(mode.dmDisplayFrequency)
              : 60;
  }
  return std::clamp(fps, 30, 360);
}


// rex::cvar::SaveConfig would also persist one-off command-line flags (e.g. a
// test's --audio_mute) and writes strings unescaped; save only what came from
// the config file or was set at runtime, as valid TOML.
void SsxSaveConfig(const std::filesystem::path& path) {
  std::string out = "# SSX settings - written by the game. Edit in-game with F1.\n";
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.type == rex::cvar::FlagType::Command ||
        (entry.source != rex::cvar::Source::kConfig && entry.source != rex::cvar::Source::kRuntime)) {
      continue;
    }
    std::string value = entry.getter();
    if (value == entry.default_value) continue;
    if (entry.type == rex::cvar::FlagType::String) {
      std::string quoted = "\"";
      for (char c : value) {
        if (c == '"' || c == '\\') quoted += '\\';
        quoted += c;
      }
      value = quoted + "\"";
    }
    out += entry.name + " = " + value + "\n";
  }
  std::ofstream(path, std::ios::binary | std::ios::trunc) << out;
}

void SsxApplyStartupSettings() {
  // The default RTV render-target path copies EDRAM between host render targets
  // by whole tile rows; when the cutscene UI renders into a small target at row
  // 681, the main 1280x720 target only gets rows 0-687 back and the helicopter
  // cutscenes show a black strip at the bottom. The ROV path keeps EDRAM as one
  // buffer like the console (and measured faster in races). An explicit choice
  // in ssx.toml wins.
  if (rex::cvar::GetFlagSource("render_target_path_d3d12") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("render_target_path_d3d12", "rov");
  }
  if (REXCVAR_GET(ssx_render_resolution) == "720p") {  // replaced by high, see sub_82FB6120
    rex::cvar::SetFlagByName("ssx_render_resolution", "high");
  }
  // FSR 1 upscales the game's output to the window much more sharply than bilinear,
  // at almost no cost.
  if (rex::cvar::GetFlagSource("present_effect") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("present_effect", "fsr");
  }
  // With ROV the GPU thread otherwise runs screen-space vertex shaders on the CPU;
  // SSX is bound by that thread, and letting the host GPU do it measured +12% in races.
  if (rex::cvar::GetFlagSource("execute_unclipped_draw_vs_on_cpu_for_psi_render_backend") ==
      rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu_for_psi_render_backend", "false");
  }

  // Antarctica's cold: the game decides sun or shade by reading a 32x16 render target on the
  // CPU (resolve to 0x0AB42000 on Mount Craddock). Without readback it always reads shade and
  // the rider freezes in the sun. Only tiny resolves are read back, one frame late, so nothing
  // stalls; large ones are never read by the CPU and reading them back corrupted memory.
  if (rex::cvar::GetFlagSource("readback_resolve") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("readback_resolve", "fast");
  }
  if (rex::cvar::GetFlagSource("readback_resolve_max_size") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("readback_resolve_max_size", "4096");
  }
  // Flare light textures (night Deadly Descents, e.g. Denali > Emergency Exit): at load the game
  // renders each flare into a 512x256 target, resolves it with mips and compresses it to DXT1 on
  // the CPU. Without the resolved data in memory the flares projected noise and stale blocks onto
  // the snow. These one-off resolves are read back right away; per-frame ones stay delayed.
  if (rex::cvar::GetFlagSource("readback_resolve_one_off_sync") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("readback_resolve_one_off_sync", "true");
  }

  // Shadow maps (depth-only passes at EDRAM pitch 1040: 1024x1024 cascades, and 512x512 with
  // 4x MSAA) are drawn and resolved at 1x with resolution scaling. At 4x they cost ~4.7 of
  // 23 ms per frame scaled, ~0.7 ms at 1x (Grand Golliat start line).
  if (rex::cvar::GetFlagSource("rov_unscaled_depth_pitch") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("rov_unscaled_depth_pitch", "1040");
  }

  // Scales above 4 are untested in SSX.
  // (resolution_scale lives in the GPU plugin, which must be loaded by now.)
  if (rex::string::from_string<int32_t>(rex::cvar::GetFlagByName("resolution_scale"), 1) > 4) {
    REXLOG_WARN("resolution_scale above 4 is untested in SSX; using 4");
    rex::cvar::SetFlagByName("resolution_scale", "4");
  }

  ApplyVolume(REXCVAR_GET(ssx_volume));
  rex::cvar::RegisterChangeCallback("ssx_volume", [](std::string_view, std::string_view value) {
    ApplyVolume(rex::string::from_string<int32_t>(value, 100));
  });
}

// Runs after `li r11,2` at 0x8233CB10: the device asked for present interval 2
// (every other vblank of the 60 Hz emulated display = 30 fps). Above 60 fps
// the swap doesn't wait for vblank at all (interval 0) and the main-loop task
// below paces frames; the runtime caps the emulated display at 240 Hz, so
// vblank pacing can't go higher.
void SsxPresentInterval(PPCRegister& r11) {
  int32_t fps = SsxTargetFps();
  r11.u64 = fps <= 30 ? 2 : fps <= 60 ? 1 : 0;
}

// The main loop is a periodic task (vtable 0x82007738) run by the scheduler
// loop in sub_83047120: each pass, sub_83047248 ticks the task once its period
// (ns, u64 at +72) has elapsed, passing the measured frame time (+132) to the
// game, so the simulation keeps real-time speed at any rate. The game keeps
// the period at 1/60 s; rewrite it before every pass.
REX_EXTERN(__imp__sub_83047248);
REX_HOOK_RAW(sub_83047248) {
  uint32_t task = ctx.r3.u32;
  int32_t fps = SsxTargetFps();
  if (fps > 60 && REX_LOAD_U32(task) == 0x82007738) {
    REX_STORE_U64(task + 72, uint64_t(1000000000 / fps));
  }
  __imp__sub_83047248(ctx, base);
}

// Scheduler clock (ns) for the tasks above; the original is a millisecond tick
// ((ms + 1) * 1e6), which can't pace periods under ~4 ms. Only the scheduler
// (sub_830469F8..sub_83047120) calls it, and only differences are used.
static uint64_t SchedulerClockNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
REX_HOOK(sub_83047368, SchedulerClockNs)

// sub_82FB6120(r4 = quality mode 1-8, r5/r6 = fallback size, r7/r8 = out width/height).
// Gameplay uses mode 4, 1120x584. The renderer places its targets at fixed EDRAM
// tiles (color 0, depth 720, half-res buffers 1440-2048) and one pass puts a
// full-width depth buffer at tile 1440, leaving 608 of the 2048 tiles (80x16
// pixels each) for it. 1280x720 needs 16 x 45 = 720 there: rows 608-719 wrap onto
// the top of the lighting buffer, giving hard-edged light/shadow rectangles on
// the snow. 1200x640 needs 15 x 40 = 600 and fits every pass in the layout.
REX_EXTERN(__imp__sub_82FB6120);
REX_HOOK_RAW(sub_82FB6120) {
  static std::set<uint32_t> seen;
  uint32_t mode = ctx.r4.u32;
  if (seen.insert(mode).second) {
    REXLOG_INFO("Render size query: mode {} (fallback {}x{})", mode, ctx.r5.u32, ctx.r6.u32);
  }
  static const std::string setting = REXCVAR_GET(ssx_render_resolution);
  if (setting == "high" && mode >= 2 && mode <= 7) {
    REX_STORE_U32(ctx.r7.u32, 1200);
    REX_STORE_U32(ctx.r8.u32, 640);
    return;
  }
  __imp__sub_82FB6120(ctx, base);
}
