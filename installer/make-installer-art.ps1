# Renders the installer artwork in Knob Hub's style (its violet gradient and knob logo, as in
# hub\KnobHub\Assets\make-icon.ps1):
#   dialog.bmp  493 x 312  welcome and finish screens (the left 164 px show beside the text)
#   banner.bmp  493 x 58   the top banner of the other screens
# Run from Windows PowerShell:  powershell -ExecutionPolicy Bypass -File installer\make-installer-art.ps1
param([string]$OutDir = $PSScriptRoot)

Add-Type -AssemblyName PresentationCore, PresentationFramework, WindowsBase

function New-Gradient {
    $g = New-Object System.Windows.Media.LinearGradientBrush
    $g.StartPoint = '0,0'; $g.EndPoint = '1,1'
    $g.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x9D, 0x90, 0xF8)), 0))
    $g.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x6E, 0x5E, 0xF0)), 1))
    return $g
}

# The logo's knob (a ring and its pointer) in white at (x, y) with the given size (designed on a 256 grid).
function Draw-Knob($dc, [double]$x, [double]$y, [double]$size) {
    $dc.PushTransform((New-Object System.Windows.Media.TranslateTransform $x, $y))
    $dc.PushTransform((New-Object System.Windows.Media.ScaleTransform ($size / 256.0), ($size / 256.0)))
    $pen = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 22
    $dc.DrawEllipse($null, $pen, (New-Object System.Windows.Point 128, 136), 64, 64)
    $pointer = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 22
    $pointer.StartLineCap = [System.Windows.Media.PenLineCap]::Round
    $pointer.EndLineCap = [System.Windows.Media.PenLineCap]::Round
    $dc.DrawLine($pointer, (New-Object System.Windows.Point 128, 60), (New-Object System.Windows.Point 128, 104))
    $dc.Pop(); $dc.Pop()
}

function Save-Bmp($visual, [int]$w, [int]$h, [string]$path) {
    $rtb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $w, $h, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $rtb.Render($visual)
    # MSI dialogs need a plain 24-bit BMP.
    $rgb = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap $rtb, ([System.Windows.Media.PixelFormats]::Bgr24), $null, 0
    $encoder = New-Object System.Windows.Media.Imaging.BmpBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($rgb))
    $stream = [IO.File]::Create($path)
    try { $encoder.Save($stream) } finally { $stream.Dispose() }
}

# Dialog: the gradient panel on the left with the logo, white where the dialog's text goes.
$v = New-Object System.Windows.Media.DrawingVisual
$dc = $v.RenderOpen()
$dc.DrawRectangle([System.Windows.Media.Brushes]::White, $null, (New-Object System.Windows.Rect 0, 0, 493, 312))
$dc.DrawRectangle((New-Gradient), $null, (New-Object System.Windows.Rect 0, 0, 164, 312))
Draw-Knob $dc 22 62 120
$label = New-Object System.Windows.Media.FormattedText 'Knob Hub', ([Globalization.CultureInfo]::InvariantCulture), 'LeftToRight', (New-Object System.Windows.Media.Typeface 'Segoe UI Semibold'), 20, ([System.Windows.Media.Brushes]::White), 1.0
$dc.DrawText($label, (New-Object System.Windows.Point ((164 - $label.Width) / 2), 200))
$dc.Close()
Save-Bmp $v 493 312 (Join-Path $OutDir 'dialog.bmp')

# Banner: white, with a small logo tile on the right (the title sits on the left).
$v = New-Object System.Windows.Media.DrawingVisual
$dc = $v.RenderOpen()
$dc.DrawRectangle([System.Windows.Media.Brushes]::White, $null, (New-Object System.Windows.Rect 0, 0, 493, 58))
$dc.DrawRoundedRectangle((New-Gradient), $null, (New-Object System.Windows.Rect 442, 9, 40, 40), 9, 9)
Draw-Knob $dc 442 7 40
$dc.Close()
Save-Bmp $v 493 58 (Join-Path $OutDir 'banner.bmp')

"Wrote dialog.bmp and banner.bmp to $OutDir"
