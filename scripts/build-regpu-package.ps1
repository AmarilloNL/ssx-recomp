# Builds the prebuilt ReGPU renderer package the builder downloads (build-ssx.ps1, $ReGpuPackage):
# rexgpu-regpu.dll (ReGPU at the pinned commit + patches\regpu-*.patch, through regpu-plugin\),
# Microsoft's redistributable d3dcompiler_47.dll, and the licences. No game code.
# The DLL is built against the SDK package in tools\rexglue-sdk-ssx: rebuild and re-release this
# package whenever $SdkPackage in build-ssx.ps1 changes (the plugin ABI is the SDK's).
# Usage: scripts\build-regpu-package.ps1 [-Version 1]  ->  tools\rexgpu-regpu-<Version>.zip
param([int]$Version = 1)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..").Path
$stage = "$root\out\regpu-package"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

& "$root\scripts\build-regpu.ps1" -Dist $stage
$ErrorActionPreference = 'Stop'

$regpu = Get-ChildItem "$root\tools" -Directory -Filter 'regpu-*' | Where-Object { Test-Path "$($_.FullName)\.ssx-patches" } | Select-Object -First 1
$fsr1 = Get-ChildItem "$root\tools" -Recurse -Filter 'ffx_fsr1.h' | Select-Object -First 1
Copy-Item "$($regpu.FullName)\LICENSE" "$stage\LICENSE-ReGPU.txt"
Copy-Item "$root\regpu-plugin\LICENSE" "$stage\LICENSE-Glue2GPU.txt"
# The FSR 1 shader headers carry their MIT notice in their first comment.
$notice = (Get-Content $fsr1.FullName -Raw) -replace '(?s)^\s*(/\*.*?\*/|(//[^\n]*\n)+).*$', '$1'
Set-Content "$stage\LICENSE-FidelityFX-FSR1.txt" $notice -Encoding utf8
Set-Content "$stage\README-ReGPU.txt" -Encoding utf8 @"
SSX PC port - the ReGPU renderer (package regpu-$Version)

rexgpu-regpu.dll: ReGPU (https://github.com/zer0ducksgiven/ReGPU, MIT) with this project's
patches (patches\regpu-*.patch), loaded by ssx.exe as a GPU plugin (regpu-plugin\, adapted from
Glue2GPU, https://github.com/zer0ducksgiven/Glue2GPU, MIT). Its upscaling uses AMD FidelityFX
FSR 1 shader code (MIT). d3dcompiler_47.dll: Microsoft's redistributable HLSL compiler.
Built by scripts\build-regpu-package.ps1; contains no game code or data.
"@

$zip = "$root\tools\rexgpu-regpu-$Version.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive "$stage\*" $zip
Get-ChildItem $stage | Select-Object Name, @{ n = 'KB'; e = { [math]::Round($_.Length / 1KB) } }
Write-Host "==> $zip"
