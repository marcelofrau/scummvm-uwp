# fetch-payload.ps1
# Downloads the runtime payload (RetroArch UWP shell + ScummVM libretro core)
# into the launcher project. Nothing here is built or versioned in git:
#   - Shell exe + DLLs:  RetroArch-SeriesConsoles.appx from the
#                        XboxEmulationHub/RetroArch fork release (pinned tag).
#   - Core DLL:          scummvm_libretro.dll from the libretro buildbot
#                        (nightly windows/x86_64/latest).

param(
    [string]$ForkTag = '08-10-2026'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$launcherDir = Join-Path $root 'launcher\ScummVMLauncher'
$coresDir = Join-Path $launcherDir 'cores'
$tmp = Join-Path $env:TEMP 'scummvm-uwp-payload'

New-Item -ItemType Directory -Force $tmp | Out-Null
New-Item -ItemType Directory -Force $coresDir | Out-Null

Write-Host "Fetching RetroArch UWP shell ($ForkTag) ..." -ForegroundColor Cyan
$appx = Join-Path $tmp 'RetroArch-SeriesConsoles.appx'
& curl.exe -sL -o $appx "https://github.com/XboxEmulationHub/RetroArch/releases/download/$ForkTag/RetroArch-SeriesConsoles.appx"
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $appx)) { Write-Error "Failed to download shell appx ($ForkTag)."; exit 1 }
& tar.exe -xf $appx -C $launcherDir "*.exe" "*.dll"
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to extract shell payload."; exit 1 }

Write-Host "Fetching ScummVM libretro core (buildbot) ..." -ForegroundColor Cyan
$coreZip = Join-Path $tmp 'scummvm_libretro.dll.zip'
& curl.exe -sL -o $coreZip "https://buildbot.libretro.com/nightly/windows/x86_64/latest/scummvm_libretro.dll.zip"
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $coreZip)) { Write-Error "Failed to download core zip."; exit 1 }
& tar.exe -xf $coreZip -C $coresDir
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to extract core."; exit 1 }

$exe = Join-Path $launcherDir 'RetroArch-msvcUWP.exe'
$core = Join-Path $coresDir 'scummvm_libretro.dll'
if (-not (Test-Path $exe))   { Write-Error "RetroArch-msvcUWP.exe missing after fetch."; exit 1 }
if (-not (Test-Path $core))  { Write-Error "scummvm_libretro.dll missing after fetch."; exit 1 }
Write-Host "Payload ready: shell ($((Get-Item $exe).Length) bytes) + core ($((Get-Item $core).Length) bytes)." -ForegroundColor Green
