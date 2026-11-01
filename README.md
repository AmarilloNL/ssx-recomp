# SSX (2012) — native PC port

> [!CAUTION]
> ## You need your own copy of the game
> This repository contains **no game code, graphics, music, video or any other EA content**, and the
> installer downloads none. To play, you must **own SSX (2012) for Xbox 360** and **dump your own
> disc to an ISO**. The PC version is built on your computer from that ISO.
>
> **Do not ask for ISOs, game files or pre-built executables. They will not be provided**, and
> requests or links to pirated copies will be removed.

A native Windows build of EA's *SSX* (2012), made by statically recompiling the Xbox 360
executable to x86-64 with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). It is not
an emulator: the game's code runs natively, and Xbox 360 graphics are translated to Direct3D 12.

## Features

- Native Windows build, no emulator
- **Up to 300 fps** (or match your monitor) with real-time game speed. The original runs at 30.
- Native 720p rendering (the 360 renders 1120×584), optional 2× resolution (1440p)
- **AMD FSR 1** upscaling (default), CAS sharpening, FXAA, anisotropic filtering
- In-game settings menu (**F1**, or **Back + Start** on a controller)
- Keyboard controls with rebinding, controller vibration, deadzone
- Runs straight from your ISO; saves stay in `Documents\ssx`
- DLC support (Mt. Eddie & Classic Characters pack, from your own download)

## Installing

1. Make an ISO of **your own** SSX (USA) Xbox 360 disc.
2. Download **`SSX-Installer.exe`** from the [latest release](../../releases/latest).
3. Run it. Windows SmartScreen may warn you because the installer isn't code-signed. Click
   **More info → Run anyway**. The installer's source is [`installer/SSXInstaller.cs`](installer/SSXInstaller.cs).
4. Select your ISO, tick the ownership box, and click **Build SSX**.

The installer then, entirely on your PC:
- checks that the ISO is the supported release,
- installs the build tools through `winget` (Git, CMake, Ninja, LLVM, and the Visual Studio 2022
  Build Tools; Windows asks for administrator permission once),
- downloads the ReXGlue SDK and recompiles the game,
- produces the playable folder `SSX-PC\source\dist\SSX`. Click **Play** or start `ssx.exe`.

It needs Windows 10/11 x64, about **30 GB** of free space, an internet connection and roughly
**30–60 minutes**. Running it again later updates and rebuilds only what changed.

### Supported disc

| | |
|---|---|
| Game | SSX (USA), Xbox 360 |
| Title ID / Media ID | `4541096D` / `55ABAB6C` |
| `default.xex` SHA-256 | `F509845401D65A6791C5D28A3E9636A6558DA204F3851028B0B369A44A4B68EA` |

Other regions or versions are not supported yet, because the recompilation settings are tied
to this exact executable.

## Playing

| | |
|---|---|
| **F1** / **Esc** | Settings menu (controller: **Back + Start**, LB/RB tabs, B closes) |
| Keyboard | WASD steer, Space jump, arrow keys = trick stick, Q/E triggers, 1/3 bumpers, Enter start, Tab back |
| DLC | copy your DLC package files (from `4541096D\00000002\`) into `Documents\ssx\dlc` |

**Tip:** 1× + FSR runs at very high frame rates and looks sharp. 2× is the sharpest but the GPU
emulation limits it to roughly 60 fps. If a setting ever stops the game from starting, delete
`ssx.toml` next to `ssx.exe`.

## Known limitations

- Only SSX (USA) is supported.
- 3× resolution scale and render sizes above 720p are not available: they crash or break the
  game's own render buffers.
- Online features (RiderNet, global events) don't work. EA's servers are gone.

## Building manually

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 -Iso "D:\path\to\your\SSX.iso"
```

`patches/` holds the fixes applied to the ReXGlue SDK, `ssx_config.toml` holds the
recompilation overrides, and `src/` holds the PC-side code (settings, launcher, hooks).
`PROGRESS.md` is the development log.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay: the static recompilation toolkit and runtime
- [Xenia](https://xenia.jp): the Xbox 360 research the ReXGlue runtime is derived from
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (hedge-dev): the codegen approach ReXGlue builds on
- [xenia-canary game patches](https://github.com/xenia-canary/game-patches) (Margen67): the SSX 60 fps and 720p findings
- AMD FidelityFX (FSR / CAS)

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Electronic Arts. *SSX* is a
trademark of Electronic Arts Inc. The code in this repository is released under the MIT license
(see `LICENSE`). It contains no Electronic Arts code or assets.
