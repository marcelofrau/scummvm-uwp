param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Install the SIGNED appx. dist\ScummVM.appx is produced by package.ps1 (signtool).
# The raw MSBuild appx in AppPackages is UNSIGNED (AppxPackageSigningEnabled=false).
$dist = Join-Path $root 'dist\ScummVM.appx'
if (-not (Test-Path $dist)) {
    Write-Host "dist\ScummVM.appx missing; running package.ps1 -SkipBuild to sign ..." -ForegroundColor Cyan
    & "$PSScriptRoot\package.ps1" -SkipBuild -Configuration $Configuration -Platform $Platform
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# Make sure the signing cert is trusted on this machine (needed by Add-AppxPackage).
$pfx = Join-Path $root 'certs\dosbox-uwp.pfx'
$cer = Join-Path $root 'certs\dosbox-uwp.cer'
if (-not (Test-Path $pfx)) { Write-Error "Missing cert: $pfx"; exit 1 }
$certThumb = (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($pfx, 'dev')).Thumbprint
if (-not (Get-ChildItem 'Cert:\CurrentUser\Root' -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $certThumb })) {
    Write-Host "Installing dosbox-uwp.cer into CurrentUser\Root trust store ..." -ForegroundColor Cyan
    Import-Certificate -FilePath $cer -CertStoreLocation 'Cert:\CurrentUser\Root' | Out-Null
}

# Verify the signature is present and by our cert.
$winkit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory |
    Where-Object { $_.Name -match '^10\.0\.\d+' } | Sort-Object Name -Descending | Select-Object -First 1
$signtool = Join-Path $winkit.FullName 'x64\signtool.exe'
$verifyOut = & $signtool verify /pa /v $dist 2>&1 | Out-String
if ($verifyOut -notmatch 'Issued to: Marcelo Frau') {
    Write-Error "dist\ScummVM.appx is not validly signed (expected signer 'Marcelo Frau' missing). Run package.ps1 first."
    exit 1
}

$version = (Get-Content (Join-Path $root 'version.txt')).Trim()
$appxDir = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test"
$depsDir = Join-Path $appxDir "Dependencies\x64"

$deps = @()
if (Test-Path $depsDir) {
    $deps = Get-ChildItem $depsDir -Filter *.appx | ForEach-Object { $_.FullName }
    if ($deps.Count -gt 0) {
        Write-Host "Installing dependencies ..." -ForegroundColor Cyan
        Add-AppxPackage -Path $deps -ForceApplicationShutdown -ErrorAction Continue
    }
}

Write-Host "Registering appx ..." -ForegroundColor Cyan
Add-AppxPackage -Path $dist -ForceApplicationShutdown
if (-not $?) { exit 1 }
Write-Host "Installed." -ForegroundColor Green
