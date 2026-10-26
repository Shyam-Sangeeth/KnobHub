# Draws the AudioDesk app's own icons, a game controller and an aux cable (game.png, aux-cable.png), as
# white-on-transparent PNGs in this folder, next to sonar.png. The plugin embeds them and hands them to Knob Hub
# (AudioDeskApp.Icons), which draws them at the size the knob shows them.
# Run from Windows PowerShell:  powershell -ExecutionPolicy Bypass -File hub\Plugins\AudioDesk\Icons\make_icons.ps1
param([int]$Size = 128)

Add-Type -AssemblyName PresentationCore, PresentationFramework, WindowsBase

# Shapes are designed on a 64 x 64 grid.
function Rect($x, $y, $w, $h, $r = 0) { New-Object System.Windows.Media.RectangleGeometry (New-Object System.Windows.Rect $x, $y, $w, $h), $r, $r }
function Ellipse($cx, $cy, $rx, $ry) { New-Object System.Windows.Media.EllipseGeometry (New-Object System.Windows.Point $cx, $cy), $rx, $ry }
function Union($a, $b) { New-Object System.Windows.Media.CombinedGeometry ([System.Windows.Media.GeometryCombineMode]::Union), $a, $b }
function Cut($a, $b) { New-Object System.Windows.Media.CombinedGeometry ([System.Windows.Media.GeometryCombineMode]::Exclude), $a, $b }

# A game controller: a body with two grips, a d-pad and two buttons cut out.
function GameGeometry {
    $body = Union (Union (Rect 5 17 54 24 12) (Ellipse 16 40 11 13)) (Ellipse 48 40 11 13)
    $dpad = Union (Rect 10.5 26.5 13 5 1.5) (Rect 14.5 22.5 5 13 1.5)
    $buttons = Union (Ellipse 44 32 3.6 3.6) (Ellipse 51 26 3.6 3.6)
    Cut (Cut $body $dpad) $buttons
}

# An aux cable: a 3.5 mm plug (tip, two rings, sleeve), its handle and the cable, drawn upright then tilted.
function AuxGeometry {
    $plug = Union (Rect 28 3 8 28 4) (Rect 28 12 8 19)     # rounded tip on top, square bottom
    $rings = Union (Rect 26 12 12 2.2) (Rect 26 19 12 2.2)
    $handle = Rect 22.5 31 19 18 3
    $cable = Rect 28.5 48 7 16 2
    $shape = Union (Union (Cut $plug $rings) $handle) $cable
    $shape.Transform = New-Object System.Windows.Media.RotateTransform 40, 32, 32
    $shape
}

function Save($name, $geometry) {
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()
    $dc.PushTransform((New-Object System.Windows.Media.ScaleTransform ($Size / 64.0), ($Size / 64.0)))
    $dc.DrawGeometry([System.Windows.Media.Brushes]::White, $null, $geometry)
    $dc.Pop()
    $dc.Close()
    $bitmap = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $Size, $Size, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [IO.File]::Create((Join-Path $PSScriptRoot "$name.png"))
    $encoder.Save($stream)
    $stream.Close()
}

Save "game" (GameGeometry)
Save "aux-cable" (AuxGeometry)  # not "aux": a reserved name on Windows
"Wrote game.png and aux-cable.png ($Size x $Size) to $PSScriptRoot"
