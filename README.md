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
- Sharper 1200×640 rendering (the 360 renders 1120×584), optional 2× resolution scale (2400×1280)
- **AMD FSR 1** upscaling (default), CAS sharpening, FXAA, anisotropic filtering
- In-game settings menu (**F1**, or **Back + Start** on a controller)
- Keyboard controls with rebinding, controller vibration, deadzone
- Runs straight from your ISO; saves stay in `Documents\ssx`
- DLC support (Mt. Eddie & Classic Characters pack, from your own console: see [Adding DLC](#adding-dlc))

## Installing

1. Make an ISO of **your own** SSX (USA) Xbox 360 disc.
2. Download **`SSX-Installer.exe`** from the [latest release](../../releases/latest).
3. Run it. Windows SmartScreen may warn you because the installer isn't code-signed. Click
   **More info → Run anyway**. The installer's source is [`installer/SSXInstaller.cs`](installer/SSXInstaller.cs).
4. Click **Next**, select your ISO, tick the ownership box, and click **Install**.

The installer then, entirely on your PC:
- checks that the ISO is the supported release,
- installs the build tools that are missing (CMake, Ninja, LLVM, and the Visual Studio 2022
  Build Tools) through `winget`, or from their official downloads if `winget` isn't available;
  Windows asks for administrator permission for LLVM and Visual Studio,
- downloads the prebuilt ReXGlue recompiler SDK (no game code; see
  [`scripts/build-sdk-package.ps1`](scripts/build-sdk-package.ps1)) and recompiles the game,
- produces the playable folder `SSX-PC\source\dist\SSX`. Click **Play SSX**, use the desktop
  shortcut, or start `ssx.exe`.

It needs Windows 10/11 x64, about **10 GB** of free space (about 7.5 GB of that is the build
tools, which other software can share; the build itself is about 1 GB), an internet connection
and roughly **10–30 minutes** the first time, mostly for downloading the tools. Running it again
later updates and rebuilds only what changed.

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
| DLC | see [Adding DLC](#adding-dlc) below |

**Tip:** 1× + FSR runs at very high frame rates and looks sharp. 2× is the sharpest and needs a
stronger GPU (about 60% of the 1× frame rate). If a setting ever stops the game from starting, delete
`ssx.toml` next to `ssx.exe`.

## Adding DLC

The base game works without DLC. Like the game itself, **DLC is not included**: you need the
DLC packages from your own Xbox 360, bought on your own Xbox Live account.

**What the files look like.** Xbox 360 DLC is stored as package files with a long hexadecimal
name and no extension, for example `8A7D066F7125F0B02BF4D8F520A12607DD5CB41B45`. SSX's DLC
lives in a folder named after the game's title ID and the content type "marketplace":

```
Content\0000000000000000\4541096D\00000002\
```

| Package | Size | Contents |
|---|---|---|
| Mt. Eddie & Classic Characters Pack | ~250 MB | the Mt. Eddie region and classic riders |
| Pre-order bonus pack | ~9 MB | extra gear (if you had it) |

**Getting them off your console.** Copy the game's DLC from your console's hard drive to a USB
drive (*Settings → System → Storage*), or connect the console's drive to your PC. Xbox 360
storage uses its own file system, so use an Xbox 360 file tool (for example *FATXplorer*,
*Velocity* or *Horizon*) to open it and copy the package files out of the
`4541096D\00000002` folder. Copy the package files themselves, not the folder.

**Installing them.**

1. Close the game.
2. Open the folder `Documents\ssx\dlc`. The game creates it the first time it starts, or you
   can create it yourself.
3. Copy the package files (the long-named files) into it. The result looks like
   `Documents\ssx\dlc\8A7D066F7125F0B02BF4D8F520A12607DD5CB41B45`.
4. Start the game. On startup it unpacks each new package into
   `Documents\ssx\0000000000000000\4541096D\00000002\`. This only happens once per package,
   and the files in `dlc` can be deleted afterwards.

**Checking it worked.** Mt. Eddie appears on the world map and the classic riders are
selectable. The game's log (`logs` folder next to `ssx.exe`) shows a line like
`DLC: installed 8A7D066F...` for each package. If it says `failed to install`, the file is not
an Xbox 360 content package. Check that you copied the long-named package file, not a folder or
an archive.

## Known limitations

- Only SSX (USA) is supported.
- 3× resolution scale and render sizes above 1200×640 are not available: they crash or break the
  game's own render buffers.
- Online features (RiderNet, global events) don't work. EA's servers are gone.

## Roadmap

The game is playable and is being tested by playing through the World Tour. What's being worked on:

- **Playing through every region** to find and fix bugs, especially the Deadly Descent mechanics
  (cold, darkness, oxygen and so on), which depend on details of the original hardware. Fixed so
  far: Antarctica's thermal protection, yellow specks on dark rock (Africa's caves), blocky
  bands on the snow at 2x resolution scale (snow trails now show up at 2x too).
- **Known issue:** a peak label on the world map was once seen drawn as a solid box (Africa).
  It hasn't been reproduced yet. Screenshots of it are welcome.
- **Installer reliability** on more PCs, based on the reports in the issues.

Updates arrive through the installer: run it again to get the latest fixes. The development log is
in `PROGRESS.md`.

**Found a bug?** Open an issue with a screenshot, where it happened (region, peak, event) and, if
the game crashed, the newest file from the `logs` folder next to `ssx.exe`.

## Building manually

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 -Iso "D:\path\to\your\SSX.iso"
```

`patches/` holds the fixes applied to the ReXGlue SDK, `ssx_config.toml` holds the
recompilation overrides, and `src/` holds the PC-side code (settings, launcher, hooks).
`PROGRESS.md` is the development log.

The builder uses the CMake preset `ssx-player`, which builds against the prebuilt SDK package
made by `scripts/build-sdk-package.ps1` (the SDK source at a fixed commit plus `patches/`). For
SDK development, the `ssx-release` preset builds the SDK from source in `tools/rexglue-src`.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay: the static recompilation toolkit and runtime
- [Xenia](https://xenia.jp): the Xbox 360 research the ReXGlue runtime is derived from
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (hedge-dev): the codegen approach ReXGlue builds on
- [xenia-canary game patches](https://github.com/xenia-canary/game-patches) (Margen67): the SSX 60 fps and render size findings
- AMD FidelityFX (FSR / CAS)

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Electronic Arts. *SSX* is a
trademark of Electronic Arts Inc. The code in this repository is released under the MIT license
(see `LICENSE`). It contains no Electronic Arts code or assets.
