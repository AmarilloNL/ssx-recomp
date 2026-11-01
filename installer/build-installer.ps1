# Compiles SSX-Installer.exe with the C# compiler that ships with Windows (.NET Framework 4.x).
$here = $PSScriptRoot
$csc = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
& $csc /nologo /target:winexe /optimize+ "/out:$here\SSX-Installer.exe" `
    /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll `
    /r:System.IO.Compression.FileSystem.dll "$here\SSXInstaller.cs"
if ($LASTEXITCODE) { throw "compile failed" }
Get-Item "$here\SSX-Installer.exe" | Select-Object Name, Length
