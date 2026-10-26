# Builds every Knob Hub app in this folder (Release) and puts what a GitHub release needs into hub\Plugins\release\:
# <id>.knobapp (the app) and <id>.app.json (its details, for Knob Hub's Add apps page). Then it prints the gh command
# that publishes each app as its own release, tagged <id>-v<version>. It publishes nothing itself.
# Run from Windows PowerShell:  powershell -ExecutionPolicy Bypass -File hub\Plugins\pack-release.ps1 [-Repo owner/name]
param([string]$Repo = "Shyam-Sangeeth/KnobHub")

$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "release"
New-Item -ItemType Directory -Force $out | Out-Null

foreach ($project in Get-ChildItem $PSScriptRoot -Recurse -Filter *.csproj) {
    $folder = $project.Directory.FullName
    $manifestPath = Join-Path $folder "app.json"
    if (-not (Test-Path $manifestPath)) { continue }
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json

    Write-Host "Building $($manifest.name) $($manifest.version)..."
    dotnet build $project.FullName -c Release --nologo -v quiet | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$($project.Name) didn't build" }
    $package = Get-ChildItem (Join-Path $folder "bin\Release") -Filter *.knobapp | Select-Object -First 1
    if (-not $package) { throw "$($project.Name) made no .knobapp" }

    $id = $manifest.id
    Copy-Item $package.FullName (Join-Path $out "$id.knobapp") -Force
    Copy-Item $manifestPath (Join-Path $out "$id.app.json") -Force

    $tag = "$id-v$($manifest.version)"
    $notes = if ($manifest.description) { $manifest.description } else { $manifest.subtitle }
    Write-Host "  release\$id.knobapp, release\$id.app.json"
    Write-Host "  gh release create $tag `"release/$id.knobapp`" `"release/$id.app.json`" --repo $Repo --title `"$($manifest.name) $($manifest.version)`" --notes `"$notes`""
}
Write-Host "Done: the files are in $out. Run the gh commands above (from hub\Plugins) to publish."
