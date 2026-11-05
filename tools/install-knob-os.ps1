# Drives Knob Hub's Settings > Knob OS through UI Automation: reads the card, clicks Install, confirms, and prints the
# status until the knob is back. -ReadOnly only prints the card. Knob Hub must be running, with a knob plugged in.
#
#   powershell -ExecutionPolicy Bypass -File tools\install-knob-os.ps1 [-ReadOnly]
param([switch]$ReadOnly)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$A = [System.Windows.Automation.AutomationElement]
$p = Get-Process KnobHub | Select-Object -First 1
if ($p.MainWindowHandle -eq 0) {
    [System.Threading.EventWaitHandle]::OpenExisting("KnobHub.ShowWindow").Set() | Out-Null
    Start-Sleep -Seconds 2
    $p = Get-Process KnobHub | Select-Object -First 1
}
$win = $A::FromHandle($p.MainWindowHandle)
& (Join-Path $PSScriptRoot "hub-page.ps1") -Page 3 | Out-Null
Start-Sleep -Milliseconds 800

function Texts {
    $all = $win.FindAll('Descendants', [System.Windows.Automation.Condition]::TrueCondition)
    $names = @($all | ForEach-Object { $_.Current.Name })
    $i = [array]::IndexOf($names, 'Knob OS')
    if ($i -ge 0) { $names[($i + 1)..([Math]::Min($i + 4, $names.Count - 1))] | Where-Object { $_ } }
}
"card: " + ((Texts) -join ' | ')
if ($ReadOnly) { return }

$boxCondition = New-Object System.Windows.Automation.PropertyCondition ($A::NameProperty), 'Install Knob OS'
$box = $win.FindFirst('Children', $boxCondition)  # the confirmation, if it's open already
if (-not $box) {
    $button = $win.FindAll('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button))) |
        Where-Object { $_.Current.Name -like 'Install*' } | Select-Object -First 1
    if (-not $button) { "no Install button"; return }
    "clicking: $($button.Current.Name)"
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    for ($i = 0; $i -lt 10 -and -not $box; $i++) { Start-Sleep -Milliseconds 500; $box = $win.FindFirst('Children', $boxCondition) }
}
if (-not $box) { "no confirmation"; return }
$question = $box.FindAll('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Text))) | ForEach-Object { $_.Current.Name }
"asked: $question"
$ok = $box.FindAll('Descendants', (New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button))) |
    Where-Object { $_.Current.Name -eq 'OK' } | Select-Object -First 1
"box: " + (($box.FindAll('Descendants', [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { "$($_.Current.ControlType.ProgrammaticName.Substring(12))='$($_.Current.Name)'" }) -join '; ')
# A Win32 message box: post it IDOK.
Add-Type 'using System; using System.Runtime.InteropServices; public static class Dlg { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }'
[void][Dlg]::PostMessage([IntPtr]$box.Current.NativeWindowHandle, 0x0111, [IntPtr]1, [IntPtr]::Zero)
$start = Get-Date; $last = ''
while (((Get-Date) - $start).TotalSeconds -lt 180) {
    $t = (Texts) -join ' | '
    if ($t -ne $last) { "{0,5:N1}s  {1}" -f ((Get-Date) - $start).TotalSeconds, $t; $last = $t }
    if ($t -match 'the latest|wasn''t installed') { break }
    Start-Sleep -Milliseconds 500
}
