# Build the Release configuration and assemble dist\SSX (exe + runtime DLLs + readme).
# No game data is included: on first launch the game asks for the user's own disc/ISO.
$root = Resolve-Path "$PSScriptRoot\.."
$env:PATH = "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"

$build = "$root\out\build\ssx-release"  # SDK built from tools/rexglue-src (CMakeUserPresets.json)
Push-Location $root
# The recompiled sources are only added to the build if codegen output exists at configure
# time, so on a fresh tree: configure, run codegen, then configure again to pick them up.
if (-not (Test-Path "$root\generated\default\sources.cmake")) {
    cmake --preset ssx-release
    if ($LASTEXITCODE) { Pop-Location; throw "configure failed" }
    Write-Host "==> Recompiling the Xbox 360 code"
    cmake --build $build --target ssx_codegen
    if ($LASTEXITCODE) { Pop-Location; throw "codegen failed" }
}
cmake --preset ssx-release
Pop-Location
if ($LASTEXITCODE) { throw "configure failed" }
# FSR needs AMD's FidelityFX runtime; the fetched SDK ships it prebuilt and signed.
# The SDK tools (shared output dir) and the game both load it at startup.
$ffx = "$build\_deps\fidelityfx-src\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll"
if (Test-Path $ffx) {
    Copy-Item $ffx "$root\tools\rexglue-src\out\win-amd64\" -Force
    Copy-Item $ffx $build -Force
}
Write-Host "==> Compiling the PC version"
cmake --build $build
if ($LASTEXITCODE) { cmake --build $build }  # codegen can rewrite the PCH mid-build; retry once
if ($LASTEXITCODE) { throw "build failed" }

$dist = "$root\dist\SSX"
# Replace the program files only; the player's ssx.toml and logs stay.
New-Item -ItemType Directory $dist -Force | Out-Null
Remove-Item "$dist\*" -Include *.exe, *.dll, README.txt -ErrorAction SilentlyContinue
Copy-Item "$build\*" $dist -Include ssx.exe, *.dll
Copy-Item "$root\README-player.txt" "$dist\README.txt"
Copy-Item "$root\installer\ssx.ico" $dist  # used by the installer's desktop shortcut
Get-ChildItem $dist | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
