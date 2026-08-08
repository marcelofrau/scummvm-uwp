param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [switch]$SkipBuild
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
$pfx = Join-Path $root 'certs\dosbox-uwp.pfx'

Write-Host "Signing $appx ..." -ForegroundColor Cyan
& $signtool sign /f $pfx /p dev /fd SHA256 /a $appx
if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed."; exit $LASTEXITCODE }

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $appx $dist -Force
Write-Host "Packaged: $dist" -ForegroundColor Green
