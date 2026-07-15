[CmdletBinding()]
param(
    [string]$ImageDirectory = "C:\Users\sahil\Downloads\000\000",
    [string]$OutputRoot = "$env:USERPROFILE\Documents\ODM Comparisons",
    [string]$ProjectName = "kestrel-rededge-000",
    [ValidateRange(0.1, 20.0)]
    [double]$OrthophotoResolutionCm = 1.0,
    [ValidateRange(1, 128)]
    [int]$MaxConcurrency = [Math]::Min(8, [Math]::Max(1, [Environment]::ProcessorCount - 2)),
    [switch]$RerunAll
)

$ErrorActionPreference = "Stop"
$odmImage = "opendronemap/odm@sha256:fc56c7cda68ca20c62aa2cc8f48d112986eee350c766fe23afb2d282f6f49521"

function Convert-ToDockerPath([string]$Path) {
    return ([System.IO.Path]::GetFullPath($Path) -replace "\\", "/")
}

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "Docker was not found. Install Docker Desktop, then run this script again."
}

if ($ProjectName -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "ProjectName may contain only letters, numbers, periods, underscores, and hyphens."
}

try {
    docker info --format "{{.ServerVersion}}" | Out-Null
} catch {
    throw "Docker Desktop is installed but its Linux engine is not running. Start Docker Desktop and retry."
}
if ($LASTEXITCODE -ne 0) {
    throw "Docker Desktop is installed but its Linux engine is not running. Start Docker Desktop and retry."
}

$resolvedImages = (Resolve-Path -LiteralPath $ImageDirectory).Path
$tiffCount = @(Get-ChildItem -LiteralPath $resolvedImages -File |
    Where-Object { $_.Extension -in ".tif", ".tiff" }).Count
if ($tiffCount -eq 0) {
    throw "No TIFF images were found in $resolvedImages"
}

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputRoot)
$projectDirectory = Join-Path $resolvedOutput $ProjectName
New-Item -ItemType Directory -Path $projectDirectory -Force | Out-Null

$outputMount = Convert-ToDockerPath $resolvedOutput
$imageMount = Convert-ToDockerPath $resolvedImages
$containerProject = "/datasets/$ProjectName"
$logPath = Join-Path $projectDirectory "odm-console.log"

$odmArguments = @(
    "run", "--rm",
    "--name", "kestrel-odm-comparison",
    "-v", "${outputMount}:/datasets",
    "-v", "${imageMount}:${containerProject}/images:ro",
    $odmImage,
    "--project-path", "/datasets",
    $ProjectName,
    "--auto-boundary",
    "--radiometric-calibration", "camera",
    "--feature-quality", "ultra",
    "--pc-quality", "high",
    "--orthophoto-resolution", $OrthophotoResolutionCm.ToString(
        [System.Globalization.CultureInfo]::InvariantCulture),
    "--dem-resolution", ([Math]::Max(2.0, $OrthophotoResolutionCm * 2.0)).ToString(
        [System.Globalization.CultureInfo]::InvariantCulture),
    "--dsm",
    "--skip-3dmodel",
    "--build-overviews",
    "--max-concurrency", $MaxConcurrency.ToString()
)
if ($RerunAll) {
    $odmArguments += "--rerun-all"
}

Write-Host "ODM image:    $odmImage"
Write-Host "Input TIFFs:  $tiffCount from $resolvedImages"
Write-Host "Output:       $projectDirectory"
Write-Host "Resolution:   $OrthophotoResolutionCm cm/pixel"
Write-Host "This correctness-oriented run can take hours and use many gigabytes."

$containerName = "kestrel-odm-comparison"
$existingContainer = @(& docker ps --quiet --filter "name=^/${containerName}$") |
    Select-Object -First 1
if ($existingContainer) {
    $existingContainer = $existingContainer.Trim()
}
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    if ($existingContainer) {
        Write-Host "Reattaching to the running ODM container $existingContainer..."
        # Rebuild the log from Docker's complete retained output, then follow
        # it until the current processing run exits.
        & docker logs --follow $containerName 2>&1 |
            ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath $logPath
        $odmExitCode = $LASTEXITCODE
    } else {
        & docker @odmArguments 2>&1 |
            ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath $logPath
        $odmExitCode = $LASTEXITCODE
    }
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
if ($odmExitCode -ne 0) {
    throw "ODM failed with exit code $odmExitCode. See $logPath"
}

$orthophoto = Join-Path $projectDirectory "odm_orthophoto\odm_orthophoto.tif"
if (-not (Test-Path -LiteralPath $orthophoto)) {
    throw "ODM completed but the expected orthophoto was not found at $orthophoto"
}

# ODM writes the RedEdge-M orthophoto as a multispectral GeoTIFF. Export its
# verified Red, Green, and Blue output bands to a regular 8-bit PNG using the
# GDAL copy already bundled inside the pinned ODM image.
$rgbPng = Join-Path $projectDirectory "odm_orthophoto\odm_rgb.png"
$containerOrthophoto = "/datasets/$ProjectName/odm_orthophoto/odm_orthophoto.tif"
$containerPng = "/datasets/$ProjectName/odm_orthophoto/odm_rgb.png"
$gdalCommand = "/code/SuperBuild/install/bin/gdal_translate " +
    "-of PNG -ot Byte -b 1 -b 2 -b 3 " +
    "-scale_1 -scale_2 -scale_3 " +
    "-colorinterp red,green,blue -co ZLEVEL=6 " +
    "'$containerOrthophoto' '$containerPng'"
$pngArguments = @(
    "run", "--rm",
    "--entrypoint", "/bin/bash",
    "-v", "${outputMount}:/datasets",
    $odmImage,
    "-lc", $gdalCommand
)

Write-Host "Exporting an RGB PNG with bundled GDAL..."
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & docker @pngArguments 2>&1 | ForEach-Object { $_.ToString() }
    $pngExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
if ($pngExitCode -ne 0 -or -not (Test-Path -LiteralPath $rgbPng)) {
    throw "ODM succeeded, but RGB PNG export failed. The GeoTIFF remains at $orthophoto"
}

Write-Host ""
Write-Host "ODM comparison completed."
Write-Host "Orthophoto: $orthophoto"
Write-Host "RGB PNG:    $rgbPng"
