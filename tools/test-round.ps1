# One round of the automated tests: every Knob OS build, the preview checks, Knob Hub's Debug and Release builds and
# hub-tests. Takes about ten minutes (the checks wait for the real screen timeout). No knob needed.
#
#   powershell -ExecutionPolicy Bypass -File tools\test-round.ps1
#
# A running Knob Hub (installed or Debug) is closed for the builds and started again afterwards the way it was: with
# its window open, or in the tray. Prints a summary; exits with 1 if any step failed.
$repo = Split-Path $PSScriptRoot -Parent
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$results = @()
$failed = 0
function Step($name, [scriptblock]$run) {
    $start = Get-Date
    "===== $name"
    & $run 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($code -ne 0) { $script:failed++ }
    $script:results += "{0,-22} {1,-6} ({2:N0} s)" -f $name, $(if ($code -eq 0) { 'ok' } else { "exit $code" }), ((Get-Date) - $start).TotalSeconds
}

Push-Location "$repo\knob-os"
foreach ($env in "preview", "sim", "crowpanel-128", "crowpanel-146") {
    Step "knob-os $env" { & $pio run -e $env -s }
}
Pop-Location
Push-Location $repo
Step "preview checks" { & $py -I tools\checks.py }

# The builds replace files a running Debug Knob Hub has open, and hub-tests loads its DLLs: close whichever Knob Hub
# is running and start the same one again afterwards.
$running = Get-Process KnobHub -ErrorAction SilentlyContinue | Select-Object -First 1
$exe = $null; $windowOpen = $false
if ($running) {
    $exe = $running.Path
    $windowOpen = [bool](Get-Process KnobHub | Where-Object { $_.MainWindowHandle -ne 0 })
    & $exe --exit
    "closed Knob Hub ($exe)"
}
try {
    Step "hub build Debug" { dotnet build "$repo\hub\KnobHub\KnobHub.csproj" -c Debug -v q -nologo }
    Step "hub build Release" { dotnet build "$repo\hub\KnobHub\KnobHub.csproj" -c Release -v q -nologo }
    Step "hub-tests" { dotnet run --project "$repo\tools\hub-tests" }
} finally {
    if ($exe) {
        if ($windowOpen) { Start-Process $exe } else { Start-Process $exe -ArgumentList "--minimized" }
        "started Knob Hub again $(if ($windowOpen) { 'with its window open' } else { 'in the tray' })"
    }
    Pop-Location
}

"===== SUMMARY"
$results
if ($failed) { "$failed step(s) failed"; exit 1 }
"all passed"
