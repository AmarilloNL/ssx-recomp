# Builds rexgpu-regpu.dll: ReGPU (https://github.com/zer0ducksgiven/ReGPU) as SSX's renderer, through
# the plugin in regpu-plugin/. Experimental: start the game with `ssx.exe --gpu_plugin=regpu` to use it.
# Needs what build-ssx.ps1 installs (CMake, Ninja, LLVM, the VS Build Tools, the SDK in tools\rexglue-sdk-ssx)
# plus Python 3. Copies the DLL and Microsoft's d3dcompiler_47.dll to -Dist (default dist\SSX).
param([string]$Dist)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
if (-not $Dist) { $Dist = "$root\dist\SSX" }
$tools = "$root\tools"
$sdk = "$tools\rexglue-sdk-ssx"
$ReGpuCommit = '9171c2f7a0942f2fef963bcd6c69fe7e61054495'      # rexglue-driver branch
$FfxCommit = '7539ae8b6568ad29ed78a753404351daf3b5cca7'        # AMD FSR SDK 2.4.0
$FfxFiles = @('api/internal/gpu/ffx_common_types.h', 'api/internal/gpu/ffx_core.h', 'api/internal/gpu/ffx_core_cpu.h',
              'api/internal/gpu/ffx_core_gpu_common.h', 'api/internal/gpu/ffx_core_gpu_common_half.h',
              'api/internal/gpu/ffx_core_hlsl.h', 'api/internal/gpu/ffx_core_portability.h',
              'upscalers/fsr3/include/gpu/fsr1/ffx_fsr1.h')

if (-not (Test-Path "$sdk\include\rex\ppc\context.h")) { throw "Run build-ssx.ps1 first (no SDK in $sdk)." }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ReGPU at the pinned commit, with patches\regpu-*.patch applied (fetched again when they change).
$regpu = "$tools\regpu-$($ReGpuCommit.Substring(0, 7))"
$patches = @(Get-ChildItem "$root\patches\regpu-*.patch" | Sort-Object Name)
$stamp = ($patches | ForEach-Object { "$($_.Name) $((Get-FileHash $_.FullName).Hash)" }) -join "`n"
if ((Test-Path "$regpu\.ssx-patches") -and ((Get-Content "$regpu\.ssx-patches" -Raw).Trim() -ne $stamp.Trim())) {
    Remove-Item $regpu -Recurse -Force
}
if (-not (Test-Path "$regpu\CMakeLists.txt")) {
    Write-Host "==> Downloading ReGPU $($ReGpuCommit.Substring(0, 7))"
    $zip = "$tools\regpu.zip"
    Invoke-WebRequest "https://github.com/zer0ducksgiven/ReGPU/archive/$ReGpuCommit.zip" -OutFile $zip -UseBasicParsing
    Remove-Item "$tools\ReGPU-$ReGpuCommit" -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive $zip $tools -Force
    Move-Item "$tools\ReGPU-$ReGpuCommit" $regpu
    Remove-Item $zip
    $env:GIT_CEILING_DIRECTORIES = $tools  # (tools is inside this repo: apply to the folder, not the repo)
    foreach ($p in $patches) {
        git -C $regpu apply --whitespace=nowarn $p.FullName
        if ($LASTEXITCODE) { throw "Applying $($p.Name) failed." }
    }
    Set-Content "$regpu\.ssx-patches" $stamp
}

# The FidelityFX FSR 1 shader headers (MIT), the only part of the FidelityFX SDK ReGPU needs here.
$ffx = "$tools\fidelityfx-$($FfxCommit.Substring(0, 7))\Kits\FidelityFX"
foreach ($f in $FfxFiles) {
    $out = "$ffx\$($f -replace '/', '\')"
    if (Test-Path $out) { continue }
    New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
    Invoke-WebRequest "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$FfxCommit/Kits/FidelityFX/$f" -OutFile $out -UseBasicParsing
}

# The compiler environment (MSVC libraries, Windows SDK with fxc), as build-ssx.ps1 sets it up.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$env:ProgramFiles\LLVM\bin;$env:PATH"
if (-not (Get-Command fxc.exe -ErrorAction SilentlyContinue)) { throw "fxc.exe (Windows SDK) not found." }

Write-Host "==> Building rexgpu-regpu.dll"
$ErrorActionPreference = 'Continue'  # (CMake writes warnings to stderr)
$build = "$root\out\build\regpu"
cmake -S "$root\regpu-plugin" -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS=-march=x86-64-v2" `
    "-DCMAKE_PREFIX_PATH=$sdk" "-DSSX_REGPU_SOURCE=$regpu" "-DSSX_REXGLUE_SOURCE=$sdk" "-DREGPU_FFX=$ffx"
if ($LASTEXITCODE) { throw "CMake configure failed." }
cmake --build $build
if ($LASTEXITCODE) { throw "Build failed." }

New-Item -ItemType Directory -Force $Dist | Out-Null
Copy-Item "$build\rexgpu-regpu.dll" $Dist
Copy-Item "${env:ProgramFiles(x86)}\Windows Kits\10\Redist\D3D\x64\d3dcompiler_47.dll" $Dist
Write-Host "==> Done: $Dist\rexgpu-regpu.dll (start ssx.exe --gpu_plugin=regpu)"
