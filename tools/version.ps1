# version.ps1
# Syncs the app version with the bundled ScummVM core.
# Base  = display_version from cores/scummvm_libretro.info ("2026.3.1git" -> "2026.3.1")
# Build = build_counter.txt, incremented every build.
# Rewrites Package.appxmanifest (Identity Version) + version.txt.
# Called as PreBuildEvent by MSBuild (or manually: -DontIncrement to just normalize).

param(
    [switch]$DontIncrement
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$infoPath     = Join-Path $root 'launcher\ScummVMLauncher\cores\scummvm_libretro.info'
$counterPath  = Join-Path $root 'build_counter.txt'
$manifestPath = Join-Path $root 'launcher\ScummVMLauncher\Package.appxmanifest'
$versionTxt   = Join-Path $root 'version.txt'
$utf8         = New-Object System.Text.UTF8Encoding $false

# 1. Base version from the shipped core's display_version
$info = Get-Content $infoPath -Raw
if ($info -notmatch 'display_version\s*=\s*"([^"]+)"') {
    Write-Error "[version] display_version not found in $infoPath"; exit 1
}
$coreVer = $Matches[1]
if ($coreVer -notmatch '(\d+\.\d+\.\d+)') {
    Write-Error "[version] cannot parse version from '$coreVer'"; exit 1
}
$base = $Matches[1]

# 2. Build counter
$buildNum = 0
if (Test-Path $counterPath) {
    $buildNum = [int](Get-Content $counterPath -Raw)
}
if (-not $DontIncrement) { $buildNum++ }
[System.IO.File]::WriteAllText($counterPath, "$buildNum", $utf8)

$fullVersion = "$base.$buildNum"

# 3. Rewrite Package.appxmanifest - Identity Version only
$manifest = Get-Content $manifestPath -Raw
$manifest = $manifest -replace '(<Identity[^>]*Version=")(\d+\.\d+\.\d+\.\d+)(")', "`${1}$fullVersion`${3}"
[System.IO.File]::WriteAllText($manifestPath, $manifest, $utf8)

# 4. version.txt
[System.IO.File]::WriteAllText($versionTxt, $fullVersion, $utf8)

Write-Host "[version] core=$coreVer -> app $fullVersion (build $buildNum)"

# 5. Prune old AppPackages, keep last 3
$appPackagesDir = Join-Path $root 'launcher\ScummVMLauncher\AppPackages'
if (Test-Path $appPackagesDir) {
    $dirs = Get-ChildItem $appPackagesDir -Directory | Sort-Object LastWriteTime -Descending
    if ($dirs.Count -gt 3) {
        $dirs | Select-Object -Skip 3 | ForEach-Object {
            Write-Host "[version] Removing old package: $($_.Name)"
            Remove-Item $_.FullName -Recurse -Force
        }
    }
}
