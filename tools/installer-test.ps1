# Knob Hub's installer, one step at a time, each followed by what's on the PC (install folder, Start menu, the Run
# entry, the running Knob Hub and its log). install and upgrade take the .msi to run; msiexec's logs go to
# %TEMP%\knobhub-msi. Installing asks for administrator approval (it's a per-machine install).
#
#   powershell -ExecutionPolicy Bypass -File tools\installer-test.ps1 -Step install -Msi installer\out\KnobHub-1.0.3-x64.msi
#   ... -Step upgrade -Msi <a newer .msi>;  -Step uninstall;  -Step report
param([Parameter(Mandatory)][ValidateSet('install', 'upgrade', 'uninstall', 'report')][string]$Step, [string]$Msi)
$logs = "$env:TEMP\knobhub-msi"
New-Item -ItemType Directory -Force $logs | Out-Null
if ($Step -in 'install', 'upgrade') {
    if (-not $Msi -or -not (Test-Path $Msi)) { "-Msi <path to the .msi> is needed"; exit 1 }
    $Msi = (Resolve-Path $Msi).Path
}

function Report {
    $folder = (Get-ItemProperty 'HKLM:\Software\KnobHub\Installer' -ErrorAction SilentlyContinue).InstallFolder
    "install folder (registry): $folder"
    $dir = 'C:\Program Files\Knob Hub'
    if (Test-Path $dir) {
        $exe = Get-Item "$dir\KnobHub.exe" -ErrorAction SilentlyContinue
        "exe: $($exe.VersionInfo.ProductVersion)  preview dll: $(Test-Path "$dir\knob_preview.dll")  firmware: $((Get-ChildItem "$dir\firmware" -ErrorAction SilentlyContinue).Name -join ', ')"
    } else { "no $dir" }
    "start menu: $(Test-Path "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Knob Hub.lnk")"
    "run entry: $((Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue).KnobHub)"
    Get-Process KnobHub -ErrorAction SilentlyContinue | ForEach-Object { "running: $($_.Path) (window $($_.MainWindowHandle -ne 0))" }
    Get-Content "$env:LOCALAPPDATA\KnobHub\knobhub.log" -Tail 3
}

switch ($Step) {
    'install' {
        Get-Process KnobHub -ErrorAction SilentlyContinue | Stop-Process -PassThru | Wait-Process -Timeout 10
        $p = Start-Process msiexec -ArgumentList "/i `"$Msi`" /passive /l*v `"$logs\install.log`"" -PassThru -Wait
        "msiexec exit: $($p.ExitCode)"; Start-Sleep 6; Report
    }
    'upgrade' {
        $p = Start-Process msiexec -ArgumentList "/i `"$Msi`" /passive /l*v `"$logs\upgrade.log`"" -PassThru -Wait
        "msiexec exit: $($p.ExitCode)"; Start-Sleep 6; Report
    }
    'uninstall' {
        $code = (Get-ChildItem 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall' | Get-ItemProperty | Where-Object { $_.DisplayName -eq 'Knob Hub' }).PSChildName
        "product: $code"
        $p = Start-Process msiexec -ArgumentList "/x $code /passive /l*v `"$logs\uninstall.log`"" -PassThru -Wait
        "msiexec exit: $($p.ExitCode)"; Start-Sleep 3; Report
    }
    'report' { Report }
}
