# Build the Release configuration and assemble dist\SSX (exe + runtime DLLs + readme).
# No game data is included: on first launch the game asks for the user's own disc/ISO.
$root = Resolve-Path "$PSScriptRoot\.."
$env:PATH = "C:\Program Files\CMake\bin;C:\Program Files\LLVM\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Links;$env:PATH"

$build = "$root\out\build\ssx-release"  # SDK built from tools/rexglue-src (CMakeUserPresets.json)
if (-not (Test-Path "$build\CMakeCache.txt")) {
    Push-Location $root; cmake --preset ssx-release; Pop-Location
    if ($LASTEXITCODE) { throw "configure failed" }
}
# FSR needs AMD's FidelityFX runtime; the fetched SDK ships it prebuilt and signed.
# The SDK tools (shared output dir) and the game both load it at startup.
$ffx = "$build\_deps\fidelityfx-src\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll"
if (Test-Path $ffx) {
    Copy-Item $ffx "$root\tools\rexglue-src\out\win-amd64\" -Force
    Copy-Item $ffx $build -Force
}
cmake --build $build
if ($LASTEXITCODE) { cmake --build $build }  # codegen can rewrite the PCH mid-build; retry once
if ($LASTEXITCODE) { throw "build failed" }

$dist = "$root\dist\SSX"
# Replace the program files only; the player's ssx.toml and logs stay.
New-Item -ItemType Directory $dist -Force | Out-Null
Remove-Item "$dist\*" -Include *.exe, *.dll, README.txt -ErrorAction SilentlyContinue
Copy-Item "$build\*" $dist -Include ssx.exe, *.dll
Copy-Item "$root\README-player.txt" "$dist\README.txt"
Get-ChildItem $dist | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } }
