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
- 2026-11-02: Hard-edged light/shadow rectangles on the snow (user report, also at 1x). Cause: the
  720p render size. The renderer uses fixed EDRAM bases (color 0, depth 720, half-res buffers
  1440/1624/1808) and one pass binds a full-width depth buffer at tile 1440: at 1280 wide that
  leaves 608 rows before wrapping past tile 2048 onto the lighting buffer at tile 0, so rows
  608-719 and the top ~112 rows of the light pre-pass broke (both edges seen in screenshots and
  in a RenderDoc capture of the light buffer). Found with RenderDoc (RTV-path capture, per-draw
  light buffer dumps) plus a temporary RB_SURFACE_INFO / RB_*_INFO base log in the render target
  cache. New render size "high" = 1200x640 (15 x 40 = 600 tiles at 1440, every pass fits; log
  verified); old "720p" configs map to it. The xenia-canary 720p patch has the same overflow.
  Note: frame-rate measurements that day were contaminated by another game running on the PC.
- 2026-11-02: 2x resolution scale showed stale textures in the menus after a race (SSX title logo
  in event icons, map markers and gear thumbnails; empty event bubbles). Cause (upstream Xenia
  behaviour too): a texture is "scaled" if any page of its range holds scaled resolve data, and
  only pages the CPU actually rewrites lose that mark. When the game reuses a resolve region for
  loaded assets, untouched pages stay marked and the new textures sample the stale upscaled
  buffer. patches/rexglue-0003: record resolve regions; a CPU write into one drops the whole
  region. Verified (temp log): a 512 KB region whose first page alone was rewritten is cleared;
  the "Broken Elbow" event bubble went from empty to its skull icon after a race at 2x.
  build-ssx.ps1 now applies patches not yet applied on every run (git apply --check --reverse),
  so existing installs get new patches.
- 2026-11-04: 2x snow glitch (blocky light patches): the SDK's 3D-as-2D texture wrapper was
  created at the unscaled size while loading resolution-scaled data (the upload clamp kept a
  quarter of it) and was never reloaded after its first load. patches/rexglue-0004 sizes it
  scaled and reloads it when outdated. Verified: clamp warning gone; Mount Eddie at 2x clean.
  2x GPU profile (temporary timestamp profiler per EDRAM layout, Mount Eddie start line):
  11.9 ms/frame vs 5.5 at 1x; shadow cascades are cheap; ~3 ms/frame is reloading a 128x128
  scaled texture during a ~12-step ping-pong effect (160-pitch RT at EDRAM 332). Next target.
- 2026-11-04: Issue #1 (build too heavy). Real disk use was ~10 GB, not 30. The builder now
  downloads a prebuilt ReXGlue SDK package (15 MB zip, release sdk-1; made by
  scripts/build-sdk-package.ps1: SDK commit + patches, Release, CMake install rules, AMD's signed
  FidelityFX DLL, bundled library licenses) instead of cloning and compiling the SDK; preset
  ssx-player. Git is no longer needed; tools fall back to official downloads without winget
  (CMake/Ninja portable zips, LLVM MSI, VS bootstrapper). Clean-folder test: 3.5 min to ssx.exe
  (codegen 137 s), 1 GB folder, title screen 60 fps. Old installer-made SDK copies
  (.ssx-ready) are removed on update.
  Testing aid: SSX_TEST_INPUT_FILE=path presses each appended line live, with left-stick
  directions (lleft/lright/lup/ldown) for the globe.
- 2026-11-04: 2x frame rate. The ~3 ms/frame was a 128x128x256 3D k_8_8_8_8 texture (16 MB,
  64 MB at 2x) that the game renders into one slice at a time; every slice write invalidated
  the whole volume, so it was fully reloaded ~8 times a frame. patches/rexglue-0005: volume
  textures (3D, tiled, 16+ slices, no mips) record the byte range written since their last load
  through a global shared-memory watch, and a reload loads only those slices. 3D tiling stores
  every 4 slices as one contiguous block and a z shift by a multiple of 8 keeps the bank swizzle,
  so the slice range is a plain source offset (GetTiledOffset3D(0, 0, z_first)) plus a copy to
  that Z. Mount Eddie at 2x / 300 fps cap: start line 85 -> ~120 fps, ride ~116 fps; snow trail
  and footprints correct at 2x and 1x (1x ride ~190 fps). SDK package sdk-2.
- 2026-11-04: Issue #1 follow-up: "This ISO is not the supported SSX release" with an empty hash.
  Reading default.xex threw a .NET exception, which in PowerShell 5.1 only aborts that statement,
  so the check ran on a missing file. build-ssx.ps1 now catches it and reports the real error
  (tested with a locked ISO), and checks the extracted file still exists (antivirus). The
  installer reads the script's output in the OEM code page (Cyrillic messages were garbled).
- 2026-11-04: Antarctica cold bug (user report, Mount Craddock "Root Down: TRICK IT"): the rider
  froze in the sun with the solar panel on. The game decides sun or shade by reading a 32x16
  RGBA8 render target on the CPU (resolve to 0x0AB42000, every other frame); without resolve
  readback the CPU never sees it. Found by logging every resolve (address, length, format, size)
  and enabling readback per address while measuring the CORE TEMP bar in screenshots
  (scratchpad live/rootdown.ps1 + coretemp.ps1): no readback 0.97 -> 0 in 10 s in the sun; with
  only that resolve read back it stays up and drops only in the ice tunnels.
  readback_resolve=fast crashed while loading and full halved the frame rate (it waits for the
  GPU on every resolve). patches/rexglue-0006: delayed readback keeps 4 slots per resolve and
  copies only the newest one the GPU has finished (2 slots read copies still in flight at high
  frame rates: garbage); buffers replaced while a copy may be in flight are released after the
  submission; new cvar readback_resolve_max_size. The crash: 3D slice resolves (the snow volume)
  report a written range ~0x80000 that runs 0x880 bytes past the volume, and delayed readback
  wrote older data over the memory after it (bisected: cap 448 KB loads, 520 KB crashes). SSX
  defaults: readback_resolve=fast, readback_resolve_max_size=4096. Measured at 300 fps cap:
  Root Down 1x ~190-240 fps (no readback ~220), 2x ~120-130; Mount Eddie 2x start line 129 avg
  (was 123). SDK package sdk-3.
- 2026-11-04: Issue #1 log: Add-Type -TypeDefinition (the C# XDVDFS helper) failed on the
  reporter's PC with "A required privilege is not held by the client" (Win32Exception from
  compiling with csc; security software or policy). The ISO reader is now plain PowerShell
  (same logic; extracts default.xex in ~60 ms, hash verified; locked-file and non-Xbox errors
  tested). No other compiled helpers are used by the builder.
- 2026-11-05: Issue #2: "configure failed" right after "cmake: found", with no CMake output in
  build-ssx.log (Windows PowerShell's transcript skips what native programs print). package.ps1
  now runs CMake through Invoke-CMake, which passes its output (stderr by its raw text) through
  PowerShell so it lands in the log; exit codes unchanged. build-ssx.ps1 checks cmake --version
  and downloads a portable CMake when the PC's is older than 3.25 (presets v6); portable tools now
  come first on PATH. Tested: a bad preset's CMake error appears in the transcript, the version
  check (4.4.4 ok, fake 3.20.6 / unreadable -> old), and a full ssx-player build from the sdk-3
  package (exit 0, ninja progress in the log, ssx.exe reaches 300 fps at the title screen).
  Their "SSX (World)" ISO passed the default.xex check (same release).
- 2026-11-05: Yellow specks on dark rock (user report, Africa, Meru "Red Light: TRICK IT"). Same at
  1x/2x, ROV/RTV and without resolve readback. RenderDoc pixel history: the last post pass (scene,
  bloom, then a 32x32x32 color-grading table sampled at g*31/32 + 0.5/32) turned near-black pixels
  yellow; entry 0 of every axis of the table held the brightest value (0xFF instead of ~0x10), so
  colors below 1/32 blended it in. The tables (linear 3D, physical 0x0B078000..0x0B177FFF) are built
  on the CPU and memcpy'd in; their raw guest memory already had the bad entry. Cause: the
  recompiler's vexptefp (2^x estimate) builds 2^floor(x) in the exponent bits, which is garbage
  outside [-126, 127] and NaN for -inf, so pow(0, y) = exp2(log2(0) * y) was NaN instead of 0.
  patches/rexglue-0007: clamp, then 0 below -126 (incl. -inf), +inf from 128, NaN kept; checked on
  edge values against exp2f. 69 vexptefp sites in 26 generated files. Verified: table entry 0 now
  0x10, cave walls clean. SDK package sdk-4 (codegen runs on the player's PC, so the fix ships
  in the SDK). Found with a temporary log of the host PC at write-watch faults in the table range
  (llvm-symbolizer on ssx.pdb -> the memcpy) and a dump of guest memory from the VdSwap hook.
  Still open from the same report: an Africa peak label drawn as a solid box (once, not
  reproduced in 13 tries; possibly the same vexptefp bug in a UI effect).
- 2026-11-06: Issue #2 again, now with CMake's output: the compiler check failed at linking,
  "lld-link: could not open 'msvcrtd.lib' / 'oldnames.lib'". vswhere had found an installation
  with the C++ tools, but clang picks a Visual Studio installation itself (the newest), likely
  another one without them. build-ssx.ps1 now imports INCLUDE/LIB/LIBPATH/VCToolsInstallDir and
  the SDK variables from that installation's vcvars64.bat (not PATH, so our CMake stays first),
  logs which installation it uses, and fails with a clear message if msvcrt.lib is missing.
  Tested: the import (MSVC 14.44, SDK 26100), the exact failing debug link command, and a fresh
  ssx-player configure + build.
- 2026-11-06: 2x-only snow glitch (gray/white blocky bands; user report, Alps; reproduced every run
  at Grand Golliat "Hard Currency" right after the drop). Chain found with RenderDoc 1x/2x pairs:
  the snow surface samples a 128x128x256 trail volume (3D, 0x12734000) that a trail simulation
  updates every frame (stamp draws into a 128x128 target at EDRAM 332, then a 4-step ping-pong of
  screen-space quads, each resolved into volume slices 0-3). At 2x the quads drew nothing (texture
  diff: 0 px vs 16384 at 1x), so the target kept the data the render target cache had transferred
  into it (depth read as color) and that was resolved into the volume. The quads are indexed, and
  their index buffer sits at the volume's end; the slice resolves report an extent 0x880 bytes past
  the volume, over that page. With scaling, the resolve skipped shared_memory.RequestRange (only
  the unscaled path uploads pending CPU writes first) but still marked the extent written by the
  GPU (MakeRangeValid), so the CPU-written indices never reached the GPU: indices 0,0,0,0 at 2x,
  0,1,2,3 at 1x. patches/rexglue-0008: request the range in the scaled path too. Verified: bands
  gone at 2x, trails and footprints now drawn at 2x, 1x unchanged, no fps cost (139 vs 138 at the
  start line). Ruled out on the way: ROV/RTV, readback, CPU writes, patches 0003/0005, the three
  half-pixel settings, scaled 3D resolve/load addressing (disassembled: identical math), the
  unclipped-VS setting. SDK package sdk-5.
- 2026-11-06: 3x and 4x resolution scale unlocked (menu + startup clamp now 4). The old 3x
  device removal (~30 s into a race) no longer happens with patches 0001-0008: 90 s Grand
  Golliat rides at 3x and 4x, no device errors, image correct (trails included). Start-line fps
  (RTX 4080 SUPER, uncapped, ROV): 2x ~120, 3x 76, 4x 43, GPU 99%. The RTV path is far slower at
  4x (23). Render sizes: 3x = 3600x1920, 4x = 4800x2560 (from the 1200x640 "high" size).
  Next lever if 4x needs more fps: shadow maps at 1x (per-render-target scale), see 2x perf notes.
- 2026-11-06: 4x performance: shadow maps drawn at 1x (patch 0009, `rov_unscaled_depth_pitch`,
  SSX default 1040). GPU timestamp profile at 4x (Grand Golliat start line, 23 ms/frame): shadow
  passes (depth-only, EDRAM pitch 1040: 1024x1024 cascades and 512x512 4x MSAA) 3.0 ms, their
  resolves 0.5 ms, the scaled 1024x3072 atlas texture load 1.2 ms. ROV draws of those passes now
  use 1x shader variants (pixel shader modification bit, 1x depth-only shader, 1x viewport/scissor
  and system constants) that address an unscaled EDRAM region placed after the scaled EDRAM in
  the same buffer; depth resolves from it use the unscaled copy pipelines and mark the
  destination as not scaled, so the atlas loads at 1x; depth clears also clear that region.
  Result at 4x: start line 43 -> 49-52 fps, mid-race 47 -> 57-60; 3x 76 -> 83; shadows look the
  same apart from 1x edge sharpness. Remaining 4x cost: scaled texture loads (~3 ms, a load
  shader writes a buffer that is then copied into the texture) and resolves (~2 ms).
- 2026-11-06: Flare noise (user video, Alaska > Denali > Emergency Exit, night): yellow/green
  blocky noise or flat orange bands on the snow lit by flares, on both render paths, random per
  run. At level load SSX renders each flare light (the skull, AVALANCHE, UNSTABLE...) into a
  512x256 target, resolves it with mips and compresses it to DXT1 on the CPU; resolve readback
  was limited to 4 KB (Antarctica fix), so the CPU compressed stale memory. Found by logging
  texture loads and resolves into the light textures' range (RenderDoc showed the projected
  512x256 BC1 textures held garbage). Fix (patch 0009, `readback_resolve_one_off_sync`, SSX
  default on): a 2D resolve not also done in the previous frame (up to 4 MB, above the readback
  size limit) is read back synchronously; per-frame resolves keep the delayed mode, 3D/array
  destinations (snow volume slices, new address every frame) are excluded. No fps change.
  Scripts: scratchpad/live/denali.ps1 (globe nav Himalayas -> Siberia -> Alaska), sheet.sh.
- 2026-11-06: One-off readback refined: "not done in the previous frame" also caught resolves
  repeating every few frames (e.g. 12AF0000/0A228000 every ~8 frames), each a full GPU wait -
  a stutter at 4x. Now the first 4 resolves to each 2D range are read back synchronously
  (the flare textures reuse a range up to 3 times), later ones keep the delayed mode.
  Remaining 4x stutter (user report, measured): ~15 times a second a frame takes ~one extra GPU
  frame and the next follows within 0-5 ms (VdSwap interval histogram; 75 pairs per 5 s on both
  Grand Golliat and Emergency Exit, at 2x/3x/4x alike; at 3x 22+2 ms, at 4x 35-45+2 ms). Not a
  host drain (none measured), not the frame queue depth (2/3/4 frames in flight all the same),
  not memexport readback, not CPU running ahead (a 50 fps cap pairs too). Points at a guest
  wait on GPU progress at ~15 Hz; not found yet.
- 2026-11-06/07: 4x stutter traced and fixed. Render thread timeline (hooks on the D3D wait
  loops, the sub_82335DE0 callers): every ~3rd frame game code 0x8273859C (sub_82738568,
  virtual; frees/updates an object after the GPU is done with it) calls sub_8232C258 = D3D
  block on the object's fence (+52). With the GPU queue full that waits a whole frame, then two
  VdSwaps go out within 3 ms. Not avoidable by skipping (memory reuse). Fix: adaptive pacing
  (SsxPaceFrameEnd, called from VdSwap; hooks on sub_8233D770 = per-frame throttle and
  sub_8232C258 measure the render thread's GPU wait per frame): when GPU-bound (wait > 25% of
  the frame AND frames slower than the pace, 3 frames in a row) pace the main loop 4% above the
  GPU-bound frame time, then ease back (fast while above the 10 s floor, slowly near it).
  The wait alone is not a GPU-bound signal: at 2x the fence blocks wait out GPU latency every
  frame, which made the first version ratchet 2x down to ~85 fps. Frame samples are clamped to
  2x the average so a streaming hitch can't inflate the pace. Pacing only below 100 fps: above
  that the pairs are ~1-2 ms and pacing cost ~12% (1x/2x). Result, Grand Golliat ride at 4x:
  49-52 fps with ~10 pairs per 5 s at scene changes (was ~75); 2x 125-175 fps, 1x unchanged.
  Bench note: this PC's absolute fps varies a lot between hours (old build 1x 237 -> 193);
  compare builds back to back only.
- 2026-11-07: User report: 4x on Rockies > Whitehorn Mountain stutters a lot at ~42 fps
  (ssx_framerate=0 = match monitor, 300 Hz). Reproduced with scratchpad/live/whitehorn.ps1:
  40-90 pairs (35-40 + 0-5 ms) per 5 s from ~30 s into the ride, in windows running 49-53 fps.
  Cause: the pace drifted just below the GPU limit and the bound test missed it - a pair
  averages out to the pace, so "frame_avg > pace" stays false, and the 2 ms frame resets the
  3-in-a-row count. Fix: also count a single GPU wait longer than 1.25 x the pace as bound
  (only while pacing). Result: pairs 40-90 -> 0-3 per 5 s, frames 20-25 ms, 45-51 fps.
  Ruled out on the way: EnumDisplaySettingsW per call (1 us), the scheduler's lateness carry
  (+112; dropping it while pacing changed nothing; task mode +52 is 1 = measured frame time),
  readback_resolve_one_off_sync (off: same pairs).
  Open: user's installed build hit DEVICE_HUNG (TDR) at 4x ~108 s into a run on another
  mountain, nothing logged before it.
- 2026-11-07: GPU hang (DEVICE_HUNG / TDR) at 4x after restarting a drop (user: Explore >
  Rockies > Extinguisher Tower > Wrecking Crew; reproduced on other drops too, any restart).
  Found with the D3D12 debug layer (--d3d12_debug=true) plus a temporary ID3D12InfoQueue1
  message callback logging a stack (RtlCaptureStackBackTrace, symbolized with llvm-symbolizer
  --relative-address on the RelWithDebInfo *rd.dll): "CORRUPTION: ID3D12Resource final-release
  while referenced by GPU operations in flight" from EvictOldReadbackBuffers. Its size limit
  (256 entries) evicts recently used readback buffers and Released them at once, while a copy
  into them was still in flight. Since patch 0009 every resolve makes an entry (resolve_count),
  so the limit is hit often, and a drop restart bursts past it. Fix (patch 0010): evicted
  buffers go to resources_for_deletion_ (released once the submission completes). A/B with the
  debug layer, scratchpad/live/whrestart.ps1 (World Tour > Rockies > Whitehorn, restart x8):
  without the fix 1 corruption + crash before run 3; with it 8 restarts, 0 corruption.
  Note: Explore's globe opens on the last region visited, so Explore routes aren't repeatable;
  World Tour routes are. SDK package sdk-7.
- 2026-11-07: User report: 3x with the default 60 fps cap stutters a lot (The Dagger: TRICK IT,
  Explore > Rockies > Extinguisher Tower). Bench (TEMPHITCH 5 ms histogram): counter 59.x fps but
  ~90 of 300 frames 0-5 ms and ~95 at 25-35 ms per 5 s: 30 + 3 ms pairs. At 300 fps the same
  ride ran 60-87 fps at 12-20 ms. Cause: at <= 60 fps the swap used present interval 1 (wait for
  the emulated 60 Hz vblank) on top of the 1/60 s main-loop task period, and the adaptive pacer
  was off. Fix: 60 fps uses interval 0 plus the task period and the pacer like higher caps (only
  30 keeps vblank pacing). A/B Whitehorn 3x/60: pairs 150-240 per 5 s -> none, max 33-49 ->
  17-19 ms, race clock identical (game speed unchanged).
  Then two pacer fixes from TEMPPACE/TEMPBOUND logs of the user's runs: (1) a 0.2 s spike
  (frames 40-73 ms, full GPU waits) counted as GPU-bound and paced 3x/60 at ~43 fps for ~15 s
  (floor kept 10 s): frames over 2.2x the pace are now left out of the average and the bound /
  pair tests. (2) Pairs at the target itself weren't caught (pair test required pacing): now 3
  late frames (wait > 1.25 x pace) within 1 s trigger it. (3) The floor is remembered 2 s
  instead of 10 s when it is within 15% of the target (3x/60 recovered after ~12 s, now ~3 s);
  4x keeps 10 s. Result: one short dip to ~55 fps for 2-3 s in the cave on The Dagger (GPU
  limit at 3x). The 70 ms spike in every session is on the first drop's loading screen
  (harmless). Regression: Whitehorn 3x/60 steady 60, 4x 44-54 fps (as before).
- 2026-11-07: User: micro dips at 2x, frame rate "match display" (300). The Dagger at 2x/300:
  ~105 fps, unpaced (pacing was only below 100 fps), frames spread 0-20 ms (~110 per 5 s under
  5 ms, ~75 at 15-20) plus a 35-40 ms frame every 5-10 s. Also found: the spike filter compared
  frames with the target (3.3 ms at 300), so at 2x most real frames counted as spikes and
  frame_avg only saw the short ones. Fix: spikes are > 2.2x max(pace, frame_avg), 8 long frames
  in a row count (heavier scene); pace up to 150 fps (period < 6.67 ms -> target). Result on the
  same ride: ~90 fps, 280-410 of ~450 frames at 10-15 ms, max 19-24 ms. User: dips "way less".
  Open: the SSX title logo drawn over Psymon's portrait in rider select at 2x right after boot
  (stale upscaled resolve data; like the 2026-11-02 fix, but no race before it).
- 2026-11-07: GPU profile of the Whitehorn race (temporary timestamp profiler per render target
  layout / resolve / texture load; script kept outside the repo). GPU ms per frame: 2x 7.3, 3x 13.3,
  4x 22.5. At 4x: main scene passes 7.5 (EDRAM pitch 1200, color 8888 + 7e3), loading the 9
  full-screen resolves per frame as textures 3.3 (load shader 1.5 + buffer-to-texture copy 1.7;
  all distinct: 6 k_8_8_8_8, 2 k_24_8_FLOAT depth, 1 k_16_16_16_16_FLOAT), resolves 2.2, shadows
  1.8 at every scale (already 1x; ~0.8 of it is vertex work), glow/blur passes small (so drawing
  them at a lower scale would gain little). The texture load and copy grow faster than the pixel
  count from 3x to 4x (0.47 -> 1.7 ms copy): 4x full-screen targets (49-98 MB) no longer fit the
  GPU's L2 cache.
  - patches/rexglue-0012: resolution-scaled 2D textures without mips in those three formats are
    loaded straight into the texture through a typed UAV (new compute shaders, source in
    src/graphics/shaders/texture_load_direct_scaled.cs.hlsl), no copy buffer and copy. Note: this
    SDK's scaled resolve layout is the pre-2022-06 one (16-byte x 1-row groups, column-major), not
    current Xenia's XeniaTextureGetResolutionScaledAddressing (16x2 blocks for 32bpb); the first
    version used upstream's and gave a blocky 1x-looking image. A/B, 4x, two interleaved rounds:
    21.5 -> 19.3 ms GPU per frame, 45 -> 51 fps. 3x unchanged (fits in L2), 2x ~+2%.
  - patches/rexglue-0011: depth-only 1x passes (shadow maps) with a depth write, a less/greater
    (or equal) test and no stencil use a compare-exchange loop on a plain UAV instead of the ROV
    (new imm_atomic_cmp_exch emitter): the result is the min/max of all fragments in any order.
    Shadow pass 1.31 -> 1.09 ms; shadows unchanged.
  Ruled out: duplicate texture loads (none), lower scale for glow/blur (small passes).
  SDK package sdk-8.
- 2026-11-08: player name and gamer picture (src/ssx_profile.cpp, settings tab Profile). The
  runtime's profile is "User" and XamReadTileToTexture fills white (or, for tile id 0 = the
  user's own picture, which SSX asks for through XUserReadGamerPicture, returns an error: empty
  square). Overrides: XamUserGetName and XamUserGetSigninInfo (name at +24) write
  ssx_player_name; XamReadTileToTexture decodes ssx_player_picture with WIC (centre-cropped
  square, scaled to the requested size) into the game's linear 32bpp buffer as big-endian ARGB
  (checked with a four-colour test image: orientation and channels right). Both need a restart.
  The settings menu now also shows a pop-up when 3x or 4x is picked (expect low frame rates,
  2x at most recommended, "Use 2x" / "Keep it").
- 2026-11-08: frame rate capped at 40 fps at resolution scale 3x and 4x (kHeavyScaleFps in
  SsxTargetFps; the scale is read once, it needs a restart). User at 4x: 40 "plays great, a lot
  better than 30 and no stutter"; 45 brought the 4x dips and stutter back (4x needs 19-25 ms per
  frame, 45 fps allows 22). Presentation doesn't wait for vsync, so 40 comes out evenly on any
  refresh rate. Menu: note under Frame rate, the 3x/4x pop-up explains the cap.
- 2026-11-08: frame rate locked per resolution scale instead of one cap: 3x offers 30/40/50/55/60
  (ssx_framerate_3x, default 60), 4x 30/35/40 (ssx_framerate_4x, default 40); 1x/2x keep
  ssx_framerate. User tests at 3x: 45, 50, 55 smooth, 60 "a very little dip" (The Dagger cave),
  accepted. Settings menu tidied: every tab the same height (no jumping window), every row has a
  one-line help text, "fps" on every frame rate choice, footer the same height with or without
  the restart button, and the key bindings list now scales with the menu (it kept the 1440p font
  size on smaller screens, cutting off the Reset buttons). Checked the SDK package sdk-8 end to
  end: the ssx-player preset builds against it (all 371 steps), 4x Whitehorn race locked at 40 fps
  (one dip to 36), image and gamer picture correct.
- 2026-11-08: issue #3: the builder used the first clang on PATH, which on the reporter's PC was
  another tool's clang 22 for MinGW (libc++, GNU ABI): chrono.h failed to compile and, patched,
  ssx.exe couldn't link against the MSVC-ABI SDK package. build-ssx.ps1 now takes the first clang
  20+ whose target is *-windows-msvc (Program Files\LLVM first, then PATH), installs LLVM if
  there is none, and package.ps1 -Clang passes that exact path to CMake. Tested with a MinGW
  clang (hard link + clang.cfg --target) first on PATH: skipped; the compile commands of an
  existing build stay the same (no rebuild).
- 2026-11-09: issue #3, second report: the builder took Visual Studio 2017 (MSVC 14.14, Windows SDK
  10.0.16299) because it accepted any Visual Studio with C++ tools; its library lacks C++20 headers
  (<bit>, <span>), so every source failed to compile. build-ssx.ps1 now only accepts Visual Studio
  2022 (17.0) or newer (vswhere -version 17.0) and otherwise installs the 2022 Build Tools next to
  the old one. Checked on this PC: VS 2022 found as before, a newer-than-installed minimum finds
  nothing (the install path).
