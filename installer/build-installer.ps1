# Compiles ssx-recomp.exe, the setup and (once the game is built) its launcher, with the C# compiler
# that ships with Windows (.NET Framework 4.x).
# It carries this port's source (the committed HEAD, without the README artwork), so building the
# game never downloads the port: a release's ssx-recomp.exe builds exactly that release.
$here = $PSScriptRoot
$root = Split-Path $here
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

$sha = git -C $root rev-parse --short=12 HEAD
if ($LASTEXITCODE) { throw "git rev-parse failed: build this from a git checkout" }
if (git -C $root status --porcelain --untracked-files=no) {
    Write-Warning "Uncommitted changes aren't in the embedded source: it is the commit $sha."
}
$zip = Join-Path $env:TEMP "ssx-recomp-source.zip"
$id = Join-Path $env:TEMP "ssx-recomp-source-id.txt"
git -C $root archive --format=zip --prefix=ssx-recomp/ -o $zip HEAD -- . ':(exclude)art' ':(exclude)PROGRESS.md'
if ($LASTEXITCODE) { throw "git archive failed" }
Set-Content $id $sha -Encoding ascii -NoNewline

& $csc /nologo /target:winexe /optimize+ "/out:$here\ssx-recomp.exe" "/win32icon:$here\ssx.ico" `
    /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll `
    /r:System.IO.Compression.FileSystem.dll "/resource:$zip,source.zip" "/resource:$id,source-id.txt" `
    "$here\SSXInstaller.cs"
$failed = $LASTEXITCODE
Remove-Item $zip, $id -ErrorAction SilentlyContinue
if ($failed) { throw "compile failed" }
Get-Item "$here\ssx-recomp.exe" | Select-Object Name, Length, @{n = 'Source'; e = { $sha } }
