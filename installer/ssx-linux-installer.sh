#!/usr/bin/env bash
# SSX (2012) PC port - installer for Linux.
#
# Downloads this project's latest source (no game code or data) and builds the game from YOUR OWN
# Xbox 360 SSX (USA) ISO with scripts/build-ssx-linux.sh, as ssx-recomp.exe does on Windows.
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
new=$(mktemp -d)
trap 'rm -rf "$archive" "$new"' EXIT
curl -fsSL -o "$archive" "$source_url" || { echo "Downloading the source failed."; exit 1; }
tar -xzf "$archive" --strip-components=1 -C "$new" || { echo "Unpacking the source failed."; exit 1; }
# Files removed or renamed upstream don't linger in the source folders (an old patch would be
# applied next to its new version). Build output (tools, out, dist, generated) stays.
for dir in patches scripts src regpu-plugin installer; do
  [ -d "$source/$dir" ] || continue
  (cd "$source/$dir" && find . -type f) | while IFS= read -r f; do
    [ -e "$new/$dir/$f" ] || rm -f "$source/$dir/$f"
  done
done
# Only changed files are copied, and they get the current time rather than the commit's: the build
# recompiles files newer than its last build, and the commit can be older than that build.
(cd "$new" && find . -type f) | while IFS= read -r f; do
  cmp -s "$new/$f" "$source/$f" && continue
  mkdir -p "$(dirname "$source/$f")"
  cp "$new/$f" "$source/$f" || { echo "Copying the source failed."; exit 1; }
done || exit 1
exec bash "$source/scripts/build-ssx-linux.sh" "${args[@]}"
