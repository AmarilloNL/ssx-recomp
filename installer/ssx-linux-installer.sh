#!/usr/bin/env bash
# SSX (2012) PC port - installer for Linux.
#
# Downloads this project's latest source (no game code or data) and builds the game from YOUR OWN
# Xbox 360 SSX (USA) ISO with scripts/build-ssx-linux.sh, as SSX-Installer.exe does on Windows.
# Run it again to update: only what changed is rebuilt, and your saves (~/.local/share/ssx) and
# settings (ssx.toml next to the game) are kept.
#
# Usage: bash ssx-linux-installer.sh [--folder DIR] [--iso PATH] [--title-update PATH | --no-title-update] [--no-menu] [--portable]
#   --folder: where the port goes (default ~/SSX-PC; the game ends up in DIR/source/dist/SSX).
#   The other options go to the build script (see the top of scripts/build-ssx-linux.sh): without
#   --iso it asks for your ISO with a file dialog.
set -euo pipefail

source_url=https://github.com/AmarilloNL/ssx-recomp/archive/refs/heads/main.tar.gz
folder="$HOME/SSX-PC"
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --folder) folder=${2:?--folder needs a directory}; shift 2 ;;
    --source-url) source_url=${2:?}; shift 2 ;; # (testing: another archive of the source)
    *) args+=("$1"); shift ;;
  esac
done
for tool in curl tar; do
  command -v "$tool" >/dev/null || { echo "Please install $tool first, then run this again."; exit 1; }
done

source="$folder/source"
mkdir -p "$source"
echo "==> Downloading the SSX PC port source (no game files) into $source"
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT
curl -fsSL -o "$archive" "$source_url" || { echo "Downloading the source failed."; exit 1; }
# The source folders are replaced, so files removed or renamed upstream don't linger (an old patch
# would be applied next to its new version). Build output (tools, out, dist, generated) stays.
rm -rf "$source/patches" "$source/scripts" "$source/src" "$source/regpu-plugin" "$source/installer"
tar -xzf "$archive" --strip-components=1 -C "$source" || { echo "Unpacking the source failed."; exit 1; }
exec bash "$source/scripts/build-ssx-linux.sh" "${args[@]}"
