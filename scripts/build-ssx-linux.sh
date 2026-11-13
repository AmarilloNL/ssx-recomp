#!/usr/bin/env bash
# SSX (2012) PC port - builder for Linux.
#
# Builds the native Linux version from YOUR OWN Xbox 360 SSX disc image. Nothing from the game is
# downloaded or included: the game code is recompiled on this PC from the default.xex inside your
# ISO, and the game data is read from your ISO at runtime.
#
# Usage: scripts/build-ssx-linux.sh [--iso PATH] [--title-update PATH | --no-title-update]
#   --iso: your SSX (USA) Xbox 360 ISO (asked for with a file dialog when left out).
#   --title-update: the game's last title update (v0.0.3.2) from your own console, an Xbox 360
#     package file. Once added, later builds keep using it; --no-title-update goes back to the disc version.
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
log="$root/build-ssx-linux.log"
exec > >(tee "$log") 2>&1

# The recompilation config (function addresses, patches) matches exactly this default.xex:
# SSX (USA) for Xbox 360, title 4541096D, media ID 55ABAB6C, version 0.0.0.2.
ExpectedXexSha256=f509845401d65a6791c5d28a3e9636a6558da204f3851028b0b369a44a4b68ea
# The game's last title update, version 0.0.3.2 (from 0.0.0.2): its patch and data files.
ExpectedXexpSha256=c0f52bee35602a0fe2aef8e39d2856c2063bda6b9f6e7f12a4b4eaa75c4b27a2
ExpectedPatch0Sha256=aa087bca90d458faa865b388afd969510a6138e3c32e633e4e84b3181cbec752
# An earlier update (0.0.1.2), which 0.0.3.2 contains.
OlderXexpSha256=3ed16a2d3590277ef6277bd726af7c035ab6303d624c12f01000f9a4f1d14344
# The ReXGlue SDK is built here from this commit + the port's patches/ (no prebuilt Linux package).
SdkRepo=https://github.com/rexglue/rexglue-sdk.git
SdkCommit=bd833a2ab69d305f99ddf57418fc38ffec8af93d

step() { printf '\n\033[36m==> %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mERROR: %s\033[0m\nDetails are in %s\n' "$*" "$log"; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

iso= title_update= no_title_update=
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) iso=${2:-}; shift 2 ;;
    --title-update) title_update=${2:-}; shift 2 ;;
    --no-title-update) no_title_update=1; shift ;;
    *) fail "Unknown option: $1 (see the top of this script)" ;;
  esac
done

echo
echo "  SSX (2012) - native PC port builder for Linux"
echo "  ---------------------------------------------"
echo "  You need your OWN legally obtained Xbox 360 copy of SSX (USA), dumped to an ISO."
echo "  This builder does not download any game code or data."
echo "  It needs about 15 GB of free space during the build, an internet connection,"
echo "  and 20-60 minutes the first time."

# ---------------------------------------------------------------- 1. build tools
step "Checking build tools (missing ones are installed)"
need_cmds=(git cmake ninja clang clang++ python3 pkg-config)
need_pkgs=(x11-xcb wayland-client vulkan)
missing() {
  for c in "${need_cmds[@]}"; do command -v "$c" >/dev/null || return 0; done
  for p in "${need_pkgs[@]}"; do pkg-config --exists "$p" 2>/dev/null || return 0; done
  return 1
}
if missing; then
  # Note: only the pacman list is tested (CachyOS); the apt and dnf lists follow SDL's Linux docs.
  if command -v pacman >/dev/null; then
    cmd=(sudo pacman -S --needed --noconfirm git cmake ninja clang python pkgconf libx11 libxcb wayland
         vulkan-headers vulkan-icd-loader ffmpeg)
  elif command -v apt-get >/dev/null; then
    cmd=(sudo apt-get install -y build-essential git cmake ninja-build clang python3 pkg-config
         libx11-xcb-dev libwayland-dev wayland-protocols libxkbcommon-dev libvulkan-dev libdecor-0-dev
         libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libxrandr-dev libxi-dev
         libxcursor-dev libxss-dev libxtst-dev ffmpeg)
  elif command -v dnf >/dev/null; then
    cmd=(sudo dnf install -y gcc-c++ git cmake ninja-build clang python3 pkgconf-pkg-config libX11-devel
         libxcb-devel wayland-devel wayland-protocols-devel libxkbcommon-devel vulkan-headers
         vulkan-loader-devel libdecor-devel alsa-lib-devel pulseaudio-libs-devel pipewire-devel
         systemd-devel libXrandr-devel libXi-devel libXcursor-devel libXScrnSaver-devel libXtst-devel
         ffmpeg-free)
  else
    fail "Please install: ${need_cmds[*]} and the development files for ${need_pkgs[*]}, then run this again."
  fi
  echo "  installing with: ${cmd[*]}"
  echo "  (your password is asked for by sudo)"
  "${cmd[@]}" || fail "Installing the build tools failed."
  missing && fail "Still missing after installing: check ${need_cmds[*]} and pkg-config ${need_pkgs[*]}."
fi
cmake_version=$(cmake --version | sed -n '1s/[^0-9]*\([0-9.]*\).*/\1/p')
[ "$(printf '%s\n3.25\n' "$cmake_version" | sort -V | head -1)" = 3.25 ] ||
  fail "CMake 3.25 or newer is needed (found $cmake_version)."
clang_major=$(clang -dumpversion | cut -d. -f1)
[ "$clang_major" -ge 18 ] || fail "clang 18 or newer is needed (found $(clang -dumpversion))."
command -v ffmpeg >/dev/null || echo "  Note: ffmpeg not found; your own music in the game needs it."
echo "  cmake $cmake_version, clang $(clang -dumpversion): OK"

# ---------------------------------------------------------------- 2. the player's ISO
step "Select your SSX (Xbox 360) ISO"
if [ -z "$iso" ]; then
  if command -v kdialog >/dev/null; then
    iso=$(kdialog --title 'Select your own SSX (Xbox 360) disc image' --getopenfilename "$HOME" '*.iso|Xbox 360 disc image' 2>/dev/null)
  elif command -v zenity >/dev/null; then
    iso=$(zenity --file-selection --title='Select your own SSX (Xbox 360) disc image' --file-filter='Xbox 360 disc image | *.iso' 2>/dev/null)
  fi
  [ -n "$iso" ] || fail "No ISO selected. Run it as: $0 --iso /path/to/SSX.iso"
fi
iso=$(realpath "$iso" 2>/dev/null) && [ -f "$iso" ] || fail "ISO not found: $iso"
echo "Using $iso"

# Minimal readers for the two Xbox 360 formats the builder needs (the same as build-ssx.ps1's):
#   xdvdfs ISO NAME OUT: copies a file from the disc's root directory (a binary tree of entries).
#   stfs PACKAGE NAME OUT: copies a file from a read-only STFS package (title updates); its blocks
#     are stored one after another, with a hash table block after every 170 data blocks.
extract() {
  python3 - "$@" <<'PY'
import struct, sys
kind, src, name, out = sys.argv[1:5]
f = open(src, 'rb')

def copy(offsets, size):
    with open(out, 'wb') as o:
        for off in offsets:
            if size <= 0: break
            f.seek(off); data = f.read(min(4096 if kind == 'stfs' else 1 << 20, size))
            if not data: sys.exit(1)
            o.write(data); size -= len(data)

if kind == 'xdvdfs':
    for base in (0, 0xFD90000, 0x2080000, 0x18300000):
        f.seek(base + 32 * 2048); head = f.read(28)
        if len(head) < 28 or head[:20] != b'MICROSOFT*XBOX*MEDIA': continue
        root_sector, root_size = struct.unpack_from('<II', head, 20)
        f.seek(base + root_sector * 2048); d = f.read(root_size)
        todo = [0]
        while todo:
            at = todo.pop()
            if at + 14 > len(d): continue
            left, right, sector, size = struct.unpack_from('<HHII', d, at)
            if left == 0xFFFF and right == 0xFFFF: continue
            if d[at + 14:at + 14 + d[at + 13]].decode('ascii', 'replace').lower() == name.lower():
                start = base + sector * 2048
                copy(range(start, start + size, 1 << 20), size); sys.exit(0)
            todo += [x * 4 for x in (left, right) if x]
        sys.exit(1)
    sys.exit(1)

h = f.read(0x1000)
if h[:4] not in (b'LIVE', b'PIRS', b'CON '): sys.exit(1)
header_size = struct.unpack_from('>I', h, 0x340)[0]
shift = 0 if (((header_size + 0xFFF) & 0xB000) == 0xB000 or h[0x37B] & 1) else 1
table_blocks = struct.unpack_from('<H', h, 0x37C)[0]
table_start = int.from_bytes(h[0x37E:0x381], 'little')

def block_offset(index):
    block, base = index, 0xAA
    for _ in range(3):
        block += ((index + base) // base) << shift
        if index < base: break
        base *= 0xAA
    return ((header_size + 0xFFF) & ~0xFFF) + block * 4096

for t in range(table_blocks):
    f.seek(block_offset(table_start + t)); b = f.read(4096)
    for o in range(0, 4096, 64):
        n = b[o + 0x28] & 0x3F
        if not n or b[o + 0x28] & 0x80 or b[o:o + n].decode('ascii', 'replace').lower() != name.lower(): continue
        start = int.from_bytes(b[o + 0x2F:o + 0x32], 'little')
        size = struct.unpack_from('>I', b, o + 0x34)[0]
        copy((block_offset(start + k) for k in range((size + 4095) // 4096)), size); sys.exit(0)
sys.exit(1)
PY
}

xex="$root/game-dump/files/default.xex"
mkdir -p "$(dirname "$xex")"
extract xdvdfs "$iso" default.xex "$xex" || fail "Could not find default.xex in that file. Is it an Xbox 360 disc image?"
hash=$(sha "$xex")
if [ "$hash" != "$ExpectedXexSha256" ]; then
  rm -f "$xex"
  fail "This ISO is not the supported SSX release.
Supported: SSX (USA) for Xbox 360, title 4541096D, media ID 55ABAB6C.
default.xex SHA-256 was $hash"
fi
printf '\033[32mSSX (USA) default.xex verified.\033[0m\n'

# ---------------------------------------------------------------- 2b. the title update (optional)
# With default.xexp next to default.xex the game is recompiled from the patched executable
# (CMakeLists.txt), and update/ next to the game holds the files it loads.
xexp="$root/game-dump/files/default.xexp"
update_dir="$root/game-dump/update"
if [ -n "$no_title_update" ]; then
  rm -rf "$xexp" "$update_dir"
elif [ -n "$title_update" ]; then
  step "Check the title update"
  [ -f "$title_update" ] || fail "Title update not found: $title_update"
  echo "Using $title_update"
  rm -rf "$update_dir"; mkdir -p "$update_dir"
  extract stfs "$title_update" default.xexp "$update_dir/default.xexp" ||
    fail "That file isn't an SSX title update (it has no default.xexp)."
  xexp_hash=$(sha "$update_dir/default.xexp")
  if [ "$xexp_hash" = "$OlderXexpSha256" ]; then
    rm -rf "$update_dir"
    fail "This is an older SSX title update (0.0.1.2, about 1 MB). Use the latest one (0.0.3.2, about 9 MB,
in the same console folder); it already contains this one."
  fi
  if [ "$xexp_hash" != "$ExpectedXexpSha256" ] ||
     ! extract stfs "$title_update" patch0.big "$update_dir/patch0.big" ||
     [ "$(sha "$update_dir/patch0.big")" != "$ExpectedPatch0Sha256" ]; then
    rm -rf "$update_dir"
    fail "This title update isn't the supported one.
Supported: SSX title update 0.0.3.2 (title 4541096D).
default.xexp SHA-256 was $xexp_hash"
  fi
  cp -f "$update_dir/default.xexp" "$xexp"
  printf '\033[32mSSX title update 0.0.3.2 verified.\033[0m\n'
fi
game_version=0.0.0.2; [ -f "$xexp" ] && game_version=0.0.3.2
if [ "$game_version" = 0.0.3.2 ] && [ ! -f "$update_dir/patch0.big" ]; then
  fail "The title update's files are incomplete. Run this again with --title-update."
fi
echo "Building the game version $game_version$([ "$game_version" = 0.0.3.2 ] && echo ' (with the title update).' || echo ' (the disc release).')"
# Code generated for the other version is removed, so it is generated again from scratch.
version_config="$root/game-dump/ssx_version.toml"
if [ -f "$version_config" ] && ! grep -qF "v$game_version " "$version_config"; then
  rm -rf "$root/generated/default"
fi

# ---------------------------------------------------------------- 3. ReXGlue SDK
# Built from source + this project's patches (no game code). Rebuilt when the patches change.
sdk="$root/tools/rexglue-sdk-ssx"
sdk_version=$(cat "$root"/patches/rexglue-*.patch | sha256sum | cut -c1-16)-$SdkCommit
if [ "$(cat "$sdk/.version" 2>/dev/null)" != "$sdk_version" ]; then
  step "Building the ReXGlue recompiler SDK (the longest step)"
  src="$root/tools/rexglue-src-linux"
  rm -rf "$src" "$sdk"
  git init -q "$src" && git -C "$src" remote add origin "$SdkRepo" &&
    git -C "$src" fetch -q --depth 1 origin "$SdkCommit" && git -C "$src" checkout -q FETCH_HEAD &&
    git -C "$src" -c submodule.thirdparty/moltenvk.update=none submodule update --init --recursive --depth 1 --jobs 8 ||
    fail "Downloading the SDK source failed."
  for p in "$root"/patches/rexglue-*.patch; do
    echo "  applying $(basename "$p")"
    git -C "$src" apply --ignore-whitespace "$p" || fail "Applying $(basename "$p") failed."
  done
  # Don't register the SDK in the user's CMake package registry.
  python3 - "$src/cmake/rexglue_install.cmake" <<'PY'
import sys
p = sys.argv[1]; t = open(p).read(); i = t.find('install(CODE [[')
if i >= 0: t = t[:i] + t[t.find(']])', i) + 3:]
open(p, 'w').write(t)
PY
  (cd "$src" && cmake --preset linux-amd64 && cmake --build --preset linux-amd64-release &&
   cmake --install out/build/linux-amd64 --config Release --prefix "$sdk") || fail "Building the SDK failed."
  echo "$sdk_version" > "$sdk/.version"
  rm -rf "$src"
  # Codegen only re-runs when the game's inputs change, so code made by an older SDK's recompiler
  # (which may carry its bugs) is removed to be generated again.
  rm -rf "$root/generated/default"
else
  step "ReXGlue SDK already built"
fi

# ---------------------------------------------------------------- 4. recompile + build
step "Recompiling SSX and building the PC version (this takes a while)"
build="$root/out/build/ssx-linux"
cd "$root"
# The recompiled sources are only added to the build if codegen output exists at configure time,
# so on a fresh tree: configure, run codegen, then configure again to pick them up.
if [ ! -f generated/default/sources.cmake ]; then
  cmake --preset ssx-linux || fail "Configuring failed."
  echo "==> Recompiling the Xbox 360 code"
  cmake --build "$build" --target ssx_codegen || fail "Recompiling failed."
fi
cmake --preset ssx-linux || fail "Configuring failed."
echo "==> Compiling the PC version"
# Codegen can rewrite the PCH mid-build; retry once.
cmake --build "$build" || cmake --build "$build" || fail "The build failed."

dist="$root/dist/SSX"
# Replace the program files only; the player's ssx.toml stays.
mkdir -p "$dist"
rm -rf "$dist/update" "$dist"/*.so*
cp -f "$build/ssx" "$sdk"/lib/*.so "$root/installer/ssx.ico" "$dist/"
if [ "$game_version" = 0.0.3.2 ]; then
  mkdir -p "$dist/update" && cp -f "$update_dir/default.xexp" "$update_dir/patch0.big" "$dist/update/"
fi
# Point the game at the ISO so the first start needs no questions.
if [ ! -f "$dist/ssx.toml" ]; then
  printf '# SSX settings - written by the game. Edit in-game with F1.\ngame_data_root = "%s"\n' \
    "$(printf '%s' "$iso" | sed 's/\\/\\\\/g; s/"/\\"/g')" > "$dist/ssx.toml"
fi
# Start menu entry.
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps"
cat > "$apps/ssx-recomp.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=SSX
Comment=SSX (2012) PC port
Exec="$dist/ssx"
Path=$dist
Icon=$dist/ssx.ico
Categories=Game;
EOF

echo
printf '\033[32mDONE. Your game is in: %s\033[0m\n' "$dist"
echo "Start it from your app menu (SSX) or with $dist/ssx (F1 opens the settings)."
echo "Saves are in ${XDG_DATA_HOME:-$HOME/.local/share}/ssx."
