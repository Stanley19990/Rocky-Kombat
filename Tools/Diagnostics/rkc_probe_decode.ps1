# rkc_probe_decode.ps1
#
# Decodes a captured screenshot with .NET's own PNG decoder and writes the raw
# pixels out as an uncompressed BMP, so a second, independent decoder can be
# compared against the first. Used to tell a decode surprise apart from a real
# problem in the rendered frame.
#
# Usage: powershell -NoProfile -File Tools\Diagnostics\rkc_probe_decode.ps1 <in.png> <out.bmp>

param(
    [Parameter(Mandatory = $true)][string]$InPath,
    [Parameter(Mandatory = $true)][string]$OutPath
)

Add-Type -AssemblyName System.Drawing

$source = [System.Drawing.Image]::FromFile($InPath)
Write-Output ("source: {0} x {1}, format {2}, dpi {3}x{4}" -f $source.Width, $source.Height, $source.PixelFormat, $source.HorizontalResolution, $source.VerticalResolution)

$bitmap = New-Object System.Drawing.Bitmap $source.Width, $source.Height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.DrawImage($source, 0, 0, $source.Width, $source.Height)
$graphics.Dispose()

# A few sample pixels from the same places the Python probe reads, so the two
# decodes can be compared pixel for pixel.
foreach ($p in @(@(300, 330), @(700, 300), @(975, 330), @(640, 200), @(100, 600))) {
    $c = $bitmap.GetPixel($p[0], $p[1])
    Write-Output ("pixel ({0},{1}) = {2},{3},{4}" -f $p[0], $p[1], $c.R, $c.G, $c.B)
}

$bitmap.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$bitmap.Dispose()
$source.Dispose()
Write-Output ("wrote {0}" -f $OutPath)
