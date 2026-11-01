# Launch the dev build for N seconds (muted), capture the game window, then kill it.
# Prints exit status and the log's errors.
# Usage: scripts\test-run.ps1 [-Seconds 40] [-Shot run.png] [-ShotsAt 20,30] [-Extra '--flag=x']
# -ShotsAt saves extra captures as run-<seconds>.png next to -Shot.
param([int]$Seconds = 40, [string]$Shot = "$PSScriptRoot\..\run.png", [int[]]$ShotsAt = @(),
      [string[]]$Extra = @())

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

function Capture($process, $path) {
    $process.Refresh()
    $r = New-Object Win+RECT
    [void][Win]::GetClientRect($process.MainWindowHandle, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R)), ([Math]::Max(1, $r.B))
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win]::PrintWindow($process.MainWindowHandle, $hdc, 3)  # client area + PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $bmp.Save($path)
}

$root = Resolve-Path "$PSScriptRoot\.."
$bin = "$root\out\build\ssx"
# Tests never touch the player's saves: they run on a private copy (seeded once).
$userData = "$root\test-userdata"
if (-not (Test-Path $userData)) {
    robocopy "$env:USERPROFILE\Documents\ssx" $userData /E /XD cache iso-mount /NFL /NDL /NJH /NJS | Out-Null
}
Remove-Item "$root\run.log" -ErrorAction SilentlyContinue
$p = Start-Process "$bin\ssx.exe" -WorkingDirectory $bin -PassThru -ArgumentList (@(
    "--game_data_root=$root\game-dump\files", "--user_data_root=$userData", '--audio_mute',
    "--log_file=$root\run.log") + $Extra)
$elapsed = 0
foreach ($t in ($ShotsAt | Sort-Object)) {
    if ($p.WaitForExit(($t - $elapsed) * 1000)) { break }
    $elapsed = $t
    Capture $p ([IO.Path]::ChangeExtension($Shot, $null).TrimEnd('.') + "-$t.png")
}
if ($p.HasExited -or $p.WaitForExit(($Seconds - $elapsed) * 1000)) {
    "exited code 0x{0:X8}" -f $p.ExitCode
} else {
    Capture $p $Shot
    Stop-Process -Id $p.Id -Force
    "still running after ${Seconds}s; window capture: $Shot"
}
Get-Content "$root\run.log" | Select-String '\[(error|critical)\]' |
    Where-Object { $_ -notmatch 'cache:|ResolvePath' } | Select-Object -First 20
