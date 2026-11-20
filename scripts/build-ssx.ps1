# SSX (2012) PC port - one-click builder.
#
# Builds the native PC version from YOUR OWN Xbox 360 SSX disc image. Nothing from the
# game is downloaded or included: the game code is recompiled on this PC from the
# default.xex inside your ISO, and the game data is read from your ISO at runtime.
#
# Usage: ssx-recomp.exe runs this. Manually:
#   powershell -ExecutionPolicy Bypass -File scripts\build-ssx.ps1 [-Iso path] [-TitleUpdate path] [-NoPause]
# -Iso (or -Game): your SSX disc image, or the extracted disc's folder (or its default.xex).
# -TitleUpdate: the game's last title update (v0.0.3.2) from your own console, an Xbox 360
#   package file. Once added, later builds keep using it; -NoTitleUpdate goes back to the disc version.
# -NoPause: don't wait for Enter at the end (when driven by the installer).
# -NoReGpu: leave out the ReGPU renderer (the default renderer; F1 > Graphics > Renderer also has
#   the standard one). Developers build it from source with scripts\build-regpu.ps1.
# Missing build tools are installed; a Visual Studio whose C++ tools are missing or broken has them
# added or repaired by its own installer (Windows asks for permission).
# Exit codes, by what went wrong (ssx-recomp.exe passes them on; recomplauncher.json's players see
# the ERROR line): 10-13 the copy of the game, 20-22 the title update, 30-31 the build tools,
# 40 a download, 50 the build itself.
param([Alias('Game')][string]$Iso, [string]$TitleUpdate, [switch]$NoTitleUpdate, [switch]$NoPause, [switch]$NoReGpu)

$ErrorActionPreference = 'Continue'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$log = "$root\build-ssx.log"
Start-Transcript -Path $log -Force | Out-Null
# On some PCs Windows' own folders are missing from PATH, and even SystemRoot/ComSpec can be
# unset, so cmd, msiexec and the tools that vcvars64.bat runs aren't found (issue #15). Ask
# Windows itself where its system folder is (that doesn't depend on any variable) and put
# them back for this build.
$sys = [Environment]::SystemDirectory
if (-not $env:SystemRoot) { $env:SystemRoot = Split-Path $sys }
if (-not $env:ComSpec) { $env:ComSpec = "$sys\cmd.exe" }
foreach ($dir in "$sys\WindowsPowerShell\v1.0", "$sys\Wbem", $sys) {
    if (($env:Path -split ';' | ForEach-Object { $_.TrimEnd('\') }) -notcontains $dir) { $env:Path = "$dir;$env:Path" }
}

# The recompilation config (function addresses, patches) matches exactly this default.xex:
# SSX (USA) for Xbox 360, title 4541096D, media ID 55ABAB6C, version 0.0.0.2.
$ExpectedXexSha256 = 'F509845401D65A6791C5D28A3E9636A6558DA204F3851028B0B369A44A4B68EA'
# The game's last title update, version 0.0.3.2 (from 0.0.0.2): its patch and data files.
$ExpectedXexpSha256 = 'C0F52BEE35602A0FE2AEF8E39D2856C2063BDA6B9F6E7F12A4B4EAA75C4B27A2'
$ExpectedPatch0Sha256 = 'AA087BCA90D458FAA865B388AFD969510A6138E3C32E633E4E84B3181CBEC752'
# An earlier update (0.0.1.2), which 0.0.3.2 contains.
$OlderXexpSha256 = '3ED16A2D3590277EF6277BD726AF7C035AB6303D624C12F01000F9A4F1D14344'
# ReXGlue SDK commit bd833a2ab69d305f99ddf57418fc38ffec8af93d + patches 0001-0015, Release.
$SdkPackage = 'sdk-10'
$SdkPackageUrl = "https://github.com/AmarilloNL/ssx-recomp/releases/download/$SdkPackage/rexglue-sdk-ssx-10.zip"
# The ReGPU renderer, prebuilt against that SDK package by scripts\build-regpu-package.ps1 (no game code).
$ReGpuPackage = 'regpu-4'
$ReGpuPackageUrl = "https://github.com/AmarilloNL/ssx-recomp/releases/download/$ReGpuPackage/rexgpu-regpu-4.zip"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text, [int]$code = 50) {
    Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red
    Write-Host "Details are in $log"
    Stop-Transcript | Out-Null
    if (-not $NoPause) { Read-Host "Press Enter to close" }
    exit $code
}
# Portable tools come first: the builder only downloads them when the PC's own copy is missing
# or too old.
function Refresh-Path {
    $env:PATH = "$root\tools\portable\cmake\bin;$root\tools\portable\ninja;" +
                [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('PATH', 'User') + ';' +
                "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links"
}

Write-Host ""
Write-Host "  SSX (2012) - native PC port builder" -ForegroundColor Cyan
Write-Host "  ------------------------------------"
Write-Host "  You need your OWN legally obtained Xbox 360 copy of SSX (USA), dumped to an ISO."
Write-Host "  This builder does not download any game code or data."
Write-Host "  It needs about 10 GB of free space (mostly the build tools, which other software can share),"
Write-Host "  an internet connection, and 10-30 minutes the first time. Keep this window open until it says DONE."

# ---------------------------------------------------------------- 1. the player's ISO
Step "Select your SSX (Xbox 360) ISO"
if (-not $Iso) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select your own SSX (Xbox 360) disc image'
    $dialog.Filter = 'SSX disc image or extracted default.xex (*.iso, default.xex)|*.iso;default.xex'
    if ($dialog.ShowDialog() -ne 'OK') { Fail 'No ISO selected.' 10 }
    $Iso = $dialog.FileName
}
if (-not (Test-Path $Iso)) { Fail "Your copy of SSX wasn't found: $Iso" 10 }
# An extracted disc: its folder, or the default.xex in it. The game reads the folder as it runs.
$GameFolder = $null
if ((Split-Path $Iso -Leaf) -eq 'default.xex') { $Iso = Split-Path $Iso -Parent }
if (Test-Path $Iso -PathType Container) {
    if (-not (Test-Path (Join-Path $Iso 'default.xex'))) {
        Fail "That folder has no default.xex: pick the extracted SSX disc's folder, or the disc image (.iso)." 11
    }
    # The whole disc, not default.xex alone (like game-dump\files here): the game reads its data from
    # this folder, and without it crashes at once (issue #23).
    if (-not (Test-Path (Join-Path $Iso 'data0.big'))) {
        Fail ("That folder has default.xex but not the rest of the disc (data0.big and the other files): " +
              "pick your whole extracted SSX disc's folder, or the disc image (.iso).") 11
    }
    $GameFolder = (Resolve-Path $Iso).Path
    $Iso = $GameFolder  # what ssx.toml points the game at
}
Write-Host "Using $Iso"

# Minimal XDVDFS reader: find default.xex in the disc's game partition and copy it out.
# Plain PowerShell on purpose: compiling a C# helper (Add-Type) fails on some PCs
# ("A required privilege is not held by the client", security software blocking csc).
# Directory entries form a binary tree; left/right child offsets are in dwords.
function Find-XdvdfsEntry([byte[]]$Dir, [int]$Offset, [string]$Name) {
    if ($Offset -lt 0 -or $Offset + 14 -gt $Dir.Length) { return $null }
    $left = [BitConverter]::ToUInt16($Dir, $Offset); $right = [BitConverter]::ToUInt16($Dir, $Offset + 2)
    if ($left -eq 0xFFFF -and $right -eq 0xFFFF) { return $null }
    $nameLength = [Math]::Min([int]$Dir[$Offset + 13], $Dir.Length - $Offset - 14)
    if ([Text.Encoding]::ASCII.GetString($Dir, $Offset + 14, $nameLength) -eq $Name) {  # -eq ignores case
        return @([BitConverter]::ToUInt32($Dir, $Offset + 4), [BitConverter]::ToUInt32($Dir, $Offset + 8))
    }
    if ($left) { $found = Find-XdvdfsEntry $Dir ($left * 4) $Name; if ($found) { return $found } }
    if ($right) { return Find-XdvdfsEntry $Dir ($right * 4) $Name }
    return $null
}
function Export-XdvdfsFile([string]$IsoPath, [string]$Name, [string]$OutPath) {
    $iso = [IO.File]::OpenRead($IsoPath)
    try {
        foreach ($base in 0, 0xFD90000, 0x2080000, 0x18300000) {
            if ($base + 33 * 2048 -gt $iso.Length) { continue }
            $head = New-Object byte[] 28
            $iso.Position = $base + 32 * 2048
            [void]$iso.Read($head, 0, 28)
            if ([Text.Encoding]::ASCII.GetString($head, 0, 20) -cne 'MICROSOFT*XBOX*MEDIA') { continue }
            $rootSector = [BitConverter]::ToUInt32($head, 20); $rootSize = [BitConverter]::ToUInt32($head, 24)
            $dir = New-Object byte[] $rootSize
            $iso.Position = $base + [long]$rootSector * 2048
            [void]$iso.Read($dir, 0, $dir.Length)
            $entry = Find-XdvdfsEntry $dir 0 $Name
            if (-not $entry) { return $false }
            $iso.Position = $base + [long]$entry[0] * 2048
            $remaining = [long]$entry[1]
            $out = [IO.File]::Create($OutPath)
            try {
                $buffer = New-Object byte[] (1MB)
                while ($remaining -gt 0) {
                    $n = $iso.Read($buffer, 0, [int][Math]::Min([long]$buffer.Length, $remaining))
                    if ($n -le 0) { return $false }
                    $out.Write($buffer, 0, $n); $remaining -= $n
                }
            } finally { $out.Dispose() }
            return $true
        }
        return $false
    } finally { $iso.Dispose() }
}
$xex = "$root\game-dump\files\default.xex"
New-Item -ItemType Directory -Force (Split-Path $xex) | Out-Null
# Catch read errors here, or the checks below run on a missing file and report the wrong problem.
if ($GameFolder) {
    try { Copy-Item (Join-Path $GameFolder 'default.xex') $xex -Force; $found = $true }
    catch { Fail "Could not read default.xex from that folder: $($_.Exception.GetBaseException().Message)" 10 }
} else {
    try { $found = Export-XdvdfsFile $Iso 'default.xex' $xex }
    catch { Fail "Could not read default.xex from the ISO: $($_.Exception.GetBaseException().Message)" 10 }
}
if (-not $found) {
    Fail "Could not find default.xex in that file. Is it an Xbox 360 disc image?" 11
}
if (-not (Test-Path $xex)) {
    Fail "default.xex was extracted to $xex but is gone. An antivirus program may have removed it; allow that folder and try again." 13
}
$hash = (Get-FileHash $xex -Algorithm SHA256).Hash
if ($hash -ne $ExpectedXexSha256) {
    Remove-Item $xex
    Fail ("This copy is not the supported SSX release: it needs SSX for Xbox 360 (USA, Europe and Australia disc, " +
          "title 4541096D, media ID 55ABAB6C).`ndefault.xex SHA-256 was $hash") 12
}
Write-Host "SSX (USA) default.xex verified." -ForegroundColor Green

# ---------------------------------------------------------------- 1b. the title update (optional)
# An Xbox 360 STFS package holding default.xexp (a patch for default.xex) and patch0.big (data).
# With default.xexp next to default.xex the game is recompiled from the patched executable
# (CMakeLists.txt), and package.ps1 puts both files in dist\SSX\update for the game to load.
# Minimal STFS reader, plain PowerShell like the disc reader above. Read-only packages store a
# file's blocks one after another; a hash table block follows every 170 data blocks.
function Get-StfsBlockOffset([long]$HeaderSize, [int]$Shift, [long]$Index) {
    $block = $Index; $base = 0xAA
    for ($i = 0; $i -lt 3; $i++) {
        $block += [Math]::Floor(($Index + $base) / $base) * [Math]::Pow(2, $Shift)
        if ($Index -lt $base) { break }
        $base *= 0xAA
    }
    return (($HeaderSize + 0xFFF) -band -bnot [long]0xFFF) + $block * 4096
}
function Export-StfsFile([string]$Package, [string]$Name, [string]$OutPath) {
    $f = [IO.File]::OpenRead($Package)
    try {
        $h = New-Object byte[] 0x1000
        if ($f.Read($h, 0, $h.Length) -lt $h.Length) { return $false }
        $magic = [Text.Encoding]::ASCII.GetString($h, 0, 4)
        if ($magic -ne 'LIVE' -and $magic -ne 'PIRS' -and $magic -ne 'CON ') { return $false }
        $headerSize = ([long]$h[0x340] -shl 24) + ([long]$h[0x341] -shl 16) + ([long]$h[0x342] -shl 8) + $h[0x343]
        $shift = if (((($headerSize + 0xFFF) -band 0xB000) -eq 0xB000) -or ($h[0x37B] -band 1)) { 0 } else { 1 }
        $tableBlocks = [BitConverter]::ToUInt16($h, 0x37C)
        # (Bytes are cast before shifting: [byte] -shl 8 is 0 in PowerShell.)
        $tableStart = $h[0x37E] + ([int]$h[0x37F] -shl 8) + ([int]$h[0x380] -shl 16)
        $block = New-Object byte[] 4096
        for ($t = 0; $t -lt $tableBlocks; $t++) {
            $f.Position = Get-StfsBlockOffset $headerSize $shift ($tableStart + $t)
            [void]$f.Read($block, 0, 4096)
            for ($e = 0; $e -lt 64; $e++) {
                $o = $e * 64
                $nameLength = $block[$o + 0x28] -band 0x3F
                if ($nameLength -eq 0 -or ($block[$o + 0x28] -band 0x80)) { continue }
                if ([Text.Encoding]::ASCII.GetString($block, $o, $nameLength) -ne $Name) { continue }
                $start = $block[$o + 0x2F] + ([int]$block[$o + 0x30] -shl 8) + ([int]$block[$o + 0x31] -shl 16)
                $remaining = ([long]$block[$o + 0x34] -shl 24) + ([long]$block[$o + 0x35] -shl 16) +
                             ([long]$block[$o + 0x36] -shl 8) + $block[$o + 0x37]
                $out = [IO.File]::Create($OutPath)
                try {
                    for ($k = 0; $remaining -gt 0; $k++) {
                        $f.Position = Get-StfsBlockOffset $headerSize $shift ($start + $k)
                        $n = $f.Read($block, 0, [int][Math]::Min(4096, $remaining))
                        if ($n -le 0) { return $false }
                        $out.Write($block, 0, $n); $remaining -= $n
                    }
                } finally { $out.Dispose() }
                return $true
            }
        }
        return $false
    } finally { $f.Dispose() }
}
$xexp = "$root\game-dump\files\default.xexp"
$updateDir = "$root\game-dump\update"
if ($NoTitleUpdate) {
    Remove-Item $xexp, $updateDir -Recurse -Force -ErrorAction SilentlyContinue
} elseif ($TitleUpdate) {
    Step "Check the title update"
    if (-not (Test-Path $TitleUpdate)) { Fail "Your title update wasn't found: $TitleUpdate" 20 }
    Write-Host "Using $TitleUpdate"
    Remove-Item $updateDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $updateDir | Out-Null
    try {
        $hasXexp = Export-StfsFile $TitleUpdate 'default.xexp' "$updateDir\default.xexp"
        $hasPatch0 = Export-StfsFile $TitleUpdate 'patch0.big' "$updateDir\patch0.big"
    } catch { Fail "Could not read the title update: $($_.Exception.GetBaseException().Message)" 20 }
    if (-not $hasXexp) { Fail "That file isn't an SSX title update (it has no default.xexp)." 21 }
    $xexpHash = (Get-FileHash "$updateDir\default.xexp" -Algorithm SHA256).Hash
    if ($xexpHash -eq $OlderXexpSha256) {
        Remove-Item $updateDir -Recurse -Force
        Fail ("This is an older SSX title update (0.0.1.2, about 1 MB). Use the latest one (0.0.3.2, about 9 MB, " +
              "in the same console folder); it already contains this one.") 21
    }
    if ($xexpHash -ne $ExpectedXexpSha256 -or -not $hasPatch0 -or
        (Get-FileHash "$updateDir\patch0.big" -Algorithm SHA256).Hash -ne $ExpectedPatch0Sha256) {
        Remove-Item $updateDir -Recurse -Force
        Fail ("This title update isn't the supported one.`nSupported: SSX title update 0.0.3.2 " +
              "(title 4541096D).`ndefault.xexp SHA-256 was $xexpHash") 21
    }
    Copy-Item "$updateDir\default.xexp" $xexp -Force
    Write-Host "SSX title update 0.0.3.2 verified." -ForegroundColor Green
}
$gameVersion = if (Test-Path $xexp) { '0.0.3.2' } else { '0.0.0.2' }
if ($gameVersion -eq '0.0.3.2' -and -not (Test-Path "$updateDir\patch0.big")) {
    Fail "The title update's files are incomplete. Start ssx-recomp.exe, click Add DLC / rebuild and add the title update." 22
}
Write-Host ("Building the game version $gameVersion" + $(if ($gameVersion -eq '0.0.3.2') { ' (with the title update).' } else { ' (the disc release).' }))
# Code generated for the other version is removed, so it is generated again from scratch.
$versionConfig = "$root\game-dump\ssx_version.toml"
if ((Test-Path $versionConfig) -and -not (Select-String -Path $versionConfig -SimpleMatch "v$gameVersion " -Quiet)) {
    Remove-Item "$root\generated\default" -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------- 2. build tools
# winget when it's there, otherwise each tool's official download.
Step "Checking build tools (missing ones are installed)"
Refresh-Path
$hasWinget = [bool](Get-Command winget -ErrorAction SilentlyContinue)
$portable = "$root\tools\portable"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-ReleaseAsset($repo, $pattern) {
    $release = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest" -Headers @{ 'User-Agent' = 'ssx-recomp-builder' }
    ($release.assets | Where-Object { $_.name -match $pattern } | Select-Object -First 1).browser_download_url
}
function Get-Download($url) {
    $file = Join-Path $env:TEMP ([IO.Path]::GetFileName($url))
    Write-Host "  downloading $url"
    Invoke-WebRequest $url -OutFile $file -UseBasicParsing
    $file
}
function Expand-Portable($zip, $name) {
    $dest = "$portable\$name"
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Expand-Archive $zip "$portable\$name-tmp" -Force
    $inner = Get-ChildItem "$portable\$name-tmp" -Directory
    if ($inner.Count -eq 1 -and -not (Get-ChildItem "$portable\$name-tmp" -File)) { Move-Item $inner[0].FullName $dest; Remove-Item "$portable\$name-tmp" }
    else { Move-Item "$portable\$name-tmp" $dest }
}
function Install-Tool($cmd, $wingetId, [scriptblock]$fallback) {
    if (Get-Command $cmd -ErrorAction SilentlyContinue) { Write-Host "  ${cmd}: found"; return }
    if ($hasWinget) {
        Write-Host "  installing $wingetId with winget..."
        winget install --id $wingetId -e --silent --accept-source-agreements --accept-package-agreements
        Refresh-Path
    }
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        Write-Host "  installing $cmd from its official download..."
        try { & $fallback } catch { Write-Host "  $_" }
        Refresh-Path
    }
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { Fail "Installing $cmd failed. Windows may have asked for administrator permission: allow it, or install $cmd yourself." 30 }
}

$installPortableCMake = {
    Expand-Portable (Get-Download (Get-ReleaseAsset 'Kitware/CMake' '^cmake-[\d.]+-windows-x86_64\.zip$')) 'cmake'
}
function Get-CMakeVersion {
    try { [version]((cmake --version | Select-Object -First 1) -replace '^cmake version (\d+\.\d+\.\d+).*$', '$1') }
    catch { [version]'0.0' }
}
Install-Tool 'cmake' 'Kitware.CMake' $installPortableCMake
# Another program's older CMake can be first on PATH; the project's presets need 3.25.
if ((Get-CMakeVersion) -lt [version]'3.25') {
    Write-Host "  cmake $(Get-CMakeVersion) is too old (3.25 or newer is needed); downloading a portable copy..."
    try { & $installPortableCMake } catch { Write-Host "  $_" }
    Refresh-Path
    if ((Get-CMakeVersion) -lt [version]'3.25') { Fail "CMake 3.25 or newer is needed (found $(Get-CMakeVersion))." 30 }
}
Install-Tool 'ninja' 'Ninja-build.Ninja' {
    Expand-Portable (Get-Download (Get-ReleaseAsset 'ninja-build/ninja' '^ninja-win\.zip$')) 'ninja'
}
# The prebuilt SDK is compiled for the MSVC ABI, so the game must be too. Other tools can put
# a clang made for MinGW on PATH; it uses another C++ library and can't link against the SDK
# (issue #3). So use the first clang 20 or newer that targets MSVC, and install LLVM if none.
function Find-Clang {
    $found = @("$env:ProgramFiles\LLVM\bin\clang.exe") + @(Get-Command clang.exe -All -ErrorAction SilentlyContinue).Source
    foreach ($clang in $found | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique) {
        $info = (& $clang --version 2>$null) -join "`n"
        if ($info -match 'version (\d+)' -and [int]$Matches[1] -ge 20 -and $info -match 'Target: \S+-windows-msvc') { return $clang }
        Write-Host "  not using $clang (version 20 or newer for MSVC is needed): $(($info -split "`n")[0..1] -join ', ')"
    }
}
$clang = Find-Clang
if (-not $clang -and $hasWinget) {
    Write-Host "  installing LLVM.LLVM with winget..."
    winget install --id LLVM.LLVM -e --silent --accept-source-agreements --accept-package-agreements
    $clang = Find-Clang
}
if (-not $clang) {
    Write-Host "  installing LLVM from its official download..."
    Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
    try {
        $installer = Get-Download (Get-ReleaseAsset 'llvm/llvm-project' '^LLVM-[\d.]+-win64\.(msi|exe)$')
        if ($installer -like '*.msi') { Start-Process msiexec -ArgumentList "/i `"$installer`" /qn /norestart" -Verb RunAs -Wait }
        else { Start-Process $installer -ArgumentList '/S' -Verb RunAs -Wait }
    } catch { Write-Host "  $_" }
    $clang = Find-Clang
}
if (-not $clang) { Fail "No suitable clang found: LLVM 20 or newer for Windows (x86_64-pc-windows-msvc) is needed." 30 }
Write-Host "  clang: $clang"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
# Visual Studio 2022 (17.0) or newer: older ones (2017, 2019) lack the C++20 headers the SDK
# uses, like <bit> and <span> (issue #3). They can stay installed next to it.
$vsFilter = '-products', '*', '-version', '17.0', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
function Test-Msvc { (Test-Path $vswhere) -and (& $vswhere @vsFilter -property installationPath) }
$vsComponents = '--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
                '--add Microsoft.VisualStudio.Component.Windows11SDK.26100'
$vsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"
# Visual Studio's own installer on an installation there: modify adds the C++ tools it lacks, repair
# puts back broken or missing files. Windows asks for permission once.
function Invoke-VsInstaller([string]$Verb, [string]$Path) {
    if (-not (Test-Path $vsInstaller) -or -not $Path) { return }
    Write-Host "  installing Visual Studio C++ build tools ($Verb of $Path)."
    Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
    $a = "$Verb --installPath `"$Path`" --quiet --norestart" + $(if ($Verb -eq 'modify') { " $vsComponents" } else { '' })
    try { Start-Process $vsInstaller -ArgumentList $a -Verb RunAs -Wait } catch { Write-Host "  $_" }
}
if (-not (Test-Msvc)) {
    if ((Test-Path $vswhere) -and (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)) {
        Write-Host "  The Visual Studio found is older than 2022, too old for this build."
    }
    # Visual Studio 2022 or newer without its C++ tools (another workload, or they were removed): they're
    # added to it, rather than a second installation next to it.
    if (Test-Path $vswhere) { Invoke-VsInstaller 'modify' (& $vswhere -products * -version 17.0 -latest -property installationPath) }
}
if (-not (Test-Msvc)) {
    Write-Host "  installing Visual Studio 2022 Build Tools (C++ libraries + Windows SDK)."
    Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
    $vsArgs = "--quiet --wait --norestart $vsComponents"
    if ($hasWinget) {
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override $vsArgs
    }
    if (-not (Test-Msvc)) {
        try { Start-Process (Get-Download 'https://aka.ms/vs/17/release/vs_BuildTools.exe') -ArgumentList $vsArgs -Verb RunAs -Wait }
        catch { Write-Host "  $_" }
    }
    if (-not (Test-Msvc)) { Fail "Installing the Visual Studio Build Tools failed. Windows may have asked for administrator permission: allow it, or install them yourself." 30 }
} else { Write-Host "  Visual Studio C++ build tools: found" }
# Use the C++ headers and libraries of that installation. Left alone, clang picks a Visual
# Studio installation by itself (the newest), which may lack them: "could not open
# 'msvcrtd.lib'" (issue #2). INCLUDE, LIB and VCToolsInstallDir take precedence for clang/lld.
$vsPath = & $vswhere -latest @vsFilter -property installationPath
$vcvars = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path "$sys\cmd.exe")) {
    Fail "Windows' command prompt ($sys\cmd.exe) is missing or blocked on this PC. Run 'sfc /scannow' as administrator to restore it." 31
}
# The installation's C++ environment (vcvars64.bat).
function Import-VcVars {
    if (-not (Test-Path $vcvars)) { return }
    & "$sys\cmd.exe" /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^(INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCToolsVersion|VCINSTALLDIR|VSINSTALLDIR|WindowsSdkDir|WindowsSDKVersion|UniversalCRTSdkDir|UCRTVersion)=(.*)$') {
            Set-Item "env:$($Matches[1])" $Matches[2]
        }
    }
}
function Test-Lib($name) { [bool]($env:LIB -split ';' | Where-Object { $_ -and (Test-Path "$_\$name") }) }
# Its environment, and whether its C++ libraries are there.
function Use-VcVars { Import-VcVars; return (Test-Path $vcvars) -and (Test-Lib 'msvcrt.lib') }
# Installed but incomplete or broken: what's missing is added, then the installation repaired, before giving up.
if (-not (Use-VcVars)) {
    Write-Host "  The Visual Studio C++ build tools in $vsPath are incomplete; adding what's missing." -ForegroundColor Yellow
    Invoke-VsInstaller 'modify' $vsPath
    if (-not (Use-VcVars)) { Invoke-VsInstaller 'repair' $vsPath }
    if (-not (Use-VcVars)) {
        Fail ("The Visual Studio C++ build tools in $vsPath are incomplete and couldn't be repaired. Open the Visual Studio " +
              "Installer, choose Modify and tick 'MSVC v143 - VS 2022 C++ x64/x86 build tools' and a Windows SDK.") 31
    }
}
# The Windows SDK (kernel32.lib, ucrt.lib, ...) can be missing from a Visual Studio that has the
# C++ tools, or be installed where vcvars64.bat doesn't find it (issue #15).
$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
function Find-WinSdk { Get-ChildItem "$kits\Lib\10.*\um\x64\kernel32.lib" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1 }
if (-not ((Test-Lib 'kernel32.lib') -and (Test-Lib 'ucrt.lib'))) {
    if (-not (Find-WinSdk)) {
        Write-Host "  The Windows SDK is missing; adding it to Visual Studio."
        Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
        $vsSetup = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"
        try { Start-Process $vsSetup -ArgumentList "modify --installPath `"$vsPath`" --add Microsoft.VisualStudio.Component.Windows11SDK.26100 --quiet --norestart" -Verb RunAs -Wait }
        catch { Write-Host "  $_" }
        Import-VcVars
    }
    $sdkLib = Find-WinSdk
    if ($sdkLib -and -not ((Test-Lib 'kernel32.lib') -and (Test-Lib 'ucrt.lib'))) {
        # Installed, but vcvars64.bat didn't pick it up: use it directly.
        $ver = $sdkLib.Directory.Parent.Parent.Name
        $env:LIB = "$env:LIB;$kits\Lib\$ver\um\x64;$kits\Lib\$ver\ucrt\x64"
        $env:INCLUDE = "$env:INCLUDE;$kits\Include\$ver\ucrt;$kits\Include\$ver\um;$kits\Include\$ver\shared;$kits\Include\$ver\winrt;$kits\Include\$ver\cppwinrt"
        $env:WindowsSdkDir = "$kits\"; $env:WindowsSDKVersion = "$ver\"; $env:UniversalCRTSdkDir = "$kits\"; $env:UCRTVersion = $ver
    }
    if (-not ((Test-Lib 'kernel32.lib') -and (Test-Lib 'ucrt.lib'))) {
        Fail "The Windows SDK (kernel32.lib) was not found. Open the Visual Studio Installer, choose Modify, and tick 'Windows 11 SDK' under Individual components." 31
    }
}
Write-Host "  Using the C++ tools in $vsPath (MSVC $env:VCToolsVersion, Windows SDK $($env:WindowsSDKVersion.TrimEnd('\')))"

# ---------------------------------------------------------------- 3. ReXGlue SDK
# Prebuilt from the ReXGlue SDK source + this project's patches/ (no game code), so the
# SDK doesn't have to be compiled here. scripts\build-sdk-package.ps1 makes it.
$sdk = "$root\tools\rexglue-sdk-ssx"
if (-not (Test-Path "$sdk\.version") -or (Get-Content "$sdk\.version") -ne $SdkPackage) {
    Step "Downloading the ReXGlue recompiler SDK"
    $zip = "$env:TEMP\$SdkPackage.zip"
    try { Invoke-WebRequest $SdkPackageUrl -OutFile $zip -UseBasicParsing } catch { Fail "Downloading the recompiler SDK failed (is this PC online?): $_" 40 }
    if (Test-Path $sdk) { Remove-Item $sdk -Recurse -Force }
    Expand-Archive $zip $sdk -Force
    Remove-Item $zip
    Set-Content "$sdk\.version" $SdkPackage
    # Codegen only re-runs when the game's inputs change, so code made by an older SDK's
    # recompiler (which may carry its bugs) is removed to be generated again.
    Remove-Item "$root\generated\default" -Recurse -Force -ErrorAction SilentlyContinue
} else { Step "ReXGlue SDK already downloaded" }
# The recompiler and the game read their own paths in the PC's ANSI code page, so in a folder
# with other characters, like a Cyrillic user name, the recompiler can't open the project and
# the game crashes at start (issue #15). There both run in UTF-8, set in their manifests; the
# generated code is the same.
$nonAscii = $root -match '[^\x00-\x7F]'
function Set-Utf8CodePage($exe) {
    $mt = Get-ChildItem "$kits\bin\10.*\x64\mt.exe" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
    if (-not $mt) { Fail "The install folder $root has letters the recompiler can't read, and the Windows SDK's mt.exe to fix that wasn't found. Move ssx-recomp.exe to a folder with only English letters, like C:\Games\SSX, and set SSX up there." }
    $utf8 = "$env:TEMP\ssx-utf8.manifest"
    Set-Content $utf8 -Encoding UTF8 -Value @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>
    </windowsSettings>
  </application>
</assembly>
'@
    & $mt.FullName -nologo -manifest $utf8 "-updateresource:$exe;#1"
    $failed = $LASTEXITCODE
    Remove-Item $utf8
    if ($failed) { Fail "The install folder $root has letters the recompiler can't read, and setting $exe to UTF-8 failed. Move ssx-recomp.exe to a folder with only English letters, like C:\Games\SSX, and set SSX up there." }
}
if ($nonAscii -and -not (Test-Path "$sdk\.utf8")) {
    Set-Utf8CodePage "$sdk\bin\rexglue.exe"
    Set-Content "$sdk\.utf8" 'rexglue.exe runs in UTF-8'
}
# Older versions of this builder compiled the SDK here; that copy and its build (~2 GB)
# are no longer used.
if (Test-Path "$root\tools\rexglue-src\.ssx-ready") {
    Remove-Item "$root\tools\rexglue-src", "$root\out\build\ssx-release" -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------- 4. recompile + build
Step "Recompiling SSX and building the PC version (this takes a while)"
& "$root\scripts\package.ps1" -Preset ssx-player -Clang $clang
if (-not (Test-Path "$root\dist\SSX\ssx.exe")) { Fail "The build failed. The details are in build-ssx.log." 50 }
if ($nonAscii) { Set-Utf8CodePage "$root\dist\SSX\ssx.exe" }  # see Set-Utf8CodePage
# The ReGPU renderer next to ssx.exe (the game's default). Without it the game uses the standard
# renderer, so a failed download isn't fatal. Kept in tools\regpu like the SDK, so a rebuild
# downloads it only when the package version changes.
if (-not $NoReGpu) {
    Step "Installing the ReGPU renderer"
    $regpu = "$root\tools\regpu"
    try {
        if (-not (Test-Path "$regpu\.version") -or (Get-Content "$regpu\.version") -ne $ReGpuPackage) {
            $zip = "$env:TEMP\$ReGpuPackage.zip"
            Invoke-WebRequest $ReGpuPackageUrl -OutFile $zip -UseBasicParsing
            if (Test-Path $regpu) { Remove-Item $regpu -Recurse -Force }
            Expand-Archive $zip $regpu -Force
            Remove-Item $zip
            Set-Content "$regpu\.version" $ReGpuPackage
        }
        Copy-Item "$regpu\*" "$root\dist\SSX" -Recurse -Force -Exclude .version
        Write-Host "ReGPU renderer installed."
    } catch {
        Write-Host "Couldn't download the ReGPU renderer ($($_.Exception.Message)); the game will use the standard renderer." -ForegroundColor Yellow
    }
}

# Point the game at the ISO so the first start needs no questions.
$toml = "$root\dist\SSX\ssx.toml"
if (-not (Test-Path $toml)) {
    $isoEscaped = $Iso.Replace('\', '\\').Replace('"', '\"')
    Set-Content $toml "# SSX settings - written by the game. Edit in-game with F1.`ngame_data_root = `"$isoEscaped`"" -Encoding ascii
}

# SSX's shaders for ReGPU's first-boot compile (issue #17): the game starts once, loads all its effects,
# writes static-shaders.bin beside ssx.exe and quits (about 15 s). Without the file ReGPU compiles each
# shader the first time it is drawn, as before, so a failure here only warns.
if (Test-Path "$root\dist\SSX\rexgpu-regpu.dll") {
    Step "Collecting the game's shaders for the renderer"
    Remove-Item "$root\dist\SSX\static-shaders.bin" -ErrorAction SilentlyContinue
    $collect = Start-Process "$root\dist\SSX\ssx.exe" -WorkingDirectory "$root\dist\SSX" -WindowStyle Minimized -PassThru `
        -ArgumentList '--ssx_collect_shaders=true', '--audio_mute', '--fullscreen=false'
    if (-not $collect.WaitForExit(120000)) {
        Stop-Process -Id $collect.Id -ErrorAction SilentlyContinue
        Remove-Item "$root\dist\SSX\regpu-starting.flag" -ErrorAction SilentlyContinue
    }
    # The pipelines (shader pairs and render states) rides need, so ReGPU builds them at the first start too.
    Copy-Item "$root\regpu-plugin\pipeline-seed.txt" "$root\dist\SSX\" -Force
    if (Test-Path "$root\dist\SSX\static-shaders.bin") { Write-Host "  Shaders collected." }
    else { Write-Host "  Couldn't collect them; the game compiles each shader the first time it's used instead." -ForegroundColor Yellow }
}

Write-Host ""
Write-Host "DONE. Your game is in: $root\dist\SSX" -ForegroundColor Green
Write-Host "Start it with ssx.exe (F1 opens the settings)."
Write-Host "Keep your ISO (or extracted disc) where it is: the game reads its data from it every time it starts."
Stop-Transcript | Out-Null
if (-not $NoPause) {
    Start-Process explorer.exe "$root\dist\SSX"
    Read-Host "Press Enter to close"
}
