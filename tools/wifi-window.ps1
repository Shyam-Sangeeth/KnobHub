# Opens Knob Hub's Wi-Fi window through UI Automation, waits for the knob's network list, captures the window with
# PrintWindow, lists the networks and closes it again, then puts Knob Hub back on the page it was on. Knob Hub must be
# running with its window open and a knob connected. The picture shows the networks near you: don't publish it as is.
#
#   powershell -ExecutionPolicy Bypass -File tools\wifi-window.ps1 -Out wifi.png
param([Parameter(Mandatory)][string]$Out)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class Shot {
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  public struct RECT { public int L, T, R, B; }
  public static void Save(IntPtr h, string path) {
    RECT r; GetWindowRect(h, out r);
    using (var bmp = new Bitmap(r.R - r.L, r.B - r.T)) using (var g = Graphics.FromImage(bmp)) {
      IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 2); g.ReleaseHdc(dc); bmp.Save(path);
    }
  }
}
'@
$A = [System.Windows.Automation.AutomationElement]
$T = [System.Windows.Automation.TreeScope]
$p = Get-Process KnobHub | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$main = $A::FromHandle($p.MainWindowHandle)
$listItem = New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::ListItem)
$pages = $main.FindAll($T::Descendants, $listItem)
$was = 0
for ($i = 0; $i -lt 4; $i++) { if ($pages[$i].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected) { $was = $i } }
$pages[3].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
Start-Sleep -Milliseconds 500
$buttonType = New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button)
$button = $main.FindAll($T::Descendants, $buttonType) | Where-Object { $_.Current.Name -like "Set up Wi-Fi*" -or $_.Current.Name -like "Stop Wi-Fi*" } | Select-Object -First 1
"button: " + $button.Current.Name
if (-not $button) { "no Set up Wi-Fi button (enabled: knob connected?)"; exit 1 }
"button enabled: " + $button.Current.IsEnabled
# Invoke returns only when the modal window closes, so it runs on another thread.
$invoke = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
$ps = [powershell]::Create().AddScript({ param($i) $i.Invoke() }).AddArgument($invoke)
$null = $ps.BeginInvoke()
$wifi = $null
for ($i = 0; $i -lt 20 -and -not $wifi; $i++) {
  Start-Sleep -Milliseconds 300
  $wifi = $A::RootElement.FindFirst($T::Children, (New-Object System.Windows.Automation.PropertyCondition ($A::NameProperty), "Wi-Fi"))
}
if (-not $wifi) { "the Wi-Fi window didn't open"; exit 1 }
Start-Sleep -Seconds 8   # the knob's scan
[Shot]::Save([IntPtr]$wifi.Current.NativeWindowHandle, $Out)
$nets = $wifi.FindFirst($T::Descendants, (New-Object System.Windows.Automation.PropertyCondition ($A::AutomationIdProperty), "Networks"))
$rows = $nets.FindAll($T::Children, $listItem)
"networks: " + $rows.Count
$wifi.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close()
Start-Sleep -Milliseconds 300
$pages[$was].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
"back on page $was"
