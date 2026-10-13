param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Bitmap]::new((Join-Path $ProjectRoot 'stuff\AirMirror.png'))
$dark = [System.Drawing.Color]::FromArgb(16, 21, 18)
$paper = [System.Drawing.Color]::FromArgb(244, 240, 232)
$coral = [System.Drawing.Color]::FromArgb(242, 141, 115)

function New-Canvas([int]$Width, [int]$Height) {
  [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
}

function Draw-Icon($Graphics, [int]$X, [int]$Y, [int]$Size) {
  $Graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $Graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $Graphics.DrawImage($source, [System.Drawing.Rectangle]::new($X, $Y, $Size, $Size))
}

try {
  $dialog = New-Canvas 493 312
  try {
    $g = [System.Drawing.Graphics]::FromImage($dialog)
    try {
      $g.Clear($paper)
      $darkBrush = [System.Drawing.SolidBrush]::new($dark)
      $paperBrush = [System.Drawing.SolidBrush]::new($paper)
      $coralBrush = [System.Drawing.SolidBrush]::new($coral)
      $titleFont = [System.Drawing.Font]::new('Segoe UI', 17, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
      $bylineFont = [System.Drawing.Font]::new('Segoe UI', 10, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
      try {
        $g.FillRectangle($darkBrush, 0, 0, 164, 312)
        Draw-Icon $g 41 79 82
        $g.DrawString('AirMirror', $titleFont, $paperBrush, 24, 188)
        $g.DrawString('BY FONIAS', $bylineFont, $coralBrush, 26, 215)
        $g.FillRectangle($coralBrush, 25, 263, 34, 3)
      } finally { $titleFont.Dispose(); $bylineFont.Dispose(); $darkBrush.Dispose(); $paperBrush.Dispose(); $coralBrush.Dispose() }
    } finally { $g.Dispose() }
    $dialog.Save((Join-Path $ProjectRoot 'stuff\AirMirror-dialog.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
  } finally { $dialog.Dispose() }

  $banner = New-Canvas 493 58
  try {
    $g = [System.Drawing.Graphics]::FromImage($banner)
    try {
      $g.Clear($paper)
      $line = [System.Drawing.SolidBrush]::new($coral)
      try {
        $g.FillRectangle($line, 0, 55, 493, 3)
        Draw-Icon $g 440 7 43
      } finally { $line.Dispose() }
    } finally { $g.Dispose() }
    $banner.Save((Join-Path $ProjectRoot 'stuff\AirMirror-banner.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
  } finally { $banner.Dispose() }
} finally { $source.Dispose() }

Write-Output 'Created branded WiX banner and dialog bitmaps.'
