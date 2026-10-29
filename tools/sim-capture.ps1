# Drives the Knob OS simulator with posted mouse messages (no focus change, no mouse; it can stay minimized) and
# optionally captures its window (PrintWindow; a minimized simulator is shown without activating it for the capture,
# then minimized again). The simulator must be running (`pio run -e sim -t exec`, or
# program.exe --board <id>).
#
#   powershell -ExecutionPolicy Bypass -File tools\sim-capture.ps1 [-Turn 2] [-Press] [-LongPress] [-TapX 120 -TapY 100] [-Out sim.png]
#
# -Turn: detents, positive = right (the wheel down), negative = left. -Press: the knob's button; -LongPress holds it
# (back). -TapX/-TapY: a tap at screen pixels. Posted left clicks may not register while the window isn't in front, and
# a posted long press turns into a short one when the window is in the foreground (SDL checks the real button).
param([int]$Turn = 0, [switch]$Press, [switch]$LongPress, [int]$TapX = -1, [int]$TapY = -1, [string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class SimCapture {
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  // The simulator's screen window, by its title: Windows may report its console (log) window as the process's main one.
  public static IntPtr Find() {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      var s = new System.Text.StringBuilder(256);
      GetWindowText(h, s, 256);
      if (s.ToString().StartsWith("Knob OS simulator")) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@
$h = [SimCapture]::Find()
if ($h -eq [IntPtr]::Zero) { "The simulator isn't running"; exit 1 }
$at = [IntPtr]((100 -shl 16) + 100)  # a point in the window (client pixels)

for ($i = 0; $i -lt [Math]::Abs($Turn); $i++) {
  $delta = if ($Turn -gt 0) { -120 } else { 120 }  # wheel down = turn right
  [void][SimCapture]::PostMessage($h, 0x020A, [IntPtr](([int16]$delta -band 0xFFFF) -shl 16), [IntPtr]0)
  Start-Sleep -Milliseconds 350
}
if ($Press -or $LongPress) {
  [void][SimCapture]::PostMessage($h, 0x0207, [IntPtr]0x10, $at)  # middle button down
  Start-Sleep -Milliseconds $(if ($LongPress) { 700 } else { 80 })
  [void][SimCapture]::PostMessage($h, 0x0208, [IntPtr]0, $at)
  Start-Sleep -Milliseconds 900
}
if ($TapX -ge 0) {
  $zoom = 2  # the window shows the screen at zoom 2
  $cl = [IntPtr]((($TapY * $zoom) -shl 16) -bor ($TapX * $zoom))
  [void][SimCapture]::PostMessage($h, 0x0200, [IntPtr]0, $cl); Start-Sleep -Milliseconds 60
  [void][SimCapture]::PostMessage($h, 0x0201, [IntPtr]1, $cl); Start-Sleep -Milliseconds 80
  [void][SimCapture]::PostMessage($h, 0x0202, [IntPtr]0, $cl); Start-Sleep -Milliseconds 600
}
if ($Out) {
  $minimized = [SimCapture]::IsIconic($h)
  if ($minimized) { [void][SimCapture]::ShowWindow($h, 4) }  # SW_SHOWNOACTIVATE
  Start-Sleep -Milliseconds 300
  $r = New-Object SimCapture+RECT; [void][SimCapture]::GetWindowRect($h, [ref]$r)
  $bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R - $r.L)), ([Math]::Max(1, $r.B - $r.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc(); [void][SimCapture]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc)
  if ($minimized) { [void][SimCapture]::ShowWindow($h, 7) }  # SW_SHOWMINNOACTIVE
  $bmp.Save($Out); "saved $Out"
}
