# Screenshots of Knob Hub's pages (hub-knob.png, hub-apps.png, hub-board.png, hub-settings.png) at the README's size
# (a 1478 x 834 window), captured with PrintWindow: no focus change, no mouse. With -Board, the simulator runs as that
# board and Knob Hub connects to it first; without, no knob is connected (the README's shots).
#
#   powershell -ExecutionPolicy Bypass -File tools\hub-shots.ps1 -OutDir docs\screenshots [-Board crowpanel-1.46] [-Pages knob,board]
#
# Knob Hub's settings and the simulator's kept data are backed up first and restored afterwards. A Knob Hub that was
# running is closed for the shots and started again as it was (window open, or in the tray). Build Knob Hub (Debug)
# and the simulator first.
param([Parameter(Mandatory)][string]$OutDir, [string]$Board, [string]$Pages = "knob,apps,board,settings")
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class HubShots {
  public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int cy, uint f);
}
"@
$PageNames = "knob", "apps", "board", "settings"
$exe = Join-Path $PSScriptRoot "..\hub\KnobHub\bin\Debug\net10.0-windows10.0.19041.0\KnobHub.exe"
$sim = "$env:USERPROFILE\.cache\knob-os\build\sim\program.exe"
$hubSettings = "$env:LOCALAPPDATA\KnobHub\settings.json"
$simData = "$env:LOCALAPPDATA\KnobHub\Simulator"
if (-not (Test-Path $exe)) { "Knob Hub isn't built: $exe"; exit 1 }
if ($Board -and -not (Test-Path $sim)) { "The simulator isn't built: $sim"; exit 1 }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$wasRunning = @(Get-Process KnobHub -ErrorAction SilentlyContinue).Count -gt 0
$wasShown = @(Get-Process KnobHub -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 }).Count -gt 0
if ($wasRunning) { Stop-Process -Name KnobHub -Force -Confirm:$false; Start-Sleep -Seconds 1 }
$backup = Join-Path ([System.IO.Path]::GetTempPath()) "knobhub-shots-backup"
if (Test-Path $backup) { Remove-Item $backup -Recurse -Force }
New-Item -ItemType Directory $backup | Out-Null
if (Test-Path $hubSettings) { Copy-Item $hubSettings "$backup\settings.json" }
if (Test-Path $simData) { Copy-Item $simData "$backup\Simulator" -Recurse }
# The screen stays on for the shots: no timeout, no dimming (the settings are restored afterwards).
if (Test-Path $hubSettings) {
  $json = Get-Content $hubSettings -Raw | ConvertFrom-Json
  if ($json.knob) { $json.knob.timeout = 4; $json.knob.dimIdle = $false }
  $json | ConvertTo-Json -Depth 20 | Set-Content $hubSettings -Encoding UTF8
}

try {
  $simProcess = $null
  if ($Board) {
    $simProcess = Start-Process $sim -ArgumentList "--board", $Board -PassThru -WindowStyle Minimized
    Start-Sleep -Seconds 2
  }
  Start-Process $exe -ArgumentList "--minimized"; Start-Sleep -Seconds 4
  [System.Threading.EventWaitHandle]::OpenExisting("KnobHub.ShowWindow").Set() | Out-Null; Start-Sleep -Seconds 2
  $h = (Get-Process KnobHub | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
  [void][HubShots]::ShowWindow($h, 4)  # SW_SHOWNOACTIVATE: restored, not activated
  Start-Sleep -Milliseconds 500
  # The visible frame 1478 x 834: the window rect also has invisible resize borders.
  $w = New-Object HubShots+RECT; [void][HubShots]::GetWindowRect($h, [ref]$w)
  $v = New-Object HubShots+RECT; [void][HubShots]::DwmGetWindowAttribute($h, 9, [ref]$v, 16)
  [void][HubShots]::SetWindowPos($h, [IntPtr]::Zero, $w.L, $w.T, 1478 + ($w.R - $w.L) - ($v.R - $v.L), 834 + ($w.B - $w.T) - ($v.B - $v.T), 0x0014)
  if ($Board) { Start-Sleep -Seconds 4 } else { Start-Sleep -Milliseconds 1500 }  # Knob Hub retries the simulator every 2 s

  $A = [System.Windows.Automation.AutomationElement]
  $nav = $A::FromHandle($h).FindAll('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::ListItem)))
  foreach ($name in $Pages.Split(",")) {
    $page = [array]::IndexOf($PageNames, $name.Trim())
    if ($page -lt 0) { "no page $name (knob, apps, board, settings)"; continue }
    $nav[$page].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Start-Sleep -Milliseconds 800
    $w = New-Object HubShots+RECT; [void][HubShots]::GetWindowRect($h, [ref]$w)
    $v = New-Object HubShots+RECT; [void][HubShots]::DwmGetWindowAttribute($h, 9, [ref]$v, 16)
    $full = New-Object System.Drawing.Bitmap ($w.R - $w.L), ($w.B - $w.T)
    $g = [System.Drawing.Graphics]::FromImage($full); $dc = $g.GetHdc(); [void][HubShots]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc)
    $crop = $full.Clone((New-Object System.Drawing.Rectangle ($v.L - $w.L), ($v.T - $w.T), ($v.R - $v.L), ($v.B - $v.T)), $full.PixelFormat)
    $file = Join-Path $OutDir "hub-$($PageNames[$page]).png"
    $crop.Save($file); "saved $file"
  }
}
finally {
  Stop-Process -Name KnobHub -Force -Confirm:$false -ErrorAction SilentlyContinue; Start-Sleep -Seconds 1
  if ($simProcess) { Stop-Process -Id $simProcess.Id -Force -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500 }
  if (Test-Path "$backup\settings.json") { Copy-Item "$backup\settings.json" $hubSettings -Force }
  if (Test-Path "$backup\Simulator") {
    if (Test-Path $simData) { Remove-Item $simData -Recurse -Force }
    Copy-Item "$backup\Simulator" $simData -Recurse
  }
  Remove-Item $backup -Recurse -Force
  Remove-Item "$env:TEMP\KnobHub\Preview\knob_preview-*.dll" -ErrorAction SilentlyContinue
  if ($wasShown) { Start-Process $exe; "Knob Hub started again, window open" }
  elseif ($wasRunning) { Start-Process $exe -ArgumentList "--minimized"; "Knob Hub started again in the tray" }
  "settings restored"
}
