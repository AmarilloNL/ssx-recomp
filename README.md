<div align="center">

# 🏂 SSX (2012) — Native PC Port

**EA's SSX, running natively on Windows and Linux — up to 300 fps, up to 4K, no emulator.**

[![Latest release](https://img.shields.io/github/v/release/AmarilloNL/ssx-recomp?style=for-the-badge&label=installer&color=0aa5e6)](../../releases/latest)
[![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4?style=for-the-badge&logo=windows11&logoColor=white)](#installing)
[![Linux](https://img.shields.io/badge/Linux-beta-f59e0b?style=for-the-badge&logo=linux&logoColor=white)](#linux)
[![Direct3D 12](https://img.shields.io/badge/Direct3D-12-7a3cff?style=for-the-badge)](#graphics)
[![License: MIT](https://img.shields.io/badge/license-MIT-22c55e?style=for-the-badge)](LICENSE)
<a href="https://playported.com/ports/1191-ssx/"><img src="https://playported.com/badge.svg" alt="Listed on PlayPorted" height="28"></a>

<a href="https://recomplauncher.com/game/ssx-recomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

[**⬇️ Download**](../../releases/latest) · [🎬 Gameplay](#gameplay) · [✨ Features](#features) · [🛠️ Install](#installing) · [🐧 Linux](#linux) · [🎮 Controls](#playing) · [🖥️ Graphics](#graphics) · [📦 DLC & update](#adding-dlc) · [🎵 Music](#your-music) · [🗺️ Roadmap](#roadmap)

</div>

> [!CAUTION]
> **You need your own copy of the game.** This repository contains **no game code, graphics,
> music, video or any other EA content**, and the installer downloads none. You need **SSX (2012)
> for Xbox 360**, dumped from **your own disc** to an ISO. The PC version is built on your
> computer from that ISO.
>
> **ISOs, game files and pre-built executables will not be provided.** Requests or links to
> pirated copies are removed.

A native Windows and Linux build of EA's *SSX* (2012), made by statically recompiling the Xbox 360
executable to x86-64 with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's
code runs natively, and its Xbox 360 graphics are drawn with Direct3D 12 (on Linux through
[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) and Vulkan).

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
| 🖥️ **Up to 4K** | the [ReGPU renderer](#graphics) draws at your screen's resolution, 720p to 4K |
| ⚙️ **Settings menu** | in game: **F1**, or **Back + Start** on a controller |
| ⌨️ **Keyboard & controller** | key rebinding, vibration, deadzone |
| 🪪 **Your name & picture** | instead of "User" and an empty square (settings → **Profile**) |
| 💿 **Runs from your ISO** | saves stay in your SSX folder (Linux: `~/.local/share/ssx`) |
| 📦 **DLC** | Mt. Eddie, Japan and the other packs, from your own console ([how](#adding-dlc)) |
| 📈 **Title update** | the game's last update, with the gear for levels 11 to 15 ([how](#title-update)), and [riders up to level 15](#level-15) |
| 🎵 **Your own music** | your music folders as playlists in the game's Music menu, remixed live as you ride ([how](#your-music)) |
| ❄️ **Snowflake tracker** | how many snowflakes you have on every drop, per range and peak (settings → **Snowflakes**) |
| 🏂 **Boost meter in freeride** | the boost and tricky meters while you freeride (settings → **Display**) |
| 🏅 **Online-only badges** | the badges that need EA's servers, shown as earned (settings → **Profile**) |

<a id="installing"></a>

## 🛠️ Installing

### 🪟 Windows

1. 💿 Make an ISO of **your own** SSX (USA) Xbox 360 disc (an extracted copy of the disc, the folder
   with `default.xex`, works too).
2. ⬇️ Download **`ssx-recomp.exe`** from the [latest release](../../releases/latest) and put it in
   an empty folder of its own, for example `C:\Games\SSX`. That folder is your SSX folder:
   the game is built there and everything it keeps stays there.
3. ▶️ Run it. SmartScreen may warn because it isn't code-signed: click
   **More info → Run anyway**. Its source is [`installer/SSXInstaller.cs`](installer/SSXInstaller.cs).
4. ✅ The first time it opens the setup: click **Next**, select your ISO (or the extracted disc's
   `default.xex`), optionally click
   **Add files...** for DLC and the title update from your own console ([which files](#adding-dlc)),
   and click **Install**.
5. 🏂 When it says **DONE**, click **Play SSX** or use the desktop shortcut.

From then on **`ssx-recomp.exe` is the game's launcher**: it opens with **Play SSX**,
**Check for updates** and **Add DLC / rebuild**. (`ssx-recomp.exe --play`, or any options for the
game, starts the game straight away.)

**Or with [RecompLauncher](https://recomplauncher.com/game/ssx-recomp):** install SSX there, drop in
your ISO (and the title update if you have it) and it builds the game for you, checks your disc
against Redump, and shows the main settings on the game's page. Updates rebuild the game the same way.

> [!NOTE]
> Needs **Windows 10/11 x64**, about **10 GB** free (mostly build tools that other software can
> share; the game folder itself is about 1 GB), an internet connection and **10–30 minutes** the
> first time.
>
> 📁 **Everything stays in your SSX folder**: the build, saves, settings, DLC, music and the shader
> cache. Nothing goes to `Documents` or the registry; move or back up the folder as a whole. If
> you played an earlier version, which kept its saves in `Documents\ssx`, the first start copies
> them (with your DLC and music) into the SSX folder; `Documents\ssx` is left as it was.
>
> ♻️ **Installed with `SSX-Installer.exe` before?** Put `ssx-recomp.exe` in that install folder
> (`SSX-PC` in your user folder, unless you picked another) and run it: it opens on **Play SSX**;
> click **Add DLC / rebuild**, then **Rebuild**, to bring the game up to this version.
>
> 🔄 **To update, click Check for updates**: it's the only thing that goes online for the port. If
> there's a newer release, it replaces `ssx-recomp.exe` with that release's and rebuilds SSX (only
> what changed); your saves and settings stay. Playing never downloads anything.
>
> 📦 **`ssx-recomp.exe` carries the port's source code**, so building never downloads the port: each
> release builds exactly its own version. Only build tools and packages the PC doesn't have yet are
> downloaded (the first time, or when a release needs newer ones), so rebuilding later, for example
> to add the title update, works offline.

<details>
<summary><b>What the installer does on your PC</b></summary>
<br>

Everything happens locally:

- unpacks the port's source code, which `ssx-recomp.exe` carries, into `source`,
- checks that the ISO is the supported release,
- installs the build tools that are missing (CMake, Ninja, LLVM and the Visual Studio 2022 Build
  Tools) through `winget`, or from their official downloads if `winget` isn't available; Windows
  asks for administrator permission for LLVM and Visual Studio,
- downloads the prebuilt ReXGlue recompiler SDK and the prebuilt ReGPU renderer (no game code; see
  [`scripts/build-sdk-package.ps1`](scripts/build-sdk-package.ps1) and
  [`scripts/build-regpu-package.ps1`](scripts/build-regpu-package.ps1)) and recompiles the game,
- produces the playable game in `source\dist\SSX` inside your SSX folder.

If something goes wrong, the log is `build-ssx.log` in the `source` folder. Please attach it to an
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
| Title update (optional) | version 0.0.3.2, see [the title update](#title-update) |

Other regions or versions aren't supported yet, because the recompilation settings are tied to
these exact executables. Some ISOs named "World" contain this same release; the installer checks
the file for you.

</details>

<a id="linux"></a>

### 🐧 Linux (beta)

A native Linux version: Vulkan, no Wine or Proton. Like on Windows, it's built on your PC from your
own ISO, by an installer script:

1. 💿 Make an ISO of **your own** SSX (USA) Xbox 360 disc.
2. ⬇️ Download **`ssx-linux-installer.sh`** from the [latest release](../../releases/latest).
3. ▶️ Run it in a terminal (add `--title-update <file>` once for [the title update](#title-update)):
   ```sh
   bash ssx-linux-installer.sh --iso ~/Games/SSX.iso
   ```
4. 🏂 When it says **DONE**, start **SSX** from your app menu.

> [!NOTE]
> Needs about **15 GB** free during the build and **20–60 minutes** the first time. The script
> installs missing build tools with your package manager (it asks for your password through
> `sudo`). 🔄 **To update, run it again**: your saves and settings stay.
>
> **Beta:** tested on **CachyOS** (KDE Plasma on Wayland) with an NVIDIA GTX 1080 and an Xbox
> controller, where races hold 60 fps. Debian/Ubuntu (`apt`) and Fedora (`dnf`) get their packages
> installed too, but haven't been tested yet: reports are welcome.

<details>
<summary><b>Linux notes</b></summary>
<br>

- Where this README says `userdata`, Linux uses **`~/.local/share/ssx`** (saves, `dlc`,
  `music`). Your own music needs `ffmpeg`.
- The game uses **ReGPU**, as on Windows, drawing at your screen's resolution. If ReGPU can't be
  built on your PC, the script says so and the game uses the standard renderer, which runs at 1× on
  Linux (2× made the graphics driver hang in testing), scaled bilinear.
- Not on Linux yet: the gamer picture.
- The first ride in each new area can stutter briefly while the game's shaders are prepared.
  They're kept for the next start.
- The port goes to `~/SSX-PC` (`--folder` picks another place); the game is in `source/dist/SSX`.
  From a git clone, `./scripts/build-ssx-linux.sh --iso ~/Games/SSX.iso` does the same build.
- If something goes wrong, attach `build-ssx-linux.log` (in `~/SSX-PC/source`) to an issue.

</details>

<a id="playing"></a>

## 🎮 Playing

| | |
|---|---|
| ⚙️ **Settings** | **F1** / **Esc** (controller: **Back + Start**, LB/RB switch tabs, B closes); **Quit game** at the bottom |
| 🎮 **Controller** | as on the console; vibration and deadzone in **F1 → Controls** |
| ⌨️ **Keyboard** | WASD steer · Space jump · arrow keys = trick stick · Q/E triggers · 1/3 bumpers · Enter start · Tab back |

> [!TIP]
> - **More frames:** pick a lower drawing size in **F1 → Graphics → Resolution**.
> - **Tearing** (a horizontal cut through the picture) on a screen without G-Sync/FreeSync, like a
>   handheld? Turn on **VSync** in **F1 → Display**. It works with any frame rate (with the standard
>   renderer, restart after turning it on).
> - **The game won't start after changing a setting?** Delete `ssx.toml` next to `ssx.exe`.
> - **Everything stays in your SSX folder:** saves, DLC, music and the shader cache live in
>   `source\dist\SSX\userdata` (called `userdata` below). Back up or move the folder as a whole.

<a id="graphics"></a>
<a id="regpu"></a>

## 🖥️ Graphics

The game has two renderers. **ReGPU is the default**; the standard renderer is there as a fallback
(**F1 → Graphics → Renderer**, then restart).

| | 🚀 **ReGPU** (default) | 🔧 **Standard** |
|---|---|---|
| What it is | [ReGPU](https://github.com/zer0ducksgiven/ReGPU), a newer, much smaller Direct3D 12 renderer for recompiled Xbox 360 games | the ReXGlue SDK's own renderer, an emulation of the Xbox 360's graphics chip |
| Resolution | your screen's: *Match my screen*, 720p, 1080p, 1440p or 4K, changed while the game runs | 1200×640 with a 1×, 2× or 3× scale |
| Speed (RTX 4080 SUPER) | 1440p about 200 fps (Mount Wakefield's race 130–190), 4K about 110 | 2× about 140 fps; 3× [locked to 30–60](#frame-rate-lock) |
| Image options | — | AMD FSR 1, CAS, FXAA |

- **Frame rate:** **F1 → Display → Frame rate**: 30 up to 300, or *match your monitor*.
  When the GPU can't keep up, the game slows its pace a little so frames still come out evenly.
- **Ultrawide screens** get a 16:9 picture for now. Any other 16:9 size can go in `ssx.toml` as
  `regpu_resolution = "WIDTHxHEIGHT"`.
- **If ReGPU can't start** on a PC, the game uses the standard renderer by itself at the next start.
- **On Linux** ReGPU runs on Vulkan through vkd3d-proton; the standard renderer runs at 1× there.
- **Tested** mostly on NVIDIA graphics cards. If something looks different from the standard
  renderer, a screenshot helps a lot (F12 saves one with the renderer's own debug data).

<a id="frame-rate-lock"></a>

<details>
<summary><b>Standard renderer: why 3× is locked to 60 fps (and 4× was removed)</b></summary>
<br>

| Scale | Frame rate choices | Default |
|---|---|---|
| 1×, 2× | 30, 60, 70, 72, 90, 120, 144, 165, 240, 300 or match your monitor | 60 |
| 3× | 30, 40, 50, 55, 60 | 60 |

We recommend 2× (or ReGPU). 3× draws about 4K (3600×1920 = 6.9 million pixels, 9 times the 1×
image), and every pixel goes through the emulation of the Xbox 360's on-chip video memory. On an
RTX 4080 SUPER the GPU sits at 99% at full clock with the CPU at about 20%: higher rates keep the
GPU at its limit, the frame rate keeps jumping (it feels like stutter), and on some PCs the
graphics driver can hang or crash. Tested there, 3× is smooth at every choice, with a short, small
dip at 60 in the heaviest spots (riding into the cave on The Dagger), and stutters at 70. The
settings menu warns you when you pick 3×.

Already done to speed it up: shadows are drawn at 1× when scaling (about 15%), full-screen images
the game reads back are loaded without an extra copy (about 12%), and depth-only passes skip the
emulated memory's pixel ordering. 4× (4800×2560, 12.3 million pixels) still needed about 18 ms
per frame and stuttered above 40 fps, so it was removed; on a 1440p screen 3× looks almost the
same.

</details>

<details>
<summary><b>How ReGPU is installed</b></summary>
<br>

SSX loads ReGPU as a ReXGlue GPU plugin (`regpu-plugin/`, adapted from
[Glue2GPU](https://github.com/zer0ducksgiven/Glue2GPU)). The installer downloads it prebuilt (the
`regpu-*` releases, made by [`scripts/build-regpu-package.ps1`](scripts/build-regpu-package.ps1):
ReGPU with this project's `patches/regpu-*.patch`, no game code). Developers build it from source
with `scripts\build-regpu.ps1` (needs Git and Python 3).

</details>

<a id="adding-dlc"></a>

## 📦 DLC and the title update

The base game works without DLC and without the title update. Like the game itself, **neither is
included**: you need the files from your own Xbox 360, bought or downloaded on your own Xbox Live
account.

| Package | Size | Contents |
|---|---|---|
| 🏔️ SSX Mount Eddie | ~250 MB | the Mt. Eddie region and the classic riders |
| 🎁 SSX Presell Pack | ~9 MB | the pre-order bonuses: Japan (Mount Fuji), Eddie as a rider, Mac's Ultimate Board |
| 🗾 SSX Japan Range | ~9 MB | pre-order bonus: Japan (Mount Fuji) |
| 🧔 SSX Eddie Pack | <1 MB | pre-order bonus: Eddie as a playable rider |
| 🏂 SSX Mac Board Pack | <1 MB | pre-order bonus: Mac's Ultimate Board |
| 📈 Title update 0.0.3.2 | ~9 MB | the game's last update ([below](#title-update)) |

All of them work. Japan Range, Eddie Pack and Mac Board Pack are the original pre-order versions:
with Mount Eddie and the Presell Pack installed, Japan, Eddie and Mac's Ultimate Board are already
there, so they add nothing new (installing them anyway does no harm).

> [!TIP]
> **Quick version:** in the installer, click **Add files...** on the *Your game* page and select
> the files (DLC and the title update together; the picks add up). The installer shows what each
> file is, or why it isn't an SSX package. DLC can also be copied to `userdata\dlc` by hand:
> the game installs it the next time it starts.

<details>
<summary><b>Full guide: finding, copying and checking the DLC files</b></summary>
<br>

**What the files look like.** Xbox 360 DLC is stored as package files with a long hexadecimal
name and no extension. SSX's DLC lives in a folder named after the game's title ID and the
content type "marketplace":

```
Content\0000000000000000\4541096D\00000002\
```

**Getting them off your console.** Copy the game's DLC from your console's hard drive to a USB
drive (*Settings → System → Storage*), or connect the console's drive to your PC. Xbox 360
storage uses its own file system, so use an Xbox 360 file tool (for example *FATXplorer*,
*Velocity* or *Horizon*) to open it and copy the package files out of the `4541096D\00000002`
folder. Copy the package files themselves, not the folder.

**Installing them by hand** (the installer's **Add files...** does the same):

1. Close the game.
2. Open the folder `userdata\dlc`. The game creates it the first time it starts, or you can
   create it yourself.
3. Copy the package files (the long-named files) directly into it, not in a subfolder.
4. Start the game. On startup it unpacks each new package into
   `userdata\0000000000000000\4541096D\00000002\`. This happens once per package, and the
   files in `dlc` can be deleted afterwards.

**Checking it worked.** Mt. Eddie and Japan appear on the world map, and the classic riders and
Eddie are selectable. The game's log (`logs` folder next to `ssx.exe`) shows a line starting with
`DLC: installed` for each package. If it says `failed to install`, the file isn't an Xbox 360
content package: check that you copied the long-named package file, not a folder or an archive.

</details>

<a id="title-update"></a>

### 📈 The title update

SSX's last title update (version 0.0.3.2) adds the gear for levels 11 to 15 to the store. With it,
the game is built from the updated version of its own code, so everything in this port (frame
rates, resolution, DLC, your name and picture) works the same.

- **Getting it off your console:** like the DLC, with an Xbox 360 file tool. Installed title
  updates are in `Content\0000000000000000\4541096D\000B0000\`. If there are two files, use the
  larger one (about 9 MB, version 0.0.3.2); the smaller one (about 1 MB) is an older update that's
  already contained in it.
- **Adding it:** **Add files...** in the installer, then **Install**. The installer checks that
  it's the supported update. Updating later keeps it; you only add it once.
- **Saves** work with both versions: the update loads saves from the disc version, and the disc
  version loads saves the update wrote.
- **Going back to the disc version:** run the builder in your SSX folder with `-NoTitleUpdate`:
  `powershell -ExecutionPolicy Bypass -File "C:\Games\SSX\source\scripts\build-ssx.ps1" -NoTitleUpdate`
  (with your own SSX folder instead of `C:\Games\SSX`). It asks for your ISO again.

<a id="level-15"></a>

### 🎚️ Levels 11 to 15

EA raised the level cap of 10 through *tuning updates*: small files the game downloads when you
sign in to EA's servers. The port can't reach those servers (they go through Xbox Live), so a new
save would never get one and its riders would stop at level 10, title update or not.

**The port takes care of that itself.** With the [title update](#title-update) installed, it makes
the game's level tables as long as EA's last tuning update did: the XP for each level, the price
of access to it, the gear slots, and the stat ranges of the gear. There is nothing to set up, and
no file from EA is involved.

A level 10 rider is offered access to level 11 on the rider screen, for 1,000,000 credits. After
buying it, the rider moves up at the end of the next event as soon as they have the XP for it (a
full XP bar at level 10 is enough). The same goes for every level up to 15, as on the console.

<details>
<summary><b>Saves that already have EA's tuning update</b></summary>
<br>

A save from a console that has been online since mid-2012 carries EA's tuning update itself, and
the game keeps using it. The port also keeps such an update aside (`userdata\tuning\update.bin`)
and gives it to your other saves; a save file put in `userdata\tuning` is picked up the same
way.

</details>

<a id="your-music"></a>

## 🎵 Your own music

SSX can play your own music instead of its soundtrack, and remixes it live as you ride, like on
the console:

1. Put your music in `userdata\music`, one folder per playlist: for example
   `music\Chill\` and `music\Punk\`. The folder name is the playlist's name; songs in subfolders
   count too. In races the game shuffles them. Music files right in `music\` form a playlist called
   *Music*. MP3, M4A/AAC, WMA, FLAC and WAV work.
2. In the game, press **Back** on the main screen, then **Music → Personalize Your Music**.
3. For race, trick, survive and menu music, pick **Playlist** and then one of your folders.
   (The game's own text says to create a playlist on the Xbox 360 console: a folder is that here.)

The game's Audio Settings have a volume for your music and a **Remix** switch. New folders show up
the next time you open the playlist list; no restart needed.

<a id="roadmap"></a>

## 🗺️ Roadmap

The game is playable and is being tested by playing through the whole World Tour.

**🔧 Being worked on**
- 🏔️ **Playing through every region** to find and fix bugs, especially the Deadly Descent
  mechanics (cold, darkness, oxygen and so on), which depend on details of the original hardware.
- 🐧 **Linux (beta):** testing on more PCs (AMD and Intel graphics, other distributions).
- 💻 **Installer reliability** on more PCs, based on the reports in the issues.

**👀 Known issues**
- A peak label on the world map was once seen drawn as a solid box (Africa). Not reproduced yet;
  screenshots are welcome.
- Only SSX (USA) is supported, with or without its last title update (0.0.3.2).
- Ultrawide screens get a 16:9 picture.
- Linux: no gamer picture yet, and the standard renderer runs at 1× only.
- Online features (RiderNet, global events, online races) don't work: they go through Xbox Live,
  which the port can't sign in to. EA's SSX servers themselves are still running for the consoles
  (on Xbox 360 with fewer features than on PS3). The badges that need them can be shown as earned
  (**F1 → Profile**).

<details>
<summary><b>✅ Fixed so far</b></summary>
<br>

- ✅ Every controller is player 1 (a controller that wasn't first, for example behind a Steam or
  DS4Windows virtual pad, got stuck at "sign in"), and **Quit game** in the settings menu (1.7.1)
- ✅ One `ssx-recomp.exe` that sets SSX up and is its launcher: a portable SSX folder (saves from
  earlier versions come along), **Check for updates**, extracted discs and
  [RecompLauncher](https://recomplauncher.com/game/ssx-recomp) support (thanks @Xarishark) (1.7.0)
- ✅ A snowflake tracker, the boost meter in freeride, the online-only badges, a portable install
  option, VSync with ReGPU at any frame rate, and full frame rates right from the start
  (thanks @StephenNarcisse) (1.6.0)
- ✅ A native Linux version (beta), with ReGPU (1.5.0)
- ✅ Races on ReGPU no longer sink to 40–60 fps: the snow's trail table uploads in about 1 ms
  instead of 10–20 (1.4.3)
- ✅ Your own music plays after every restart of an event (1.4.2)
- ✅ ReGPU is the default renderer, drawing up to 4K (1.4.0)
- ✅ Levels 11 to 15 without EA's servers ([how it works](#level-15))
- ✅ The title update (0.0.3.2) from your own console: the gear for levels 11 to 15
- ✅ All five DLC packs work, and the installer can add them for you (**Add files...**)
- ✅ Your own name and gamer picture instead of "User" and an empty square
- ✅ Antarctica's thermal protection (the rider froze in the sun)
- ✅ Yellow specks on dark rock (Africa's caves)
- ✅ Flickering yellow/green noise in the flare light on night Deadly Descents (Alaska)
- ✅ Standard renderer: blocky bands on the snow at 2×, a washed-out image at 2× and 3×, uneven
  frame pacing at the GPU's limit, and stutter at 3× (now locked to what a fast GPU holds there)

</details>

> [!TIP]
> **Found a bug?** [Open an issue](../../issues/new) with a screenshot, where it happened (region,
> peak, event) and, if the game crashed, the newest file from the `logs` folder next to `ssx.exe`.

<details>
<summary><b>🧑‍💻 Building manually / development</b></summary>
<br>

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 -Iso "D:\path\to\your\SSX.iso"
```

`patches/` holds the fixes applied to the ReXGlue SDK (`rexglue-*`) and to ReGPU (`regpu-*`),
`ssx_config.toml` holds the recompilation overrides, and `src/` holds the PC-side code (settings,
launcher, hooks). [`PROGRESS.md`](PROGRESS.md) is the development log.

The builder uses the CMake preset `ssx-player`, which builds against the prebuilt SDK package made
by [`scripts/build-sdk-package.ps1`](scripts/build-sdk-package.ps1) (the SDK source at a fixed
commit plus `patches/`). For SDK development, the `ssx-release` preset builds the SDK from source
in `tools/rexglue-src`.

</details>

<details>
<summary><b>🚀 For launchers: installing without a window</b></summary>
<br>

Both installers also run without a window, for a launcher that installs the game for the player:

```
ssx-recomp.exe          --game <ISO or extracted folder> [--folder <folder>] [--title-update <file>] [--dlc <file>]...
ssx-linux-installer.sh  --iso <ISO> --folder <folder> [--title-update <file>] [--no-menu]
```

On Windows `--game` (or `--iso`) takes the disc image or the extracted disc's folder, every option
also takes its value after `=` (`--dlc=<file>`), and `--folder` defaults to the folder
`ssx-recomp.exe` is in. `--prepare` prints only `progress <0-1> <step>` lines and, when it fails,
the reason as its last line (it's [RecompLauncher's](recomplauncher.json) setup step). The exit code
says what happened:

| Code | |
|---|---|
| 0 | the game is built |
| 2 | wrong options |
| 10 to 13 | your copy of the game: not found or not readable (10), not an Xbox 360 game (11), not the supported release (12), removed by an antivirus program (13) |
| 20 to 22 | the title update: not found or not readable (20), not the supported one (21), incomplete (22) |
| 30, 31 | the build tools couldn't be installed (30), or Visual Studio's are incomplete and couldn't be repaired (31) |
| 40 | a download failed |
| 50 | the build failed (the details are in `source\build-ssx.log`) |
| 1 | something unexpected |

The game itself (`ssx.exe`) takes `--game <ISO or folder>` too, for that start only. It exits with
10 when it has no copy of the game to start, 11 when it's built for the title update but the
update's files are missing, and ReXGlue's 1 when it couldn't start or read the disc.

The game ends up in `<folder>/source/dist/SSX/ssx.exe` (Windows) or
`<folder>/source/dist/SSX/ssx` (Linux), set up to read the ISO it was built from; a first build takes
10–30 minutes, longer when build tools have to be installed. `--no-menu` leaves the player's own SSX
app-menu entry alone (Linux; accepted and ignored on Windows, which makes no shortcut here). Without
a terminal, the Linux installer installs missing build tools through the desktop's password prompt
(pkexec) instead of sudo.

</details>

## 🙏 Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay: the static recompilation toolkit and runtime
- [Xenia](https://xenia.jp): the Xbox 360 research the ReXGlue runtime is derived from
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (hedge-dev): the codegen approach ReXGlue builds on
- [xenia-canary game patches](https://github.com/xenia-canary/game-patches) (Margen67): the SSX 60 fps and render size findings
- [ReGPU](https://github.com/zer0ducksgiven/ReGPU) and [Glue2GPU](https://github.com/zer0ducksgiven/Glue2GPU)
  (MIT): the ReGPU renderer and its ReXGlue plugin
- [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton): Direct3D 12 on Vulkan for the Linux version
- AMD FidelityFX (FSR / CAS)

---

<div align="center">

<sub>⚖️ Unofficial fan project, not affiliated with or endorsed by Electronic Arts.</sub><br>
<sub><i>SSX</i> is a trademark of Electronic Arts Inc.</sub><br>
<sub>The code in this repository is released under the <a href="LICENSE">MIT license</a></sub><br>
<sub>and contains no Electronic Arts code or assets.</sub>

</div>
