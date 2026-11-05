# Build the Release configuration and assemble dist\SSX (exe + runtime DLLs + readme).
# No game data is included: on first launch the game asks for the user's own disc/ISO.
# -Preset ssx-player builds against the prebuilt SDK package in tools\rexglue-sdk-ssx (what
# build-ssx.ps1 uses); ssx-release builds the SDK from tools\rexglue-src (development).
param([string]$Preset = 'ssx-release')
$root = Resolve-Path "$PSScriptRoot\.."
# Portable tools first: build-ssx.ps1 only downloads them when the PC's own copy is missing or too old.
$env:PATH = "$root\tools\portable\cmake\bin;$root\tools\portable\ninja;" +
            "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"

# Pass CMake's output through PowerShell so it also lands in build-ssx.log: Windows PowerShell's
# transcript leaves out what native programs write to the console themselves.
function Invoke-CMake {
    & cmake @args 2>&1 | ForEach-Object { if ($_ -is [Management.Automation.ErrorRecord]) { "$($_.TargetObject)" } else { "$_" } }
}

$build = "$root\out\build\$Preset"
Push-Location $root
# The recompiled sources are only added to the build if codegen output exists at configure
# time, so on a fresh tree: configure, run codegen, then configure again to pick them up.
if (-not (Test-Path "$root\generated\default\sources.cmake")) {
    Invoke-CMake --preset $Preset
    if ($LASTEXITCODE) { Pop-Location; throw "configure failed" }
    Write-Host "==> Recompiling the Xbox 360 code"
    Invoke-CMake --build $build --target ssx_codegen
    if ($LASTEXITCODE) { Pop-Location; throw "codegen failed" }
}
Invoke-CMake --preset $Preset
Pop-Location
if ($LASTEXITCODE) { throw "configure failed" }
# FSR needs AMD's FidelityFX runtime, which ships prebuilt and signed: from the fetched
# FidelityFX SDK when building the SDK here, else from the prebuilt SDK package.
# The SDK tools (shared output dir) and the game both load it at startup.
$ffx = "$build\_deps\fidelityfx-src\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll"
if (Test-Path $ffx) {
    Copy-Item $ffx "$root\tools\rexglue-src\out\win-amd64\" -Force
    Copy-Item $ffx $build -Force
} elseif (Test-Path "$root\tools\rexglue-sdk-ssx\bin\amd_fidelityfx_dx12.dll") {
    Copy-Item "$root\tools\rexglue-sdk-ssx\bin\amd_fidelityfx_dx12.dll" $build -Force
}
Write-Host "==> Compiling the PC version"
Invoke-CMake --build $build
if ($LASTEXITCODE) { Invoke-CMake --build $build }  # codegen can rewrite the PCH mid-build; retry once
if ($LASTEXITCODE) { throw "build failed" }

$dist = "$root\dist\SSX"
# Replace the program files only; the player's ssx.toml and logs stay.
New-Item -ItemType Directory $dist -Force | Out-Null
Remove-Item "$dist\*" -Include *.exe, *.dll, README.txt -ErrorAction SilentlyContinue
Copy-Item "$build\*" $dist -Include ssx.exe, *.dll
Copy-Item "$root\README-player.txt" "$dist\README.txt"
Copy-Item "$root\installer\ssx.ico" $dist  # used by the installer's desktop shortcut
Get-ChildItem $dist | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
