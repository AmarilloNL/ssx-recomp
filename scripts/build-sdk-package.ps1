# Builds the prebuilt ReXGlue SDK package that build-ssx.ps1 downloads (no game code).
# ReXGlue SDK at $SdkCommit + this project's patches\, Release, FidelityFX on, installed with
# CMake's own install rules, plus AMD's signed amd_fidelityfx_dx12.dll. Needs git, cmake,
# ninja, clang 20+ and the VS Build Tools.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\build-sdk-package.ps1 [-Out file.zip]
param([string]$Out = "$PSScriptRoot\..\tools\rexglue-sdk-ssx-2.zip")
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$SdkRepo = 'https://github.com/rexglue/rexglue-sdk.git'
$SdkCommit = 'bd833a2ab69d305f99ddf57418fc38ffec8af93d'
$work = "$root\tools\sdkpkg"
$src = "$work\src"
$stage = "$work\stage"
$env:PATH = "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"
function Check($what) { if ($LASTEXITCODE) { throw "$what failed" } }

foreach ($dir in $src, $stage) { if (Test-Path $dir) { Remove-Item $dir -Recurse -Force } }
New-Item -ItemType Directory -Force $src | Out-Null
git -C $src init -q
git -C $src remote add origin $SdkRepo
git -C $src fetch -q --depth 1 origin $SdkCommit; Check 'fetch'
git -C $src checkout -q FETCH_HEAD
git -C $src -c submodule.thirdparty/moltenvk.update=none submodule update --init --recursive --depth 1 --jobs 8
Check 'submodules'

# Git on Windows checks symlinks out as small text files holding the target path.
$repos = @($src) + (git -C $src submodule foreach --quiet --recursive 'echo $toplevel/$sm_path' |
    ForEach-Object { $_ -replace '/', '\' })
foreach ($repo in $repos) {
    foreach ($rel in (git -C $repo ls-files -s | Where-Object { $_ -like '120000*' } | ForEach-Object { ($_ -split "`t", 2)[1] })) {
        $path = Join-Path $repo $rel
        if (-not (Test-Path $path -PathType Leaf) -or (Get-Item $path).Length -gt 512) { continue }
        $target = [IO.Path]::GetFullPath((Join-Path (Split-Path $path) (Get-Content $path -Raw).Trim()))
        if (Test-Path $target -PathType Leaf) { Copy-Item $target $path -Force }
        elseif (Test-Path $target -PathType Container) { Remove-Item $path; Copy-Item $target $path -Recurse }
    }
}

foreach ($patch in Get-ChildItem "$root\patches\*.patch" | Sort-Object Name) {
    git -C $src apply --ignore-whitespace $patch.FullName; Check "applying $($patch.Name)"
}
# The SDK's install step registers itself in the user's CMake package registry; not wanted
# for a package that gets zipped and shipped.
$install = "$src\cmake\rexglue_install.cmake"
$text = Get-Content $install -Raw
$i = $text.IndexOf('install(CODE [[')
if ($i -ge 0) { Set-Content $install ($text.Substring(0, $i) + $text.Substring($text.IndexOf(']])', $i) + 3)) -NoNewline }

Push-Location $src
cmake --preset win-amd64 -DREXGLUE_ENABLE_FIDELITYFX=ON; Check 'configure'
cmake --build --preset win-amd64-release; Check 'build'
cmake --install out/build/win-amd64 --config Release --prefix $stage; Check 'install'
Pop-Location
Copy-Item "$src\out\build\win-amd64\_deps\fidelityfx-src\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll" "$stage\bin\" -Force
Copy-Item "$src\LICENSE" "$stage\licenses\rexglue-sdk-LICENSE.txt" -Force
# License texts of the bundled libraries (FFmpeg and libmspack are LGPL: their source is
# the SDK's submodules at the commits this build used).
$thirdparty = @(Get-ChildItem "$src\thirdparty" -Directory) + @(Get-Item "$src\out\build\win-amd64\_deps\fidelityfx-src")
foreach ($lib in $thirdparty) {
    $files = Get-ChildItem $lib.FullName -File | Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING|COPYRIGHT|NOTICE)' }
    if (-not $files) {  # some keep it one or two folders down (libmspack)
        $files = Get-ChildItem $lib.FullName -File -Recurse -Depth 2 | Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING)' }
    }
    if (-not $files) { continue }
    $dest = "$stage\licenses\$($lib.Name -replace '-src$', '')"
    New-Item -ItemType Directory -Force $dest | Out-Null
    $files | Copy-Item -Destination $dest -Force
}
Set-Content "$stage\SOURCE.txt" ("ReXGlue SDK $SdkRepo at $SdkCommit`r`n" +
    "with the patches in https://github.com/AmarilloNL/ssx-recomp/tree/main/patches`r`n" +
    "built by scripts/build-sdk-package.ps1 in that repository.")

if (Test-Path $Out) { Remove-Item $Out }
Compress-Archive "$stage\*" $Out
Get-Item $Out | Select-Object FullName, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
