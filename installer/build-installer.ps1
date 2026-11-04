# Builds a Knob Hub release into installer\out:
#   KnobHub-<version>-x64.msi                 the installer (Knob Hub, its preview and the Knob OS images it installs)
#   knob-os-<os version>-<board>.bin          each board's Knob OS image, for flashing by hand at 0x0
# Steps: Knob OS for every real knob and the preview (PlatformIO), a self-contained Knob Hub in installer\payload,
# then the installer. The version is <Version> in hub\KnobHub\KnobHub.csproj unless given.
#   powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1 [-Version 1.2.0]
param([string]$Version)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'hub\KnobHub\KnobHub.csproj'
$knobOs = Join-Path $root 'knob-os'
$cache = Join-Path $env:USERPROFILE '.cache\knob-os'
$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
# The real knobs' PlatformIO envs (one per board; see knob-os\platformio.ini).
$boards = @('crowpanel-128', 'crowpanel-146')

if (-not $Version) {
    [xml]$csproj = Get-Content $project
    $Version = @($csproj.Project.PropertyGroup.Version | Where-Object { $_ })[0]
}

# Knob OS. The image (knob-os.bin) and its copy in the firmware folder are made after linking, so relink every board
# into a fresh firmware folder: only this build's images go in.
Write-Host 'Building Knob OS for the knobs and the preview...'
$env:Path = (Join-Path $env:USERPROFILE '.mingw64\bin') + ';' + $env:Path
Remove-Item (Join-Path $cache 'firmware') -Recurse -Force -ErrorAction SilentlyContinue
foreach ($board in $boards) { Remove-Item (Join-Path $cache "build\$board\firmware.bin") -Force -ErrorAction SilentlyContinue }
Push-Location $knobOs
try {
    & $pio run -e preview @($boards | ForEach-Object { '-e'; $_ })
    if ($LASTEXITCODE) { throw 'Knob OS failed to build' }
} finally { Pop-Location }
$firmware = Get-Content (Join-Path $cache 'firmware\firmware.json') -Raw | ConvertFrom-Json
if (@($firmware.PSObject.Properties).Count -ne $boards.Count) { throw 'A board''s Knob OS image is missing' }

$payload = Join-Path $PSScriptRoot 'payload'
$out = Join-Path $PSScriptRoot 'out'
Remove-Item $payload, $out -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Publishing Knob Hub $Version (self-contained, win-x64)..."
dotnet publish $project -c Release -r win-x64 --self-contained true -p:Version=$Version -o $payload
if ($LASTEXITCODE) { throw 'dotnet publish failed' }
foreach ($file in 'knob_preview.dll', 'firmware\firmware.json') {
    if (-not (Test-Path (Join-Path $payload $file))) { throw "The payload has no $file" }
}

Write-Host 'Building the installer...'
dotnet build (Join-Path $PSScriptRoot 'KnobHub.Installer.wixproj') -c Release -p:Version=$Version -p:PayloadDir=$payload -o $out
if ($LASTEXITCODE) { throw 'Installer build failed' }
Get-ChildItem $out -Exclude *.msi | Remove-Item -Recurse -Force  # leave only the release's files

foreach ($board in $firmware.PSObject.Properties) {
    $name = "knob-os-$($board.Value.version)-$($board.Name).bin"
    Copy-Item (Join-Path $cache "firmware\$($board.Value.file)") (Join-Path $out $name)
}

Get-ChildItem $out | Select-Object Name, @{ n = 'Size (MB)'; e = { [math]::Round($_.Length / 1MB, 1) } }, FullName
