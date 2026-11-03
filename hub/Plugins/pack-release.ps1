# Builds every Knob Hub app in this folder (Release) and publishes it to the apps repository on GitHub
# (Shyam-Sangeeth/KnobHub-Apps, apart from Knob Hub's own releases). Each app has one release, tagged with its id, that
# holds every version: <id>-<version>.knobapp and <id>-<version>.app.json. The first version creates the release; later
# ones are added to it. Knob Hub's Add apps page offers the newest version of each app. A version that's already in the
# release is skipped: bump "version" in the app's app.json to publish an update.
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

    $name = "$($manifest.id)-$($manifest.version)"
    Copy-Item $package.FullName (Join-Path $out "$name.knobapp")
    Copy-Item $manifestPath (Join-Path $out "$name.app.json")
    $apps += $manifest
}
Write-Host "Gathered $($apps.Count) apps in $out"
if ($NoPublish) { return }

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw "The GitHub CLI (gh) isn't installed: winget install GitHub.cli" }
foreach ($manifest in $apps) {
    $id = $manifest.id
    $name = "$id-$($manifest.version)"
    $files = (Join-Path $out "$name.knobapp"), (Join-Path $out "$name.app.json")
    # "release not found" (the app's first version) is gh writing to stderr: not an error here.
    $ErrorActionPreference = "Continue"
    $assets = gh release view $id --repo $Repo --json assets --jq ".assets[].name" 2>$null
    $exists = $LASTEXITCODE -eq 0
    $ErrorActionPreference = "Stop"
    if (-not $exists) {
        $notes = if ($manifest.description) { $manifest.description } else { $manifest.subtitle }
        gh release create $id @files --repo $Repo --title $manifest.name --notes $notes
        if ($LASTEXITCODE -ne 0) { throw "gh couldn't publish $($manifest.name)" }
    } elseif ($assets -contains "$name.knobapp") {
        Write-Host "$($manifest.name) $($manifest.version) is already released: skipped (bump its version to publish an update)"
        continue
    } else {
        gh release upload $id @files --repo $Repo
        if ($LASTEXITCODE -ne 0) { throw "gh couldn't add $($manifest.name) $($manifest.version) to its release" }
    }
    Write-Host "Published $($manifest.name) $($manifest.version): https://github.com/$Repo/releases/tag/$id"
}
