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

# The release zip for RecompLauncher (recomplauncher.json, docs at recomplauncher.com): ssx-recomp.exe and
# recomplauncher.json at its top. prepare.version is this version, so after an update the launcher runs
# the build again (each release carries new source). Upload it next to ssx-recomp.exe.
$version = (Get-Item "$here\ssx-recomp.exe").VersionInfo.ProductVersion
$json = [IO.File]::ReadAllText("$root\recomplauncher.json")
$versionKey = '("version"\s*:\s*")[^"]*(")'  # prepare.version: the file's only "version"
if ([regex]::Matches($json, $versionKey).Count -ne 1) { throw "recomplauncher.json needs exactly one `"version`" (prepare.version)" }
$json = [regex]::Replace($json, $versionKey, "`${1}$version`${2}")
$stage = Join-Path $env:TEMP "ssx-recomp-release"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$here\ssx-recomp.exe" $stage
[IO.File]::WriteAllText("$stage\recomplauncher.json", $json, (New-Object Text.UTF8Encoding $false))
$release = "$here\ssx-recomp-v$version-Windows-x64.zip"
Remove-Item $release -ErrorAction SilentlyContinue
Compress-Archive "$stage\*" $release
Remove-Item $stage -Recurse -Force
# The same program under the installer's earlier name, for launchers that install SSX with the
# release's SSX-Installer.exe (Quiver). Its command line is unchanged. Upload it too.
Copy-Item "$here\ssx-recomp.exe" "$here\SSX-Installer.exe" -Force
Get-Item "$here\ssx-recomp.exe", "$here\SSX-Installer.exe", $release |
    Select-Object Name, Length, @{n = 'Source'; e = { $sha } }
