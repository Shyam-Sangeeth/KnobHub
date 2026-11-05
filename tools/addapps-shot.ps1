# Captures Knob Hub's Add apps window at the README's size (1478 x 520) with PrintWindow (no focus change) and closes it
# again. Knob Hub must be running with its window open; it ends on the Knob page.
#
#   powershell -ExecutionPolicy Bypass -File tools\addapps-shot.ps1 -Out docs\screenshots\hub-add-apps.png
param([Parameter(Mandatory)][string]$Out)
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class Shot {
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int cy, uint f);
}
"@
$A = [System.Windows.Automation.AutomationElement]
$h = (Get-Process KnobHub | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$main = $A::FromHandle($h)
$items = $main.FindAll('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::ListItem)))
$items[1].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Start-Sleep -Milliseconds 800
$btn = $main.FindFirst('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::NameProperty), "Add apps"))
$btn.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
$byName = New-Object System.Windows.Automation.PropertyCondition ($A::NameProperty), "Add apps"
$dlg = $null
for ($i = 0; $i -lt 20; $i++) {
  Start-Sleep -Milliseconds 500
  $dlg = $A::RootElement.FindFirst('Children', $byName)
  if (-not $dlg) { $dlg = $main.FindFirst('Children', $byName) }
  if ($dlg) { break }
}
if (-not $dlg) { "no Add apps window"; exit 1 }
$d = [IntPtr]$dlg.Current.NativeWindowHandle
$w = New-Object Shot+RECT; [void][Shot]::GetWindowRect($d, [ref]$w)
$v = New-Object Shot+RECT; [void][Shot]::DwmGetWindowAttribute($d, 9, [ref]$v, 16)
[void][Shot]::SetWindowPos($d, [IntPtr]::Zero, $w.L, $w.T, 1478 + ($w.R - $w.L) - ($v.R - $v.L), 520 + ($w.B - $w.T) - ($v.B - $v.T), 0x0014)
Start-Sleep -Seconds 6   # the list comes from GitHub
$w = New-Object Shot+RECT; [void][Shot]::GetWindowRect($d, [ref]$w)
$v = New-Object Shot+RECT; [void][Shot]::DwmGetWindowAttribute($d, 9, [ref]$v, 16)
$full = New-Object System.Drawing.Bitmap ($w.R - $w.L), ($w.B - $w.T)
$g = [System.Drawing.Graphics]::FromImage($full); $dc = $g.GetHdc(); [void][Shot]::PrintWindow($d, $dc, 2); $g.ReleaseHdc($dc)
$crop = $full.Clone((New-Object System.Drawing.Rectangle ($v.L - $w.L), ($v.T - $w.T), ($v.R - $v.L), ($v.B - $v.T)), $full.PixelFormat)
$crop.Save($Out); "saved $Out"
$dlg.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close()
$items[0].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
