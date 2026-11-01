# SSX (2012) PC port - progress log

Static recompilation of the Xbox 360 `default.xex` (title 4541096D, media 55ABAB6C, v0.0.0.2)
using the ReXGlue SDK (prebuilt nightly 0.10.0.24 in `tools/rexglue-sdk/win-amd64`).

## Layout
- `game-dump/files/` - extracted ISO (user-supplied, never committed)
- `ssx_manifest.toml` - project manifest; `ssx_config.toml` - manual analysis overrides
- `src/ssx_hooks.cpp` - native replacements for guest functions
- `generated/default/` - codegen output (regenerated, never committed)

## Build
    $env:PATH = "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"
    cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH="$PWD\tools\rexglue-sdk\win-amd64"
    cmake --build out/build/win-amd64-relwithdebinfo

## Log
- 2026-11-01: ISO extracted, project scaffolded. Codegen: 3 unresolved tail-call targets
  (added as functions), one shared `li r3,0; blr` tail at 0x82BF793C (chunk of 0x82BF7944),
  `mfvscr` in sub_83046110 replaced by a hook returning NJ=1. Codegen clean.
- 2026-11-01: Runtime crashes on calls to undiscovered functions (vtable thunks, static
  initializers, leaf callbacks without .pdata). `scripts/find_missing_funcs.py` scans a dumped
  image (run with SSX_DUMP_IMAGE=path) for pointer tables and lis/addi-built code addresses;
  `scripts/drop_splitting_funcs.py` removes entries that codegen shows are interior labels.
  Enabled the xenos (D3D12) GPU plugin. Game boots and renders the front-end scene at 45 s.
  `scripts/test-run.ps1` = timed muted launch + screenshot; `play.bat` = user launcher.

## Expected log noise
- `cache:\` device not found (deliberate in SDK), `D:\...delete_in_ship\*.dds` / `.fxo`
  (dev loose files, data comes from .big), `!\lua\...` (Lua package.path probing).
- 2026-11-01: Tutorial crash = access violation in rexruntime InputSystem::RefreshDevices
  (unlocked device vector rebuilt per call; SSX polls input from two threads). Fixed in
  `src/ssx_input_lock.cpp`: the exe defines the 4 used `__imp__XamInput*` symbols itself
  (shadowing the DLL exports), takes one lock, forwards via GetProcAddress. Also enabled
  `gpu_allow_invalid_fetch_constants` (two type-0 texture fetch constants per frame).
  SDK source for reading lives in tools/rexglue-src (commit bd833a2, matches the prebuilt).
- 2026-11-01: PC features.
  - DLC: packages dropped in <user data>/dlc are installed via ContentManager::InstallContent
    on startup (`src/ssx_dlc.cpp`). Mt. Eddie + pre-order packs installed; in-game unverified.
  - 60 fps: midasm hook after `li r11,2` at 0x8233CB10 (present interval) -> ssx_framerate.
  - Render size: sub_82FB6120 maps quality mode -> scene size; gameplay uses mode 4 = 1120x584
    (fits 2xMSAA in 10 MB EDRAM). Hooked to ssx_render_resolution (original | 720p, default 720p).
    Above 720p fails: 1440p glitches everywhere (doubled geometry) and crashes on race load,
    2160p crashes at boot. 720p and 60 fps confirmed good in races by the user (2026-11-01). Source: xenia-canary
    game-patches 4541096D (Margen67). Runtime `resolution_scale` >1 removes the D3D12 device
    (INVALID_CALL) -> not exposed.
  - Settings menu (F1, `src/ssx_settings_dialog.cpp`): cvars, saved by SsxSaveConfig (only
    config/runtime-sourced values, escaped; rex::cvar::SaveConfig also persists CLI flags
    and writes unescaped strings). Keyboard (mnk_mode) on by default.
  - Launcher: OnFinalizePaths locates the game (config, ./game, ./*.iso, file picker). An ISO is
    mounted as \Device\Cdrom0 in OnLoadXexImage with game:/d: relinked; an empty stand-in
    default.xex in <user data>/iso-mount satisfies ReXApp's host-side existence check.
  - `scripts/package.ps1` -> dist/SSX (Release exe + rexruntime.dll + rexgpu-xenos.dll + README).
  - VdSwap wrapper (`src/ssx_frame_stats.cpp`) logs fps and front buffer size (black-bar hunt:
    front buffer stays 1280x720 at the title screen; bar not yet reproduced).
- 2026-11-01: Settings menu "Restart now" (src/ssx_restart.cpp): relaunch with the same args +
  --ssx_wait_pid; the new instance waits for the old one to exit before opening user files.
- 2026-11-01: Frame rate above 60. The 60 cap is the main-loop periodic task (vtable 0x82007738)
  run by the scheduler loop sub_83047120, which spins on sub_82356C38 (NtYieldExecution) until
  the task period (u64 ns at +72, kept at 1/60 s by the game) elapses; sub_83047248 then ticks
  it with the measured frame time (float at +132), so sim speed is frame-rate independent.
  Fix: hook sub_83047248 to rewrite +72 = 1e9/fps each pass; hook the scheduler clock
  sub_83047368 ((ms+1)*1e6) with a ns steady clock; present interval 0 above 60 (runtime clamps
  video_mode_refresh_rate to 24..240, so vblank pacing can't reach 300). Measured exact
  30/60/144/300 fps at the title screen; ssx_framerate 0 = match main display. Live, no restart.
  User confirmed 300 fps plays great in races.
- 2026-11-01: Building the SDK from source (tools/rexglue-src @ bd833a2, presets `ssx` /
  `ssx-release` in CMakeUserPresets.json, REXSDK_DIR in-tree). Setup notes:
  `git submodule update --init --recursive --depth 1` (skip thirdparty/moltenvk), then replace
  git symlink placeholders with their targets (Windows checkout writes them as text files;
  libmspack's cabextract/mspack/* broke the build), then apply patches/*.patch.
  - patches/rexglue-0001: D3D12 texture upload copied a padded mip footprint past the scaled
    subresource (CopyTextureRegion DSTREGIONOUTOFBOUNDS -> device removed INVALID_CALL) whenever
    resolution_scale > 1. Ported Xenia's later clamp. Found with Graphics Tools (debug layer)
    + an OutputDebugString listener.
  - With the patch, resolution_scale 1/2/3 run with zero debug-layer errors at the title
    screen (60 fps). Re-exposed in the F1 menu as "Resolution scale" (restart). Release at 2x
    with ssx_framerate=300 reaches ~165 fps at the title screen (1x: 300).
  Fix: quit via window()->RequestClose() (ReXApp::OnClosing terminates the title and
  std::_Exit's); app_context().QuitFromUIThread() left the old process running.
- 2026-11-01: resolution_scale 3 removes the D3D12 device (INVALID_CALL) ~30 s into a race
  (another scaled path; not investigated, ran ~54 fps anyway). Menu offers 1x/2x; startup
  clamps >2 to 2. To read plugin cvars in OnPreSetup the app now loads the GPU plugin itself
  (config.graphics = LoadGpuPlugin) so its cvars are registered first.
- 2026-11-01 (in progress): 2x race crash = CopyTextureRegion 256x256 into 128x128 (R8G8B8A8),
  reproduced via SSX_TEST_INPUT scripted pad (scripts/test-run.ps1, isolated test-userdata).
  patches/rexglue-0001 now clamps uploads against the real resource size and logs
  "Texture upload larger than its texture" once. Earlier retest was INVALID: the in-tree plugin
  DLL wasn't restaged (only staged when ssx relinks) -> added ssx_stage_gpu_plugin target.
  Verified: 400 s scripted run into a race at 2x with --d3d12_debug: 0 errors, warning fired
  once ("copy 256x256 into level 0 of 128x128, guest 128x128, scaled_resolve true, format 6"),
  i.e. a scaled-resolve texture created unscaled; the clamp keeps the device alive (that one
  texture may look off). Root cause in texture creation not yet found. Repackaged.
  Black bar SOLVED: helicopter cutscenes render the scene full 720 rows, but the cutscene UI
  draws into a small render target overlapping EDRAM at row ~681. On the default RTV path the
  render target cache then transfers EDRAM ownership back by whole tile rows only up to 688
  (RenderDoc: full-width transfer quads y=[0,688]), so rows 688-719 of the final image are lost.
  The ROV path keeps EDRAM as one buffer: no strip, and it measured faster in a 2x race
  (avg 73 vs 62 fps). SsxApplyStartupSettings defaults render_target_path_d3d12=rov unless the
  config sets it. Tools used: RenderDoc 1.47 portable (tools/renderdoc) launched via
  renderdoccmd, capture bracketed by StartFrameCapture/EndFrameCapture between two VdSwaps,
  analysed with qrenderdoc --python scripts (post-VS positions per draw).
- 2026-11-01: Performance.
  - Races are bound by the guest main thread at 1x (it spends much of its time in
    sub_82335DE0 / sub_8232BD48 = D3D waiting for the GPU ring buffer read pointer, i.e. on the
    GPU emulation thread) and by the host GPU at 2x (RTX 4080 at 99%: ROV/EDRAM emulation at 4x
    pixels). Measured with scripts in the session scratchpad: per-thread CPU sampling, an
    instruction-pointer sampler with PDB symbols, nvidia-smi, scripted-race benchmark
    (mid-race 190-260 s window).
  - execute_unclipped_draw_vs_on_cpu_for_psi_render_backend=false (now default): 176 -> 197 fps
    at 1x; identical image. occlusion_query_enable=false: no change. readback_memexport=false:
    slower (151).
  - codegen: cr/ctr/xer as locals enabled (no measurable change, kept). reserved_as_local +
    non_argument_as_local + skip_msr together hang the game at boot (likely r13 kept local while
    kernel calls read ctx.r13) - leave off.
- 2026-11-01: FSR + menu redesign.
  - REXGLUE_ENABLE_FIDELITYFX=ON in both presets: fetches rexglue/FidelityFX-SDK and unlocks
    present_effect cas/fsr (FSR 1 EASU+RCAS shaders are built into the D3D12 presenter).
    patches/rexglue-0002 drops the UTF-16 version .rc that llvm-rc can't read. The runtime
    imports AMD's prebuilt amd_fidelityfx_dx12(drel).dll: package.ps1 copies
    _deps/fidelityfx-src/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll next to the SDK tools and the
    game (the dev build needed it copied by hand as amd_fidelityfx_dx12drel.dll).
  - present_effect defaults to fsr (verified visibly sharper than bilinear at 1x).
  - Settings menu redesign (src/ssx_settings_dialog.cpp): Segoe UI fonts via OnConfigureFonts,
    scoped SSX colour theme, header, Display/Graphics/Audio/Controls tabs, two-column rows with
    help text, upscaling + sharpness, Esc closes. SSX_OPEN_SETTINGS=<tab> opens it at startup
    (testing aid, used for screenshots).
- 2026-11-01: Settings menu controller support: PollPad reads pad 0 via InputSystem::GetStateForUI
  (bypasses the menu's input blocker) under the same lock as the guest's XAM input calls
  (SsxReadPadForUI in ssx_input_lock.cpp - an unlocked read would reintroduce the device-list
  race), feeds ImGui gamepad nav keys; Back+Start toggles, LB/RB tabs, B closes. Not testable
  with SSX_TEST_INPUT (that only feeds the guest); needs a real pad.
- 2026-11-01: Fresh-install link failure (undefined PPCImageConfig / sub_*): generated/rexglue.cmake
  only adds the recompiled sources if generated/default/sources.cmake exists at configure time,
  and on a fresh tree codegen first runs during the build. package.ps1 now configures, builds
  ssx_codegen, then configures again (verified on a tree with generated/default removed).
  Installer redesigned as a wizard (Welcome / Your game / Install / Play): painted header, step
  rail, determinate progress from "==> " stage lines plus ninja [n/N] counters, stage checklist,
  collapsible log, desktop shortcut (icon = dist/SSX/ssx.ico), taskkill /T on cancel.
