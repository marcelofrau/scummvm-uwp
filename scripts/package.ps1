param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [switch]$SkipBuild,
    [string]$PfxPath = '',
    [string]$PfxPassword = 'dev'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$version = (Get-Content (Join-Path $root 'version.txt')).Trim()
$appxDir = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test"
$appx = Join-Path $appxDir "ScummVMLauncher_${version}_x64.appx"
$distDir = Join-Path $root 'dist'
$dist = Join-Path $distDir 'ScummVM.appx'

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Configuration $Configuration -Platform $Platform
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if (-not (Test-Path $appx)) { Write-Error "Appx not found: $appx"; exit 1 }

$winkit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+' } | Sort-Object Name -Descending | Select-Object -First 1
$signtool = Join-Path $winkit.FullName 'x64\signtool.exe'
$pfx = if ($PfxPath) { $PfxPath } else { Join-Path $root 'certs\dosbox-uwp.pfx' }
if (-not (Test-Path $pfx)) { Write-Error "Signing cert not found: $pfx"; exit 1 }

Write-Host "Signing $appx ..." -ForegroundColor Cyan
& $signtool sign /f $pfx /p $PfxPassword /fd SHA256 /a $appx
if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed."; exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $appx $dist -Force
Write-Host "Packaged: $dist" -ForegroundColor Green

# Release zip: appx + x64 dependencies only
$zipName = "scummvm-uwp_${version}_x64.zip"
$zipPath = Join-Path $root $zipName
$staging = Join-Path $env:TEMP "scummvm-uwp-dist-${version}"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $appx $staging
$depDir = Join-Path $appxDir 'Dependencies\x64'
if (Test-Path $depDir) {
    $dest = Join-Path $staging 'Dependencies\x64'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Get-ChildItem $depDir -Filter '*.appx' | Copy-Item -Destination $dest
}

Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zipPath -Force -CompressionLevel Optimal
Remove-Item $staging -Recurse -Force
$zipSize = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host "Release zip: $zipPath ($zipSize MB)" -ForegroundColor Green
