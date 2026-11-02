# SSX (2012) PC port - one-click builder.
#
# Builds the native PC version from YOUR OWN Xbox 360 SSX disc image. Nothing from the
# game is downloaded or included: the game code is recompiled on this PC from the
# default.xex inside your ISO, and the game data is read from your ISO at runtime.
#
# Usage: SSX-Installer.exe runs this. Manually:
#   powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 [-Iso path] [-NoPause]
# -NoPause: don't wait for Enter at the end (when driven by the installer).
param([string]$Iso, [switch]$NoPause)

$ErrorActionPreference = 'Continue'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$log = "$root\build-ssx.log"
Start-Transcript -Path $log -Force | Out-Null

# The recompilation config (function addresses, patches) matches exactly this default.xex:
# SSX (USA) for Xbox 360, title 4541096D, media ID 55ABAB6C, version 0.0.0.2.
$ExpectedXexSha256 = 'F509845401D65A6791C5D28A3E9636A6558DA204F3851028B0B369A44A4B68EA'
$SdkRepo = 'https://github.com/rexglue/rexglue-sdk.git'
$SdkCommit = 'bd833a2ab69d305f99ddf57418fc38ffec8af93d'

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) {
    Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red
    Write-Host "Details are in $log"
    Stop-Transcript | Out-Null
    if (-not $NoPause) { Read-Host "Press Enter to close" }
    exit 1
}
function Refresh-Path {
    $env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('PATH', 'User') + ';' +
                "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;C:\Program Files\Git\cmd;" +
                "$env:LOCALAPPDATA\Microsoft\WinGet\Links"
}

Write-Host ""
Write-Host "  SSX (2012) - native PC port builder" -ForegroundColor Cyan
Write-Host "  ------------------------------------"
Write-Host "  You need your OWN legally obtained Xbox 360 copy of SSX (USA), dumped to an ISO."
Write-Host "  This builder does not download any game code or data."
Write-Host "  It needs about 30 GB of free space, an internet connection for the build tools,"
Write-Host "  and 30-60 minutes. Keep this window open until it says DONE."

# ---------------------------------------------------------------- 1. the player's ISO
Step "Select your SSX (Xbox 360) ISO"
if (-not $Iso) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select your own SSX (Xbox 360) disc image'
    $dialog.Filter = 'Xbox 360 disc image (*.iso)|*.iso'
    if ($dialog.ShowDialog() -ne 'OK') { Fail 'No ISO selected.' }
    $Iso = $dialog.FileName
}
if (-not (Test-Path $Iso)) { Fail "ISO not found: $Iso" }
Write-Host "Using $Iso"

# Minimal XDVDFS reader: find default.xex in the disc's game partition and copy it out.
Add-Type -TypeDefinition @'
using System; using System.IO; using System.Text;
public static class Xdvdfs {
  static readonly long[] PartitionOffsets = { 0, 0xFD90000, 0x2080000, 0x18300000 };
  public static bool ExtractFile(string iso, string name, string outPath) {
    using (var fs = File.OpenRead(iso)) {
      foreach (long baseOffset in PartitionOffsets) {
        if (baseOffset + 33 * 2048 > fs.Length) continue;
        var head = new byte[28];
        fs.Position = baseOffset + 32 * 2048;
        fs.Read(head, 0, 28);
        if (Encoding.ASCII.GetString(head, 0, 20) != "MICROSOFT*XBOX*MEDIA") continue;
        uint rootSector = BitConverter.ToUInt32(head, 20), rootSize = BitConverter.ToUInt32(head, 24);
        var dir = new byte[rootSize];
        fs.Position = baseOffset + (long)rootSector * 2048;
        fs.Read(dir, 0, (int)rootSize);
        long start, size;
        if (!Find(dir, 0, name, out start, out size)) return false;
        fs.Position = baseOffset + start * 2048;
        using (var outFile = File.Create(outPath)) {
          var buffer = new byte[1 << 20];
          while (size > 0) {
            int n = fs.Read(buffer, 0, (int)Math.Min(buffer.Length, size));
            if (n <= 0) return false;
            outFile.Write(buffer, 0, n); size -= n;
          }
        }
        return true;
      }
    }
    return false;
  }
  // Directory entries form a binary tree: left/right child offsets are in dwords.
  static bool Find(byte[] dir, int offset, string name, out long sector, out long size) {
    sector = size = 0;
    if (offset < 0 || offset + 14 > dir.Length) return false;
    ushort left = BitConverter.ToUInt16(dir, offset), right = BitConverter.ToUInt16(dir, offset + 2);
    if (left == 0xFFFF && right == 0xFFFF) return false;
    int nameLen = dir[offset + 13];
    string entry = Encoding.ASCII.GetString(dir, offset + 14, Math.Min(nameLen, dir.Length - offset - 14));
    if (string.Equals(entry, name, StringComparison.OrdinalIgnoreCase)) {
      sector = BitConverter.ToUInt32(dir, offset + 4); size = BitConverter.ToUInt32(dir, offset + 8);
      return true;
    }
    return (left != 0 && Find(dir, left * 4, name, out sector, out size)) ||
           (right != 0 && Find(dir, right * 4, name, out sector, out size));
  }
}
'@
$xex = "$root\game-dump\files\default.xex"
New-Item -ItemType Directory -Force (Split-Path $xex) | Out-Null
if (-not [Xdvdfs]::ExtractFile($Iso, 'default.xex', $xex)) {
    Fail "Could not find default.xex in that file. Is it an Xbox 360 disc image?"
}
$hash = (Get-FileHash $xex -Algorithm SHA256).Hash
if ($hash -ne $ExpectedXexSha256) {
    Remove-Item $xex
    Fail ("This ISO is not the supported SSX release.`n" +
          "Supported: SSX (USA) for Xbox 360, title 4541096D, media ID 55ABAB6C.`n" +
          "default.xex SHA-256 was $hash")
}
Write-Host "SSX (USA) default.xex verified." -ForegroundColor Green

# ---------------------------------------------------------------- 2. build tools
Step "Checking build tools (missing ones are installed with winget)"
Refresh-Path
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Fail "winget is missing. Install 'App Installer' from the Microsoft Store and run this again."
}
$tools = @(
    @{ Cmd = 'git';   Id = 'Git.Git' },
    @{ Cmd = 'cmake'; Id = 'Kitware.CMake' },
    @{ Cmd = 'ninja'; Id = 'Ninja-build.Ninja' },
    @{ Cmd = 'clang'; Id = 'LLVM.LLVM' }
)
foreach ($t in $tools) {
    if (Get-Command $t.Cmd -ErrorAction SilentlyContinue) { Write-Host "  $($t.Cmd): found"; continue }
    Write-Host "  installing $($t.Id)..."
    winget install --id $t.Id -e --silent --accept-source-agreements --accept-package-agreements
    Refresh-Path
    if (-not (Get-Command $t.Cmd -ErrorAction SilentlyContinue)) { Fail "Installing $($t.Id) failed." }
}
$clangMajor = [int]((clang --version | Select-Object -First 1) -replace '^.*version (\d+).*$', '$1')
if ($clangMajor -lt 20) { Fail "clang $clangMajor is too old; version 20 or newer is needed (winget upgrade LLVM.LLVM)." }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasMsvc = (Test-Path $vswhere) -and
    (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if (-not $hasMsvc) {
    Write-Host "  installing Visual Studio 2022 Build Tools (C++ libraries + Windows SDK)."
    Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements `
        --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
    if (-not (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
        Fail "Installing the Visual Studio Build Tools failed."
    }
} else { Write-Host "  Visual Studio C++ build tools: found" }

# ---------------------------------------------------------------- 3. ReXGlue SDK
$sdk = "$root\tools\rexglue-src"
if (-not (Test-Path "$sdk\.ssx-ready")) {
    Step "Downloading the ReXGlue recompiler SDK"
    if (Test-Path $sdk) { Remove-Item $sdk -Recurse -Force }
    New-Item -ItemType Directory -Force $sdk | Out-Null
    git -C $sdk init -q
    git -C $sdk remote add origin $SdkRepo
    git -C $sdk fetch -q --depth 1 origin $SdkCommit
    if ($LASTEXITCODE) { Fail "Downloading the SDK failed." }
    git -C $sdk checkout -q FETCH_HEAD
    git -C $sdk -c submodule.thirdparty/moltenvk.update=none submodule update --init --recursive --depth 1 --jobs 8
    if ($LASTEXITCODE) { Fail "Downloading the SDK's libraries failed." }

    # Git on Windows checks symlinks out as small text files holding the target path;
    # replace them with copies of their targets.
    $repos = @($sdk) + (git -C $sdk submodule foreach --quiet --recursive 'echo $toplevel/$sm_path' |
        ForEach-Object { $_ -replace '/', '\' })
    foreach ($repo in $repos) {
        $links = git -C $repo ls-files -s | Where-Object { $_ -like '120000*' } | ForEach-Object { ($_ -split "`t", 2)[1] }
        foreach ($rel in $links) {
            $path = Join-Path $repo $rel
            if (-not (Test-Path $path -PathType Leaf) -or (Get-Item $path).Length -gt 512) { continue }
            $target = [IO.Path]::GetFullPath((Join-Path (Split-Path $path) (Get-Content $path -Raw).Trim()))
            if (Test-Path $target -PathType Leaf) { Copy-Item $target $path -Force }
            elseif (Test-Path $target -PathType Container) { Remove-Item $path; Copy-Item $target $path -Recurse }
        }
    }

    Set-Content "$sdk\.ssx-ready" $SdkCommit
} else { Step "ReXGlue SDK already prepared" }

# Every run, so patches added in an update reach SDKs prepared by an older version.
foreach ($patch in Get-ChildItem "$root\patches\*.patch" | Sort-Object Name) {
    git -C $sdk apply --check --reverse --ignore-whitespace $patch.FullName 2>$null
    if (-not $LASTEXITCODE) { continue }  # already applied
    git -C $sdk apply --ignore-whitespace $patch.FullName
    if ($LASTEXITCODE) { Fail "Applying $($patch.Name) failed." }
}

# ---------------------------------------------------------------- 4. recompile + build
Step "Recompiling SSX and building the PC version (this takes a while)"
& "$root\scripts\package.ps1"
if (-not (Test-Path "$root\dist\SSX\ssx.exe")) { Fail "The build failed." }

# Point the game at the ISO so the first start needs no questions.
$toml = "$root\dist\SSX\ssx.toml"
if (-not (Test-Path $toml)) {
    $isoEscaped = $Iso.Replace('\', '\\').Replace('"', '\"')
    Set-Content $toml "# SSX settings - written by the game. Edit in-game with F1.`ngame_data_root = `"$isoEscaped`"" -Encoding ascii
}

Write-Host ""
Write-Host "DONE. Your game is in: $root\dist\SSX" -ForegroundColor Green
Write-Host "Start it with ssx.exe (F1 opens the settings)."
Stop-Transcript | Out-Null
if (-not $NoPause) {
    Start-Process explorer.exe "$root\dist\SSX"
    Read-Host "Press Enter to close"
}
