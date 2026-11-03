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
$published = $false
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
        # GitHub marks one release "Latest": the All apps release keeps it (below).
        gh release create $id @files --repo $Repo --title $manifest.name --notes $notes --latest=false
        if ($LASTEXITCODE -ne 0) { throw "gh couldn't publish $($manifest.name)" }
    } elseif ($assets -contains "$name.knobapp") {
        Write-Host "$($manifest.name) $($manifest.version) is already released: skipped (bump its version to publish an update)"
        continue
    } else {
        gh release upload $id @files --repo $Repo
        if ($LASTEXITCODE -ne 0) { throw "gh couldn't add $($manifest.name) $($manifest.version) to its release" }
    }
    Write-Host "Published $($manifest.name) $($manifest.version): https://github.com/$Repo/releases/tag/$id"
    $published = $true
}
if (-not $published) { return }

# The All apps release (tag "apps", no app files, so Add apps ignores it) lists every app and carries GitHub's "Latest"
# mark, so the releases page's Latest leads to an overview instead of whichever app was released last.
$rows = $apps | Sort-Object name | ForEach-Object {
    "| [$($_.name)](https://github.com/$Repo/releases/tag/$($_.id)) | $(if ($_.description) { $_.description } else { $_.subtitle }) |"
}
$notes = @(
    "Apps for [Knob Hub](https://github.com/Shyam-Sangeeth/KnobHub). Each app has its own release here, holding every version of it."
    ""
    "**To install or update an app, use Knob Hub:** open the **Apps** page and click **Add apps**. It lists the newest version of each app, and shows **Update** when a newer version is out."
    ""
    "| App | What it does |"
    "|---|---|"
) + $rows + @(
    ""
    "To install a version by hand, download its ``.knobapp`` from the app's release and use **Install from file...** on the Add apps page. More about each app is in the [README](https://github.com/$Repo#readme)."
)
$notesFile = Join-Path $out "all-apps.md"
[System.IO.File]::WriteAllText($notesFile, ($notes -join "`n") + "`n", (New-Object System.Text.UTF8Encoding $false))
$ErrorActionPreference = "Continue"
gh release view apps --repo $Repo 2>$null | Out-Null
$exists = $LASTEXITCODE -eq 0
$ErrorActionPreference = "Stop"
if ($exists) { gh release edit apps --repo $Repo --notes-file $notesFile --latest | Out-Null }
else { gh release create apps --repo $Repo --title "All apps" --notes-file $notesFile --latest | Out-Null }
if ($LASTEXITCODE -ne 0) { throw "gh couldn't update the All apps release" }
Write-Host "Updated All apps: https://github.com/$Repo/releases/tag/apps"
