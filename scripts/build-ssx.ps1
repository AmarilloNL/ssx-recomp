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
# ReXGlue SDK commit bd833a2ab69d305f99ddf57418fc38ffec8af93d + patches 0001-0012, Release.
$SdkPackage = 'sdk-8'
$SdkPackageUrl = "https://github.com/AmarilloNL/ssx-recomp/releases/download/$SdkPackage/rexglue-sdk-ssx-8.zip"

function Step($text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) {
    Write-Host ""; Write-Host "ERROR: $text" -ForegroundColor Red
    Write-Host "Details are in $log"
    Stop-Transcript | Out-Null
    if (-not $NoPause) { Read-Host "Press Enter to close" }
    exit 1
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
    $dialog.Filter = 'Xbox 360 disc image (*.iso)|*.iso'
    if ($dialog.ShowDialog() -ne 'OK') { Fail 'No ISO selected.' }
    $Iso = $dialog.FileName
}
if (-not (Test-Path $Iso)) { Fail "ISO not found: $Iso" }
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
try { $found = Export-XdvdfsFile $Iso 'default.xex' $xex }
catch { Fail "Could not read default.xex from the ISO: $($_.Exception.GetBaseException().Message)" }
if (-not $found) {
    Fail "Could not find default.xex in that file. Is it an Xbox 360 disc image?"
}
if (-not (Test-Path $xex)) {
    Fail "default.xex was extracted to $xex but is gone. An antivirus program may have removed it; allow that folder and try again."
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
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { Fail "Installing $cmd failed." }
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
    if ((Get-CMakeVersion) -lt [version]'3.25') { Fail "CMake 3.25 or newer is needed (found $(Get-CMakeVersion))." }
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
if (-not $clang) { Fail "No suitable clang found: LLVM 20 or newer for Windows (x86_64-pc-windows-msvc) is needed." }
Write-Host "  clang: $clang"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function Test-Msvc { (Test-Path $vswhere) -and (& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) }
if (-not (Test-Msvc)) {
    Write-Host "  installing Visual Studio 2022 Build Tools (C++ libraries + Windows SDK)."
    Write-Host "  Windows will ask for administrator permission; please allow it." -ForegroundColor Yellow
    $vsArgs = '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools ' +
              '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100'
    if ($hasWinget) {
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements --override $vsArgs
    }
    if (-not (Test-Msvc)) {
        try { Start-Process (Get-Download 'https://aka.ms/vs/17/release/vs_BuildTools.exe') -ArgumentList $vsArgs -Verb RunAs -Wait }
        catch { Write-Host "  $_" }
    }
    if (-not (Test-Msvc)) { Fail "Installing the Visual Studio Build Tools failed." }
} else { Write-Host "  Visual Studio C++ build tools: found" }
# Use the C++ headers and libraries of that installation. Left alone, clang picks a Visual
# Studio installation by itself (the newest), which may lack them: "could not open
# 'msvcrtd.lib'" (issue #2). INCLUDE, LIB and VCToolsInstallDir take precedence for clang/lld.
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Fail "The Visual Studio C++ build tools in $vsPath are incomplete ($vcvars is missing). Open the Visual Studio Installer and choose Repair."
}
cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^(INCLUDE|LIB|LIBPATH|VCToolsInstallDir|VCToolsVersion|VCINSTALLDIR|VSINSTALLDIR|WindowsSdkDir|WindowsSDKVersion|UniversalCRTSdkDir|UCRTVersion)=(.*)$') {
        Set-Item "env:$($Matches[1])" $Matches[2]
    }
}
if (-not ($env:LIB -split ';' | Where-Object { $_ -and (Test-Path "$_\msvcrt.lib") })) {
    Fail "The C++ libraries (msvcrt.lib) were not found in $vsPath. Open the Visual Studio Installer, choose Modify and tick 'MSVC v143 - VS 2022 C++ x64/x86 build tools' and a Windows SDK."
}
Write-Host "  Using the C++ tools in $vsPath (MSVC $env:VCToolsVersion, Windows SDK $($env:WindowsSDKVersion.TrimEnd('\')))"

# ---------------------------------------------------------------- 3. ReXGlue SDK
# Prebuilt from the ReXGlue SDK source + this project's patches/ (no game code), so the
# SDK doesn't have to be compiled here. scripts\build-sdk-package.ps1 makes it.
$sdk = "$root\tools\rexglue-sdk-ssx"
if (-not (Test-Path "$sdk\.version") -or (Get-Content "$sdk\.version") -ne $SdkPackage) {
    Step "Downloading the ReXGlue recompiler SDK"
    $zip = "$env:TEMP\$SdkPackage.zip"
    try { Invoke-WebRequest $SdkPackageUrl -OutFile $zip -UseBasicParsing } catch { Fail "Downloading the SDK failed: $_" }
    if (Test-Path $sdk) { Remove-Item $sdk -Recurse -Force }
    Expand-Archive $zip $sdk -Force
    Remove-Item $zip
    Set-Content "$sdk\.version" $SdkPackage
    # Codegen only re-runs when the game's inputs change, so code made by an older SDK's
    # recompiler (which may carry its bugs) is removed to be generated again.
    Remove-Item "$root\generated\default" -Recurse -Force -ErrorAction SilentlyContinue
} else { Step "ReXGlue SDK already downloaded" }
# Older versions of this builder compiled the SDK here; that copy and its build (~2 GB)
# are no longer used.
if (Test-Path "$root\tools\rexglue-src\.ssx-ready") {
    Remove-Item "$root\tools\rexglue-src", "$root\out\build\ssx-release" -Recurse -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------- 4. recompile + build
Step "Recompiling SSX and building the PC version (this takes a while)"
& "$root\scripts\package.ps1" -Preset ssx-player -Clang $clang
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
