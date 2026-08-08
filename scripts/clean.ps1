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
    if (-not $vs) { Write-Error "MSBuild not found."; exit 1 }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
}

Write-Host "Cleaning $Configuration|$Platform ..." -ForegroundColor Cyan
& $msbuild $sln /t:Clean "/p:Configuration=$Configuration" "/p:Platform=$Platform" /nologo | Out-Null

foreach ($dir in @(
        (Join-Path $root 'AppPackages'),
        (Join-Path $root 'launcher\ScummVMLauncher\bin'),
        (Join-Path $root 'launcher\ScummVMLauncher\obj'))) {
    if (Test-Path $dir) {
        Remove-Item -Recurse -Force $dir
        Write-Host "Removed $($dir.Replace($root,''))" -ForegroundColor DarkGray
    }
}

Write-Host "Clean done." -ForegroundColor Green
