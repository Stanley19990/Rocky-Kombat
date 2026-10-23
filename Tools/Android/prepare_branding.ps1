[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$resources = Join-Path $root 'Build/Android/res'
Add-Type -AssemblyName System.Drawing
$icon = [Drawing.Image]::FromFile((Join-Path $root 'ArtSource/Presentation/Android/RockycatIcon.png'))
$logo = [Drawing.Image]::FromFile((Join-Path $root 'ArtSource/Presentation/RockycatLogo.jpeg'))
try {
    # Mechanical Android density exports of the approved artwork; no art changes.
    $densities = [ordered]@{'drawable'=192; 'drawable-ldpi'=36; 'drawable-mdpi'=48; 'drawable-hdpi'=72; 'drawable-xhdpi'=96; 'drawable-xxhdpi'=144; 'drawable-xxxhdpi'=192}
    foreach ($density in $densities.GetEnumerator()) {
        $directory = Join-Path $resources $density.Key
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $bitmap = New-Object Drawing.Bitmap($density.Value, $density.Value)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($icon, 0, 0, $bitmap.Width, $bitmap.Height)
            $bitmap.Save((Join-Path $directory 'icon.png'), [Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
    $logoDirectory = Join-Path $resources 'drawable-nodpi'
    New-Item -ItemType Directory -Path $logoDirectory -Force | Out-Null
    $logo.Save((Join-Path $logoDirectory 'rockycat_launch_logo.png'), [Drawing.Imaging.ImageFormat]::Png)
} finally { $icon.Dispose(); $logo.Dispose() }
Write-Output 'Rockycat Android launcher icons and launch logo exported.'
