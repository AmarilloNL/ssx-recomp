// ssx - PC settings and the guest hooks that apply them.
//
// All settings are cvars, so they load from / save to ssx.toml next to the exe
// and show up in the SDK's F4 overlay as well as the SSX settings menu.
// Guest addresses in comments are the disc release's; ssx_guest.h has the title update's too.

#include "ssx_settings.h"
#include "ssx_guest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <rex/audio/downmix.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/string/numeric.h>

#include "generated/default/ssx_pch.h"


REXCVAR_DEFINE_INT32(ssx_framerate, 60, "SSX",
                     "Frame rate: 30 (original) to 360, or 0 = match the main display")
    .range(0, 360);
// 3x has its own, lower choices: no GPU holds more there everywhere, and a frame rate that keeps
// jumping feels like stutter (user tests: 3x 60 smooth, 70 stutters; 4x, since removed, stuttered
// above 40).
REXCVAR_DEFINE_INT32(ssx_framerate_3x, 60, "SSX", "Frame rate at 3x resolution scale: 30 to 60")
    .range(30, 60);
REXCVAR_DEFINE_STRING(ssx_render_resolution, "high", "SSX",
                      "3D render size: original (1120x584) or high (1200x640) (restart)")
    .allowed({"original", "high", "720p"})  // 720p: older configs, now means high
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(ssx_volume, 100, "SSX", "Master volume (0-100)").range(0, 100);

namespace {

void ApplyVolume(int32_t volume) { rex::audio::SetOutputGain(volume / 100.0f); }

}  // namespace

int32_t SsxTargetFps() {
  // The scale needs a restart: read it once.
  static const int32_t scale =
      rex::string::from_string<int32_t>(rex::cvar::GetFlagByName("resolution_scale"), 1);
  if (scale >= 3) return std::clamp(REXCVAR_GET(ssx_framerate_3x), 30, 60);
  int32_t fps = REXCVAR_GET(ssx_framerate);
  if (fps == 0) {  // match the main display
#ifndef _WIN32
    // Note: 60 on Linux; ask the windowing layer for the refresh rate if players want more.
    fps = 60;
#else
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    fps = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
              ? int32_t(mode.dmDisplayFrequency)
              : 60;
#endif
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
  // Vulkan (Linux) the same way: its render-target path sends the game's picture to the screen
  // from the wrong surface after some draws (blank screen in races; SDK issue #508), FSI doesn't.
  if (rex::cvar::GetFlagSource("render_target_path_vulkan") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("render_target_path_vulkan", "fsi");
  }
  if (REXCVAR_GET(ssx_render_resolution) == "720p") {  // replaced by high, see sub_82FB6120
    rex::cvar::SetFlagByName("ssx_render_resolution", "high");
  }
  // FSR 1 upscales the game's output to the window much more sharply than bilinear,
  // at almost no cost.
#ifdef _WIN32
  if (rex::cvar::GetFlagSource("present_effect") == rex::cvar::Source::kDefault) {
    rex::cvar::SetFlagByName("present_effect", "fsr");
  }
#else
  // Note: the Vulkan renderer's FSR/CAS output comes out almost black (max 1 of 255), so
  // Linux always scales bilinear; fix that pass in the SDK if sharper scaling is wanted there.
  rex::cvar::SetFlagByName("present_effect", "bilinear");
#endif
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

  // 3x at most: 4x was removed (too slow for a steady frame rate on any GPU), higher is untested.
  // (resolution_scale lives in the GPU plugin, which must be loaded by now.)
  if (rex::string::from_string<int32_t>(rex::cvar::GetFlagByName("resolution_scale"), 1) > 3) {
    REXLOG_WARN("resolution_scale above 3 is not supported in SSX; using 3");
    rex::cvar::SetFlagByName("resolution_scale", "3");
  }

  ApplyVolume(REXCVAR_GET(ssx_volume));
  rex::cvar::RegisterChangeCallback("ssx_volume", [](std::string_view, std::string_view value) {
    ApplyVolume(rex::string::from_string<int32_t>(value, 100));
  });
}

// Runs after `li r11,2` at 0x8233CB10: the device asked for present interval 2
// (every other vblank of the 60 Hz emulated display = 30 fps). Above 30 fps
// the swap doesn't wait for vblank at all (interval 0) and the main-loop task
// below paces frames. At 60, vblank waits on top of the 1/60 s task period
// sent frames out in 30 + 3 ms pairs whenever the GPU lagged a little.
void SsxPresentInterval(PPCRegister& r11) {
  int32_t fps = SsxTargetFps();
  r11.u64 = fps <= 30 ? 2 : 0;
}

// The main loop is a periodic task (vtable 0x82007738) run by the scheduler
// loop in sub_83047120: each pass, sub_83047248 ticks the task once its period
// (ns, u64 at +72) has elapsed, passing the measured frame time (+132) to the
// game, so the simulation keeps real-time speed at any rate. The game keeps
// the period at 1/60 s; rewrite it before every pass.
// Main-loop period from the adaptive pacing below (SsxPaceFrameEnd), 0 until set.
static std::atomic<int64_t> pace_period_ns{0};

REX_EXTERN(SSX_FN_TASK_TICK_IMP);
REX_HOOK_RAW(SSX_FN_TASK_TICK) {
  uint32_t task = ctx.r3.u32;
  int32_t fps = SsxTargetFps();
  if (fps > 30 && REX_LOAD_U32(task) == SSX_VTABLE_MAIN_LOOP_TASK) {
    int64_t period = pace_period_ns.load(std::memory_order_relaxed);
    REX_STORE_U64(task + 72, uint64_t(std::max<int64_t>(period, 1000000000 / fps)));

  }
  SSX_FN_TASK_TICK_IMP(ctx, base);
}

// Adaptive pacing when the host GPU can't reach the target rate (e.g. 4x resolution scale):
// about 15 times a second the game blocks until the GPU is done with something the previous
// frame used (game code 0x8273859C -> sub_8232C258, a D3D block on fence), and with a full
// GPU queue that costs a whole extra frame, then the next frame goes out at once (35 + 3 ms
// pairs at 4x). Pacing the main loop a little below what the GPU manages keeps the queue
// short: those blocks then return at once and frames come out evenly.
// The signal is the time the render thread spends waiting for the GPU in the game's
// per-frame throttle (sub_8233D770) and in those blocks: near 0 when paced, ~a frame when
// GPU-bound. Bound: pace at 1.04x the measured frame time (or nudge the pace up). Otherwise,
// after a second, ease back towards the target: quickly (~5%/s) down to 4% above the lowest
// pace that was GPU-bound in the last 10 s (a heavy moment shouldn't keep it slow), then
// slowly (~0.25%/s) to follow lighter scenes. When the GPU is only a little short (within
// 15% of the target, e.g. 3x at 60 fps), remember that pace for 2 s instead: the heavy
// moment is usually over by then, and a retry costs a few pair frames, not 10 s at ~53 fps.
static std::atomic<int64_t> gpu_wait_ns{0};

static void AddGpuWait(std::chrono::steady_clock::time_point start) {
  gpu_wait_ns.fetch_add(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
          .count(),
      std::memory_order_relaxed);
}

REX_EXTERN(SSX_FN_FRAME_THROTTLE_IMP);
REX_HOOK_RAW(SSX_FN_FRAME_THROTTLE) {
  auto start = std::chrono::steady_clock::now();
  SSX_FN_FRAME_THROTTLE_IMP(ctx, base);
  AddGpuWait(start);
}

REX_EXTERN(SSX_FN_FENCE_BLOCK_IMP);
REX_HOOK_RAW(SSX_FN_FENCE_BLOCK) {
  auto start = std::chrono::steady_clock::now();
  SSX_FN_FENCE_BLOCK_IMP(ctx, base);
  AddGpuWait(start);
}

void SsxPaceFrameEnd() {
  using clock = std::chrono::steady_clock;
  static clock::time_point last = clock::now(), hold_until = clock::now(), floor_time;
  static double frame_avg = 0.0, period = 0.0, floor_period = 0.0;
  static uint32_t bound_frames = 0;
  auto now = clock::now();
  double frame = double(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count());
  last = now;
  double wait = double(gpu_wait_ns.exchange(0, std::memory_order_relaxed));
  double target = 1e9 / SsxTargetFps();
  if (SsxTargetFps() <= 30 || frame > 250e6) {  // vblank pacing, or a loading hitch
    period = target;
    frame_avg = 0.0;
  } else {
    auto floor_memory = floor_period > target * 1.15 ? std::chrono::seconds(10) : std::chrono::seconds(2);
    // A frame over 2.2x the pace (or the average frame time, when the GPU is slower than the
    // pace) is a one-off spike (streaming, a shader compile), not the GPU's pace: left out, or
    // a 0.2 s spike at 60 fps paced the game at ~43 fps for 15 s. Pair frames stay under 2x.
    // 8 in a row aren't a spike but a heavier scene.
    static uint32_t hitch_run = 0;
    bool paced = period > target * 1.001;
    bool long_frame = frame_avg > 0.0 && frame > std::max(period, frame_avg) * 2.2;
    hitch_run = long_frame ? hitch_run + 1 : 0;
    bool hitch = long_frame && hitch_run <= 8;
    if (!hitch) {
      frame_avg = frame_avg > 0.0 ? frame_avg * 0.9 + std::min(frame, frame_avg * 2) * 0.1 : frame;
    }
    // GPU-bound: the render thread waited over a quarter of the frame, 3 frames in a row,
    // and frames come slower than the pace. A wait alone isn't enough: with GPU headroom
    // (2x) the fence blocks still wait out the GPU's latency each frame.
    bool bound = !hitch && wait > frame * 0.25 && frame_avg > std::max(period, target) * 1.02;
    bound_frames = bound ? bound_frames + 1 : hitch ? bound_frames : 0;
    // Or a pair starts: one wait longer than a whole pace period means the GPU is over a frame
    // behind. The pair (35 + 2 ms) averages out to the pace, so the test above misses it.
    // Unpaced, 3 within a second (30 + 3 ms pairs at a 60 fps cap).
    static clock::time_point late_start;
    static uint32_t late_count = 0;
    bool late = !hitch && wait > period * 1.25;
    if (late) {
      if (now - late_start > std::chrono::seconds(1)) {
        late_start = now;
        late_count = 0;
      }
      ++late_count;
    }
    bool pair = late && (paced || late_count >= 3);
    if (bound_frames >= 3 || pair) {
      bound_frames = 0;
      late_count = 0;
      // Unpaced, the average frame time is what the GPU manages; once paced, nudge up
      // (more if the frames already take longer than the pace).
      double bound_at = paced ? period : frame_avg;
      if (bound_at < floor_period || now - floor_time > floor_memory) {
        floor_period = bound_at;
        floor_time = now;
      }
      period = paced ? std::max(period * 1.01, frame_avg * 1.02) : frame_avg * 1.04;
      hold_until = now + std::chrono::seconds(1);
    } else if (now > hold_until) {
      bool floor_fresh = now - floor_time < floor_memory;
      period *= period > floor_period * 1.04 || !floor_fresh ? 0.999 : 0.99995;
    }
    period = std::max(period, target);
    // Only pace below 150 fps: faster, the hitch is too short to feel and pacing costs
    // ~12% fps (measured at 1x and 2x). 2x at ~105 fps unpaced swung between 3 and 20 ms
    // frames, which shows.
    if (period < 6.67e6) period = target;
  }
  pace_period_ns.store(int64_t(period), std::memory_order_relaxed);
}

// Scheduler clock (ns) for the tasks above; the original is a millisecond tick
// ((ms + 1) * 1e6), which can't pace periods under ~4 ms. Only the scheduler
// (sub_830469F8..sub_83047120) calls it, and only differences are used.
static uint64_t SchedulerClockNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
REX_HOOK(SSX_FN_SCHEDULER_CLOCK, SchedulerClockNs)

// sub_82FB6120(r4 = quality mode 1-8, r5/r6 = fallback size, r7/r8 = out width/height).
// Gameplay uses mode 4, 1120x584. The renderer places its targets at fixed EDRAM
// tiles (color 0, depth 720, half-res buffers 1440-2048) and one pass puts a
// full-width depth buffer at tile 1440, leaving 608 of the 2048 tiles (80x16
// pixels each) for it. 1280x720 needs 16 x 45 = 720 there: rows 608-719 wrap onto
// the top of the lighting buffer, giving hard-edged light/shadow rectangles on
// the snow. 1200x640 needs 15 x 40 = 600 and fits every pass in the layout.
REX_EXTERN(SSX_FN_RENDER_SIZE_IMP);
REX_HOOK_RAW(SSX_FN_RENDER_SIZE) {
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
  SSX_FN_RENDER_SIZE_IMP(ctx, base);
}
