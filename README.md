<div align="center">

# 🏂 SSX (2012) — Native PC Port

**EA's SSX, running natively on Windows — up to 300 fps, sharper, no emulator.**

[![Latest release](https://img.shields.io/github/v/release/AmarilloNL/ssx-recomp?style=for-the-badge&label=installer&color=0aa5e6)](../../releases/latest)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=for-the-badge&logo=windows11&logoColor=white)](#installing)
[![Direct3D 12](https://img.shields.io/badge/Direct3D-12-7a3cff?style=for-the-badge)](#features)
[![License: MIT](https://img.shields.io/badge/license-MIT-22c55e?style=for-the-badge)](LICENSE)

[**⬇️ Download**](../../releases/latest) · [🎬 Gameplay](#gameplay) · [✨ Features](#features) · [🛠️ Install](#installing) · [🎮 Controls](#playing) · [📦 DLC](#adding-dlc) · [🗺️ Roadmap](#roadmap)

</div>

> [!CAUTION]
> **You need your own copy of the game.** This repository contains **no game code, graphics,
> music, video or any other EA content**, and the installer downloads none. You need **SSX (2012)
> for Xbox 360**, dumped from **your own disc** to an ISO. The PC version is built on your
> computer from that ISO.
>
> **ISOs, game files and pre-built executables will not be provided.** Requests or links to
> pirated copies are removed.

A native Windows build of EA's *SSX* (2012), made by statically recompiling the Xbox 360
executable to x86-64 with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's
code runs natively, and its Xbox 360 graphics are translated to Direct3D 12.

<a id="gameplay"></a>

## 🎬 Gameplay

<div align="center">

[![SSX (2012) Native PC Port at High FPS – Pure Gameplay (No Emulator)](https://img.youtube.com/vi/ibGjbR-AqRo/maxresdefault.jpg)](https://www.youtube.com/watch?v=ibGjbR-AqRo)

▶️ *[SSX (2012) Native PC Port at High FPS – Pure Gameplay (No Emulator)](https://www.youtube.com/watch?v=ibGjbR-AqRo)*

</div>

<a id="features"></a>

## ✨ Features

| | |
|---|---|
| ⚡ **Up to 300 fps** | or match your monitor, at real-time game speed (the original runs at 30) |
| 🖥️ **Sharper image** | 1200×640 rendering (the 360 renders 1120×584), optional 2×, 3× or 4× scale (up to 4800×2560) |
| 🔍 **AMD FSR 1** | upscaling by default, plus CAS sharpening, FXAA and anisotropic filtering |
| ⚙️ **Settings menu** | in game: **F1**, or **Back + Start** on a controller |
| ⌨️ **Keyboard & controller** | key rebinding, vibration, deadzone |
| 🪪 **Your name & picture** | instead of "User" and a white square (settings → **Profile**) |
| 💿 **Runs from your ISO** | saves stay in `Documents\ssx` |
| 📦 **DLC support** | Mt. Eddie & Classic Characters, from your own console ([how](#adding-dlc)) |

<a id="installing"></a>

## 🛠️ Installing

1. 💿 Make an ISO of **your own** SSX (USA) Xbox 360 disc.
2. ⬇️ Download **`SSX-Installer.exe`** from the [latest release](../../releases/latest).
3. ▶️ Run it. SmartScreen may warn because the installer isn't code-signed: click
   **More info → Run anyway**. Its source is [`installer/SSXInstaller.cs`](installer/SSXInstaller.cs).
4. ✅ Click **Next**, select your ISO, tick the ownership box, and click **Install**.
5. 🏂 When it says **DONE**, click **Play SSX** or use the desktop shortcut.

> [!NOTE]
> Needs **Windows 10/11 x64**, about **10 GB** free (mostly build tools that other software can
> share; the game folder itself is about 1 GB), an internet connection and **10–30 minutes** the
> first time. **To update, just run the installer again**: it rebuilds only what changed, and your
> saves and settings stay.

<details>
<summary><b>What the installer does on your PC</b></summary>
<br>

Everything happens locally:

- checks that the ISO is the supported release,
- installs the build tools that are missing (CMake, Ninja, LLVM and the Visual Studio 2022 Build
  Tools) through `winget`, or from their official downloads if `winget` isn't available; Windows
  asks for administrator permission for LLVM and Visual Studio,
- downloads the prebuilt ReXGlue recompiler SDK (no game code; see
  [`scripts/build-sdk-package.ps1`](scripts/build-sdk-package.ps1)) and recompiles the game,
- produces the playable folder `SSX-PC\source\dist\SSX`.

If something goes wrong, the log is `build-ssx.log` in the install folder. Please attach it to an
issue.

</details>

<details>
<summary><b>Supported disc</b></summary>
<br>

| | |
|---|---|
| Game | SSX (USA), Xbox 360 |
| Title ID / Media ID | `4541096D` / `55ABAB6C` |
| `default.xex` SHA-256 | `F509845401D65A6791C5D28A3E9636A6558DA204F3851028B0B369A44A4B68EA` |

Other regions or versions aren't supported yet, because the recompilation settings are tied to
this exact executable. Some ISOs named "World" contain this same release; the installer checks
the file for you.

</details>

<a id="playing"></a>

## 🎮 Playing

| | |
|---|---|
| ⚙️ **Settings** | **F1** / **Esc** (controller: **Back + Start**, LB/RB switch tabs, B closes) |
| ⌨️ **Keyboard** | WASD steer · Space jump · arrow keys = trick stick · Q/E triggers · 1/3 bumpers · Enter start · Tab back |

> [!TIP]
> **1× + FSR** runs at very high frame rates and looks sharp. **2×** is noticeably sharper (about
> 60% of the 1× frame rate). We recommend **2× at most**: 3× and 4× are expected to run at low
> frame rates (see below). If a setting ever stops the game from starting,
> delete `ssx.toml` next to `ssx.exe`.

> [!WARNING]
> **3× and 4× are expected to have low performance; we recommend 2× at most.** They draw
> about 4K (3×) and beyond 4K (4×), and every pixel goes through the emulation of the Xbox 360's
> graphics chip, so they are limited by the graphics card. They are still being worked on every
> day and get faster with updates. The settings menu warns you when you pick them.
> - **4×:** even an RTX 4080 SUPER runs it at about 45–55 fps, dropping to about 40 fps in
>   busy scenes, with hitches.
> - **3×:** an RTX 4080 SUPER holds a 60 fps cap almost everywhere, but heavy spots (for example
>   riding into the cave on The Dagger) can dip to about 55 fps for a few seconds.
>
> When the GPU can't keep up, the game slows its pace a little so frames still come out evenly
> instead of stuttering, and returns to full speed as soon as the scene gets lighter. You may see
> the fps counter drop for a moment; that's expected. For a locked 60 fps use **2×**; on a 1440p
> screen 3× looks almost the same as 4×.

<details>
<summary><b>Why is 4× so heavy, and what would it take to reach 60 fps?</b></summary>
<br>

- **The pixel count.** 4× draws 4800×2560 = 12.3 million pixels per frame: 16 times the 1×
  image and more than 4K (8.3 million). Every pass the game draws (snow, lighting, particles,
  glow, blur) is drawn at that size.
- **Emulated console video memory.** The Xbox 360 renders into a small, fast on-chip memory. The
  port reproduces it in shaders, which costs extra work for every pixel.
- **Measured** on an RTX 4080 SUPER while playing: the GPU at 99% the whole time, at full clock
  speed and well within its power and heat limits. The CPU is at about 20%. The GPU is simply
  out of headroom.
- **For 60 fps** a frame has to take 16.7 ms instead of the 19–25 ms measured, so the GPU would
  have to do about a quarter less work. Already done: shadows are drawn at 1× when scaling
  (about 15%), and the full-screen images the game reads back as textures (9 per frame) are
  loaded without an extra copy (about 12% at 4×). Drawing glow and blur at a lower resolution
  would gain little: they are small passes. Most of the time goes into the main scene itself.

</details>

<a id="adding-dlc"></a>

## 📦 Adding DLC

The base game works without DLC. Like the game itself, **DLC is not included**: you need the
packages from your own Xbox 360, bought on your own Xbox Live account.

| Package | Size | Contents |
|---|---|---|
| 🏔️ Mt. Eddie & Classic Characters Pack | ~250 MB | the Mt. Eddie region and classic riders |
| 🎁 Pre-order bonus pack | ~9 MB | extra gear (if you had it) |

**Quick version:** copy the long-named package files into `Documents\ssx\dlc` and start the game.

<details>
<summary><b>Full guide: finding, copying and checking the DLC files</b></summary>
<br>

**What the files look like.** Xbox 360 DLC is stored as package files with a long hexadecimal
name and no extension, for example `8A7D066F7125F0B02BF4D8F520A12607DD5CB41B45`. SSX's DLC lives
in a folder named after the game's title ID and the content type "marketplace":

```
Content\0000000000000000\4541096D\00000002\
```

**Getting them off your console.** Copy the game's DLC from your console's hard drive to a USB
drive (*Settings → System → Storage*), or connect the console's drive to your PC. Xbox 360
storage uses its own file system, so use an Xbox 360 file tool (for example *FATXplorer*,
*Velocity* or *Horizon*) to open it and copy the package files out of the `4541096D\00000002`
folder. Copy the package files themselves, not the folder.

**Installing them.**

1. Close the game.
2. Open the folder `Documents\ssx\dlc`. The game creates it the first time it starts, or you can
   create it yourself.
3. Copy the package files (the long-named files) into it. The result looks like
   `Documents\ssx\dlc\8A7D066F7125F0B02BF4D8F520A12607DD5CB41B45`.
4. Start the game. On startup it unpacks each new package into
   `Documents\ssx\0000000000000000\4541096D\00000002\`. This happens once per package, and the
   files in `dlc` can be deleted afterwards.

**Checking it worked.** Mt. Eddie appears on the world map and the classic riders are selectable.
The game's log (`logs` folder next to `ssx.exe`) shows a line like `DLC: installed 8A7D066F...`
for each package. If it says `failed to install`, the file isn't an Xbox 360 content package:
check that you copied the long-named package file, not a folder or an archive.

</details>

<a id="roadmap"></a>

## 🗺️ Roadmap

The game is playable and is being tested by playing through the whole World Tour.

**🔧 Being worked on**
- 🏔️ **Playing through every region** to find and fix bugs, especially the Deadly Descent
  mechanics (cold, darkness, oxygen and so on), which depend on details of the original hardware.
- 💻 **Installer reliability** on more PCs, based on the reports in the issues.

**✅ Fixed so far**
- ✅ Antarctica's thermal protection (the rider froze in the sun)
- ✅ Yellow specks on dark rock (Africa's caves)
- ✅ Blocky bands on the snow at 2× resolution scale (snow trails now show up at 2× too)
- ✅ Flickering yellow/green noise in the flare light on night Deadly Descents (Alaska)
- ✅ Uneven frame pacing (stutter) when the GPU is at its limit, for example at 4×
- ✅ Stutter with a 60 fps cap at 3× (frames came out in pairs, which looked like 30 fps), and
  long slowdowns after a short spike
- ✅ Uneven frame times at 2× with an uncapped frame rate (frames jumped between 3 and 40 ms)
- ✅ 4× about 12% faster (45 → 51 fps in the same race)

**👀 Known issues**
- A peak label on the world map was once seen drawn as a solid box (Africa). Not reproduced
  yet; screenshots are welcome.
- Only SSX (USA) is supported.
- Render sizes above 1200×640 aren't available: they break the game's own render buffers. Use the
  resolution scale instead.
- Online features (RiderNet, global events) don't work, because EA's servers are gone.

> [!TIP]
> **Found a bug?** [Open an issue](../../issues/new) with a screenshot, where it happened (region,
> peak, event) and, if the game crashed, the newest file from the `logs` folder next to `ssx.exe`.

<details>
<summary><b>🧑‍💻 Building manually / development</b></summary>
<br>

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 -Iso "D:\path\to\your\SSX.iso"
```

`patches/` holds the fixes applied to the ReXGlue SDK, `ssx_config.toml` holds the recompilation
overrides, and `src/` holds the PC-side code (settings, launcher, hooks). [`PROGRESS.md`](PROGRESS.md)
is the development log.

The builder uses the CMake preset `ssx-player`, which builds against the prebuilt SDK package made
by [`scripts/build-sdk-package.ps1`](scripts/build-sdk-package.ps1) (the SDK source at a fixed
commit plus `patches/`). For SDK development, the `ssx-release` preset builds the SDK from source
in `tools/rexglue-src`.

</details>

## 🙏 Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay: the static recompilation toolkit and runtime
- [Xenia](https://xenia.jp): the Xbox 360 research the ReXGlue runtime is derived from
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (hedge-dev): the codegen approach ReXGlue builds on
- [xenia-canary game patches](https://github.com/xenia-canary/game-patches) (Margen67): the SSX 60 fps and render size findings
- AMD FidelityFX (FSR / CAS)

---

<div align="center">
<sub>

⚖️ Unofficial fan project, not affiliated with or endorsed by Electronic Arts. *SSX* is a trademark
of Electronic Arts Inc. The code in this repository is released under the [MIT license](LICENSE)
and contains no Electronic Arts code or assets.

</sub>
</div>
