# Draws the installer icon (original artwork: snowy peaks on a dark rounded tile in the
# project's navy / icy-cyan colours) and writes installer\ssx.ico with sizes 16-256.
Add-Type -AssemblyName System.Drawing
$sizes = 16, 24, 32, 48, 64, 128, 256

function Draw([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = $n / 256.0
    function P($x, $y) { New-Object System.Drawing.PointF ([float]($x * $s)), ([float]($y * $s)) }

    # Rounded tile with a vertical navy gradient.
    $r = 56 * $s
    $tile = New-Object System.Drawing.Drawing2D.GraphicsPath
    $tile.AddArc(0, 0, 2 * $r, 2 * $r, 180, 90)
    $tile.AddArc($n - 2 * $r - 1, 0, 2 * $r, 2 * $r, 270, 90)
    $tile.AddArc($n - 2 * $r - 1, $n - 2 * $r - 1, 2 * $r, 2 * $r, 0, 90)
    $tile.AddArc(0, $n - 2 * $r - 1, 2 * $r, 2 * $r, 90, 90)
    $tile.CloseFigure()
    $bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush (P 0 0), (P 0 256),
        ([System.Drawing.Color]::FromArgb(255, 22, 46, 66)), ([System.Drawing.Color]::FromArgb(255, 6, 11, 18))
    $g.FillPath($bg, $tile)
    $g.SetClip($tile)

    # Back peak (dim) and front peak (icy cyan gradient).
    $back = [System.Drawing.PointF[]]@((P 112 210), (P 186 92), (P 262 210))
    $g.FillPolygon((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 30, 86, 112))), $back)
    $front = [System.Drawing.PointF[]]@((P -6 214), (P 104 54), (P 214 214))
    $ice = New-Object System.Drawing.Drawing2D.LinearGradientBrush (P 0 54), (P 0 214),
        ([System.Drawing.Color]::FromArgb(255, 92, 209, 245)), ([System.Drawing.Color]::FromArgb(255, 24, 92, 128))
    $g.FillPolygon($ice, $front)

    # Snow caps.
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 240, 250, 255))
    $g.FillPolygon($white, [System.Drawing.PointF[]]@((P 104 54), (P 138 104), (P 120 96), (P 104 112), (P 88 96), (P 70 104)))
    $g.FillPolygon($white, [System.Drawing.PointF[]]@((P 186 92), (P 208 126), (P 194 120), (P 180 132), (P 168 120)))

    # Snowboard-trail swoosh across the slope.
    if ($n -ge 32) {
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(230, 240, 250, 255)), ([float](10 * $s))
        $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
        $g.DrawBezier($pen, (P 40 228), (P 110 170), (P 150 236), (P 226 168))
    }
    $g.Dispose()
    return $bmp
}

$images = foreach ($n in $sizes) {
    $bmp = Draw $n
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    , $ms.ToArray()
}

# ICO container with PNG-compressed entries (supported since Windows Vista).
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $n = $sizes[$i]
    $w.Write([byte]($(if ($n -ge 256) { 0 } else { $n }))); $w.Write([byte]($(if ($n -ge 256) { 0 } else { $n })))
    $w.Write([byte]0); $w.Write([byte]0); $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$i].Length); $w.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $w.Write($img) }
[System.IO.File]::WriteAllBytes("$PSScriptRoot\ssx.ico", $out.ToArray())
Get-Item "$PSScriptRoot\ssx.ico" | Select-Object Name, Length
