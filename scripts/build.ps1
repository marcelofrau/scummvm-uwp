param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sln = Join-Path $root 'scummvm-uwp.sln'

$msbuild = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vs) { Write-Error "MSBuild not found (install VS2022+ com workload UWP)."; exit 1 }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
}

Write-Host "Restoring NuGet packages ..." -ForegroundColor Cyan
& $msbuild $sln /t:Restore "/p:Configuration=$Configuration" "/p:Platform=$Platform" /nologo | Out-Null

Write-Host "Building $Configuration|$Platform ..." -ForegroundColor Cyan
& $msbuild $sln "/p:Configuration=$Configuration" "/p:Platform=$Platform" /m /nologo
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed."; exit $LASTEXITCODE }
Write-Host "Build succeeded." -ForegroundColor Green
