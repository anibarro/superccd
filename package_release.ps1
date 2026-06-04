param(
    [string]$RepoRoot,
    [string]$BuildDir,
    [string]$ReleaseName
)

$ErrorActionPreference = "Stop"

if (-not $RepoRoot) {
    $RepoRoot = $PSScriptRoot
}

if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}

if (-not $ReleaseName) {
    $stamp = Get-Date -Format "yyyyMMdd"
    $ReleaseName = "superccd2dng-windows-x64-$stamp"
}

$distRoot = Join-Path $RepoRoot "dist"
$stageDir = Join-Path $distRoot $ReleaseName
$zipPath = Join-Path $distRoot ($ReleaseName + ".zip")

if (-not (Test-Path $BuildDir)) {
    throw "Build directory not found: $BuildDir"
}

$exePath = Join-Path $BuildDir "superccd2dng.exe"
if (-not (Test-Path $exePath)) {
    throw "Release executable not found: $exePath"
}

if (Test-Path $stageDir) {
    Remove-Item -LiteralPath $stageDir -Recurse -Force
}

if (-not (Test-Path $distRoot)) {
    New-Item -ItemType Directory -Path $distRoot | Out-Null
}

New-Item -ItemType Directory -Path $stageDir | Out-Null
Copy-Item -LiteralPath $exePath -Destination $stageDir

Get-ChildItem -Path $BuildDir -Filter "*.dll" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stageDir
}

$runtimeDirs = @(
    "generic",
    "iconengines",
    "imageformats",
    "networkinformation",
    "platforms",
    "styles",
    "tls",
    "translations"
)

foreach ($dir in $runtimeDirs) {
    $src = Join-Path $BuildDir $dir
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination $stageDir -Recurse
    }
}

$docsDir = Join-Path $stageDir "docs"
New-Item -ItemType Directory -Path $docsDir | Out-Null

$rawTherapeeProfileDir = Join-Path $RepoRoot "RawTherapee profile"
$stageRawTherapeeProfileDir = Join-Path $stageDir "RawTherapee profile"

Copy-Item -LiteralPath (Join-Path $RepoRoot "README.md") -Destination $stageDir
Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination $stageDir
Copy-Item -LiteralPath (Join-Path $RepoRoot "THIRD_PARTY_NOTICES.md") -Destination $stageDir

$manualPath = Join-Path $RepoRoot "docs\MANUAL.md"
if (Test-Path $manualPath) {
    Copy-Item -LiteralPath $manualPath -Destination $docsDir
}

if (Test-Path $rawTherapeeProfileDir) {
    Copy-Item -LiteralPath $rawTherapeeProfileDir -Destination $stageRawTherapeeProfileDir -Recurse
}

$vcRedistPath = Join-Path $BuildDir "vc_redist.x64.exe"
if (Test-Path $vcRedistPath) {
    Copy-Item -LiteralPath $vcRedistPath -Destination $stageDir
}

if (Test-Path $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}

Compress-Archive -Path $stageDir -DestinationPath $zipPath

Write-Host "Release package created:"
Write-Host "  Folder: $stageDir"
Write-Host "  Zip:    $zipPath"
