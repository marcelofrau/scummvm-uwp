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

# Read version AFTER build: build.ps1 -> tools\version.ps1 bumps it.
$version = (Get-Content (Join-Path $root 'version.txt')).Trim()

# Generate the package deterministically. /t:Publish produces
# AppPackages\ScummVMLauncher_<version>_x64_Test\ScummVMLauncher_<version>_x64.msix.
$msbuild = $null
if (Test-Path "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe") {
    $msbuild = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
} else {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vs) { Write-Error 'MSBuild not found (install VS2022+ com workload UWP).'; exit 1 }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
}
$proj = Join-Path $root 'launcher\ScummVMLauncher\ScummVMLauncher.vcxproj'
& $msbuild $proj /t:Publish /nologo "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:AppxPackageVersion=$version" "/p:PackageArchitecture=$Platform"
if ($LASTEXITCODE -ne 0) { Write-Error 'Packaging (Publish) failed.'; exit $LASTEXITCODE }

$appxDir = Join-Path $root "launcher\ScummVMLauncher\AppPackages\ScummVMLauncher_${version}_x64_Test"
# Modern MSBuild emits .msix; older configs emit .appx. Accept both.
$pkg = Get-ChildItem $appxDir -Filter "*.msix" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $pkg) {
    $pkg = Get-ChildItem $appxDir -Filter "*.appx" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}
$appx = if ($pkg) { $pkg.FullName } else { Join-Path $appxDir "ScummVMLauncher_${version}_x64.msix" }

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
