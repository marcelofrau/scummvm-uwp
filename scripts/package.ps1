param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [switch]$SkipBuild,
    [string]$PfxPath = '',
    [string]$PfxPassword = 'dev'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$distDir = Join-Path $root 'dist'
$dist = Join-Path $distDir 'ScummVM.appx'

if (-not $SkipBuild) {
    & "$PSScriptRoot\build.ps1" -Configuration $Configuration -Platform $Platform
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Read version AFTER build: the PreBuildEvent (tools\version.ps1) bumps it during the build.
$version = (Get-Content (Join-Path $root 'version.txt')).Trim()
$appxDir = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test"
$appx = Join-Path $appxDir "ScummVMLauncher_${version}_x64.appx"

if (-not (Test-Path $appx)) { Write-Error "Appx not found: $appx"; exit 1 }

$winkit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+' } | Sort-Object Name -Descending | Select-Object -First 1
$signtool = Join-Path $winkit.FullName 'x64\signtool.exe'
$pfx = if ($PfxPath) { $PfxPath } else { Join-Path $root 'certs\dosbox-uwp.pfx' }
if (-not (Test-Path $pfx)) { Write-Error "Signing cert not found: $pfx"; exit 1 }

Write-Host "Signing $appx ..." -ForegroundColor Cyan
& $signtool sign /f $pfx /p $PfxPassword /fd SHA256 /a $appx
if ($LASTEXITCODE -ne 0) { Write-Error "Signing failed."; exit $LASTEXITCODE }

# Verify the signature is present and by our cert.
# Note: signtool verify /pa exit code fails on machines where the self-signed
# root is not in the trust store, so verify by signer identity instead.
$verifyOut = & $signtool verify /pa /v $appx 2>&1 | Out-String
if ($verifyOut -notmatch 'Issued to: Marcelo Frau') {
    Write-Error "Signing verification failed: expected signer 'Marcelo Frau' not found."
    exit 1
}
Write-Host "Signed OK (verifier: Marcelo Frau)." -ForegroundColor Green

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Copy-Item $appx $dist -Force
Write-Host "Packaged: $dist" -ForegroundColor Green

# Release zip: appx + x64 dependencies only
$zipName = "scummvm-uwp_${version}_x64.zip"
$zipPath = Join-Path $distDir $zipName
$staging = Join-Path $env:TEMP "scummvm-uwp-dist-${version}"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

Copy-Item $appx $staging
$cer = Join-Path $root 'certs\dosbox-uwp.cer'
if (Test-Path $cer) { Copy-Item $cer $staging }
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
exit 0
