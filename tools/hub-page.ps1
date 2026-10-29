# Shows a page of the running Knob Hub window (0 Knob, 1 Apps, 2 Board, 3 Settings) through UI Automation: no mouse
# or focus needed. The window must be open (set the event KnobHub.ShowWindow to open it).
param([Parameter(Mandatory)][ValidateRange(0, 3)][int]$Page)
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$p = Get-Process KnobHub -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { "Knob Hub's window isn't open"; exit 1 }
$A = [System.Windows.Automation.AutomationElement]
$cond = New-Object System.Windows.Automation.PropertyCondition ($A::ControlTypeProperty), ([System.Windows.Automation.ControlType]::ListItem)
$items = $A::FromHandle($p.MainWindowHandle).FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
$items[$Page].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
"showing page $Page"
