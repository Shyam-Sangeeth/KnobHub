# Installs every app update Knob Hub offers: opens Add apps from the Apps page, clicks each Update, answers Yes to
# Knob Hub's "Install app" question, and closes the window. Use it before retaking the README screenshots, so the Apps
# page shows current versions. Knob Hub's window must be open.
#
#   powershell -ExecutionPolicy Bypass -File tools\update-apps.ps1
#
# The question is a Win32 MessageBox: UI Automation sees its buttons as panes without an Invoke pattern, so they're
# clicked with BM_CLICK on their window handles.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class MsgBox { [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l); }
"@
$A = [System.Windows.Automation.AutomationElement]
$CT = [System.Windows.Automation.ControlType]
function Cond($prop, $value) { New-Object System.Windows.Automation.PropertyCondition ($prop, $value) }
function Find($root, $name, $type, $scope = 'Descendants') {
    $root.FindFirst($scope, (New-Object System.Windows.Automation.AndCondition ((Cond $A::NameProperty $name), (Cond $A::ControlTypeProperty $type))))
}
function Invoke($el) { $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke() }
function WaitFor([scriptblock]$get, [int]$seconds) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) { $r = & $get; if ($r) { return $r }; Start-Sleep -Milliseconds 300 }
    return $null
}
# A MessageBox ("Install app") from Knob Hub, whether UI Automation lists it at the top level or under the dialog.
function FindQuestion($dlg) {
    (Find $A::RootElement "Install app" $CT::Window 'Children'), (Find $dlg "Install app" $CT::Window) | Where-Object { $_ } | Select-Object -First 1
}
function MessageText($box) {
    ($box.FindAll('Descendants', (Cond $A::ControlTypeProperty $CT::Text)) | ForEach-Object { $_.Current.Name }) -join " "
}
# Clicks a MessageBox button by its text ("Yes", "OK"), whatever control type UI Automation gives it.
function ClickButton($box, $name) {
    $b = $box.FindFirst('Descendants', (Cond $A::NameProperty $name))
    if (-not $b) { throw "no '$name' button" }
    $h = [IntPtr]$b.Current.NativeWindowHandle
    if ($h -ne [IntPtr]::Zero) { [void][MsgBox]::SendMessage($h, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) }  # BM_CLICK
    else { Invoke $b }
}

$h = (Get-Process KnobHub -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
if (-not $h) { "Knob Hub's window isn't open"; exit 1 }
$main = $A::FromHandle($h)
# The Apps page (the second item in the side list) has the Add apps button.
$items = $main.FindAll('Descendants', (Cond $A::ControlTypeProperty $CT::ListItem))
$items[1].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); Start-Sleep -Milliseconds 800
Invoke (Find $main "Add apps" $CT::Button)
$dlg = WaitFor { Find $A::RootElement "Add apps" $CT::Window 'Children' } 10
if (-not $dlg) { $dlg = WaitFor { Find $main "Add apps" $CT::Window } 5 }
if (-not $dlg) { "no Add apps window"; exit 1 }

# The catalog loads from GitHub: wait for its tiles.
$null = WaitFor { Find $dlg "Installed" $CT::Button } 30
$offered = $dlg.FindAll('Descendants', (New-Object System.Windows.Automation.AndCondition ((Cond $A::NameProperty "Update"), (Cond $A::ControlTypeProperty $CT::Button)))).Count
"updates offered: $offered"
$done = 0; $failed = 0
for ($i = 0; $i -lt $offered; $i++) {
    $btn = Find $dlg "Update" $CT::Button
    if (-not $btn) { break }
    Invoke $btn
    $ask = WaitFor { FindQuestion $dlg } 60
    if (-not $ask) { "no question appeared"; $failed++; break }
    "asked: " + ((MessageText $ask) -split "`n")[0]
    ClickButton $ask "Yes"
    $null = WaitFor { -not (Find $dlg "Installing$([char]0x2026)" $CT::Button) } 30  # "Installing" + an ellipsis; PowerShell 5.1 reads this file as ANSI
    Start-Sleep -Seconds 1
    $err = FindQuestion $dlg
    if ($err) { "message after install: " + (MessageText $err); ClickButton $err "OK"; $failed++ } else { $done++ }
}
"installed: $done"
Invoke (Find $dlg "Close" $CT::Button)
if ($failed) { exit 1 }
