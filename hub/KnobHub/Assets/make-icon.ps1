# Renders the Knob Hub logo to a multi-size KnobHub.ico.
# Run from Windows PowerShell:  powershell -ExecutionPolicy Bypass -File Assets\make-icon.ps1
param([string]$OutDir = $PSScriptRoot)

Add-Type -AssemblyName PresentationCore, PresentationFramework, WindowsBase

function New-Logo([int]$size) {
    # Designed on a 256 x 256 grid.
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()
    $dc.PushTransform((New-Object System.Windows.Media.ScaleTransform ($size / 256.0), ($size / 256.0)))

    # Rounded square with a violet gradient.
    $gradient = New-Object System.Windows.Media.LinearGradientBrush
    $gradient.StartPoint = '0,0'; $gradient.EndPoint = '1,1'
    $gradient.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x9D, 0x90, 0xF8)), 0))
    $gradient.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x6E, 0x5E, 0xF0)), 1))
    $dc.DrawRoundedRectangle($gradient, $null, (New-Object System.Windows.Rect 8, 8, 240, 240), 56, 56)

    # Soft top highlight for depth.
    $shine = New-Object System.Windows.Media.LinearGradientBrush
    $shine.StartPoint = '0,0'; $shine.EndPoint = '0,1'
    $shine.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromArgb(0x38, 0xFF, 0xFF, 0xFF)), 0))
    $shine.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromArgb(0x00, 0xFF, 0xFF, 0xFF)), 0.55))
    $dc.DrawRoundedRectangle($shine, $null, (New-Object System.Windows.Rect 8, 8, 240, 240), 56, 56)

    # Knob: white ring and pointer.
    $ringPen = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 22
    $dc.DrawEllipse($null, $ringPen, (New-Object System.Windows.Point 128, 136), 64, 64)

    $pointerPen = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 22
    $pointerPen.StartLineCap = [System.Windows.Media.PenLineCap]::Round
    $pointerPen.EndLineCap = [System.Windows.Media.PenLineCap]::Round
    $dc.DrawLine($pointerPen, (New-Object System.Windows.Point 128, 60), (New-Object System.Windows.Point 128, 104))

    $dc.Pop()
    $dc.Close()

    $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $size, $size, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = New-Object System.IO.MemoryStream
    $encoder.Save($stream)
    return ,$stream.ToArray()
}

# At 16 px the scaled design blurs, so the tray size gets a hand-placed version on whole pixels.
function New-Logo16 {
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()

    $gradient = New-Object System.Windows.Media.LinearGradientBrush
    $gradient.StartPoint = '0,0'; $gradient.EndPoint = '1,1'
    $gradient.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x9D, 0x90, 0xF8)), 0))
    $gradient.GradientStops.Add((New-Object System.Windows.Media.GradientStop ([System.Windows.Media.Color]::FromRgb(0x6E, 0x5E, 0xF0)), 1))
    $dc.DrawRoundedRectangle($gradient, $null, (New-Object System.Windows.Rect 0, 0, 16, 16), 3.5, 3.5)

    $ringPen = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 2
    $dc.DrawEllipse($null, $ringPen, (New-Object System.Windows.Point 8, 8.5), 4, 4)

    $pointerPen = New-Object System.Windows.Media.Pen ([System.Windows.Media.Brushes]::White), 2
    $pointerPen.StartLineCap = [System.Windows.Media.PenLineCap]::Flat
    $pointerPen.EndLineCap = [System.Windows.Media.PenLineCap]::Flat
    $dc.DrawLine($pointerPen, (New-Object System.Windows.Point 8, 3), (New-Object System.Windows.Point 8, 6))

    $dc.Close()

    $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap 16, 16, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = New-Object System.IO.MemoryStream
    $encoder.Save($stream)
    return ,$stream.ToArray()
}

# Icon sizes Windows uses for the tray, title bar, taskbar, Explorer and Start (incl. high-DPI variants).
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = @{}
foreach ($s in $sizes) { $images[$s] = if ($s -eq 16) { New-Logo16 } else { New-Logo $s } }

# ICO container with PNG-compressed images (supported by Windows Vista and later).
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$s].Length); $w.Write([uint32]$offset)
    $offset += $images[$s].Length
}
foreach ($s in $sizes) { $w.Write($images[$s]) }
$w.Flush()
[IO.File]::WriteAllBytes((Join-Path $OutDir 'KnobHub.ico'), $ico.ToArray())

"Wrote KnobHub.ico ($($sizes -join ', ') px) to $OutDir"
