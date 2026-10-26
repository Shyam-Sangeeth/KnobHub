# Builds every Knob Hub app in this folder (Release) and publishes each to the apps repository on GitHub
# (Shyam-Sangeeth/KnobHub-Apps, apart from Knob Hub's own releases) as a release of its own: tagged <id>-v<version>,
# with <id>.knobapp and <id>.app.json. Knob Hub's Add apps page lists the newest release of each app. An app whose
# version already has a release is skipped: bump "version" in its app.json to publish an update.
# Needs the GitHub CLI (gh), logged in. With -NoPublish it only builds and gathers the files in hub\Plugins\release.
# Run from Windows PowerShell:  powershell -ExecutionPolicy Bypass -File hub\Plugins\pack-release.ps1 [-Repo owner/name]
param([string]$Repo = "Shyam-Sangeeth/KnobHub-Apps", [switch]$NoPublish)

$ErrorActionPreference = "Stop"
$out = Join-Path $PSScriptRoot "release"
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null

$apps = @()
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
    Copy-Item $package.FullName (Join-Path $out "$id.knobapp")
    Copy-Item $manifestPath (Join-Path $out "$id.app.json")
    $apps += $manifest
}
Write-Host "Gathered $($apps.Count) apps in $out"
if ($NoPublish) { return }

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "The GitHub CLI (gh) isn't installed: winget install GitHub.cli" }
foreach ($manifest in $apps) {
    $id = $manifest.id
    $tag = "$id-v$($manifest.version)"
    # "release not found" (the usual case) is gh writing to stderr: not an error here.
    $ErrorActionPreference = "Continue"
    gh release view $tag --repo $Repo 2>&1 | Out-Null
    $released = $LASTEXITCODE -eq 0
    $ErrorActionPreference = "Stop"
    if ($released) {
        Write-Host "$($manifest.name) $($manifest.version) is already released: skipped (bump its version to publish an update)"
        continue
    }
    $notes = if ($manifest.description) { $manifest.description } else { $manifest.subtitle }
    gh release create $tag (Join-Path $out "$id.knobapp") (Join-Path $out "$id.app.json") --repo $Repo `
        --title "$($manifest.name) $($manifest.version)" --notes $notes
    if ($LASTEXITCODE -ne 0) { throw "gh couldn't publish $($manifest.name)" }
    Write-Host "Published $($manifest.name) $($manifest.version): https://github.com/$Repo/releases/tag/$tag"
}
