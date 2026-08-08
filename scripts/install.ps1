param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$version = (Get-Content (Join-Path $root 'version.txt')).Trim()
$appxDir = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test"
$appx = Join-Path $appxDir "ScummVMLauncher_${version}_x64.appx"
$depsDir = Join-Path $appxDir "Dependencies\x64"

if (-not (Test-Path $appx)) { Write-Error "Run package.ps1 first. Appx not found: $appx"; exit 1 }

$deps = @()
if (Test-Path $depsDir) {
    $deps = Get-ChildItem $depsDir -Filter *.appx | ForEach-Object { $_.FullName }
    if ($deps.Count -gt 0) {
        Write-Host "Installing dependencies ..." -ForegroundColor Cyan
        Add-AppxPackage -Path $deps -ForceApplicationShutdown -ErrorAction Continue
    }
}

Write-Host "Registering appx ..." -ForegroundColor Cyan
Add-AppxPackage -Path $appx -ForceApplicationShutdown
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Installed." -ForegroundColor Green
