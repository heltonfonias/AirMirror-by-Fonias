param(
    [string] $OutputPath = 'stuff\readme-banner.png'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$logoPath = Join-Path $repoRoot 'stuff\AirMirror.png'
$desktopPath = Join-Path $repoRoot 'stuff\screenshots\airmirror-windows-desktop.png'
$phonePath = Join-Path $repoRoot 'stuff\screenshots\airmirror-iphone.png'
foreach ($path in @($logoPath, $desktopPath, $phonePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required banner image is missing: $path" }
}

$width = 1600
$height = 360
$bitmap = [Drawing.Bitmap]::new($width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit

function New-RoundedPath([float] $X, [float] $Y, [float] $W, [float] $H, [float] $Radius) {
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $W - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $W - $diameter, $Y + $H - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $H - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Draw-RoundedImage([Drawing.Graphics] $Canvas, [Drawing.Image] $Image, [float] $X, [float] $Y, [float] $W, [float] $H, [float] $Radius) {
    $shadow = New-RoundedPath ($X + 4) ($Y + 7) $W $H ($Radius + 1)
    $shadowBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(145, 0, 0, 0))
    $Canvas.FillPath($shadowBrush, $shadow)
    $shadowBrush.Dispose(); $shadow.Dispose()
    $clip = New-RoundedPath $X $Y $W $H $Radius
    $Canvas.SetClip($clip)
    $Canvas.DrawImage($Image, [Drawing.RectangleF]::new($X, $Y, $W, $H))
    $Canvas.ResetClip()
    $border = [Drawing.Pen]::new([Drawing.Color]::FromArgb(125, 238, 231, 226), 1.4)
    $Canvas.DrawPath($border, $clip)
    $border.Dispose(); $clip.Dispose()
}

try {
    $background = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 12, 10, 9))
    $graphics.FillRectangle($background, 0, 0, $width, $height)
    $background.Dispose()

    $glowPath = [Drawing.Drawing2D.GraphicsPath]::new()
    $glowPath.AddEllipse(1060, -180, 680, 680)
    $glow = [Drawing.Drawing2D.PathGradientBrush]::new($glowPath)
    $glow.CenterColor = [Drawing.Color]::FromArgb(38, 255, 45, 63)
    $glow.SurroundColors = @([Drawing.Color]::FromArgb(0, 255, 45, 63))
    $graphics.FillPath($glow, $glowPath)
    $glow.Dispose(); $glowPath.Dispose()

    $frame = New-RoundedPath 1 1 ($width - 2) ($height - 2) 28
    $framePen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 49, 43, 40), 2)
    $graphics.DrawPath($framePen, $frame)
    $framePen.Dispose(); $frame.Dispose()

    $logo = [Drawing.Image]::FromFile($logoPath)
    $graphics.DrawImage($logo, [Drawing.Rectangle]::new(54, 118, 104, 104))
    $logo.Dispose()

    $accent = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 255, 75, 88))
    $graphics.FillEllipse($accent, 188, 80, 9, 9)
    $labelFont = [Drawing.Font]::new('Segoe UI Semibold', 12, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $titleFont = [Drawing.Font]::new('Segoe UI Semibold', 43, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $subFont = [Drawing.Font]::new('Segoe UI', 18, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $buttonFont = [Drawing.Font]::new('Segoe UI Semibold', 15, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $muted = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 178, 169, 162))
    $paper = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 250, 248, 245))
    $graphics.DrawString('AIRPLAY RECEIVER FOR WINDOWS', $labelFont, $accent, 205, 72)
    $graphics.DrawString('AirMirror', $titleFont, $paper, 184, 100)
    $bylineFont = [Drawing.Font]::new('Segoe UI', 21, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $graphics.DrawString('by Fonias', $bylineFont, $muted, 188, 153)
    $graphics.DrawString('Your iPhone. On your PC.', $subFont, $muted, 189, 198)

    $buttonPath = New-RoundedPath 188 248 304 48 24
    $button = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 255, 45, 63))
    $graphics.FillPath($button, $buttonPath)
    $arrow = [string][char]0x2197
    $graphics.DrawString(('airplay.heltonfonias.com  ' + $arrow), $buttonFont, $paper, 207, 261)

    $desktop = [Drawing.Image]::FromFile($desktopPath)
    $phone = [Drawing.Image]::FromFile($phonePath)
    $desktopW = 552.0
    $desktopH = $desktopW * $desktop.Height / $desktop.Width
    $phoneH = 286.0
    $phoneW = $phoneH * $phone.Width / $phone.Height
    Draw-RoundedImage $graphics $desktop 798 18 $desktopW $desktopH 15
    Draw-RoundedImage $graphics $phone 1302 38 $phoneW $phoneH 24
    $desktop.Dispose(); $phone.Dispose()

    if ([IO.Path]::IsPathRooted($OutputPath)) {
        $OutputPath = [IO.Path]::GetFullPath($OutputPath)
    } else {
        $OutputPath = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
    }
    $outputDirectory = Split-Path -Parent $OutputPath
    if (-not (Test-Path -LiteralPath $outputDirectory)) { New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null }
    $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
    Write-Host "Created README banner: $OutputPath"
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
    if ($accent) { $accent.Dispose() }
    if ($labelFont) { $labelFont.Dispose() }
    if ($titleFont) { $titleFont.Dispose() }
    if ($subFont) { $subFont.Dispose() }
    if ($bylineFont) { $bylineFont.Dispose() }
    if ($buttonFont) { $buttonFont.Dispose() }
    if ($muted) { $muted.Dispose() }
    if ($paper) { $paper.Dispose() }
    if ($button) { $button.Dispose() }
    if ($buttonPath) { $buttonPath.Dispose() }
}
