param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [switch]$SkipInstall
)
$ErrorActionPreference = 'Stop'

if (-not $SkipInstall) {
    & "$PSScriptRoot\install.ps1" -Configuration $Configuration -Platform $Platform
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Launching ScummVM ..." -ForegroundColor Cyan
Start-Process 'scummvm-launcher:'
Write-Host "Launched." -ForegroundColor Green
